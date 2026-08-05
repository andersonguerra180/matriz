#include "ProjetoAberto.h"

#include "../Ingest/Miniaturas.h"

#include "../I18n/Strings.h"

#include <algorithm>
#include <unordered_map>

namespace matriz::ui {

namespace {

// Nó intermediário de construção da árvore (Origem ou Acervo) — heap-
// alocado via unique_ptr de propósito: o endereço de cada nó precisa ficar
// estável enquanto a árvore cresce, porque guardamos ponteiros pra ele em
// mapas de busca (por nome na Origem, por id no Acervo) durante a
// construção. Um std::vector<NoArvore> comum invalidaria esses ponteiros a
// cada realocação — bug real que foi pensado e evitado aqui, não só teórico
// (a árvore Origem pode ter centenas de milhares de segmentos de caminho).
struct NoBuilder {
    juce::String nome;
    std::string id;
    std::vector<std::unique_ptr<NoBuilder>> filhos;
    std::map<juce::String, NoBuilder*> indiceFilhosPorNome; // dono é `filhos`; só busca
    std::set<std::string> itemIdsDiretos;
};

NoBuilder* obterOuCriarFilhoPorNome(NoBuilder& pai, const juce::String& nome) {
    auto it = pai.indiceFilhosPorNome.find(nome);
    if (it != pai.indiceFilhosPorNome.end()) return it->second;
    auto novo = std::make_unique<NoBuilder>();
    novo->nome = nome;
    NoBuilder* ptr = novo.get();
    pai.indiceFilhosPorNome[nome] = ptr;
    pai.filhos.push_back(std::move(novo));
    return ptr;
}

juce::String csvDeConjunto(const std::set<juce::String>& s) {
    juce::StringArray arr;
    for (auto& v : s) arr.add(v);
    return arr.joinIntoString(",");
}

std::set<juce::String> conjuntoDeCsv(const juce::String& csv) {
    std::set<juce::String> out;
    for (auto& v : juce::StringArray::fromTokens(csv, ",", ""))
        if (v.isNotEmpty()) out.insert(v);
    return out;
}

ProjetoAberto::NoArvore materializar(const NoBuilder& b, bool ordenarAlfabetico) {
    ProjetoAberto::NoArvore n;
    n.id = b.id;
    n.nome = b.nome;
    n.itemIds = b.itemIdsDiretos;
    n.itemIdsDiretos = b.itemIdsDiretos;

    std::vector<const NoBuilder*> ordemFilhos;
    ordemFilhos.reserve(b.filhos.size());
    for (auto& f : b.filhos) ordemFilhos.push_back(f.get());
    if (ordenarAlfabetico)
        std::sort(ordemFilhos.begin(), ordemFilhos.end(),
                  [](const NoBuilder* a, const NoBuilder* c) { return a->nome.compareIgnoreCase(c->nome) < 0; });

    n.filhos.reserve(ordemFilhos.size());
    for (auto* filho : ordemFilhos) {
        ProjetoAberto::NoArvore noFilho = materializar(*filho, ordenarAlfabetico);
        for (auto& id : noFilho.itemIds) n.itemIds.insert(id);
        n.filhos.push_back(std::move(noFilho));
    }
    return n;
}

} // namespace

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
        " AND c.campo_id = 'titulo'), "
        "(SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem'), "
        "(SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'ano') "
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
        if (!stmt.columnIsNull(9))
            r.extensaoArquivo = juce::File(juce::String(stmt.columnText(9)))
                                     .getFileExtension()
                                     .trimCharactersAtStart(".")
                                     .toLowerCase()
                                     .toStdString();
        if (!stmt.columnIsNull(10)) r.origem = stmt.columnText(10);
        if (!stmt.columnIsNull(11)) {
            juce::String anoTexto = stmt.columnText(11);
            if (anoTexto.containsOnly("0123456789")) r.ano = anoTexto.getIntValue();
        }

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

int ProjetoAberto::definirCapa(const std::vector<std::string>& itemIds, const juce::File& imagem) {
    if (!imagem.existsAsFile()) return 0;

    int aplicadas = 0;
    for (auto& itemId : itemIds) {
        // Uma capa por item: trocar substitui a anterior em vez de empilhar
        // capas que ninguém mais consegue distinguir.
        removerCapa({itemId});

        std::string arquivoId = matriz::model::novoUuid();
        juce::File destino = projeto_->pasta().getChildFile("arquivos").getChildFile(arquivoId).getChildFile(
            imagem.getFileName());
        destino.getParentDirectory().createDirectory();
        if (!imagem.copyFileTo(destino)) continue; // falha num item não derruba os outros

        std::string agora = matriz::model::agoraIso8601();
        projeto_->registro().run(
            "INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, "
            "criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'capa_frente', 0, ?, ?)",
            {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId),
             matriz::db::Value::of(destino.getRelativePathFrom(projeto_->pasta()).toStdString()),
             matriz::db::Value::of(imagem.getFullPathName().toStdString()), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});

        // Grava a miniatura por cima: caminhoMiniaturaPrincipal() resolve por
        // gerado_em DESC, então a linha nova passa a valer sem precisar
        // apagar a gerada — e apagar a capa depois faz a antiga voltar sozinha.
        matriz::ingest::gerarEGravarMiniaturaPrincipal(projeto_->indice(), projeto_->pasta(), itemId, arquivoId,
                                                        destino, matriz::ingest::CategoriaMidia::Imagem,
                                                        std::nullopt);
        ++aplicadas;
    }
    return aplicadas;
}

void ProjetoAberto::removerCapa(const std::vector<std::string>& itemIds) {
    for (auto& itemId : itemIds) {
        // Primeiro as miniaturas geradas A PARTIR da capa (no índice, que é
        // outro banco — não há CASCADE entre os dois).
        auto stmt = projeto_->registro().prepare(
            "SELECT id FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'");
        stmt.bind(1, matriz::db::Value::of(itemId));
        std::vector<std::string> capas;
        while (stmt.step()) capas.push_back(stmt.columnText(0));

        for (auto& arquivoId : capas)
            projeto_->indice().run("DELETE FROM miniatura WHERE arquivo_id = ?",
                                    {matriz::db::Value::of(arquivoId)});

        projeto_->registro().run("DELETE FROM arquivo WHERE item_id = ? AND papel = 'capa_frente'",
                                  {matriz::db::Value::of(itemId)});
    }
}

bool ProjetoAberto::temCapa(const std::string& itemId) const {
    auto stmt = projeto_->registro().prepare(
        "SELECT 1 FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    return stmt.step();
}

std::vector<std::string> ProjetoAberto::valoresUsadosNoCampo(const std::string& campoId) const {
    std::vector<std::string> out;
    // Restrito aos itens DESTE projeto: o registro é por projeto, mas a
    // junção deixa isso explícito e resiste a um banco que um dia guarde
    // mais de um. Valor vazio não é vocabulário — é campo não preenchido.
    auto stmt = projeto_->registro().prepare(
        "SELECT DISTINCT ic.valor FROM item_campo ic JOIN item i ON i.id = ic.item_id "
        "WHERE i.projeto_id = ? AND ic.campo_id = ? AND ic.valor IS NOT NULL AND TRIM(ic.valor) <> '' "
        "ORDER BY ic.valor COLLATE NOCASE");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(campoId));
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

std::optional<ProjetoAberto::ArquivoInfo> ProjetoAberto::arquivoPrincipal(const std::string& itemId) const {
    auto stmt = projeto_->registro().prepare(
        "SELECT id, caminho_relativo, papel, eh_master, caracteristicas_tecnicas_json FROM arquivo "
        "WHERE item_id = ? ORDER BY eh_master DESC, id LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;

    ArquivoInfo info;
    info.id = stmt.columnText(0);
    juce::String relativo = stmt.columnText(1);
    info.caminhoAbsoluto = projeto_->pasta().getChildFile(relativo).getFullPathName();
    info.papel = stmt.columnText(2);
    info.ehMaster = stmt.columnInt(3) != 0;
    info.caracteristicasTecnicasJson = stmt.columnText(4);
    return info;
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

std::vector<ProjetoAberto::ItemObservacao> ProjetoAberto::observacoesDoItem(const std::string& itemId) const {
    std::vector<ItemObservacao> out;
    auto stmt = projeto_->registro().prepare(
        "SELECT id, texto, autor, criado_em, minutagem_ms FROM item_observacao WHERE item_id = ? ORDER BY criado_em");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) {
        ItemObservacao o;
        o.id = stmt.columnText(0);
        o.texto = stmt.columnText(1);
        o.autor = stmt.columnText(2);
        o.criadoEm = stmt.columnText(3);
        if (!stmt.columnIsNull(4)) o.minutagemMs = static_cast<int64_t>(stmt.columnInt(4));
        out.push_back(std::move(o));
    }
    return out;
}

std::string ProjetoAberto::adicionarObservacao(const std::string& itemId, const std::string& texto,
                                                std::optional<int64_t> minutagemMs, const std::string& autor) {
    std::string id = matriz::model::novoUuid();
    projeto_->registro().run(
        "INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms) VALUES (?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(itemId), matriz::db::Value::of(texto),
         matriz::db::Value::of(autor), matriz::db::Value::of(matriz::model::agoraIso8601()),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null()});
    return id;
}

void ProjetoAberto::atualizarObservacao(const std::string& observacaoId, const std::string& texto,
                                         std::optional<int64_t> minutagemMs) {
    projeto_->registro().run(
        "UPDATE item_observacao SET texto = ?, minutagem_ms = ? WHERE id = ?",
        {matriz::db::Value::of(texto),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null(),
         matriz::db::Value::of(observacaoId)});
}

void ProjetoAberto::removerObservacao(const std::string& observacaoId) {
    projeto_->registro().run("DELETE FROM item_observacao WHERE id = ?", {matriz::db::Value::of(observacaoId)});
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreOrigem() const {
    NoBuilder raiz;

    // Um caminho de origem por item — o do arquivo "principal" (mesmo
    // critério de arquivoPrincipal(): master primeiro), pra um item com
    // arquivos de origens diferentes (raro — ex.: capa escaneada em outra
    // pasta) aparecer uma vez só, pelo seu arquivo mais importante.
    auto stmt = projeto_->registro().prepare(
        "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a WHERE a.caminho_absoluto_origem IS NOT NULL "
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)");

    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();

        juce::StringArray segmentos;
        segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
        segmentos.removeEmptyStrings();

        NoBuilder* atual = &raiz;
        for (auto& seg : segmentos) atual = obterOuCriarFilhoPorNome(*atual, seg);
        atual->itemIdsDiretos.insert(itemId);
    }

    return materializar(raiz, true);
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreAcervo() const {
    struct Registro {
        std::string id;
        std::optional<std::string> paiId;
    };
    std::vector<Registro> registros;
    std::unordered_map<std::string, std::unique_ptr<NoBuilder>> porId;
    std::unordered_map<std::string, NoBuilder*> ptrPorId;

    auto stmt = projeto_->registro().prepare(
        "SELECT id, pasta_pai_id, nome FROM acervo_pasta WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        Registro r;
        r.id = stmt.columnText(0);
        if (!stmt.columnIsNull(1)) r.paiId = stmt.columnText(1);
        registros.push_back(r);

        auto no = std::make_unique<NoBuilder>();
        no->id = r.id;
        no->nome = stmt.columnText(2);
        ptrPorId[r.id] = no.get();
        porId[r.id] = std::move(no);
    }

    // Segunda passada: anexa cada nó ao pai (ou à raiz sintética), na ordem
    // da consulta (já ordenada por `ordem, criado_em`) — funciona mesmo se
    // uma subpasta aparecer antes da pai na consulta, porque todos os
    // NoBuilder já existem em porId antes desta passada começar.
    NoBuilder raiz;
    for (auto& r : registros) {
        NoBuilder* alvo = (r.paiId && ptrPorId.count(*r.paiId)) ? ptrPorId[*r.paiId] : &raiz;
        alvo->filhos.push_back(std::move(porId[r.id]));
    }

    auto stmtItens = projeto_->registro().prepare(
        "SELECT aip.pasta_id, aip.item_id FROM acervo_item_pasta aip "
        "JOIN acervo_pasta ap ON ap.id = aip.pasta_id WHERE ap.projeto_id = ?");
    stmtItens.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmtItens.step()) {
        auto it = ptrPorId.find(stmtItens.columnText(0));
        if (it != ptrPorId.end()) it->second->itemIdsDiretos.insert(stmtItens.columnText(1));
    }

    NoArvore raizFinal = materializar(raiz, false);
    raizFinal.id.clear();
    raizFinal.nome = juce::String();

    // "Não organizados" (§5.5) — itens do projeto sem nenhuma linha em
    // acervo_item_pasta, calculado por ausência, nunca guardado como fato
    // próprio no banco.
    NoArvore naoOrganizados;
    naoOrganizados.nome = matriz::i18n::t("arvore.nao_organizados");
    auto stmtOrfaos = projeto_->registro().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND id NOT IN (SELECT item_id FROM acervo_item_pasta)");
    stmtOrfaos.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmtOrfaos.step()) naoOrganizados.itemIds.insert(stmtOrfaos.columnText(0));
    raizFinal.filhos.push_back(std::move(naoOrganizados));

    return raizFinal;
}

std::string ProjetoAberto::criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId) {
    std::string id = matriz::model::novoUuid();
    std::string agora = matriz::model::agoraIso8601();

    int ordem = 0;
    {
        auto stmt = projeto_->registro().prepare(
            pastaPaiId ? "SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id = ?"
                       : "SELECT COALESCE(MAX(ordem), -1) + 1 FROM acervo_pasta WHERE pasta_pai_id IS NULL AND projeto_id = ?");
        stmt.bind(1, pastaPaiId ? matriz::db::Value::of(*pastaPaiId) : matriz::db::Value::of(projeto_->projetoId()));
        if (stmt.step()) ordem = stmt.columnInt(0);
    }

    projeto_->registro().run(
        "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         pastaPaiId ? matriz::db::Value::of(*pastaPaiId) : matriz::db::Value::null(), matriz::db::Value::of(nome),
         matriz::db::Value::of(ordem), matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
    return id;
}

void ProjetoAberto::renomearPastaAcervo(const std::string& pastaId, const std::string& novoNome) {
    projeto_->registro().run("UPDATE acervo_pasta SET nome = ?, atualizado_em = ? WHERE id = ?",
                              {matriz::db::Value::of(novoNome), matriz::db::Value::of(matriz::model::agoraIso8601()),
                               matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::apagarPastaAcervo(const std::string& pastaId) {
    // ON DELETE CASCADE no schema cuida de subpastas e de acervo_item_pasta
    // — nunca toca em `item` (planejamento é sempre reversível, §5.3).
    projeto_->registro().run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::adicionarItensAPasta(const std::vector<std::string>& itemIds, const std::string& pastaId) {
    std::string agora = matriz::model::agoraIso8601();
    for (auto& itemId : itemIds) {
        projeto_->registro().run(
            "INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(pastaId), matriz::db::Value::of(agora)});
    }
}

void ProjetoAberto::removerItensDoBackup(const std::vector<std::string>& itemIds) {
    for (auto& itemId : itemIds)
        projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ?",
                                  {matriz::db::Value::of(itemId)});
}

void ProjetoAberto::removerItensDoProjeto(const std::vector<std::string>& itemIds) {
    // ON DELETE CASCADE cuida de item_campo/arquivo/acervo_item_pasta. Nada
    // aqui toca o sistema de arquivos — ver nota no header.
    for (auto& itemId : itemIds)
        projeto_->registro().run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
}

void ProjetoAberto::renomearItens(const std::vector<std::string>& itemIds, const std::string& novoTitulo) {
    std::string agora = matriz::model::agoraIso8601();
    for (auto& itemId : itemIds)
        projeto_->registro().run("UPDATE item SET titulo = ?, atualizado_em = ? WHERE id = ?",
                                  {matriz::db::Value::of(novoTitulo), matriz::db::Value::of(agora),
                                   matriz::db::Value::of(itemId)});
}

std::optional<juce::String> ProjetoAberto::caminhoDeOrigem(const std::string& itemId) const {
    // Mesmo critério de arquivoPrincipal(): master primeiro.
    auto stmt = projeto_->registro().prepare(
        "SELECT caminho_absoluto_origem FROM arquivo WHERE item_id = ? AND caminho_absoluto_origem IS NOT NULL "
        "ORDER BY eh_master DESC, id LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;
    juce::String caminho = stmt.columnText(0);
    return caminho.isEmpty() ? std::nullopt : std::optional(caminho);
}

std::set<std::string> ProjetoAberto::itensComMesmoConteudo(const std::string& itemId) const {
    std::set<std::string> out;
    auto stmt = projeto_->registro().prepare(
        "SELECT DISTINCT a2.item_id FROM arquivo a1 JOIN arquivo a2 ON a2.checksum_sha256 = a1.checksum_sha256 "
        "WHERE a1.item_id = ? AND a1.checksum_sha256 IS NOT NULL AND a1.checksum_sha256 <> ''");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.insert(stmt.columnText(0));

    // Só o próprio item = não há duplicata; devolver {ele} faria a grade
    // filtrar pra um item só e parecer que "achou" alguma coisa.
    if (out.size() <= 1) out.clear();
    return out;
}

int ProjetoAberto::replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId,
                                              bool manterEstrutura) {
    if (!manterEstrutura) {
        // Achatar: tudo o que existe na subárvore (itemIds já é recursivo)
        // entra direto na pasta de destino, sem criar nível nenhum.
        std::vector<std::string> ids(origem.itemIds.begin(), origem.itemIds.end());
        if (ids.empty()) return 0;
        adicionarItensAPasta(ids, pastaPaiId);
        return static_cast<int>(ids.size());
    }

    // Manter estrutura (padrão). Uma transação só: replicar um catálogo com
    // milhares de pastas em auto-commit levaria um fsync por INSERT, e uma
    // falha no meio deixaria meia hierarquia montada.
    projeto_->registro().run("BEGIN", {});
    int vinculados = 0;
    try {
        // Pilha explícita em vez de recursão: uma hierarquia de disco pode
        // ser bem funda, e estourar a pilha nativa no meio de uma transação
        // seria o pior lugar possível pra descobrir isso.
        struct Pendente {
            const NoArvore* no;
            std::string pastaPaiId;
        };
        std::vector<Pendente> pilha{{&origem, pastaPaiId}};

        while (!pilha.empty()) {
            Pendente atual = pilha.back();
            pilha.pop_back();

            // A própria pasta arrastada também é recriada no destino — é o
            // que faz "arrastar a pasta X" produzir X lá dentro, e não só o
            // conteúdo dela derramado na pasta de destino.
            std::optional<std::string> pai =
                atual.pastaPaiId.empty() ? std::nullopt : std::optional(atual.pastaPaiId);
            std::string novaPastaId = criarPastaAcervo(atual.no->nome.toStdString(), pai);

            if (!atual.no->itemIdsDiretos.empty()) {
                std::vector<std::string> ids(atual.no->itemIdsDiretos.begin(), atual.no->itemIdsDiretos.end());
                adicionarItensAPasta(ids, novaPastaId);
                vinculados += static_cast<int>(ids.size());
            }

            for (auto& filho : atual.no->filhos) pilha.push_back({&filho, novaPastaId});
        }
        projeto_->registro().run("COMMIT", {});
    } catch (...) {
        projeto_->registro().run("ROLLBACK", {});
        throw;
    }
    return vinculados;
}

void ProjetoAberto::removerItemDaPasta(const std::string& itemId, const std::string& pastaId) {
    projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of(pastaId)});
}

std::set<std::string> ProjetoAberto::buscarItens(const juce::String& texto) const {
    std::set<std::string> out;
    juce::String termo = texto.trim();
    if (termo.isEmpty()) return out;
    juce::String coringa = "%" + termo + "%";
    std::string projetoId = projeto_->projetoId();

    // Três fontes de texto buscável hoje: código/título do item, valor de
    // qualquer campo de ficha, e assunto (§10.1). OCR e transcrição não
    // existem ainda nesta etapa — nenhum texto extraído de imagem ou áudio
    // participa desta busca (gap declarado, não fingido).
    auto stmt = projeto_->registro().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND (codigo_acervo LIKE ? OR titulo LIKE ?) "
        "UNION "
        "SELECT ic.item_id FROM item_campo ic JOIN item i ON i.id = ic.item_id "
        "WHERE i.projeto_id = ? AND ic.valor LIKE ? "
        "UNION "
        "SELECT ia.item_id FROM item_assunto ia JOIN assunto a ON a.id = ia.assunto_id "
        "JOIN item i ON i.id = ia.item_id WHERE i.projeto_id = ? AND a.termo LIKE ?");
    stmt.bind(1, matriz::db::Value::of(projetoId));
    stmt.bind(2, matriz::db::Value::of(coringa.toStdString()));
    stmt.bind(3, matriz::db::Value::of(coringa.toStdString()));
    stmt.bind(4, matriz::db::Value::of(projetoId));
    stmt.bind(5, matriz::db::Value::of(coringa.toStdString()));
    stmt.bind(6, matriz::db::Value::of(projetoId));
    stmt.bind(7, matriz::db::Value::of(coringa.toStdString()));
    while (stmt.step()) out.insert(stmt.columnText(0));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorTipoMidia() const {
    std::map<std::string, int> out;
    auto stmt = projeto_->registro().prepare("SELECT tipo_midia, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY tipo_midia");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorEstado() const {
    std::map<std::string, int> out;
    auto stmt = projeto_->registro().prepare("SELECT estado, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY estado");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorExtensao() const {
    std::map<std::string, int> out;
    auto stmt = projeto_->registro().prepare(
        "SELECT (SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        if (stmt.columnIsNull(0)) continue;
        juce::String ext =
            juce::File(juce::String(stmt.columnText(0))).getFileExtension().trimCharactersAtStart(".").toLowerCase();
        if (ext.isEmpty()) continue;
        out[ext.toStdString()]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorOrigem() const {
    std::map<std::string, int> out;
    auto stmt = projeto_->registro().prepare(
        "SELECT (SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem') "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnIsNull(0) ? std::string() : stmt.columnText(0)]++;
    return out;
}

std::set<std::string> ProjetoAberto::itensPorFaixaAno(int anoDe, int anoAte) const {
    std::set<std::string> out;
    auto stmt = projeto_->registro().prepare(
        "SELECT c.item_id FROM item_campo c JOIN item i ON i.id = c.item_id "
        "WHERE i.projeto_id = ? AND c.nivel = 'raiz' AND c.nivel_indice = 0 AND c.campo_id = 'ano' "
        "AND CAST(c.valor AS INTEGER) BETWEEN ? AND ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    stmt.bind(2, matriz::db::Value::of(anoDe));
    stmt.bind(3, matriz::db::Value::of(anoAte));
    while (stmt.step()) out.insert(stmt.columnText(0));
    return out;
}

std::vector<ProjetoAberto::ColecaoInteligente> ProjetoAberto::listarColecoes() const {
    std::vector<ColecaoInteligente> out;
    auto stmt = projeto_->registro().prepare(
        "SELECT id, nome, busca_texto, filtros_tipo_midia, filtros_estado, filtros_extensao, filtros_origem, "
        "ano_de, ano_ate FROM colecao_inteligente "
        "WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        ColecaoInteligente c;
        c.id = stmt.columnText(0);
        c.nome = stmt.columnText(1);
        c.buscaTexto = stmt.columnText(2);
        c.filtrosTipoMidia = conjuntoDeCsv(stmt.columnText(3));
        c.filtrosEstado = conjuntoDeCsv(stmt.columnText(4));
        c.filtrosExtensao = conjuntoDeCsv(stmt.columnText(5));
        c.filtrosOrigem = conjuntoDeCsv(stmt.columnText(6));
        if (!stmt.columnIsNull(7)) c.anoDe = static_cast<int>(stmt.columnInt(7));
        if (!stmt.columnIsNull(8)) c.anoAte = static_cast<int>(stmt.columnInt(8));
        out.push_back(std::move(c));
    }
    return out;
}

std::string ProjetoAberto::salvarColecao(const ColecaoInteligente& colecao) {
    std::string id = colecao.id.empty() ? matriz::model::novoUuid() : colecao.id;
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run(
        "INSERT INTO colecao_inteligente (id, projeto_id, nome, busca_texto, filtros_tipo_midia, filtros_estado, "
        "filtros_extensao, filtros_origem, ano_de, ano_ate, ordem, criado_em) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(id) DO UPDATE SET nome = excluded.nome, busca_texto = excluded.busca_texto, "
        "filtros_tipo_midia = excluded.filtros_tipo_midia, filtros_estado = excluded.filtros_estado, "
        "filtros_extensao = excluded.filtros_extensao, filtros_origem = excluded.filtros_origem, "
        "ano_de = excluded.ano_de, ano_ate = excluded.ano_ate",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         matriz::db::Value::of(colecao.nome.toStdString()), matriz::db::Value::of(colecao.buscaTexto.toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosTipoMidia).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosEstado).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosExtensao).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosOrigem).toStdString()),
         colecao.anoDe ? matriz::db::Value::of(*colecao.anoDe) : matriz::db::Value::null(),
         colecao.anoAte ? matriz::db::Value::of(*colecao.anoAte) : matriz::db::Value::null(),
         matriz::db::Value::of(agora)});
    return id;
}

void ProjetoAberto::apagarColecao(const std::string& id) {
    projeto_->registro().run("DELETE FROM colecao_inteligente WHERE id = ?", {matriz::db::Value::of(id)});
}

} // namespace matriz::ui
