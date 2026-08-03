#include "ProjetoAberto.h"

namespace matriz::ui {

ProjetoAberto::ProjetoAberto(std::unique_ptr<matriz::model::Project> projeto) : projeto_(std::move(projeto)) {}

std::vector<ItemResumo> ProjetoAberto::listarItens() const {
    std::vector<ItemResumo> out;

    // Uma consulta só (EXISTS/subquery correlacionados em vez de N+1 —
    // importante com milhares de itens no mosaico, ver B.2). As duas últimas
    // colunas só existem de fato pra tipo_midia="release" (nível raiz,
    // campos artista_principal/titulo de release.yaml) — usadas pra
    // agrupar o mosaico por artista/lançamento no modo Catalog.
    auto stmt = projeto_->registro().prepare(
        "SELECT i.id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, i.atualizado_em, "
        "EXISTS(SELECT 1 FROM arquivo a WHERE a.item_id = i.id AND a.estado_sincronizacao = 'sincronizado'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'artista_principal'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'titulo') "
        "FROM item i ORDER BY i.codigo_acervo");
    while (stmt.step()) {
        ItemResumo r;
        r.id = stmt.columnText(0);
        r.codigoAcervo = stmt.columnText(1);
        r.titulo = stmt.columnText(2);
        r.tipoMidia = stmt.columnText(3);
        r.estado = stmt.columnText(4);
        r.atualizadoEm = stmt.columnText(5);
        r.sincronizado = stmt.columnInt(6) != 0;
        if (!stmt.columnIsNull(7)) r.artistaLancamento = stmt.columnText(7);
        if (!stmt.columnIsNull(8)) r.tituloLancamento = stmt.columnText(8);

        out.push_back(std::move(r));
    }
    return out;
}

const matriz::ficha::FichaDefinition& ProjetoAberto::definicaoPara(const std::string& tipoMidia) {
    auto it = definicoesCache_.find(tipoMidia);
    if (it != definicoesCache_.end()) return it->second;

    juce::File caminho = juce::File(MATRIZ_FICHAS_DIR).getChildFile(tipoMidia + ".yaml");
    if (!caminho.existsAsFile())
        throw ProjetoAbertoError("nenhuma definição de ficha encontrada para o tipo \"" + tipoMidia + "\": " +
                                  caminho.getFullPathName().toStdString());

    auto [inserido, ok] = definicoesCache_.emplace(tipoMidia, matriz::ficha::loadFromFile(caminho.getFullPathName().toStdString()));
    return inserido->second;
}

std::optional<std::string> ProjetoAberto::valorCampo(const std::string& itemId, const std::string& nivel,
                                                       int nivelIndice, const std::string& campoId) const {
    auto stmt = projeto_->registro().prepare(
        "SELECT valor FROM item_campo WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    stmt.bind(3, matriz::db::Value::of(nivelIndice));
    stmt.bind(4, matriz::db::Value::of(campoId));
    if (stmt.step() && !stmt.columnIsNull(0)) return stmt.columnText(0);
    return std::nullopt;
}

std::vector<std::string> ProjetoAberto::papeisArquivoPresentes(const std::string& itemId) const {
    std::vector<std::string> out;
    auto stmt = projeto_->registro().prepare("SELECT DISTINCT papel FROM arquivo WHERE item_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

std::optional<juce::String> ProjetoAberto::caminhoMiniaturaPrincipal(const std::string& itemId) const {
    auto stmt = projeto_->indice().prepare(
        "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' ORDER BY gerado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;
    juce::String relativo = stmt.columnText(0);
    return projeto_->pasta().getChildFile(relativo).getFullPathName();
}

std::optional<ProjetoAberto::SugestaoCampo> ProjetoAberto::sugestaoPendente(const std::string& itemId,
                                                                             const std::string& nivel, int nivelIndice,
                                                                             const std::string& campoId) const {
    auto stmt = projeto_->indice().prepare(
        "SELECT id, valor, confianca, modelo, modelo_versao FROM sugestao_campo "
        "WHERE item_id = ? AND nivel = ? AND nivel_indice = ? AND campo_id = ? AND confirmado = 0 "
        "ORDER BY processado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    stmt.bind(3, matriz::db::Value::of(nivelIndice));
    stmt.bind(4, matriz::db::Value::of(campoId));
    if (!stmt.step()) return std::nullopt;

    SugestaoCampo s;
    s.id = stmt.columnText(0);
    s.valor = stmt.columnText(1);
    if (!stmt.columnIsNull(2)) s.confianca = stmt.columnReal(2);
    s.modelo = stmt.columnText(3);
    s.modeloVersao = stmt.columnText(4);
    return s;
}

void ProjetoAberto::confirmarSugestao(const SugestaoCampo& sugestao, const std::string& itemId,
                                       const std::string& nivel, int nivelIndice, const std::string& campoId,
                                       const std::string& autor) {
    std::string agora = matriz::model::agoraIso8601();

    projeto_->registro().run(
        "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, 'humano', ?) "
        "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) "
        "DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(nivel),
         matriz::db::Value::of(nivelIndice), matriz::db::Value::of(campoId), matriz::db::Value::of(sugestao.valor),
         matriz::db::Value::of(agora)});

    projeto_->registro().run(
        "INSERT INTO item_historico (id, item_id, tipo_evento, campo_id, valor_novo, modelo_origem, "
        "modelo_origem_versao, confianca_origem, autor, criado_em) "
        "VALUES (?, ?, 'confirmacao_sugestao_ia', ?, ?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(campoId),
         matriz::db::Value::of(sugestao.valor), matriz::db::Value::of(sugestao.modelo),
         matriz::db::Value::of(sugestao.modeloVersao),
         sugestao.confianca ? matriz::db::Value::of(*sugestao.confianca) : matriz::db::Value::null(),
         matriz::db::Value::of(autor), matriz::db::Value::of(agora)});

    projeto_->indice().run("UPDATE sugestao_campo SET confirmado = 1, confirmado_por = ?, confirmado_em = ? WHERE id = ?",
                            {matriz::db::Value::of(autor), matriz::db::Value::of(agora), matriz::db::Value::of(sugestao.id)});
}

} // namespace matriz::ui
