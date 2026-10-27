#include "PacoteCollection.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "../Ingest/Checksum.h"
#include "MetadadoEmbutido.h"
#include "SaidaBounce.h"
#include "../Model/NomesCanonicos.h"
#include "../Model/Project.h"
#include "../Model/ProjectLog.h"
#include "../Vault/Resolucao.h"

namespace matriz::consolidacao::pacote {

using matriz::db::Value;

namespace {

// Colunas de asset_geolocation que viajam (as de controle ficam).
const char* const kColunasGeo[] = {
    "latitude", "longitude", "altitude", "continent", "country", "country_code", "state_province", "state_code",
    "city", "municipality", "neighborhood", "district", "postal_code", "street", "street_number", "locality",
    "formatted_address", "source", "precision_accuracy", "confidence"};

// Mesma regra de ProjetoAberto::lerMetadado: coluna do item, senão item_campo.
std::string lerCampo(matriz::db::Database& registro, const std::string& itemId, const std::string& coluna) {
    {
        auto st = registro.prepare("SELECT COALESCE(" + coluna + ", '') FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            std::string v = st.columnText(0);
            if (!juce::String::fromUTF8(v.c_str()).trim().isEmpty()) return v;
        }
    }
    auto st = registro.prepare("SELECT COALESCE(valor, '') FROM item_campo WHERE item_id = ? AND campo_id = ? LIMIT 1");
    st.bind(1, Value::of(itemId));
    st.bind(2, Value::of(coluna));
    if (st.step()) {
        std::string v = st.columnText(0);
        if (!juce::String::fromUTF8(v.c_str()).trim().isEmpty()) return v;
    }
    return {};
}

juce::var listaJson(const std::vector<std::string>& v) {
    juce::Array<juce::var> arr;
    for (const auto& s : v) arr.add(juce::String::fromUTF8(s.c_str()));
    return arr;
}

std::vector<std::string> listaDeJson(const juce::var& v) {
    std::vector<std::string> out;
    if (auto* arr = v.getArray())
        for (const auto& e : *arr) {
            auto s = e.toString().trim();
            if (s.isNotEmpty()) out.push_back(s.toStdString());
        }
    return out;
}

std::string texto(const juce::var& obj, const char* chave) {
    return obj.getProperty(chave, {}).toString().trim().toStdString();
}

}  // namespace

bool DadosFicha::vazio() const {
    return titulo.empty() && descricao.empty() && eventDate.empty() && content.empty() && subjects.empty() &&
           tags.empty() && pessoas.empty() && !geo.isObject() && marcadores.empty();
}

std::set<std::string> chavesDaListaPessoas(matriz::db::Database& registro) {
    std::set<std::string> out;
    try {
        auto st = registro.prepare("SELECT nome FROM collection_person");
        while (st.step()) out.insert(matriz::model::nomes::chave(st.columnText(0)));
    } catch (...) {}  // projeto sem a tabela: ninguém é pessoa
    return out;
}

DadosFicha lerDadosFicha(matriz::db::Database& registro, const std::string& itemId,
                         const std::set<std::string>& chavesPessoas) {
    DadosFicha d;
    d.titulo = lerCampo(registro, itemId, "dc_title");
    d.descricao = lerCampo(registro, itemId, "dc_description");
    d.eventDate = lerCampo(registro, itemId, "ano");
    d.content = lerCampo(registro, itemId, "collection_type");
    d.subjects = matriz::model::nomes::dividirSubjects(lerCampo(registro, itemId, "dc_subject"));
    {
        auto st = registro.prepare("SELECT tag FROM item_tag WHERE item_id = ? ORDER BY rowid");
        st.bind(1, Value::of(itemId));
        while (st.step()) {
            std::string t = juce::String::fromUTF8(st.columnText(0).c_str()).trim().toStdString();
            if (t.empty()) continue;
            (chavesPessoas.count(matriz::model::nomes::chave(t)) ? d.pessoas : d.tags).push_back(t);
        }
    }
    {
        std::string cols;
        for (auto* c : kColunasGeo) cols += (cols.empty() ? "" : ", ") + std::string(c);
        auto st = registro.prepare("SELECT " + cols + " FROM asset_geolocation WHERE asset_id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) {
            auto* obj = new juce::DynamicObject();
            for (int i = 0; i < static_cast<int>(std::size(kColunasGeo)); ++i) {
                if (st.columnIsNull(i)) continue;
                const juce::String nome(kColunasGeo[i]);
                if (nome == "latitude" || nome == "longitude" || nome == "altitude" || nome == "precision_accuracy" ||
                    nome == "confidence") {
                    obj->setProperty(nome, st.columnReal(i));
                } else {
                    auto v = juce::String::fromUTF8(st.columnText(i).c_str()).trim();
                    if (v.isEmpty() || (nome == "source" && v == "NONE")) continue;
                    obj->setProperty(nome, v);
                }
            }
            if (obj->getProperties().size() > 0) d.geo = juce::var(obj);
            else delete obj;
        }
    }
    {
        auto st = registro.prepare("SELECT COALESCE(texto, ''), minutagem_ms, COALESCE(titulo, '') FROM item_observacao "
                                   "WHERE item_id = ? ORDER BY criado_em, rowid");
        st.bind(1, Value::of(itemId));
        while (st.step()) {
            Marcador m;
            m.texto = st.columnText(0);
            if (!st.columnIsNull(1)) m.tempoS = static_cast<double>(st.columnInt(1)) / 1000.0;
            m.titulo = st.columnText(2);
            if (m.texto.empty() && m.titulo.empty() && !m.tempoS) continue;
            d.marcadores.push_back(std::move(m));
        }
    }
    return d;
}

void escreverJson(const DadosFicha& d, juce::DynamicObject& o) {
    auto str = [&](const char* chave, const std::string& v) {
        if (!v.empty()) o.setProperty(chave, juce::String::fromUTF8(v.c_str()));
    };
    str("titulo", d.titulo);
    str("descricao", d.descricao);
    str("event_date", d.eventDate);
    str("content", d.content);
    if (!d.subjects.empty()) o.setProperty("subjects", listaJson(d.subjects));
    if (!d.tags.empty()) o.setProperty("tags", listaJson(d.tags));
    if (!d.pessoas.empty()) o.setProperty("pessoas", listaJson(d.pessoas));
    if (d.geo.isObject()) o.setProperty("geo", d.geo);
    if (!d.marcadores.empty()) {
        juce::Array<juce::var> arr;
        for (const auto& m : d.marcadores) {
            auto* mo = new juce::DynamicObject();
            if (m.tempoS) mo->setProperty("tempo_s", *m.tempoS);
            if (!m.texto.empty()) mo->setProperty("texto", juce::String::fromUTF8(m.texto.c_str()));
            if (!m.titulo.empty()) mo->setProperty("titulo", juce::String::fromUTF8(m.titulo.c_str()));
            arr.add(juce::var(mo));
        }
        o.setProperty("marcadores", arr);
    }
}

DadosFicha lerJson(const juce::var& r) {
    DadosFicha d;
    d.titulo = texto(r, "titulo");
    d.descricao = texto(r, "descricao");
    d.eventDate = texto(r, "event_date");
    d.content = texto(r, "content");
    d.subjects = listaDeJson(r.getProperty("subjects", {}));
    d.tags = listaDeJson(r.getProperty("tags", {}));
    d.pessoas = listaDeJson(r.getProperty("pessoas", {}));
    if (auto g = r.getProperty("geo", {}); g.isObject() && g.getDynamicObject()->getProperties().size() > 0) d.geo = g;
    if (auto* arr = r.getProperty("marcadores", {}).getArray())
        for (const auto& e : *arr) {
            if (!e.isObject()) continue;
            Marcador m;
            if (e.hasProperty("tempo_s")) m.tempoS = static_cast<double>(e.getProperty("tempo_s", 0.0));
            m.texto = e.getProperty("texto", {}).toString().toStdString();
            m.titulo = e.getProperty("titulo", {}).toString().toStdString();
            if (m.texto.empty() && m.titulo.empty() && !m.tempoS) continue;
            d.marcadores.push_back(std::move(m));
        }
    return d;
}

namespace {

// Falha de entrega de um item, com o motivo em código (vai para as pendências do pacote).
struct FalhaPacote : std::runtime_error {
    std::string motivo;
    FalhaPacote(std::string m, const std::string& msg) : std::runtime_error(msg), motivo(std::move(m)) {}
};

// Estado de um pacote sendo montado: as pastas do mapa, os registros do JSON e as pendências.
struct Montagem {
    matriz::db::Database& registro;
    juce::File pastaProjeto, pasta;
    std::string mapaId;
    juce::String nomeMapa, nomeColecao, exportadoEm, completadoEm;
    bool embutir = false;
    struct Pasta { juce::String nome; std::string pai; long long ordem = 0; };
    std::map<std::string, Pasta> pastas;  // id -> (nome, pai, ordem)
    std::set<std::string> chavesPessoas, pastasUsadas;
    juce::Array<juce::var> arquivos;
    std::vector<std::string> selecao;
    std::vector<Pendencia> pendencias;

    Montagem(matriz::db::Database& reg) : registro(reg) {}
    juce::File media() const { return pasta.getChildFile("Media"); }

    void carregarPastas() {
        auto st = registro.prepare("SELECT id, nome, COALESCE(pasta_pai_id, ''), ordem FROM acervo_pasta WHERE mapa_id = ?");
        st.bind(1, Value::of(mapaId));
        while (st.step())
            pastas[st.columnText(0)] = {juce::String::fromUTF8(st.columnText(1).c_str()), st.columnText(2), st.columnInt(3)};
        chavesPessoas = chavesDaListaPessoas(registro);
    }

    juce::StringArray caminhoDaPasta(std::string id) const {
        juce::StringArray nomes;
        for (int guarda = 0; !id.empty() && guarda < 256; ++guarda) {
            auto it = pastas.find(id);
            if (it == pastas.end()) break;
            nomes.insert(0, it->second.nome);
            id = it->second.pai;
        }
        return nomes;
    }

    void marcarPastaUsada(std::string id) {
        for (; !id.empty() && pastas.count(id); id = pastas[id].pai)
            if (!pastasUsadas.insert(id).second) break;
    }

    void pendente(const ItemPlanejado& ip, const std::string& motivo, const std::string& detalhe) {
        pendencias.push_back({ip.itemId, ip.codigoAcervo, motivo, detalhe});
    }

    // Copia o item do MAIN para Media/<caminho>, confere o SHA-256 e devolve o registro do JSON.
    // void = o item ainda não está no MAIN (fica de fora). Lança FalhaPacote; no erro só apaga o
    // que esta chamada copiou. `substituir`: o destino pode existir (sobra de cópia interrompida ou
    // arquivo alterado) e só é trocado depois que a cópia nova conferiu.
    juce::var entregar(const ItemPlanejado& ip, bool substituir, bool* adotado = nullptr) {
        if (ip.emConflito) throw FalhaPacote(kMotivoConflito, "name conflict at destination");
        // Sempre do MAIN: item que só existe no SOURCE fica de fora.
        auto noMain = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
        auto naOrigem = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto,
                                                       matriz::vault::Preferencia::Origem);
        if (!noMain || (naOrigem && *naOrigem == *noMain)) return {};
        const juce::File destino = media().getChildFile(ip.caminhoRelativoDestino);
        if (destino.existsAsFile() && !substituir)
            throw FalhaPacote(kMotivoConflito, "already exists in the package: " + destino.getFullPathName().toStdString());
        // Sobra de um export interrompido, idêntica ao MAIN: fica onde está (só entra no catálogo).
        if (substituir && adotado != nullptr && !embutir && destino.existsAsFile() && destino.getSize() == noMain->getSize()) {
            const std::string sha = juce::String(matriz::ingest::calcularChecksums(destino).sha256).toLowerCase().toStdString();
            if (!sha.empty() && sha == juce::String(matriz::ingest::calcularChecksums(*noMain).sha256).toLowerCase().toStdString()) {
                *adotado = true;
                return registroDe(ip, sha, destino);
            }
        }
        // Substituindo: copia ao lado e só troca depois de conferir (o arquivo antigo nunca some antes).
        const juce::File copia = substituir && destino.existsAsFile()
                                     ? destino.getSiblingFile(destino.getFileNameWithoutExtension() + ".matriz-tmp" +
                                                              destino.getFileExtension())
                                     : destino;
        bool copiaDestaChamada = false;  // só a cópia feita por ESTA chamada pode ser apagada no erro
        try {
            destino.getParentDirectory().createDirectory();
            copiaDestaChamada = true;
            if (!noMain->copyFileTo(copia) || copia.getSize() != noMain->getSize())
                throw FalhaPacote(kMotivoCopia, "copy failed: " + destino.getFullPathName().toStdString());
            // Cópia verificada: o SHA-256 do arquivo entregue é o que vai no
            // JSON, e tem que bater com o do MAIN que acabou de ser copiado.
            // NÃO comparar com o SHA registrado no ingest: edição de metadados
            // in-place no MAIN (mesmo tamanho) deixa o registro defasado e
            // reprovava cópias perfeitas ("checksum mismatch").
            const std::string sha = juce::String(matriz::ingest::calcularChecksums(copia).sha256).toLowerCase().toStdString();
            const std::string esperado = juce::String(matriz::ingest::calcularChecksums(*noMain).sha256).toLowerCase().toStdString();
            if (sha.empty() || sha != esperado)
                throw FalhaPacote(kMotivoChecksum, "checksum mismatch after copy: " + destino.getFullPathName().toStdString());

            // Embutir metadados (opcional): só a CÓPIA recebe a ficha — o MAIN não é tocado. O SHA-256 do JSON
            // passa a ser o do arquivo já com os metadados, que é o que de fato é entregue.
            std::string shaEntregue = sha;
            if (embutir) {
                if (copia.hasFileExtension("wav")) {
                    auto marcadores = marcadoresDoItem(registro, ip.itemId);
                    if (!marcadores.empty()) embutirMarcadoresEmWav(copia, marcadores);
                }
                std::string erroMeta;
                if (embutirFichaMatrizNaCopia(registro, ip.itemId, copia, erroMeta) == StatusEmbedFicha::Falhou)
                    throw FalhaPacote(kMotivoEmbutir, "could not embed metadata: " + erroMeta);
                shaEntregue = juce::String(matriz::ingest::calcularChecksums(copia).sha256).toLowerCase().toStdString();
            }
            if (copia != destino && (!destino.deleteFile() || !copia.moveFileTo(destino)))
                throw FalhaPacote(kMotivoCopia, "could not replace: " + destino.getFullPathName().toStdString());
            return registroDe(ip, shaEntregue, destino);
        } catch (...) {
            if (copiaDestaChamada && copia.existsAsFile()) copia.deleteFile();
            throw;
        }
    }

    // Registro do JSON de um arquivo entregue (ficha + de onde veio).
    juce::var registroDe(const ItemPlanejado& ip, const std::string& sha, const juce::File& entregue) {
        auto* reg = new juce::DynamicObject();
        reg->setProperty("sha256", juce::String(sha));
        reg->setProperty("caminho", juce::String(ip.caminhoRelativoDestino).replaceCharacter('\\', '/'));
        reg->setProperty("item_id", juce::String(ip.itemId));
        reg->setProperty("codigo", juce::String(ip.codigoAcervo));
        reg->setProperty("tamanho", entregue.getSize());
        const auto nomesPasta = caminhoDaPasta(ip.pastaId);
        if (!nomesPasta.isEmpty()) {
            juce::Array<juce::var> arr;
            for (const auto& n : nomesPasta) arr.add(n);
            reg->setProperty("pasta", arr);
            marcarPastaUsada(ip.pastaId);
        }
        escreverJson(lerDadosFicha(registro, ip.itemId, chavesPessoas), *reg);
        return juce::var(reg);
    }

    // Árvore de pastas: só as que têm itens do pacote (e seus ancestrais).
    juce::Array<juce::var> filhosDe(const std::string& pai) const {
        std::vector<std::pair<const std::string*, const Pasta*>> filhos;
        for (const auto& [id, p] : pastas)
            if (p.pai == pai && pastasUsadas.count(id)) filhos.push_back({&id, &p});
        std::sort(filhos.begin(), filhos.end(), [](const auto& a, const auto& b) {
            return a.second->ordem != b.second->ordem ? a.second->ordem < b.second->ordem
                                                      : a.second->nome.compareNatural(b.second->nome) < 0;
        });
        juce::Array<juce::var> out;
        for (const auto& [id, p] : filhos) {
            auto* no = new juce::DynamicObject();
            no->setProperty("nome", p->nome);
            auto sub = filhosDe(*id);
            if (!sub.isEmpty()) no->setProperty("pastas", sub);
            out.add(juce::var(no));
        }
        return out;
    }

    juce::var json(const juce::var& pastasExtras = {}) const {
        auto* raiz = new juce::DynamicObject();
        raiz->setProperty("formato", kFormato);
        raiz->setProperty("versao", kVersao);
        raiz->setProperty("colecao", nomeColecao);
        raiz->setProperty("exportado_em", exportadoEm);
        if (completadoEm.isNotEmpty()) raiz->setProperty("completado_em", completadoEm);
        raiz->setProperty("folder_map", nomeMapa);
        raiz->setProperty("mapa_id", juce::String(mapaId));
        raiz->setProperty("embutir", embutir);
        juce::Array<juce::var> arvore = filhosDe({});
        mesclarArvore(arvore, pastasExtras);
        raiz->setProperty("pastas", arvore);
        raiz->setProperty("arquivos", arquivos);
        juce::Array<juce::var> sel;
        for (const auto& id : selecao) sel.add(juce::String(id));
        raiz->setProperty("selecao", sel);
        juce::Array<juce::var> pend;
        for (const auto& p : pendencias) {
            auto* o = new juce::DynamicObject();
            o->setProperty("item_id", juce::String(p.itemId));
            o->setProperty("codigo", juce::String(p.codigo));
            o->setProperty("motivo", juce::String(p.motivo));
            o->setProperty("detalhe", juce::String::fromUTF8(p.detalhe.c_str()));
            pend.add(juce::var(o));
        }
        raiz->setProperty("pendencias", pend);
        return juce::var(raiz);
    }

    // Pastas de uma árvore antiga (pacote completado) que a nova não tem, por nome.
    static void mesclarArvore(juce::Array<juce::var>& destino, const juce::var& extras) {
        const auto* arr = extras.getArray();
        if (arr == nullptr) return;
        for (const auto& e : *arr) {
            const juce::String nome = e.getProperty("nome", {}).toString();
            juce::var* igual = nullptr;
            for (auto& d : destino)
                if (d.getProperty("nome", {}).toString() == nome) { igual = &d; break; }
            if (igual == nullptr) { destino.add(e); continue; }
            juce::Array<juce::var> sub;
            if (const auto* s = igual->getProperty("pastas", {}).getArray()) sub = *s;
            mesclarArvore(sub, e.getProperty("pastas", {}));
            if (!sub.isEmpty())
                if (auto* o = igual->getDynamicObject()) o->setProperty("pastas", sub);
        }
    }

    // Grava atômico (tmp + rename): quem lê nunca vê um JSON pela metade.
    bool gravar(const juce::String& nomeArquivo, const juce::var& extras = {}) const {
        const juce::File tmp = pasta.getChildFile(nomeArquivo + ".tmp");
        return tmp.replaceWithText(juce::JSON::toString(json(extras), false)) &&
               tmp.moveFileTo(pasta.getChildFile(nomeArquivo));
    }

    void registrarNoLog(const juce::String& titulo, const juce::StringArray& cabecalho) const {
        try {
            juce::StringArray linhas = cabecalho;
            int n = 0;
            for (const auto& p : pendencias) {
                if (++n > 300) {
                    linhas.add("  ... +" + juce::String((int) pendencias.size() - 300) + " (see " + juce::String(kArquivoJson) + ")");
                    break;
                }
                linhas.add("  pending: " + juce::String(p.codigo) + " [" + juce::String(p.motivo) + "] " +
                           juce::String::fromUTF8(p.detalhe.c_str()));
            }
            matriz::model::ProjectLog(pastaProjeto).appendEntry(titulo, linhas);
        } catch (...) {}
    }
};

}  // namespace

ResultadoPacote gerarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                            const juce::File& destinoPai, const juce::String& nomePacote,
                            const std::string& mapaId, const juce::String& nomeMapa,
                            const juce::String& nomeColecao, const std::set<std::string>& itens,
                            const AoProgredir& aoProgredir, bool embutirMetadados) {
    ResultadoPacote r;
    juce::String nomeSeguro = juce::File::createLegalFileName(nomePacote.trim());
    if (nomeSeguro.isEmpty()) nomeSeguro = "Matriz package";
    // Nome já usado: "Nome (2)", "Nome (3)"... (mesma regra do folder map no INTAKE).
    r.pasta = destinoPai.getChildFile(nomeSeguro);
    for (int n = 2; r.pasta.exists(); ++n) r.pasta = destinoPai.getChildFile(nomeSeguro + " (" + juce::String(n) + ")");

    Montagem m(registro);
    m.pastaProjeto = pastaProjeto;
    m.pasta = r.pasta;
    m.mapaId = mapaId;
    m.nomeMapa = nomeMapa;
    m.nomeColecao = nomeColecao;
    m.embutir = embutirMetadados;
    m.selecao.assign(itens.begin(), itens.end());

    // Mesmo planner do EXPORT por folder map: estrutura = pastas do mapa, sem
    // prefixo (nomes como no MAIN). Item selecionado sem pasta no mapa NÃO fica de
    // fora: vai para _SEM_PASTA dentro do pacote (o usuário exporta tudo que selecionou).
    auto plano = planejarConsolidacao(registro, pastaProjeto, m.media(), {NivelHierarquia::PastaManual}, {},
                                      ModoPrefixoArquivo::Nenhum, {}, /*autoResolver*/ true, false, false,
                                      /*paraExport*/ true, mapaId, /*estritoAoDestino*/ false,
                                      /*semPastaNaRaiz*/ true);
    for (const auto& id : plano.semPastaExcluidos)
        if (itens.count(id)) ++r.semPasta;
    std::vector<ItemPlanejado> doRecorte;
    for (auto& ip : plano.itens)
        if (itens.count(ip.itemId)) doRecorte.push_back(std::move(ip));
    if (doRecorte.empty()) return r;
    m.carregarPastas();

    // Andamento: o JSON parcial (matriz-pacote.parcial.json) é gravado logo depois do primeiro
    // arquivo e a cada ~15 s. Se o computador desligar no meio, a pasta continua reconhecível
    // e o COMPLETE PACKAGE retoma dali. O INTAKE só aceita o JSON final.
    const juce::uint32 kIntervaloParcialMs = 15000;
    juce::uint32 ultimoParcial = 0;
    const int total = static_cast<int>(doRecorte.size());
    int feito = 0;
    for (const auto& ip : doRecorte) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            r.cancelado = true;
            break;
        }
        ++feito;
        try {
            auto reg = m.entregar(ip, /*substituir*/ false);
            if (reg.isVoid()) {
                ++r.foraDoMain;
                m.pendente(ip, kMotivoForaDoMain, "not in the MAIN yet");
                continue;
            }
            m.arquivos.add(reg);
            ++r.copiados;
        } catch (const FalhaPacote& e) {
            r.falhas.push_back(ip.codigoAcervo + ": " + e.what());
            m.pendente(ip, e.motivo, e.what());
        } catch (const std::exception& e) {
            r.falhas.push_back(ip.codigoAcervo + ": " + e.what());
            m.pendente(ip, kMotivoErro, e.what());
        }
        const auto agora = juce::Time::getMillisecondCounter();
        if (r.copiados > 0 && (ultimoParcial == 0 || agora - ultimoParcial >= kIntervaloParcialMs)) {
            m.exportadoEm = juce::String(matriz::model::agoraIso8601());
            m.gravar(kArquivoParcial);
            ultimoParcial = agora;
        }
    }
    r.pendencias = m.pendencias;

    // Cancelado ou sem nenhum arquivo entregue: o pacote não serve pra nada
    // (e não tem JSON) — a pasta, criada agora por este export, sai.
    if (r.cancelado || r.copiados == 0) {
        r.pasta.deleteRecursively();
        return r;
    }

    m.exportadoEm = juce::String(matriz::model::agoraIso8601());
    if (!m.gravar(kArquivoJson)) r.falhas.push_back("could not write " + std::string(kArquivoJson));
    else r.pasta.getChildFile(kArquivoParcial).deleteFile();

    m.registrarNoLog("Collection Package Exported",
                     {"Package: " + r.pasta.getFullPathName(), "Folder map: " + nomeMapa,
                      "Files: " + juce::String(r.copiados) + " (without a folder in the map, skipped: " +
                          juce::String(r.semPasta) + "; not yet in MAIN, skipped: " + juce::String(r.foraDoMain) +
                          "; failures: " + juce::String((int) r.falhas.size()) + ")"});
    return r;
}

// ---------------------------------------------------------------------------
// CHECK PACKAGE
// ---------------------------------------------------------------------------

namespace {

// O que a verificação precisa guardar para o COMPLETE PACKAGE agir sem planejar de novo.
struct Verificacao {
    ResultadoVerificacao r;
    Pacote pacote;
    std::vector<ItemPlanejado> planejados;     // da seleção, na ordem do planejador
    std::map<std::string, size_t> problemaDe;  // item -> índice em r.problemas
    std::set<std::string> selecao;             // a que valeu (a gravada ou a atual)
};

std::string caminhoSemCaixa(const std::string& caminho) {
    return juce::String::fromUTF8(caminho.c_str()).toLowerCase().toStdString();
}

Verificacao verificar(matriz::db::Database& registro, const juce::File& pastaProjeto, const juce::File& pastaPacote,
                      const std::set<std::string>& selecaoAtual,
                      const std::vector<std::pair<std::string, juce::String>>& mapas, const std::string& mapaPadrao,
                      const AoProgredir& aoProgredir) {
    Verificacao v;
    auto& r = v.r;
    r.pasta = pastaPacote;
    auto leitura = lerPacote(pastaPacote, /*aceitarParcial*/ true);
    r.status = leitura.status;
    r.erro = leitura.erro;
    if (leitura.status != StatusLeitura::Ok) return v;
    v.pacote = std::move(leitura.pacote);
    const Pacote& p = v.pacote;
    r.parcial = p.parcial;
    r.exportadoEm = p.exportadoEm;

    // Mapa: o id gravado; pacote anterior, pelo nome; senão o padrão.
    for (const auto& [id, nome] : mapas)
        if (!p.mapaId.empty() && id == p.mapaId) { r.mapaId = id; r.nomeMapa = nome; }
    if (r.mapaId.empty())
        for (const auto& [id, nome] : mapas)
            if (nome == p.folderMap) { r.mapaId = id; r.nomeMapa = nome; break; }
    if (r.mapaId.empty()) {
        r.mapaId = mapaPadrao;
        for (const auto& [id, nome] : mapas)
            if (id == mapaPadrao) r.nomeMapa = nome;
    }

    auto& selecao = v.selecao;
    selecao.insert(p.selecao.begin(), p.selecao.end());
    if (selecao.empty()) {
        selecao = selecaoAtual;
        r.usouSelecaoAtual = true;
    }

    // Mesmo planejamento do export (gerarPacote): os caminhos saem iguais.
    auto plano = planejarConsolidacao(registro, pastaProjeto, p.media(), {NivelHierarquia::PastaManual}, {},
                                      ModoPrefixoArquivo::Nenhum, {}, /*autoResolver*/ true, false, false,
                                      /*paraExport*/ true, r.mapaId, /*estritoAoDestino*/ false,
                                      /*semPastaNaRaiz*/ true);
    for (auto& ip : plano.itens)
        if (selecao.count(ip.itemId)) v.planejados.push_back(std::move(ip));

    std::map<std::string, size_t> porCaminho, porItem;
    for (size_t i = 0; i < p.arquivos.size(); ++i) {
        porCaminho[caminhoSemCaixa(p.arquivos[i].caminho)] = i;
        if (!p.arquivos[i].itemId.empty()) porItem[p.arquivos[i].itemId] = i;
    }
    std::map<std::string, const Pendencia*> pendenciaDe;
    for (const auto& pe : p.pendencias) pendenciaDe[pe.itemId] = &pe;

    const int total = static_cast<int>(v.planejados.size());
    int feito = 0;
    for (const auto& ip : v.planejados) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            r.cancelado = true;
            break;
        }
        ++feito;
        ++r.selecionados;
        const RegistroArquivo* e = nullptr;
        if (auto it = porItem.find(ip.itemId); it != porItem.end()) e = &p.arquivos[it->second];
        else if (auto ic = porCaminho.find(caminhoSemCaixa(ip.caminhoRelativoDestino.toStdString())); ic != porCaminho.end() &&
                 (p.arquivos[ic->second].itemId.empty() || p.arquivos[ic->second].itemId == ip.itemId))
            e = &p.arquivos[ic->second];

        Problema pr;
        pr.itemId = ip.itemId;
        pr.codigo = ip.codigoAcervo;
        pr.caminho = e ? e->caminho : ip.caminhoRelativoDestino.replaceCharacter('\\', '/').toStdString();
        if (e) {
            const juce::File f = p.media().getChildFile(e->caminho);
            if (!f.existsAsFile() || f.getSize() == 0) pr.tipo = TipoProblema::ArquivoAusente;
            else if (e->tamanho > 0 && f.getSize() != e->tamanho) pr.tipo = TipoProblema::ArquivoAlterado;
            else {
                ++r.ok;
                continue;
            }
        } else if (ip.emConflito) {
            pr.tipo = TipoProblema::ConflitoNome;
        } else {
            auto noMain = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto);
            auto naOrigem = matriz::vault::resolverArquivo(registro, ip.arquivoId, pastaProjeto,
                                                           matriz::vault::Preferencia::Origem);
            pr.tipo = (!noMain || (naOrigem && *naOrigem == *noMain)) ? TipoProblema::ForaDoMain
                                                                      : TipoProblema::NaoExportado;
        }
        if (auto ip2 = pendenciaDe.find(ip.itemId); ip2 != pendenciaDe.end()) pr.detalhe = ip2->second->detalhe;
        v.problemaDe[ip.itemId] = r.problemas.size();
        r.problemas.push_back(std::move(pr));
    }
    return v;
}

}  // namespace

ResultadoVerificacao verificarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                     const juce::File& pastaPacote, const std::set<std::string>& selecaoAtual,
                                     const std::vector<std::pair<std::string, juce::String>>& mapas,
                                     const std::string& mapaPadrao, const AoProgredir& aoProgredir) {
    return verificar(registro, pastaProjeto, pastaPacote, selecaoAtual, mapas, mapaPadrao, aoProgredir).r;
}

ResultadoCompletar completarPacote(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                   const juce::File& pastaPacote, const std::set<std::string>& selecaoAtual,
                                   const std::vector<std::pair<std::string, juce::String>>& mapas,
                                   const std::string& mapaPadrao, const AoProgredir& aoProgredir) {
    ResultadoCompletar rc;
    auto v = verificar(registro, pastaProjeto, pastaPacote, selecaoAtual, mapas, mapaPadrao, {});
    if (v.r.status != StatusLeitura::Ok) {
        rc.falhas.push_back(v.r.erro.isNotEmpty() ? v.r.erro.toStdString() : std::string("not a package"));
        return rc;
    }
    const Pacote& p = v.pacote;

    // Os registros do JSON como estão (com todos os campos que tiverem): o que não muda fica igual.
    const juce::File origemJson = pastaPacote.getChildFile(p.parcial ? kArquivoParcial : kArquivoJson);
    const juce::var antigo = juce::JSON::parse(origemJson.loadFileAsString());
    juce::Array<juce::var> registros;
    if (const auto* arr = antigo.getProperty("arquivos", {}).getArray()) registros = *arr;
    auto caminhoDe = [](const juce::var& e) {
        return e.getProperty("caminho", {}).toString().replaceCharacter('\\', '/').toLowerCase().toStdString();
    };
    auto removerRegistro = [&](const std::string& caminho) {
        for (int i = registros.size(); --i >= 0;)
            if (caminhoDe(registros.getReference(i)) == caminhoSemCaixa(caminho)) registros.remove(i);
    };
    auto ocupadoNoCatalogo = [&](const juce::String& caminho) {
        const auto c = caminhoSemCaixa(caminho.toStdString());
        for (const auto& e : registros)
            if (caminhoDe(e) == c) return true;
        return false;
    };

    Montagem m(registro);
    m.pastaProjeto = pastaProjeto;
    m.pasta = pastaPacote;
    m.mapaId = v.r.mapaId;
    m.nomeMapa = v.r.nomeMapa.isNotEmpty() ? v.r.nomeMapa : p.folderMap;
    m.nomeColecao = p.colecao;
    m.exportadoEm = p.exportadoEm;
    m.embutir = p.embutir.value_or(false);
    m.selecao.assign(v.selecao.begin(), v.selecao.end());
    m.carregarPastas();

    const juce::String alvoJson = p.parcial ? kArquivoParcial : kArquivoJson;
    auto gravar = [&](const juce::String& nome) {
        m.arquivos = registros;
        return m.gravar(nome, antigo.getProperty("pastas", {}));
    };

    std::vector<const ItemPlanejado*> aFazer;
    for (const auto& ip : v.planejados)
        if (auto it = v.problemaDe.find(ip.itemId); it != v.problemaDe.end() && v.r.problemas[it->second].completavel())
            aFazer.push_back(&ip);
    // O que este passo não resolve continua pendente.
    for (const auto& pr : v.r.problemas)
        if (!pr.completavel())
            m.pendencias.push_back({pr.itemId, pr.codigo,
                                    pr.tipo == TipoProblema::ForaDoMain ? kMotivoForaDoMain : kMotivoConflito, pr.detalhe});

    const juce::uint32 kIntervaloMs = 15000;
    juce::uint32 ultimo = juce::Time::getMillisecondCounter();
    const int total = static_cast<int>(aFazer.size());
    int feito = 0;
    for (const auto* ipp : aFazer) {
        if (aoProgredir && !aoProgredir(feito, total)) {
            rc.cancelado = true;
            break;
        }
        ++feito;
        const auto& pr = v.r.problemas[v.problemaDe[ipp->itemId]];
        ItemPlanejado ip = *ipp;
        // O registro antigo deste item (listado sem arquivo / com outro tamanho) sai do catálogo
        // quando a cópia nova entra; o arquivo truncado num caminho que deixou de ser o dele também.
        const bool tinhaRegistro = pr.tipo != TipoProblema::NaoExportado;
        const std::string proprio = tinhaRegistro ? caminhoSemCaixa(pr.caminho) : std::string();  // o registro antigo dele
        if (caminhoSemCaixa(ip.caminhoRelativoDestino.toStdString()) != proprio) {
            // O caminho do planejador já é de outro arquivo do catálogo: nunca passar por cima, vai com _N.
            const juce::String base = ip.caminhoRelativoDestino;
            const int ponto = base.lastIndexOfChar('.');
            const int barra = base.lastIndexOfChar('/');
            const bool temExt = ponto > barra + 1;
            for (int n = 1; ocupadoNoCatalogo(ip.caminhoRelativoDestino) &&
                            caminhoSemCaixa(ip.caminhoRelativoDestino.toStdString()) != proprio;
                 ++n)
                ip.caminhoRelativoDestino = (temExt ? base.substring(0, ponto) : base) + "_" + juce::String(n) +
                                            (temExt ? base.substring(ponto) : juce::String());
        }
        try {
            bool adotado = false;
            auto reg = m.entregar(ip, /*substituir*/ true, &adotado);
            if (reg.isVoid()) {
                m.pendente(ip, kMotivoForaDoMain, "not in the MAIN yet");
                continue;
            }
            if (tinhaRegistro) {
                removerRegistro(pr.caminho);
                const juce::File velho = m.media().getChildFile(pr.caminho);
                if (caminhoSemCaixa(pr.caminho) != caminhoSemCaixa(ip.caminhoRelativoDestino.toStdString()) && velho.existsAsFile())
                    velho.deleteFile();
            }
            removerRegistro(ip.caminhoRelativoDestino.toStdString());
            registros.add(reg);
            ++(adotado ? rc.adotados : rc.copiados);
        } catch (const FalhaPacote& e) {
            rc.falhas.push_back(ip.codigoAcervo + ": " + e.what());
            m.pendente(ip, e.motivo, e.what());
        } catch (const std::exception& e) {
            rc.falhas.push_back(ip.codigoAcervo + ": " + e.what());
            m.pendente(ip, kMotivoErro, e.what());
        }
        if (juce::Time::getMillisecondCounter() - ultimo >= kIntervaloMs) {
            gravar(alvoJson);
            ultimo = juce::Time::getMillisecondCounter();
        }
    }
    // Itens que ficaram para depois (cancelado): continuam pendentes como estavam.
    for (size_t i = static_cast<size_t>(feito); i < aFazer.size(); ++i) {
        const auto& pr = v.r.problemas[v.problemaDe[aFazer[i]->itemId]];
        m.pendencias.push_back({pr.itemId, pr.codigo, kMotivoErro, pr.detalhe.empty() ? "not completed yet" : pr.detalhe});
    }

    // Cancelado no meio de um export interrompido: continua parcial. Senão o pacote fica com o JSON final.
    m.completadoEm = juce::String(matriz::model::agoraIso8601());
    const juce::String destinoJson = (rc.cancelado && p.parcial) ? juce::String(kArquivoParcial) : juce::String(kArquivoJson);
    if (!gravar(destinoJson)) rc.falhas.push_back("could not write " + destinoJson.toStdString());
    else if (destinoJson == kArquivoJson) pastaPacote.getChildFile(kArquivoParcial).deleteFile();
    rc.pendencias = m.pendencias;

    m.registrarNoLog("Collection Package Completed",
                     {"Package: " + pastaPacote.getFullPathName(), "Folder map: " + m.nomeMapa,
                      "Copied: " + juce::String(rc.copiados) + "; already there (adopted): " + juce::String(rc.adotados) +
                          "; failures: " + juce::String((int) rc.falhas.size()) + (rc.cancelado ? " (cancelled)" : "")});
    return rc;
}

// ---------------------------------------------------------------------------
// INTAKE (Fase 3)
// ---------------------------------------------------------------------------

bool pareceUmPacote(const juce::File& pasta) {
    return pasta.isDirectory() && pasta.getChildFile(kArquivoJson).existsAsFile();
}

ResultadoLeitura lerPacote(const juce::File& pasta, bool aceitarParcial) {
    ResultadoLeitura r;
    const bool parcial = !pareceUmPacote(pasta);
    if (parcial && !(aceitarParcial && pasta.getChildFile(kArquivoParcial).existsAsFile())) return r;
    r.status = StatusLeitura::Invalido;
    juce::var json;
    const auto parse = juce::JSON::parse(pasta.getChildFile(parcial ? kArquivoParcial : kArquivoJson).loadFileAsString(), json);
    if (parse.failed() || !json.isObject()) {
        r.erro = parse.failed() ? parse.getErrorMessage() : juce::String("not a JSON object");
        return r;
    }
    if (json.getProperty("formato", {}).toString() != kFormato) {
        r.erro = "\"formato\" is not \"" + juce::String(kFormato) + "\"";
        return r;
    }
    const auto versao = json.getProperty("versao", {});
    if (!(versao.isInt() || versao.isInt64() || versao.isDouble()) || static_cast<int>(versao) != kVersao) {
        r.status = StatusLeitura::VersaoDesconhecida;
        r.erro = versao.toString();
        return r;
    }
    Pacote& p = r.pacote;
    p.pasta = pasta;
    if (!p.media().isDirectory()) {
        r.erro = "Media/ folder is missing";
        return r;
    }
    p.colecao = json.getProperty("colecao", {}).toString();
    p.folderMap = json.getProperty("folder_map", {}).toString();
    p.exportadoEm = json.getProperty("exportado_em", {}).toString();
    p.pastas = json.getProperty("pastas", {});
    p.parcial = parcial;
    p.mapaId = texto(json, "mapa_id");
    if (json.hasProperty("embutir")) p.embutir = static_cast<bool>(json.getProperty("embutir", false));
    p.selecao = listaDeJson(json.getProperty("selecao", {}));
    if (auto* arr = json.getProperty("pendencias", {}).getArray())
        for (const auto& e : *arr)
            if (e.isObject())
                p.pendencias.push_back({texto(e, "item_id"), texto(e, "codigo"), texto(e, "motivo"),
                                        e.getProperty("detalhe", {}).toString().toStdString()});
    if (auto* arr = json.getProperty("arquivos", {}).getArray()) {
        for (const auto& e : *arr) {
            if (!e.isObject()) continue;
            RegistroArquivo ra;
            ra.sha256 = e.getProperty("sha256", {}).toString().trim().toLowerCase().toStdString();
            ra.caminho = e.getProperty("caminho", {}).toString().replaceCharacter('\\', '/').toStdString();
            if (auto* pa = e.getProperty("pasta", {}).getArray())
                for (const auto& n : *pa)
                    if (n.toString().trim().isNotEmpty()) ra.pasta.push_back(n.toString().trim());
            ra.itemId = texto(e, "item_id");
            ra.codigo = texto(e, "codigo");
            ra.tamanho = static_cast<juce::int64>(e.getProperty("tamanho", 0));
            ra.dados = lerJson(e);
            if (!ra.sha256.empty()) p.arquivos.push_back(std::move(ra));
        }
    }
    r.status = StatusLeitura::Ok;
    return r;
}

void gravarDadosFicha(matriz::db::Database& registro, const std::string& itemId, const DadosFicha& d,
                      matriz::model::nomes::Vocabulario& vocabTags, matriz::model::nomes::Vocabulario& vocabSubjects,
                      const std::string& autor) {
    const std::string agora = matriz::model::agoraIso8601();
    // Mesmo par de escritas de ProjetoAberto::salvarMetadado: coluna do item
    // + espelho em item_campo.
    auto gravar = [&](const char* coluna, const std::string& valor) {
        if (valor.empty()) return;
        try {
            if (std::string(coluna) == "dc_title") {
                // O título do pacote vai nas DUAS colunas (como em importarSidecarsEditados): item.titulo é o que a grade
                // mostra e item.dc_title é o campo Title da ficha. Só titulo deixava a ficha sem título após o INTAKE.
                registro.run("UPDATE item SET titulo = ?, dc_title = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                             {Value::of(valor), Value::of(valor), Value::of(agora), Value::of(itemId)});
            } else {
                registro.run(std::string("UPDATE item SET ") + coluna + " = ?, atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                             {Value::of(valor), Value::of(agora), Value::of(itemId)});
            }
        } catch (...) {
            try {
                registro.run("UPDATE item SET atualizado_em = ?, metadados_editados = 1 WHERE id = ?",
                             {Value::of(agora), Value::of(itemId)});
            } catch (...) {}
        }
        try {
            registro.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
                         "VALUES (?, ?, 'raiz', 0, ?, ?, 'humano', ?) "
                         "ON CONFLICT(item_id, nivel, nivel_indice, campo_id) DO UPDATE SET valor = excluded.valor, "
                         "fonte = 'humano', atualizado_em = excluded.atualizado_em",
                         {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(std::string(coluna)),
                          Value::of(valor), Value::of(agora)});
        } catch (...) {}
    };
    gravar("dc_title", d.titulo);
    gravar("dc_description", d.descricao);
    gravar("ano", d.eventDate);
    gravar("collection_type", d.content);
    if (!d.subjects.empty()) {
        std::string lista;
        for (const auto& s : d.subjects) lista += (lista.empty() ? "" : ", ") + s;
        gravar("dc_subject", vocabSubjects.listaSubjects(lista));
    }

    auto tag = [&](const std::string& t) {
        const std::string canon = vocabTags.canonico(t);
        if (canon.empty()) return std::string();
        registro.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                     {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(canon)});
        return canon;
    };
    for (const auto& t : d.tags) tag(t);
    if (!d.pessoas.empty()) {
        registro.exec("CREATE TABLE IF NOT EXISTS collection_person (id TEXT PRIMARY KEY, nome TEXT NOT NULL UNIQUE, "
                      "criado_em TEXT NOT NULL)");
        for (const auto& p : d.pessoas) {
            const std::string canon = tag(p);
            if (canon.empty()) continue;
            registro.run("INSERT OR IGNORE INTO collection_person (id, nome, criado_em) VALUES (?, ?, ?)",
                         {Value::of(matriz::model::novoUuid()), Value::of(canon), Value::of(agora)});
        }
    }

    // Geo: os campos do pacote valem por cima de uma leitura automática (GPS
    // do EXIF) do item recém-ingerido; o que o pacote não traz fica.
    if (auto* g = d.geo.getDynamicObject()) {
        std::string cols = "asset_id", marcas = "?", atualiza;
        std::vector<Value> vals{Value::of(itemId)};
        for (auto* c : kColunasGeo) {
            if (!g->hasProperty(c)) continue;
            const auto v = g->getProperty(c);
            cols += std::string(", ") + c;
            marcas += ", ?";
            atualiza += std::string(atualiza.empty() ? "" : ", ") + c + " = excluded." + c;
            if (v.isDouble() || v.isInt() || v.isInt64()) vals.push_back(Value::of(static_cast<double>(v)));
            else vals.push_back(Value::of(v.toString().toStdString()));
        }
        if (!atualiza.empty()) {
            cols += ", created_at, updated_at";
            marcas += ", ?, ?";
            vals.push_back(Value::of(agora));
            vals.push_back(Value::of(agora));
            registro.run("INSERT INTO asset_geolocation (" + cols + ") VALUES (" + marcas + ") "
                         "ON CONFLICT(asset_id) DO UPDATE SET " + atualiza + ", updated_at = excluded.updated_at",
                         vals);
        }
    }

    for (const auto& m : d.marcadores) {
        registro.run("INSERT INTO item_observacao (id, item_id, texto, autor, criado_em, minutagem_ms, titulo) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?)",
                     {Value::of(matriz::model::novoUuid()), Value::of(itemId), Value::of(m.texto), Value::of(autor),
                      Value::of(agora),
                      m.tempoS ? Value::of(static_cast<long long>(std::llround(*m.tempoS * 1000.0))) : Value::null(),
                      m.titulo.empty() ? Value::null() : Value::of(m.titulo)});
    }
}

}  // namespace matriz::consolidacao::pacote
