#include "ManifestoBounce.h"

#include <map>
#include <set>

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"

namespace matriz::consolidacao {

namespace {

using matriz::db::Value;

juce::var texto(const std::string& s) { return juce::var(juce::String::fromUTF8(s.c_str())); }

juce::var textoOuNulo(matriz::db::Statement& st, int col) {
    return st.columnIsNull(col) ? juce::var() : texto(st.columnText(col));
}

juce::var lerFicha(matriz::db::Database& registro, const std::string& itemId) {
    auto* ficha = new juce::DynamicObject();
    {
        auto st = registro.prepare(
            "SELECT titulo, tipo_midia, estado, notas_livres, dc_creator, collection_type, dc_subject, source_media, lista, "
            "codigo_acervo FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            const char* nomes[] = {"titulo", "tipo_midia", "estado", "notas_livres", "dc_creator", "collection_type",
                                   "dc_subject", "source_media", "lista", "codigo_acervo"};
            for (int i = 0; i < 10; ++i) ficha->setProperty(nomes[i], textoOuNulo(st, i));
        }
    }
    return juce::var(ficha);
}

juce::var lerCampos(matriz::db::Database& registro, const std::string& itemId) {
    juce::Array<juce::var> campos;
    auto st = registro.prepare(
        "SELECT nivel, nivel_indice, campo_id, valor, fonte FROM item_campo WHERE item_id = ? "
        "ORDER BY nivel, nivel_indice, campo_id");
    st.bind(1, Value::of(itemId));
    while (st.step()) {
        auto* c = new juce::DynamicObject();
        c->setProperty("nivel", texto(st.columnText(0)));
        c->setProperty("nivel_indice", static_cast<int>(st.columnInt(1)));
        c->setProperty("campo_id", texto(st.columnText(2)));
        c->setProperty("valor", textoOuNulo(st, 3));
        c->setProperty("fonte", texto(st.columnText(4)));
        campos.add(juce::var(c));
    }
    return campos;
}

juce::var lerLista(matriz::db::Database& registro, const char* sql, const std::string& itemId) {
    juce::Array<juce::var> v;
    auto st = registro.prepare(sql);
    st.bind(1, Value::of(itemId));
    while (st.step()) v.add(texto(st.columnText(0)));
    return v;
}

juce::var lerMarcadores(matriz::db::Database& registro, const std::string& itemId) {
    juce::Array<juce::var> lista;
    auto st = registro.prepare(
        "SELECT id, tempo_inicio, tempo_fim, titulo, observacao, autor FROM marcador WHERE item_id = ? ORDER BY tempo_inicio, id");
    st.bind(1, Value::of(itemId));
    while (st.step()) {
        auto* m = new juce::DynamicObject();
        const std::string marcadorId = st.columnText(0);
        m->setProperty("tempo_inicio", st.columnReal(1));
        m->setProperty("tempo_fim", st.columnIsNull(2) ? juce::var() : juce::var(st.columnReal(2)));
        m->setProperty("titulo", textoOuNulo(st, 3));
        m->setProperty("observacao", textoOuNulo(st, 4));
        m->setProperty("autor", texto(st.columnText(5)));
        juce::Array<juce::var> assuntos;
        auto sa = registro.prepare(
            "SELECT a.termo FROM marcador_assunto ma JOIN assunto a ON a.id = ma.assunto_id WHERE ma.marcador_id = ?");
        sa.bind(1, Value::of(marcadorId));
        while (sa.step()) assuntos.add(texto(sa.columnText(0)));
        m->setProperty("assuntos", assuntos);
        lista.add(juce::var(m));
    }
    return lista;
}

juce::String normalizarCaminho(juce::String c) { return c.replaceCharacter('\\', '/'); }

}  // namespace

bool escreverManifestoBounce(matriz::db::Database& registro, const juce::File& destino,
                             const std::vector<ArquivoExportadoBounce>& arquivos, const EstruturaManifestoBounce& estrutura,
                             bool copiaIdentica, const juce::String& versaoMatriz, std::string& erro) {
    try {
        auto* raiz = new juce::DynamicObject();
        juce::var raizVar(raiz);
        raiz->setProperty("formato", "matriz-bounce-manifest");
        raiz->setProperty("versao_formato", kVersaoFormatoManifestoBounce);
        raiz->setProperty("matriz_versao", versaoMatriz);
        raiz->setProperty("exportado_em", juce::Time::getCurrentTime().toISO8601(true));
        {
            auto st = registro.prepare("SELECT nome FROM projeto LIMIT 1");
            raiz->setProperty("projeto_origem", st.step() ? texto(st.columnText(0)) : juce::var());
        }

        juce::Array<juce::var> listaArquivos;
        std::map<std::string, juce::String> caminhoPorItem;
        // nest: id -> (capa item id, membros exportados)
        std::map<std::string, std::string> capaDoNest;
        std::map<std::string, std::vector<std::string>> membrosDoNest;

        for (const auto& a : arquivos) {
            const juce::String caminho = normalizarCaminho(a.caminhoRelativo);
            caminhoPorItem[a.itemId] = caminho;
            auto* o = new juce::DynamicObject();
            o->setProperty("caminho", caminho);

            std::string hashOriginal;
            {
                auto st = registro.prepare("SELECT checksum_sha256 FROM arquivo WHERE id = ?");
                st.bind(1, Value::of(a.arquivoId));
                if (st.step() && !st.columnIsNull(0)) hashOriginal = st.columnText(0);
            }
            std::string hashCopia = hashOriginal;
            if (!copiaIdentica || hashOriginal.empty()) {
                const auto copia = destino.getChildFile(caminho);
                hashCopia = copia.existsAsFile() ? matriz::ingest::sha256DoArquivo(copia) : std::string();
            }
            o->setProperty("hash_original", texto(hashOriginal));
            o->setProperty("hash_copia", texto(hashCopia));

            o->setProperty("item", lerFicha(registro, a.itemId));
            o->setProperty("campos", lerCampos(registro, a.itemId));
            o->setProperty("tags", lerLista(registro, "SELECT tag FROM item_tag WHERE item_id = ? ORDER BY tag", a.itemId));
            o->setProperty("assuntos",
                           lerLista(registro,
                                    "SELECT a.termo FROM item_assunto ia JOIN assunto a ON a.id = ia.assunto_id "
                                    "WHERE ia.item_id = ? ORDER BY a.termo",
                                    a.itemId));
            o->setProperty("marcadores", lerMarcadores(registro, a.itemId));
            o->setProperty("marcas", lerLista(registro, "SELECT tipo FROM item_marcacao WHERE item_id = ? ORDER BY tipo", a.itemId));

            {
                auto st = registro.prepare(
                    "SELECT ni.nest_id, n.capa_item_id FROM nest_item ni JOIN nest n ON n.id = ni.nest_id WHERE ni.item_id = ?");
                st.bind(1, Value::of(a.itemId));
                if (st.step()) {
                    const std::string nestId = st.columnText(0);
                    o->setProperty("nest_id", texto(nestId));
                    if (!st.columnIsNull(1)) capaDoNest[nestId] = st.columnText(1);
                    membrosDoNest[nestId].push_back(a.itemId);
                }
            }
            listaArquivos.add(juce::var(o));
        }
        raiz->setProperty("arquivos", listaArquivos);

        // Nests: só os membros que foram exportados; a capa só vale se também saiu.
        juce::Array<juce::var> nests;
        for (const auto& [nestId, membros] : membrosDoNest) {
            auto* n = new juce::DynamicObject();
            n->setProperty("id", texto(nestId));
            const auto itCapa = capaDoNest.find(nestId);
            const bool capaExportada = itCapa != capaDoNest.end() && caminhoPorItem.count(itCapa->second) > 0;
            n->setProperty("capa", capaExportada ? juce::var(caminhoPorItem[itCapa->second]) : juce::var());
            juce::Array<juce::var> caminhos;
            for (const auto& m : membros) caminhos.add(caminhoPorItem[m]);
            n->setProperty("membros", caminhos);
            nests.add(juce::var(n));
        }
        raiz->setProperty("nests", nests);

        // Folder structure.
        auto* est = new juce::DynamicObject();
        est->setProperty("origem", estrutura.porVisualEditor ? "visual_editor" : "folder_map");
        est->setProperty("hierarquia", texto(estrutura.hierarquiaCsv));
        juce::Array<juce::var> pastas, atribuicoes;
        if (!estrutura.porVisualEditor && !estrutura.mapaId.empty()) {
            {
                auto st = registro.prepare("SELECT nome, fator_cards FROM folder_map WHERE id = ?");
                st.bind(1, Value::of(estrutura.mapaId));
                if (st.step()) {
                    est->setProperty("nome_mapa", texto(st.columnText(0)));
                    est->setProperty("fator_cards", st.columnIsNull(1) ? juce::var() : juce::var(st.columnReal(1)));
                }
            }
            auto st = registro.prepare(
                "SELECT id, pasta_pai_id, nome, posicao_x, posicao_y, escala_no, cor_customizada FROM acervo_pasta "
                "WHERE mapa_id = ? ORDER BY ordem, criado_em");
            st.bind(1, Value::of(estrutura.mapaId));
            while (st.step()) {
                auto* p = new juce::DynamicObject();
                p->setProperty("id", texto(st.columnText(0)));
                p->setProperty("pai", textoOuNulo(st, 1));
                p->setProperty("nome", texto(st.columnText(2)));
                p->setProperty("x", static_cast<int>(st.columnInt(3)));
                p->setProperty("y", static_cast<int>(st.columnInt(4)));
                p->setProperty("escala", st.columnIsNull(5) ? juce::var() : juce::var(st.columnReal(5)));
                p->setProperty("cor", textoOuNulo(st, 6));
                pastas.add(juce::var(p));
            }
            for (const auto& a : arquivos) {
                auto sp = registro.prepare("SELECT pasta_id FROM acervo_item_pasta WHERE item_id = ? AND mapa_id = ?");
                sp.bind(1, Value::of(a.itemId));
                sp.bind(2, Value::of(estrutura.mapaId));
                while (sp.step()) {
                    auto* x = new juce::DynamicObject();
                    x->setProperty("caminho", caminhoPorItem[a.itemId]);
                    x->setProperty("pasta_id", texto(sp.columnText(0)));
                    atribuicoes.add(juce::var(x));
                }
            }
        } else {
            // Visual Editor (ou sem mapa): a estrutura É o caminho de cada arquivo; cada pasta distinta vira uma pasta.
            std::map<juce::String, std::string> idPorCaminhoPasta;
            auto garantir = [&](auto&& self, const juce::String& caminhoPasta) -> std::string {
                if (caminhoPasta.isEmpty()) return {};
                const auto it = idPorCaminhoPasta.find(caminhoPasta);
                if (it != idPorCaminhoPasta.end()) return it->second;
                const juce::String pai = caminhoPasta.contains("/") ? caminhoPasta.upToLastOccurrenceOf("/", false, false) : juce::String();
                const std::string idPai = self(self, pai);
                const std::string id = "p" + std::to_string(idPorCaminhoPasta.size() + 1);
                idPorCaminhoPasta[caminhoPasta] = id;
                auto* p = new juce::DynamicObject();
                p->setProperty("id", juce::String(id));
                p->setProperty("pai", idPai.empty() ? juce::var() : juce::var(juce::String(idPai)));
                p->setProperty("nome", caminhoPasta.fromLastOccurrenceOf("/", false, false));
                pastas.add(juce::var(p));
                return id;
            };
            for (const auto& a : arquivos) {
                const juce::String caminho = caminhoPorItem[a.itemId];
                if (!caminho.contains("/")) continue;
                const std::string idPasta = garantir(garantir, caminho.upToLastOccurrenceOf("/", false, false));
                auto* x = new juce::DynamicObject();
                x->setProperty("caminho", caminho);
                x->setProperty("pasta_id", juce::String(idPasta));
                atribuicoes.add(juce::var(x));
            }
        }
        est->setProperty("pastas", pastas);
        est->setProperty("atribuicoes", atribuicoes);
        raiz->setProperty("estrutura", juce::var(est));

        const juce::File alvo = destino.getChildFile(kNomeManifestoBounce);
        if (!alvo.replaceWithText(juce::JSON::toString(raizVar, false), false, false, "\n")) {
            erro = "could not write " + alvo.getFullPathName().toStdString();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        erro = e.what();
        return false;
    }
}

}  // namespace matriz::consolidacao
