#include "ProjetoAberto.h"

#include "../Ingest/Miniaturas.h"
#include "../Ingest/ProcessoExterno.h"
#include "../Vault/Reconciliacao.h"
#include "../Vault/Resolucao.h"

#include "../I18n/Strings.h"

#include <algorithm>
#include <unordered_map>
#include <thread>
#include "EventBus.h"
#include "../Ficha/OrigemPadrao.h"

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
    std::string pastaPaiId;
    int posicaoX = 0;
    int posicaoY = 0;
    bool ativo = true;
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
    n.pastaPaiId = b.pastaPaiId;
    n.posicaoX = b.posicaoX;
    n.posicaoY = b.posicaoY;
    n.ativo = b.ativo;
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

ProjetoAberto::ProjetoAberto(std::unique_ptr<matriz::model::Project> projeto) : projeto_(std::move(projeto)) {
    juce::File registroFile = projeto_->pasta().getChildFile("registro.sqlite");
    juce::File pastaProjeto = projeto_->pasta();

    std::thread([registroFile, pastaProjeto]() {
        try {
            matriz::db::Database db(registroFile.getFullPathName().toStdString());

            struct FileToUpdate {
                std::string id;
                std::string localizacaoVault;
                std::string relativo;
                std::string origem;
            };
            std::vector<FileToUpdate> pendentes;

            {
                auto stmt = db.prepare(std::string("SELECT a.id, ") + matriz::vault::colunasDeResolucao() +
                                       " FROM arquivo a " + matriz::vault::joinDeResolucao() +
                                       " WHERE a.tamanho_bytes IS NULL");
                while (stmt.step()) {
                    pendentes.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnText(2),
                                          stmt.columnText(3)});
                }
            }

            if (!pendentes.empty()) {
                db.run("BEGIN TRANSACTION", {});
                for (const auto& item : pendentes) {
                    auto f = matriz::vault::resolverCaminho(pastaProjeto, item.localizacaoVault, item.relativo,
                                                             item.origem);
                    juce::int64 sz = f ? f->getSize() : 0;
                    db.run("UPDATE arquivo SET tamanho_bytes = ? WHERE id = ?",
                           {matriz::db::Value::of(static_cast<long long>(sz)), matriz::db::Value::of(item.id)});
                }
                db.run("COMMIT TRANSACTION", {});
            }
        } catch (...) {
            // Ignore/log errors safely
        }
    }).detach();
}

juce::int64 ProjetoAberto::tamanhoTotalDosMasters() const {
    if (!projeto_) return 0;
    auto stmt = projeto_->registro().prepare(
        "SELECT SUM(tamanho_bytes) FROM ("
        "  SELECT COALESCE(tamanho_bytes, 0) as tamanho_bytes, ROW_NUMBER() OVER (PARTITION BY item_id ORDER BY eh_master DESC, id) as rn "
        "  FROM arquivo"
        ") WHERE rn = 1");
    if (stmt.step()) {
        return static_cast<juce::int64>(stmt.columnInt(0));
    }
    return 0;
}

std::vector<ItemResumo> ProjetoAberto::listarItens() const {
    std::vector<ItemResumo> out;
    if (!projeto_) return out;

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
        " AND c.campo_id = 'ano'), "
        "(SELECT a.caminho_absoluto_origem FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
        "(SELECT ap.nome FROM acervo_item_pasta aip JOIN acervo_pasta ap ON ap.id = aip.pasta_id WHERE aip.item_id = i.id LIMIT 1), "
        "(SELECT a.tamanho_bytes FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
        "i.content_type, i.collection_type, i.criado_em "
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
        if (!stmt.columnIsNull(9)) {
            juce::String caminho9 = stmt.columnText(9);
            int dotPos = caminho9.lastIndexOfChar('.');
            if (dotPos >= 0)
                r.extensaoArquivo = caminho9.substring(dotPos + 1).toLowerCase().toStdString();
        }
        if (!stmt.columnIsNull(10)) r.origem = stmt.columnText(10);
        if (!stmt.columnIsNull(11)) {
            juce::String anoTexto = stmt.columnText(11);
            if (anoTexto.containsOnly("0123456789") && anoTexto.isNotEmpty()) r.ano = anoTexto.getIntValue();
        }
        if (!stmt.columnIsNull(15)) r.contentType = stmt.columnText(15);
        if (!stmt.columnIsNull(16)) r.collectionType = stmt.columnText(16);
        r.criadoEm = stmt.columnText(17);

        // Fallback: extract creation year ONLY from dc_created, ano, data_criacao or EXIF original date
        if (!r.ano.has_value()) {
            try {
                auto yrStmt = projeto_->registro().prepare(
                    "SELECT valor FROM item_campo WHERE item_id = ? AND campo_id IN ('dc_created', 'ano', 'data_criacao') AND valor IS NOT NULL AND valor != '' LIMIT 1");
                yrStmt.bind(1, matriz::db::Value::of(r.id));
                if (yrStmt.step()) {
                    juce::String val = yrStmt.columnText(0);
                    for (int i = 0; i + 3 < val.length(); ++i) {
                        if (std::isdigit(val[i]) && std::isdigit(val[i+1]) &&
                            std::isdigit(val[i+2]) && std::isdigit(val[i+3])) {
                            int yVal = val.substring(i, i + 4).getIntValue();
                            if (yVal > 1800 && yVal <= 2025) { r.ano = yVal; break; }
                        }
                    }
                }
            } catch (...) {}

            if (!r.ano.has_value()) {
                try {
                    auto exifStmt = projeto_->registro().prepare(
                        "SELECT caracteristicas_tecnicas_json FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
                    exifStmt.bind(1, matriz::db::Value::of(r.id));
                    if (exifStmt.step() && !exifStmt.columnIsNull(0)) {
                        auto jsonStr = exifStmt.columnText(0);
                        auto varObj = juce::JSON::parse(jsonStr);
                        if (varObj.isObject() && varObj.hasProperty("exifDataOriginal")) {
                            juce::String exifDt = varObj["exifDataOriginal"].toString();
                            for (int i = 0; i + 3 < exifDt.length(); ++i) {
                                if (std::isdigit(exifDt[i]) && std::isdigit(exifDt[i+1]) &&
                                    std::isdigit(exifDt[i+2]) && std::isdigit(exifDt[i+3])) {
                                    int yVal = exifDt.substring(i, i + 4).getIntValue();
                                    if (yVal > 1800 && yVal <= 2025) { r.ano = yVal; break; }
                                }
                            }
                        }
                    }
                } catch (...) {}
            }

            if (!r.ano.has_value() && !stmt.columnIsNull(12)) {
                try {
                    juce::File fileObj(stmt.columnText(12));
                    if (fileObj.existsAsFile()) {
                        int yVal = fileObj.getCreationTime().getYear();
                        if (yVal <= 1970 || yVal > 2025) yVal = fileObj.getLastModificationTime().getYear();
                        if (yVal > 1800 && yVal <= 2025) r.ano = yVal;
                    }
                } catch (...) {}
            }
        }

        if (!r.titulo.empty()) {
            r.nomeOriginalArquivo = r.titulo;
        } else if (!stmt.columnIsNull(9)) {
            juce::String caminho9 = stmt.columnText(9);
            int slashPos = std::max(caminho9.lastIndexOfChar('/'), caminho9.lastIndexOfChar('\\'));
            r.nomeOriginalArquivo = (slashPos >= 0 ? caminho9.substring(slashPos + 1) : caminho9).toStdString();
        } else if (!stmt.columnIsNull(12)) {
            juce::String caminho12 = stmt.columnText(12);
            int slashPos = std::max(caminho12.lastIndexOfChar('/'), caminho12.lastIndexOfChar('\\'));
            r.nomeOriginalArquivo = (slashPos >= 0 ? caminho12.substring(slashPos + 1) : caminho12).toStdString();
        }

        if (!stmt.columnIsNull(13)) r.pastaNome = stmt.columnText(13);
        r.tamanhoBytes = stmt.columnIsNull(14) ? 0 : static_cast<juce::int64>(stmt.columnInt(14));

        out.push_back(std::move(r));
    }
    return out;
}

std::vector<ProjetoAberto::ItemDetalhe> ProjetoAberto::obterDetalhesItens(const std::set<std::string>& itemIds) const {
    std::vector<ItemDetalhe> out;
    if (!projeto_ || itemIds.empty()) return out;
    out.reserve(itemIds.size());

    auto stmt = projeto_->registro().prepare(
        "SELECT i.id, i.titulo, "
        "(SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1), "
        "(SELECT a.tamanho_bytes FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.id = ?");

    for (const auto& id : itemIds) {
        stmt.reset();
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step()) continue;

        ItemDetalhe d;
        d.id = stmt.columnText(0);
        auto titulo = stmt.columnText(1);
        auto caminho = stmt.columnIsNull(2) ? std::string{} : stmt.columnText(2);

        if (!titulo.empty()) {
            d.nome = titulo;
        } else if (!caminho.empty()) {
            juce::String c(caminho);
            int slashPos = std::max(c.lastIndexOfChar('/'), c.lastIndexOfChar('\\'));
            d.nome = (slashPos >= 0 ? c.substring(slashPos + 1) : c).toStdString();
        }

        if (!caminho.empty()) {
            juce::String c(caminho);
            int dotPos = c.lastIndexOfChar('.');
            if (dotPos >= 0)
                d.extensao = c.substring(dotPos + 1).toLowerCase().toStdString();
        }

        d.tamanhoBytes = stmt.columnIsNull(3) ? 0 : static_cast<juce::int64>(stmt.columnInt(3));
        out.push_back(std::move(d));
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
    if (!projeto_) return std::nullopt;
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
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare("SELECT DISTINCT papel FROM arquivo WHERE item_id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

int ProjetoAberto::definirCapa(const std::vector<std::string>& itemIds, const juce::File& imagem) {
    if (!projeto_ || !imagem.existsAsFile()) return 0;

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
            "INSERT INTO arquivo (id, item_id, caminho_relativo, caminho_absoluto_origem, papel, eh_master, tamanho_bytes, "
            "criado_em, atualizado_em) VALUES (?, ?, ?, ?, 'capa_frente', 0, ?, ?, ?)",
            {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId),
             matriz::db::Value::of(destino.getRelativePathFrom(projeto_->pasta()).toStdString()),
             matriz::db::Value::of(imagem.getFullPathName().toStdString()),
             matriz::db::Value::of(static_cast<long long>(destino.getSize())),
             matriz::db::Value::of(agora),
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
    if (!projeto_) return;
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
    if (!projeto_) return false;
    auto stmt = projeto_->registro().prepare(
        "SELECT 1 FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    return stmt.step();
}

std::vector<std::string> ProjetoAberto::valoresUsadosNoCampo(const std::string& campoId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
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

std::optional<std::string> ProjetoAberto::lerMetadado(const std::string& itemId, const std::string& coluna) const {
    if (!projeto_) return std::nullopt;
    static const std::set<std::string> kColunasPermitidas = {
        "ano", "caminho_catalogo", "content_type", "source_media", "collection_type", "isrc", "notas_livres", "titulo",
        "dc_title", "dc_creator", "dc_subject", "dc_description", "dc_publisher", "dc_contributor",
        "dc_created", "dc_issued", "dc_type", "dc_format", "dc_identifier", "dc_source",
        "dc_language", "dc_relation", "dc_coverage", "dc_rights"
    };
    if (kColunasPermitidas.find(coluna) == kColunasPermitidas.end()) return std::nullopt;

    try {
        auto stmt = projeto_->registro().prepare("SELECT " + coluna + " FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            std::string val = stmt.columnText(0);
            if (!val.empty()) return val;
        }
    } catch (...) {}

    try {
        auto stmt = projeto_->registro().prepare("SELECT valor FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
        stmt.bind(1, matriz::db::Value::of(itemId));
        stmt.bind(2, matriz::db::Value::of(coluna));
        if (stmt.step() && !stmt.columnIsNull(0)) {
            std::string val = stmt.columnText(0);
            if (!val.empty()) return val;
        }
    } catch (...) {}

    return std::nullopt;
}

void ProjetoAberto::iniciarGrupoUndo(const std::string& descricao) {
    if (desfazendo_) return;
    if (grupoAberto_) finalizarGrupoUndo();
    grupoAberto_ = UndoGroup{descricao, {}};
}

void ProjetoAberto::finalizarGrupoUndo() {
    if (!grupoAberto_ || grupoAberto_->mudancas.empty()) {
        grupoAberto_.reset();
        return;
    }
    pilhaUndo_.push_back(std::move(*grupoAberto_));
    grupoAberto_.reset();
    while (static_cast<int>(pilhaUndo_.size()) > kMaxUndo)
        pilhaUndo_.erase(pilhaUndo_.begin());
    if (aoMudarUndo) aoMudarUndo();
}

bool ProjetoAberto::desfazer() {
    if (pilhaUndo_.empty()) return false;
    auto grupo = std::move(pilhaUndo_.back());
    pilhaUndo_.pop_back();
    desfazendo_ = true;
    for (auto it = grupo.mudancas.rbegin(); it != grupo.mudancas.rend(); ++it)
        salvarMetadado(it->itemId, it->coluna, it->valorAnterior);
    desfazendo_ = false;
    if (aoMudarUndo) aoMudarUndo();
    return true;
}

std::string ProjetoAberto::descricaoUndoAtual() const {
    if (pilhaUndo_.empty()) return {};
    return pilhaUndo_.back().descricao;
}

void ProjetoAberto::salvarMetadado(const std::string& itemId, const std::string& coluna, const std::string& valor) {
    if (!projeto_) return;

    if (!desfazendo_) {
        auto old = lerMetadado(itemId, coluna);
        UndoChange change{itemId, coluna, old.value_or("")};
        if (grupoAberto_) {
            grupoAberto_->mudancas.push_back(std::move(change));
        } else {
            pilhaUndo_.push_back(UndoGroup{"Edit " + coluna, {std::move(change)}});
            while (static_cast<int>(pilhaUndo_.size()) > kMaxUndo)
                pilhaUndo_.erase(pilhaUndo_.begin());
            if (aoMudarUndo) aoMudarUndo();
        }
    }

    // Try updating column on item table
    try {
        projeto_->registro().run(
            "UPDATE item SET " + coluna + " = ?, atualizado_em = ? WHERE id = ?",
            {matriz::db::Value::of(valor),
             matriz::db::Value::of(matriz::model::agoraIso8601()),
             matriz::db::Value::of(itemId)});
    } catch (...) {}

    // Sync to item_campo
    std::string agora = matriz::model::agoraIso8601();
    try {
        projeto_->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
            "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, fonte = 'humano', atualizado_em = excluded.atualizado_em",
            {matriz::db::Value::of(matriz::model::novoUuid()),
             matriz::db::Value::of(itemId),
             matriz::db::Value::of(coluna),
             matriz::db::Value::of(valor),
             matriz::db::Value::of(agora)});
    } catch (...) {}
}

std::vector<std::string> ProjetoAberto::lerTags(const std::string& itemId) const {
    std::vector<std::string> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag COLLATE NOCASE");
    stmt.bind(1, matriz::db::Value::of(itemId));
    while (stmt.step()) out.push_back(stmt.columnText(0));
    return out;
}

void ProjetoAberto::definirTags(const std::string& itemId, const std::vector<std::string>& tags) {
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM item_tag WHERE item_id = ?",
                              {matriz::db::Value::of(itemId)});
    for (const auto& tag : tags) {
        if (tag.empty()) continue;
        projeto_->registro().run(
            "INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()),
             matriz::db::Value::of(itemId),
             matriz::db::Value::of(tag)});
    }
    // Index tags in FTS
    projeto_->registro().run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo LIKE '#%'",
                              {matriz::db::Value::of(itemId)});
    for (const auto& tag : tags) {
        if (tag.empty()) continue;
        projeto_->registro().run(
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + tag)});
    }
}

void ProjetoAberto::adicionarTag(const std::string& itemId, const std::string& tag) {
    if (!projeto_ || tag.empty()) return;
    projeto_->registro().run(
        "INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()),
         matriz::db::Value::of(itemId),
         matriz::db::Value::of(tag)});
    projeto_->registro().run(
        "INSERT INTO busca_fts(item_id, conteudo) VALUES (?, ?)",
        {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + tag)});
}

void ProjetoAberto::removerTag(const std::string& itemId, const std::string& tag) {
    if (!projeto_ || tag.empty()) return;
    projeto_->registro().run("DELETE FROM item_tag WHERE item_id = ? AND tag = ?",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of(tag)});
    projeto_->registro().run("DELETE FROM busca_fts WHERE item_id = ? AND conteudo = ?",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of("#" + tag)});
}

std::optional<juce::String> ProjetoAberto::caminhoMiniaturaPrincipal(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = projeto_->indice().prepare(
        "SELECT caminho_relativo FROM miniatura WHERE item_id = ? AND tipo = 'miniatura' ORDER BY gerado_em DESC LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;
    juce::String relativo = stmt.columnText(0);
    return projeto_->pasta().getChildFile(relativo).getFullPathName();
}

void ProjetoAberto::gerarMiniaturasFaltantes() {
    if (!projeto_) { DBG("gerarMiniaturasFaltantes: projeto_ é nulo"); return; }
    auto& reg = projeto_->registro();
    auto& idx = projeto_->indice();
    auto pasta = projeto_->pasta();

    std::set<std::string> comMiniatura;
    {
        auto stmt = idx.prepare("SELECT DISTINCT item_id FROM miniatura WHERE tipo = 'miniatura'");
        while (stmt.step()) comMiniatura.insert(stmt.columnText(0));
    }
    DBG("gerarMiniaturasFaltantes: " + juce::String((int)comMiniatura.size()) + " itens já têm miniatura");

    auto stmt = reg.prepare(
        std::string("SELECT a.item_id, a.id, a.caracteristicas_tecnicas_json, ") +
        matriz::vault::colunasDeResolucao() +
        " FROM arquivo a " + matriz::vault::joinDeResolucao() +
        " ORDER BY a.eh_master DESC, a.id");

    std::set<std::string> jaProcessados;
    int gerados = 0, pulados = 0;
    while (stmt.step()) {
        std::string itemId = stmt.columnText(0);
        if (comMiniatura.count(itemId) || jaProcessados.count(itemId)) continue;
        jaProcessados.insert(itemId);

        std::string arquivoId = stmt.columnText(1);
        std::string jsonTecnico = stmt.columnText(2);
        juce::File arq = matriz::vault::caminhoEsperado(pasta, stmt.columnText(3),
                                                         stmt.columnText(4), stmt.columnText(5));
        if (!arq.existsAsFile()) { ++pulados; continue; }

        auto cat = matriz::ingest::categoriaPorExtensao(arq);
        if (cat != matriz::ingest::CategoriaMidia::Video &&
            cat != matriz::ingest::CategoriaMidia::Audio &&
            cat != matriz::ingest::CategoriaMidia::Imagem)
            continue;

        std::optional<double> duracao;
        juce::var raiz = juce::JSON::parse(jsonTecnico);
        if (raiz.isObject() && raiz.hasProperty("duracaoSegundos"))
            duracao = static_cast<double>(raiz["duracaoSegundos"]);

        if (!duracao.has_value() && (cat == matriz::ingest::CategoriaMidia::Video ||
                                     cat == matriz::ingest::CategoriaMidia::Audio)) {
            try {
                juce::StringArray args;
                args.add("-v"); args.add("quiet");
                args.add("-show_entries"); args.add("format=duration");
                args.add("-of"); args.add("default=noprint_wrappers=1:nokey=1");
                args.add(arq.getFullPathName());
                auto saida = matriz::ingest::capturarSaidaTexto("ffprobe", args, 10000);
                double d = juce::String(saida).trim().getDoubleValue();
                if (d > 0.0) duracao = d;
            } catch (...) {}
        }

        DBG("gerarMiniatura: " + arq.getFileName() + " cat=" + juce::String((int)cat)
            + " dur=" + juce::String(duracao.value_or(-1.0)));

        try {
            matriz::ingest::gerarEGravarMiniaturaPrincipal(idx, pasta, itemId, arquivoId, arq, cat, duracao);
            ++gerados;
        } catch (const std::exception& e) {
            DBG("gerarMiniatura ERRO: " + juce::String(e.what()));
        }
    }
    DBG("gerarMiniaturasFaltantes: gerados=" + juce::String(gerados) + " pulados=" + juce::String(pulados)
        + " processados=" + juce::String((int)jaProcessados.size()));
}

std::optional<ProjetoAberto::ArquivoInfo> ProjetoAberto::arquivoPrincipal(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    auto stmt = projeto_->registro().prepare(
        std::string("SELECT a.id, a.papel, a.eh_master, a.caracteristicas_tecnicas_json, ") +
        matriz::vault::colunasDeResolucao() + " FROM arquivo a " + matriz::vault::joinDeResolucao() +
        " WHERE a.item_id = ? ORDER BY a.eh_master DESC, a.id LIMIT 1");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return std::nullopt;

    ArquivoInfo info;
    info.id = stmt.columnText(0);
    info.papel = stmt.columnText(1);
    info.ehMaster = stmt.columnInt(2) != 0;
    info.caracteristicasTecnicasJson = stmt.columnText(3);
    // Caminho ESPERADO, não resolvido: com o Vault offline (I3) a ficha
    // continua abrindo e o painel precisa poder dizer onde o arquivo mora.
    // Quem vai realmente ler os bytes checa existsAsFile().
    info.caminhoAbsoluto = matriz::vault::caminhoEsperado(projeto_->pasta(), stmt.columnText(4),
                                                           stmt.columnText(5), stmt.columnText(6))
                               .getFullPathName();
    return info;
}

std::optional<ProjetoAberto::SugestaoCampo> ProjetoAberto::sugestaoPendente(const std::string& itemId,
                                                                             const std::string& nivel, int nivelIndice,
                                                                             const std::string& campoId) const {
    if (!projeto_) return std::nullopt;
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
    if (!projeto_) return;
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
    if (!projeto_) return out;
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
    if (!projeto_) return {};
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
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE item_observacao SET texto = ?, minutagem_ms = ? WHERE id = ?",
        {matriz::db::Value::of(texto),
         minutagemMs ? matriz::db::Value::of(static_cast<long long>(*minutagemMs)) : matriz::db::Value::null(),
         matriz::db::Value::of(observacaoId)});
}

void ProjetoAberto::removerObservacao(const std::string& observacaoId) {
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM item_observacao WHERE id = ?", {matriz::db::Value::of(observacaoId)});
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreOrigem(bool incluirTodos) const {
    if (!projeto_) return {};

    NoBuilder raiz;

    std::string sql =
        "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a WHERE a.caminho_absoluto_origem IS NOT NULL "
        "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)";
    if (!incluirTodos)
        sql += " AND a.item_id NOT IN (SELECT item_id FROM acervo_item_pasta)";
    auto stmt = projeto_->registro().prepare(sql);

    struct Par { std::string itemId; juce::StringArray segmentos; };
    std::vector<Par> pares;

    while (stmt.step()) {
        Par p;
        p.itemId = stmt.columnText(0);
        juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();
        p.segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
        p.segmentos.removeEmptyStrings();
        pares.push_back(std::move(p));
    }

    // Collapse common prefix: only show from the loaded folder downward,
    // never volumes/HDs/ancestor directories.
    int prefixoComum = 0;
    if (!pares.empty()) {
        prefixoComum = pares[0].segmentos.size();
        for (size_t i = 1; i < pares.size(); ++i) {
            int n = juce::jmin(prefixoComum, pares[i].segmentos.size());
            int match = 0;
            while (match < n && pares[0].segmentos[match] == pares[i].segmentos[match])
                ++match;
            prefixoComum = match;
        }
        if (prefixoComum > 0)
            prefixoComum = prefixoComum - 1;
    }

    for (auto& p : pares) {
        NoBuilder* atual = &raiz;
        for (int s = prefixoComum; s < p.segmentos.size(); ++s)
            atual = obterOuCriarFilhoPorNome(*atual, p.segmentos[s]);
        atual->itemIdsDiretos.insert(p.itemId);
    }

    return materializar(raiz, true);
}

ProjetoAberto::NoArvore ProjetoAberto::podarArvore(const NoArvore& raiz, const std::set<std::string>& idsPermitidos) {
    NoArvore podado;
    podado.id = raiz.id;
    podado.nome = raiz.nome;
    podado.pastaPaiId = raiz.pastaPaiId;
    podado.posicaoX = raiz.posicaoX;
    podado.posicaoY = raiz.posicaoY;
    podado.ativo = raiz.ativo;

    for (const auto& id : raiz.itemIds)
        if (idsPermitidos.count(id)) podado.itemIds.insert(id);
    for (const auto& id : raiz.itemIdsDiretos)
        if (idsPermitidos.count(id)) podado.itemIdsDiretos.insert(id);

    for (const auto& filho : raiz.filhos) {
        auto filhoPodado = podarArvore(filho, idsPermitidos);
        if (!filhoPodado.itemIds.empty() || !filhoPodado.filhos.empty())
            podado.filhos.push_back(std::move(filhoPodado));
    }

    return podado;
}

ProjetoAberto::NoArvore ProjetoAberto::arvoreAcervo() const {
    if (!projeto_) return {};

    struct Registro {
        std::string id;
        std::optional<std::string> paiId;
    };
    std::vector<Registro> registros;
    std::unordered_map<std::string, std::unique_ptr<NoBuilder>> porId;
    std::unordered_map<std::string, NoBuilder*> ptrPorId;

    auto stmt = projeto_->registro().prepare(
        "SELECT id, pasta_pai_id, nome, posicao_x, posicao_y, ativo FROM acervo_pasta WHERE projeto_id = ? ORDER BY ordem, criado_em");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        Registro r;
        r.id = stmt.columnText(0);
        if (!stmt.columnIsNull(1)) r.paiId = stmt.columnText(1);
        registros.push_back(r);

        auto no = std::make_unique<NoBuilder>();
        no->id = r.id;
        if (r.paiId) no->pastaPaiId = *r.paiId;
        no->nome = stmt.columnText(2);
        no->posicaoX = stmt.columnInt(3);
        no->posicaoY = stmt.columnInt(4);
        no->ativo = (stmt.columnInt(5) != 0);
        ptrPorId[r.id] = no.get();
        porId[r.id] = std::move(no);
    }

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

    NoArvore naoOrganizados;
    naoOrganizados.nome = matriz::i18n::t("arvore.nao_organizados");
    auto stmtOrfaos = projeto_->registro().prepare(
        "SELECT id FROM item WHERE projeto_id = ? AND id NOT IN (SELECT item_id FROM acervo_item_pasta)");
    stmtOrfaos.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmtOrfaos.step()) naoOrganizados.itemIds.insert(stmtOrfaos.columnText(0));
    naoOrganizados.itemIdsDiretos = naoOrganizados.itemIds;
    raizFinal.filhos.push_back(std::move(naoOrganizados));

    return raizFinal;
}

std::string ProjetoAberto::criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId) {
    if (!projeto_) return {};
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
        "INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, posicao_x, posicao_y, ativo, criado_em, atualizado_em) "
        "VALUES (?, ?, ?, ?, ?, 0, 0, 1, ?, ?)",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         pastaPaiId ? matriz::db::Value::of(*pastaPaiId) : matriz::db::Value::null(), matriz::db::Value::of(nome),
         matriz::db::Value::of(ordem), matriz::db::Value::of(agora), matriz::db::Value::of(agora)});
    return id;
}

void ProjetoAberto::renomearPastaAcervo(const std::string& pastaId, const std::string& novoNome) {
    if (!projeto_) return;
    projeto_->registro().run("UPDATE acervo_pasta SET nome = ?, atualizado_em = ? WHERE id = ?",
                              {matriz::db::Value::of(novoNome), matriz::db::Value::of(matriz::model::agoraIso8601()),
                               matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::apagarPastaAcervo(const std::string& pastaId) {
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM acervo_pasta WHERE id = ?", {matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::moverPastaAcervo(const std::string& pastaId, const std::optional<std::string>& novaPastaPaiId) {
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET pasta_pai_id = ?, atualizado_em = ? WHERE id = ?",
        {novaPastaPaiId ? matriz::db::Value::of(*novaPastaPaiId) : matriz::db::Value::null(),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::atualizarPosicaoPastaAcervo(const std::string& pastaId, int x, int y) {
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET posicao_x = ?, posicao_y = ?, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(x), matriz::db::Value::of(y),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::alternarAtivoPastaAcervo(const std::string& pastaId, bool ativo) {
    if (!projeto_) return;
    projeto_->registro().run(
        "UPDATE acervo_pasta SET ativo = ?, atualizado_em = ? WHERE id = ?",
        {matriz::db::Value::of(ativo ? 1 : 0),
         matriz::db::Value::of(matriz::model::agoraIso8601()), matriz::db::Value::of(pastaId)});
}

void ProjetoAberto::adicionarItensAPasta(const std::vector<std::string>& itemIds, const std::string& pastaId) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    for (auto& itemId : itemIds) {
        projeto_->registro().run(
            "INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(pastaId), matriz::db::Value::of(agora)});
    }
}

std::string ProjetoAberto::agruparItensEmNovaPasta(const std::vector<std::string>& itemIds) {
    if (!projeto_) return {};
    
    std::string newFolderId = criarPastaAcervo("New Folder", std::nullopt);
    
    std::string agora = matriz::model::agoraIso8601();
    for (const auto& itemId : itemIds) {
        projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ?",
                                 {matriz::db::Value::of(itemId)});
                                 
        projeto_->registro().run(
            "INSERT OR IGNORE INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(newFolderId), matriz::db::Value::of(agora)});
    }
    
    return newFolderId;
}

void ProjetoAberto::removerItensDoBackup(const std::vector<std::string>& itemIds) {
    if (!projeto_) return;
    for (auto& itemId : itemIds)
        projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ?",
                                  {matriz::db::Value::of(itemId)});
}

void ProjetoAberto::removerItensDoProjeto(const std::vector<std::string>& itemIds) {
    if (!projeto_) return;
    for (auto& itemId : itemIds)
        projeto_->registro().run("DELETE FROM item WHERE id = ?", {matriz::db::Value::of(itemId)});
}

void ProjetoAberto::renomearItens(const std::vector<std::string>& itemIds, const std::string& novoTitulo) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    for (auto& itemId : itemIds)
        projeto_->registro().run("UPDATE item SET titulo = ?, atualizado_em = ? WHERE id = ?",
                                  {matriz::db::Value::of(novoTitulo), matriz::db::Value::of(agora),
                                   matriz::db::Value::of(itemId)});
}

std::optional<juce::String> ProjetoAberto::caminhoDeOrigem(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
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
    if (!projeto_) return out;
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

std::vector<ProjetoAberto::ParDuplicatas> ProjetoAberto::listarGruposDuplicados() const {
    std::vector<ParDuplicatas> out;
    if (!projeto_) return out;

    struct DbInfo {
        std::string itemId;
        std::string codigoAcervo;
        std::string titulo;
        std::string tipoMidia;
        std::string estado;
        juce::String caminhoRelativo;
        juce::String caminhoOrigem;
        juce::int64 tamanhoBytes = 0;
        std::string checksumSha256;
    };

    std::vector<DbInfo> arquivos;

    auto stmt = projeto_->registro().prepare(
        "SELECT a.item_id, i.codigo_acervo, i.titulo, i.tipo_midia, i.estado, "
        "a.caminho_relativo, a.caminho_absoluto_origem, a.tamanho_bytes, a.checksum_sha256 "
        "FROM arquivo a "
        "JOIN item i ON i.id = a.item_id "
        "WHERE a.eh_master = 1 AND i.estado <> 'duplicata'");

    while (stmt.step()) {
        DbInfo inf;
        inf.itemId = stmt.columnText(0);
        inf.codigoAcervo = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
        inf.titulo = stmt.columnText(2);
        inf.tipoMidia = stmt.columnIsNull(3) ? "" : stmt.columnText(3);
        inf.estado = stmt.columnText(4);
        inf.caminhoRelativo = stmt.columnText(5);
        inf.caminhoOrigem = stmt.columnIsNull(6) ? "" : stmt.columnText(6);
        inf.tamanhoBytes = stmt.columnIsNull(7) ? 0 : static_cast<juce::int64>(stmt.columnInt(7));
        inf.checksumSha256 = stmt.columnIsNull(8) ? "" : stmt.columnText(8);

        if (inf.tamanhoBytes > 0 && !inf.checksumSha256.empty()) {
            arquivos.push_back(inf);
        }
    }

    auto extrairNome = [](const juce::String& rel, const juce::String& orig) -> juce::String {
        juce::String caminho = orig.isNotEmpty() ? orig : rel;
        int slashPos = std::max(caminho.lastIndexOfChar('/'), caminho.lastIndexOfChar('\\'));
        return slashPos >= 0 ? caminho.substring(slashPos + 1) : caminho;
    };

    std::map<std::tuple<juce::String, juce::int64, std::string>, std::vector<DbInfo>> grupos;
    for (const auto& a : arquivos) {
        juce::String fname = extrairNome(a.caminhoRelativo, a.caminhoOrigem).toLowerCase();
        auto key = std::make_tuple(fname, a.tamanhoBytes, a.checksumSha256);
        grupos[key].push_back(a);
    }

    for (const auto& [key, itensGrupo] : grupos) {
        std::set<std::string> itemIds;
        for (const auto& item : itensGrupo) {
            itemIds.insert(item.itemId);
        }

        if (itemIds.size() > 1) {
            ParDuplicatas grupo;
            const auto& firstItem = itensGrupo.front();
            grupo.filename = extrairNome(firstItem.caminhoRelativo, firstItem.caminhoOrigem);
            grupo.tamanhoBytes = std::get<1>(key);
            grupo.checksumSha256 = std::get<2>(key);

            for (const auto& dbItem : itensGrupo) {
                ParDuplicatas::ItemInfo info;
                info.id = dbItem.itemId;
                info.codigoAcervo = dbItem.codigoAcervo;
                info.titulo = dbItem.titulo;
                info.tipoMidia = dbItem.tipoMidia;
                info.estado = dbItem.estado;
                info.caminhoRelativo = dbItem.caminhoRelativo;
                info.caminhoOrigem = dbItem.caminhoOrigem;
                info.tamanhoBytes = dbItem.tamanhoBytes;
                grupo.itens.push_back(info);
            }
            out.push_back(std::move(grupo));
        }
    }

    return out;
}

void ProjetoAberto::atualizarEstadoItem(const std::string& itemId, const std::string& novoEstado) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run("UPDATE item SET estado = ?, atualizado_em = ? WHERE id = ?",
                             {matriz::db::Value::of(novoEstado), matriz::db::Value::of(agora),
                              matriz::db::Value::of(itemId)});
}

int ProjetoAberto::replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId,
                                              bool manterEstrutura) {
    if (!projeto_) return 0;
    if (!manterEstrutura) {
        std::vector<std::string> ids(origem.itemIds.begin(), origem.itemIds.end());
        if (ids.empty()) return 0;
        std::string destino = pastaPaiId;
        if (destino.empty())
            destino = criarPastaAcervo(origem.nome.toStdString(), std::nullopt);
        adicionarItensAPasta(ids, destino);
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

void ProjetoAberto::resetarEImportarEstruturaOrigem() {
    if (!projeto_) return;
    std::string projetoId = projeto_->projetoId();

    // 1. Collect original folder paths for ALL items (not just unorganized ones)
    struct Par { std::string itemId; juce::StringArray segmentos; };
    std::vector<Par> pares;

    {
        auto stmt = projeto_->registro().prepare(
            "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a "
            "JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.caminho_absoluto_origem IS NOT NULL "
            "AND a.id = (SELECT id FROM arquivo a2 WHERE a2.item_id = a.item_id ORDER BY eh_master DESC, id LIMIT 1)");
        stmt.bind(1, matriz::db::Value::of(projetoId));
        while (stmt.step()) {
            Par p;
            p.itemId = stmt.columnText(0);
            juce::File pasta = juce::File(juce::String(stmt.columnText(1))).getParentDirectory();
            p.segmentos.addTokens(pasta.getFullPathName(), juce::File::getSeparatorString(), "");
            p.segmentos.removeEmptyStrings();
            pares.push_back(std::move(p));
        }
    }

    // 2. Compute common prefix to strip volume/ancestor directories
    int prefixoComum = 0;
    if (!pares.empty()) {
        prefixoComum = pares[0].segmentos.size();
        for (size_t i = 1; i < pares.size(); ++i) {
            int n = juce::jmin(prefixoComum, pares[i].segmentos.size());
            int match = 0;
            while (match < n && pares[0].segmentos[match] == pares[i].segmentos[match])
                ++match;
            prefixoComum = match;
        }
        if (prefixoComum > 0)
            prefixoComum = prefixoComum - 1;
    }

    // 3. Inside a transaction: wipe existing tree, rebuild from original paths
    projeto_->registro().run("BEGIN", {});
    try {
        // Clear all item-folder assignments and all folders for this project
        projeto_->registro().run(
            "DELETE FROM acervo_item_pasta WHERE pasta_id IN "
            "(SELECT id FROM acervo_pasta WHERE projeto_id = ?)",
            {matriz::db::Value::of(projetoId)});
        projeto_->registro().run(
            "DELETE FROM acervo_pasta WHERE projeto_id = ?",
            {matriz::db::Value::of(projetoId)});

        // Rebuild: for each item, create the chain of folders from its original path
        // and place the item in its leaf folder.
        // Cache folder IDs by (parentId, name) to avoid duplicates.
        std::map<std::pair<std::string, std::string>, std::string> folderCache;

        for (auto& p : pares) {
            std::string currentParentId;
            for (int s = prefixoComum; s < p.segmentos.size(); ++s) {
                std::string segName = p.segmentos[s].toStdString();
                auto key = std::make_pair(currentParentId, segName);
                auto it = folderCache.find(key);
                if (it != folderCache.end()) {
                    currentParentId = it->second;
                } else {
                    std::optional<std::string> pai = currentParentId.empty()
                        ? std::nullopt : std::optional<std::string>(currentParentId);
                    std::string newId = criarPastaAcervo(segName, pai);
                    folderCache[key] = newId;
                    currentParentId = newId;
                }
            }

            // Assign item to its leaf folder
            if (!currentParentId.empty()) {
                adicionarItensAPasta({p.itemId}, currentParentId);
            }
        }

        projeto_->registro().run("COMMIT", {});
    } catch (...) {
        projeto_->registro().run("ROLLBACK", {});
        throw;
    }
}

void ProjetoAberto::removerItemDaPasta(const std::string& itemId, const std::string& pastaId) {
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM acervo_item_pasta WHERE item_id = ? AND pasta_id = ?",
                              {matriz::db::Value::of(itemId), matriz::db::Value::of(pastaId)});
}

std::set<std::string> ProjetoAberto::buscarItens(const juce::String& texto) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    juce::String termo = texto.trim();
    if (termo.isEmpty()) return out;
    std::string projetoId = projeto_->projetoId();

    // FTS5 não recebe texto: recebe uma LINGUAGEM de consulta, em que "/",
    // "-", ":", "(", "*" e aspas são operadores. O que o operador digita é
    // texto comum — um caminho de pasta, um código "ALLNO-00001" — e ia
    // direto pro MATCH cru, virando erro de sintaxe que subia como exceção e
    // derrubava a busca inteira. Aspas transformam tudo numa frase literal
    // (aspa interna escapa dobrando, como em SQL) e o "*" no fim continua
    // valendo como prefixo.
    juce::String ftsQuery = "\"" + termo.replace("\"", "\"\"") + "\"*";

    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT DISTINCT b.item_id FROM busca_fts b "
            "JOIN item i ON i.id = b.item_id "
            "WHERE i.projeto_id = ? AND busca_fts MATCH ?");
        stmt.bind(1, matriz::db::Value::of(projetoId));
        stmt.bind(2, matriz::db::Value::of(ftsQuery.toStdString()));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (const std::exception&) {
        // Busca é interação de digitação: um termo que o tokenizer descarta
        // por inteiro (só pontuação, por exemplo) devolve "nada encontrado",
        // nunca uma exceção subindo pela message thread.
        out.clear();
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorTipoMidia() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare("SELECT tipo_midia, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY tipo_midia");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorEstado() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare("SELECT estado, COUNT(*) FROM item WHERE projeto_id = ? GROUP BY estado");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorExtensao() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT (SELECT a.caminho_relativo FROM arquivo a WHERE a.item_id = i.id ORDER BY a.eh_master DESC, a.id LIMIT 1) "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        if (stmt.columnIsNull(0)) continue;
        juce::String caminho = stmt.columnText(0);
        int dotPos = caminho.lastIndexOfChar('.');
        juce::String ext = dotPos >= 0 ? caminho.substring(dotPos + 1).toLowerCase() : juce::String();
        if (ext.isEmpty()) continue;
        out[ext.toStdString()]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorOrigem() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT (SELECT valor FROM item_campo c WHERE c.item_id = i.id AND c.nivel = 'raiz' AND c.nivel_indice = 0 "
        " AND c.campo_id = 'origem') "
        "FROM item i WHERE i.projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) out[stmt.columnIsNull(0) ? std::string() : stmt.columnText(0)]++;
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorContentType() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT content_type FROM item WHERE projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        if (!v.empty()) out[v]++;
    }
    return out;
}

std::map<std::string, int> ProjetoAberto::contagensPorCollectionType() const {
    std::map<std::string, int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT collection_type FROM item WHERE projeto_id = ?");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        std::string v = stmt.columnIsNull(0) ? std::string() : stmt.columnText(0);
        if (!v.empty()) out[v]++;
    }
    return out;
}

std::set<std::string> ProjetoAberto::itensPorFaixaAno(int anoDe, int anoAte) const {
    std::set<std::string> out;
    if (!projeto_) return out;

    // 1. Check user-filled creation fields in item_campo (ano, dc_created, data_criacao)
    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT c.item_id, c.valor FROM item_campo c JOIN item i ON i.id = c.item_id "
            "WHERE i.projeto_id = ? AND c.campo_id IN ('ano', 'dc_created', 'data_criacao')");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string val = stmt.columnText(1);
            if (!val.empty()) {
                for (size_t i = 0; i + 3 < val.size(); ++i) {
                    if (std::isdigit(val[i]) && std::isdigit(val[i+1]) && std::isdigit(val[i+2]) && std::isdigit(val[i+3])) {
                        int yr = std::stoi(val.substr(i, 4));
                        if (yr >= anoDe && yr <= anoAte) {
                            out.insert(itemId);
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // 2. Direct column ano in item table
    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT id FROM item WHERE projeto_id = ? AND ano IS NOT NULL AND ano >= ? AND ano <= ?");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        stmt.bind(2, matriz::db::Value::of(anoDe));
        stmt.bind(3, matriz::db::Value::of(anoAte));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (...) {}

    // 3. EXIF creation date in arquivo.caracteristicas_tecnicas_json
    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT a.item_id, a.caracteristicas_tecnicas_json FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.eh_master = 1 AND a.caracteristicas_tecnicas_json LIKE '%exifDataOriginal%'");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string jsonStr = stmt.columnText(1);
            auto varObj = juce::JSON::parse(jsonStr);
            if (varObj.isObject() && varObj.hasProperty("exifDataOriginal")) {
                juce::String exifDt = varObj["exifDataOriginal"].toString();
                for (int i = 0; i + 3 < exifDt.length(); ++i) {
                    if (std::isdigit(exifDt[i]) && std::isdigit(exifDt[i+1]) &&
                        std::isdigit(exifDt[i+2]) && std::isdigit(exifDt[i+3])) {
                        int yr = exifDt.substring(i, i + 4).getIntValue();
                        if (yr >= anoDe && yr <= anoAte) {
                            out.insert(itemId);
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // 4. Physical file creation/modification date
    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT a.item_id, a.caminho_absoluto_origem FROM arquivo a JOIN item i ON i.id = a.item_id "
            "WHERE i.projeto_id = ? AND a.eh_master = 1 AND a.caminho_absoluto_origem IS NOT NULL AND a.caminho_absoluto_origem != ''");
        stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
        while (stmt.step()) {
            std::string itemId = stmt.columnText(0);
            std::string absPath = stmt.columnText(1);
            juce::File f(absPath);
            if (f.existsAsFile()) {
                int yr = f.getCreationTime().getYear();
                if (yr <= 1970 || yr > 2025) yr = f.getLastModificationTime().getYear();
                if (yr >= anoDe && yr <= anoAte) {
                    out.insert(itemId);
                }
            }
        }
    } catch (...) {}

    return out;
}

std::vector<ProjetoAberto::ColecaoInteligente> ProjetoAberto::listarColecoes() const {
    std::vector<ColecaoInteligente> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT id, nome, busca_texto, filtros_tipo_midia, filtros_estado, filtros_extensao, filtros_origem, "
        "ano_de, ano_ate, filtros_content_type, filtros_collection_type FROM colecao_inteligente "
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
        c.filtrosContentType = conjuntoDeCsv(stmt.columnText(9));
        c.filtrosCollectionType = conjuntoDeCsv(stmt.columnText(10));
        out.push_back(std::move(c));
    }
    return out;
}

std::string ProjetoAberto::salvarColecao(const ColecaoInteligente& colecao) {
    if (!projeto_) return {};
    std::string id = colecao.id.empty() ? matriz::model::novoUuid() : colecao.id;
    std::string agora = matriz::model::agoraIso8601();
    projeto_->registro().run(
        "INSERT INTO colecao_inteligente (id, projeto_id, nome, busca_texto, filtros_tipo_midia, filtros_estado, "
        "filtros_extensao, filtros_origem, filtros_content_type, filtros_collection_type, ano_de, ano_ate, ordem, criado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(id) DO UPDATE SET nome = excluded.nome, busca_texto = excluded.busca_texto, "
        "filtros_tipo_midia = excluded.filtros_tipo_midia, filtros_estado = excluded.filtros_estado, "
        "filtros_extensao = excluded.filtros_extensao, filtros_origem = excluded.filtros_origem, "
        "filtros_content_type = excluded.filtros_content_type, filtros_collection_type = excluded.filtros_collection_type, "
        "ano_de = excluded.ano_de, ano_ate = excluded.ano_ate",
        {matriz::db::Value::of(id), matriz::db::Value::of(projeto_->projetoId()),
         matriz::db::Value::of(colecao.nome.toStdString()), matriz::db::Value::of(colecao.buscaTexto.toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosTipoMidia).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosEstado).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosExtensao).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosOrigem).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosContentType).toStdString()),
         matriz::db::Value::of(csvDeConjunto(colecao.filtrosCollectionType).toStdString()),
         colecao.anoDe ? matriz::db::Value::of(*colecao.anoDe) : matriz::db::Value::null(),
         colecao.anoAte ? matriz::db::Value::of(*colecao.anoAte) : matriz::db::Value::null(),
         matriz::db::Value::of(agora)});
    return id;
}

void ProjetoAberto::apagarColecao(const std::string& id) {
    if (!projeto_) return;
    projeto_->registro().run("DELETE FROM colecao_inteligente WHERE id = ?", {matriz::db::Value::of(id)});
}

bool ProjetoAberto::obterItemInfo(const std::string& itemId, std::string& titulo, std::string& tipoMidia, std::string& codigoAcervo) const {
    if (!projeto_) return false;
    auto stmt = projeto_->registro().prepare("SELECT titulo, tipo_midia, codigo_acervo FROM item WHERE id = ?");
    stmt.bind(1, matriz::db::Value::of(itemId));
    if (!stmt.step()) return false;
    titulo = stmt.columnText(0);
    tipoMidia = stmt.columnIsNull(1) ? "" : stmt.columnText(1);
    codigoAcervo = stmt.columnText(2);
    return true;
}

std::set<int> ProjetoAberto::indicesExistentes(const std::string& itemId, const std::string& nivel) const {
    std::set<int> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT DISTINCT nivel_indice FROM item_campo WHERE item_id = ? AND nivel = ? ORDER BY nivel_indice");
    stmt.bind(1, matriz::db::Value::of(itemId));
    stmt.bind(2, matriz::db::Value::of(nivel));
    while (stmt.step()) out.insert(static_cast<int>(stmt.columnInt(0)));
    return out;
}

void ProjetoAberto::atualizarTipoMidia(const std::string& itemId, const std::string& tipoMidia) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    // Classificar É o que move o item de 'novo' pra 'catalogado' (§4): o
    // ingest só o trouxe pra dentro; a decisão de que tipo de mídia é isto
    // é humana. Estados posteriores (revisado/aprovado/publicado) não são
    // sobrescritos — reclassificar um item já aprovado não o rebaixa.
    projeto_->registro().run(
        "UPDATE item SET tipo_midia = ?, atualizado_em = ?, "
        "estado = CASE WHEN estado IN ('novo', 'capturado', 'nao_digitalizado') THEN 'catalogado' ELSE estado END "
        "WHERE id = ?",
        {matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(itemId)});

    auto origem = matriz::ficha::origemPadraoParaTipo(tipoMidia);
    if (origem) {
        projeto_->registro().run(
            "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, 'origem', ?, 'leitura_tecnica', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
             matriz::db::Value::of(*origem), matriz::db::Value::of(agora)});
    }

    EventBus::obterInstancia().dispararItemAlterado(itemId, "classificacao");
}

void ProjetoAberto::aplicarTipoMidiaEmLote(const std::vector<std::string>& itemIds, const std::string& tipoMidia) {
    if (!projeto_) return;
    std::string agora = matriz::model::agoraIso8601();
    auto& registro = projeto_->registro();
    registro.run("BEGIN", {});
    try {
        for (auto& id : itemIds) {
            registro.run("UPDATE item SET tipo_midia = ?, atualizado_em = ? WHERE id = ?",
                         {matriz::db::Value::of(tipoMidia), matriz::db::Value::of(agora), matriz::db::Value::of(id)});

            auto origem = matriz::ficha::origemPadraoParaTipo(tipoMidia);
            if (origem) {
                registro.run(
                    "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                    "VALUES (?, ?, 'raiz', 0, 'origem', ?, 'leitura_tecnica', ?)",
                    {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(id),
                     matriz::db::Value::of(*origem), matriz::db::Value::of(agora)});
            }
        }
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        throw;
    }

    for (auto& id : itemIds) {
        EventBus::obterInstancia().dispararItemAlterado(id, "classificacao");
    }
}

void ProjetoAberto::obterTiposMidiaDosItens(const std::vector<std::string>& itemIds, std::set<std::string>& tiposPresentes, bool& algumNulo) const {
    algumNulo = false;
    if (!projeto_) return;
    for (auto& id : itemIds) {
        auto stmt = projeto_->registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(id));
        if (!stmt.step()) continue;
        if (stmt.columnIsNull(0)) algumNulo = true;
        else tiposPresentes.insert(stmt.columnText(0));
    }
}

} // namespace matriz::ui

namespace matriz::ui {

std::vector<ProjetoAberto::VaultResumo> ProjetoAberto::listarVaults() const {
    std::vector<VaultResumo> out;
    if (!projeto_) return out;
    auto stmt = projeto_->registro().prepare(
        "SELECT v.id, v.nome, v.localizacao, v.status, "
        "(SELECT COUNT(DISTINCT a.item_id) FROM arquivo a WHERE a.vault_id = v.id) "
        "FROM vault v WHERE v.projeto_id = ? ORDER BY v.nome");
    stmt.bind(1, matriz::db::Value::of(projeto_->projetoId()));
    while (stmt.step()) {
        VaultResumo v;
        v.id = stmt.columnText(0);
        v.nome = juce::String(stmt.columnText(1));
        v.localizacao = juce::String(stmt.columnText(2));
        v.online = stmt.columnText(3) == "online";
        v.totalItens = static_cast<int>(stmt.columnInt(4));
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<ProjetoAberto::ColecaoEmbutida> ProjetoAberto::listarColecoesEmbutidas() const {
    if (!projeto_) return {};

    static const std::vector<std::pair<const char*, const char*>> kEmbutidas = {
        {"clipping", "colecoes.clipping"},       {"ausentes", "colecoes.ausentes"},
        {"nao_baixados", "colecoes.nao_baixados"}, {"incompletos", "colecoes.incompletos"},
        {"vulneraveis", "colecoes.vulneraveis"}, {"revisao", "colecoes.revisao"}};

    std::map<std::string, int> contagens;
    try {
        auto stmt = projeto_->registro().prepare(
            "SELECT colecao, COUNT(DISTINCT item_id) FROM colecao_embutida GROUP BY colecao");
        while (stmt.step()) contagens[stmt.columnText(0)] = static_cast<int>(stmt.columnInt(1));
    } catch (const std::exception&) {
        // Projeto antigo reaberto antes das views existirem: a seção some,
        // o resto do painel continua.
        return {};
    }

    std::vector<ColecaoEmbutida> out;
    for (const auto& [chave, chaveI18n] : kEmbutidas) {
        ColecaoEmbutida c;
        c.chave = chave;
        c.rotulo = matriz::i18n::t(chaveI18n);
        c.contagem = contagens.count(chave) ? contagens[chave] : 0;
        out.push_back(std::move(c));
    }
    return out;
}

std::set<std::string> ProjetoAberto::itensDaColecaoEmbutida(const std::string& chave) const {
    std::set<std::string> out;
    if (!projeto_) return out;
    try {
        auto stmt = projeto_->registro().prepare("SELECT DISTINCT item_id FROM colecao_embutida WHERE colecao = ?");
        stmt.bind(1, matriz::db::Value::of(chave));
        while (stmt.step()) out.insert(stmt.columnText(0));
    } catch (const std::exception&) {
    }
    return out;
}

std::vector<std::string> ProjetoAberto::reavaliarVaults() {
    if (!projeto_) return {};
    try {
        return matriz::vault::reavaliarVaults(projeto_->registro());
    } catch (const std::exception&) {
        return {};
    }
}

// ---------------------------------------------------------------------------
// Camada de Preservação Digital (OAIS / PREMIS / FAIR)
// ---------------------------------------------------------------------------

preservation::PreservationStatus ProjetoAberto::obterPreservationStatus(const std::string& itemId) const {
    if (!projeto_) return {};
    try { return preservation::obterStatus(projeto_->registro(), itemId); }
    catch (...) { return {}; }
}

std::vector<preservation::EventoPreservacao> ProjetoAberto::listarEventosPreservacao(const std::string& itemId) const {
    if (!projeto_) return {};
    try { return preservation::listarEventos(projeto_->registro(), itemId); }
    catch (...) { return {}; }
}

std::optional<preservation::DireitosPreservacao> ProjetoAberto::obterDireitos(const std::string& itemId) const {
    if (!projeto_) return std::nullopt;
    try { return preservation::obterDireitos(projeto_->registro(), itemId); }
    catch (...) { return std::nullopt; }
}

void ProjetoAberto::salvarDireitos(const std::string& itemId, const preservation::DireitosPreservacao& d) {
    if (!projeto_) return;
    try {
        preservation::salvarDireitos(projeto_->registro(), itemId, d, "bkr-agent-sistema");
    } catch (...) {}
}

void ProjetoAberto::verificarFixityAsync(const std::string& arquivoId,
                                         const std::string& caminhoAbsoluto,
                                         std::function<void(preservation::ResultadoFixity)> callback) {
    if (!projeto_ || !callback) return;

    // Captura o caminho do arquivo do projeto para abrir uma segunda conexão
    // na thread de background (a conexão principal pertence à message thread).
    juce::File registroFile = projeto_->pasta().getChildFile("registro.sqlite");

    std::thread([registroFile, arquivoId, caminhoAbsoluto, callback = std::move(callback)]() mutable {
        preservation::ResultadoFixity resultado;
        try {
            db::Database db(registroFile.getFullPathName().toStdString());
            resultado = preservation::verificarFixity(db, arquivoId, caminhoAbsoluto, "bkr-agent-sistema");
        } catch (const std::exception& e) {
            resultado.success  = false;
            resultado.mensagem = std::string("Erro ao verificar: ") + e.what();
        }
        // Devolve resultado na message thread
        juce::MessageManager::callAsync([resultado, callback = std::move(callback)]() mutable {
            callback(resultado);
        });
    }).detach();
}

juce::String ProjetoAberto::exportarPreservacaoJson(const std::string& itemId) const {
    if (!projeto_) return "{}";
    try { return preservation::exportarJson(projeto_->registro(), itemId); }
    catch (...) { return "{}"; }
}

juce::String ProjetoAberto::exportarPreservacaoCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    try { return preservation::exportarCsv(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarFullCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    try { return preservation::exportarFullCsv(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

bool ProjetoAberto::exportarFullCsvPacote(const std::vector<std::string>& itemIds, const juce::File& destLocation, juce::String& errorOut) const {
    if (!projeto_) {
        errorOut = "No active project open";
        return false;
    }
    try { return preservation::exportarFullCsvPacote(projeto_->registro(), itemIds, destLocation, errorOut); }
    catch (const std::exception& e) { errorOut = e.what(); return false; }
    catch (...) { errorOut = "Unknown error during export"; return false; }
}

juce::String ProjetoAberto::exportarXlsXml(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    std::string projName = projeto_->nome();
    std::string catCode = "BKR-MATRIZ-01";
    try { return preservation::exportarXlsXml(projeto_->registro(), itemIds, projName, catCode); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarDublinCoreCsv(const std::vector<std::string>& itemIds) const {
    if (!projeto_) return {};
    try { return preservation::exportarCsvDublinCore(projeto_->registro(), itemIds); }
    catch (...) { return {}; }
}

juce::String ProjetoAberto::exportarFixityManifest(const std::vector<std::string>& itemIds,
                                                     const std::string& algoritmo) const {
    if (!projeto_) return {};
    try { return preservation::exportarFixityManifest(projeto_->registro(), itemIds, algoritmo); }
    catch (...) { return {}; }
}

} // namespace matriz::ui
