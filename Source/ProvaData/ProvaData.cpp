#include "ProvaData.h"

#include <atomic>
#include <mutex>
#include <random>
#include <set>

#include "../App/Preferencias.h"
#include "../I18n/Strings.h"
#include "../Model/ProjectLog.h"
#include "../Vault/Resolucao.h"
#include "../Whatsapp/WhatsappIngestao.h"
#include "OpenTimestamps.h"
#include "PdfRaster.h"

namespace matriz::provadata {

namespace {

using ots::Bytes;

juce::String lerPref(const char* k, const char* padrao = "0") { return matriz::app::lerPreferenciaTexto(k, padrao); }
void gravarPref(const char* k, bool v) { matriz::app::gravarPreferenciaTexto(k, v ? "1" : "0"); }

juce::File fila(const juce::File& alvo) { return alvo.getSiblingFile(alvo.getFileName() + ".ots.fila"); }
juce::File info(const juce::File& alvo) { return alvo.getSiblingFile(alvo.getFileName() + ".ots.info"); }

Bytes lerBytes(const juce::File& f) {
    juce::MemoryBlock b;
    if (!f.loadFileAsData(b)) return {};
    return Bytes(static_cast<const std::uint8_t*>(b.getData()), static_cast<const std::uint8_t*>(b.getData()) + b.getSize());
}

bool gravarBytes(const juce::File& f, const Bytes& b) {
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile(f.getFileName() + ".tmp");
    if (!tmp.replaceWithData(b.data(), b.size())) return false;
    return tmp.moveFileTo(f);
}

Bytes nonceAleatorio() {
    std::random_device rd;
    Bytes n(16);
    for (auto& b : n) b = static_cast<std::uint8_t>(rd() & 0xff);
    return n;
}

juce::String agoraIso() { return juce::Time::getCurrentTime().toISO8601(true); }

bool ehConversaOuDoc(matriz::model::Project& p, const std::string& itemId, bool* conversa) {
    if (conversa) *conversa = false;
    try {
        if (matriz::whatsapp::itemEhConversa(p.registroLeitura(), itemId)) {
            if (conversa) *conversa = true;
            return true;
        }
        auto st = p.registroLeitura().prepare("SELECT caminho_relativo FROM arquivo WHERE item_id = ? ORDER BY eh_master DESC, id LIMIT 1");
        st.bind(1, matriz::db::Value::of(itemId));
        // Qualquer arquivo do backup pode receber a prova de data, a pedido do usuário (TOOLS > OpenTimeStamps ou botão direito).
        return st.step();
    } catch (...) {
        return false;
    }
}

struct ArquivoDoItem {
    juce::File arquivo;
    juce::String nomeOriginal;
    juce::String sha256;
};

std::optional<ArquivoDoItem> arquivoPrincipalDoItem(matriz::model::Project& p, const std::string& itemId) {
    try {
        auto st = p.registroLeitura().prepare("SELECT id, caminho_absoluto_origem, checksum_sha256, caminho_relativo FROM arquivo WHERE item_id = ? "
                                       "ORDER BY (CASE WHEN derivada_de_arquivo_id IS NOT NULL THEN 1 ELSE 0 END) DESC, eh_master DESC, id LIMIT 1");
        st.bind(1, matriz::db::Value::of(itemId));
        if (!st.step()) return std::nullopt;
        const std::string arquivoId = st.columnText(0);
        const juce::String origem = juce::String::fromUTF8(st.columnText(1).c_str());
        const juce::String sha = juce::String(st.columnText(2));
        const juce::String relativo = juce::String::fromUTF8(st.columnText(3).c_str());
        ArquivoDoItem r;
        r.arquivo = matriz::vault::caminhoEsperadoArquivo(p.registroLeitura(), arquivoId, p.pasta());
        if (!r.arquivo.existsAsFile()) return std::nullopt;
        r.nomeOriginal = r.arquivo.getFileName();
        // O nome "como entrou": o da origem, quando houver; no backup o nome pode ter sido ajustado.
        if (origem.isNotEmpty()) r.nomeOriginal = juce::File(origem).getFileName();
        else if (relativo.isNotEmpty()) r.nomeOriginal = juce::File(relativo).getFileName();
        r.sha256 = sha.length() == 64 ? sha.toLowerCase() : sha256Hexa(r.arquivo);
        return r;
    } catch (...) {
        return std::nullopt;
    }
}

juce::String ipPublico() {
    juce::URL u("https://api.ipify.org");
    auto s = u.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(4000));
    if (!s) return {};
    return s->readEntireStreamAsString().trim().substring(0, 64);
}

juce::String macDoComputador() {
    for (const auto& m : juce::MACAddress::getAllAddresses()) {
        const auto t = m.toString();
        if (!m.isNull() && t != "00-00-00-00-00-00") return t;
    }
    return {};
}

std::mutex gMutexAtualizacao;
std::vector<juce::File> gProjetos;
std::atomic<bool> gEmExecucao{false};

class Relogio : private juce::Timer {
public:
    void iniciar() {
        if (ativo_) return;
        ativo_ = true;
        startTimer(8 * 1000);  // 1ª rodada logo depois do início
    }
    void parar() { ativo_ = false; stopTimer(); }

private:
    void timerCallback() override {
        startTimer(60 * 60 * 1000);  // depois, a cada hora
        atualizarAgora();
    }
    bool ativo_ = false;
};
Relogio& relogio() { static Relogio r; return r; }

juce::StringArray pastasExtra() {
    juce::StringArray l;
    l.addLines(matriz::app::lerPreferenciaTexto("prova_pastas_extra", ""));
    l.removeEmptyStrings();
    return l;
}

bool rodadaDeAtualizacao(const std::vector<juce::File>& projetos) {
    bool mudou = false;
    // Trechos exportados (pasta) e prints (arquivo): recibos .ots / .ots.fila fora do projeto.
    for (const auto& caminho : pastasExtra()) {
        const juce::File entrada(caminho);
        std::set<juce::String> alvos;
        if (entrada.isDirectory()) {
            for (const auto& o : entrada.findChildFiles(juce::File::findFiles, true, "*.ots"))
                alvos.insert(o.getFullPathName().upToLastOccurrenceOf(".ots", false, false));
            for (const auto& q : entrada.findChildFiles(juce::File::findFiles, true, "*.ots.fila"))
                alvos.insert(q.getFullPathName().upToLastOccurrenceOf(".ots.fila", false, false));
        } else if (entrada.existsAsFile()) {
            alvos.insert(entrada.getFullPathName());
        }
        for (const auto& alvo : alvos)
            if (atualizarCarimbo(juce::File(alvo), true)) mudou = true;
    }
    for (const auto& proj : projetos) {
        const auto pasta = pastaDeProvas(proj);
        if (!pasta.isDirectory()) continue;
        for (const auto& f : pasta.findChildFiles(juce::File::findFiles, true, "*.registro.json")) {
            if (atualizarCarimbo(f, true)) mudou = true;
        }
    }
    return mudou;
}

} // namespace

// ── Preferências ───────────────────────────────────────────────────────────────────────────────

OpcoesComputador lerOpcoesComputador() {
    OpcoesComputador o;
    o.nome = lerPref("prova_pc_nome") == "1";
    o.ip = lerPref("prova_pc_ip") == "1";
    o.mac = lerPref("prova_pc_mac") == "1";
    return o;
}
void gravarOpcoesComputador(const OpcoesComputador& o) {
    gravarPref("prova_pc_nome", o.nome);
    gravarPref("prova_pc_ip", o.ip);
    gravarPref("prova_pc_mac", o.mac);
}
bool lerRegistrarProvaPadrao() { return lerPref("prova_registrar") == "1"; }
void gravarRegistrarProvaPadrao(bool v) { gravarPref("prova_registrar", v); }
bool jaViuExplicacao() { return lerPref("prova_viu_explicacao") == "1"; }
void gravarJaViuExplicacao() { gravarPref("prova_viu_explicacao", true); }

// Por enquanto só conversas do WhatsApp recebem prova de data (e só se o usuário quiser). Documentos comuns não são carimbados;
// a prova de qualquer arquivo virá depois, por um menu Tools.
bool extensaoElegivel(const juce::String&) { return false; }

bool itemElegivel(matriz::model::Project& projeto, const std::string& itemId) { return ehConversaOuDoc(projeto, itemId, nullptr); }

std::optional<InfoArquivoItem> infoArquivoDoItem(matriz::model::Project& projeto, const std::string& itemId) {
    const auto a = arquivoPrincipalDoItem(projeto, itemId);
    if (!a) return std::nullopt;
    return InfoArquivoItem{a->arquivo, a->nomeOriginal, a->sha256};
}

juce::String sha256Hexa(const juce::File& arquivo) { return juce::String(ots::hex(ots::sha256DoArquivo(arquivo))); }

// ── Carimbo de arquivo ─────────────────────────────────────────────────────────────────────────

juce::File arquivoOts(const juce::File& alvo) { return alvo.getSiblingFile(alvo.getFileName() + ".ots"); }

bool carimbarArquivo(const juce::File& alvo, bool usarRede) {
    if (!alvo.existsAsFile()) return false;
    const auto otsFile = arquivoOts(alvo);
    if (otsFile.existsAsFile() || fila(alvo).existsAsFile()) return true;  // já carimbado
    const Bytes digest = ots::sha256DoArquivo(alvo);
    if (digest.size() != 32) return false;
    const Bytes nonce = nonceAleatorio();
    auto recibo = ots::novoRecibo(digest, nonce);
    if (usarRede && ots::enviarParaCalendarios(recibo, ots::calendariosPadrao()) > 0) return gravarBytes(otsFile, ots::serializar(recibo));
    // Sem internet: guarda o necessário para refazer o recibo igual depois (mesmo nonce).
    return fila(alvo).replaceWithText(juce::String(ots::hex(digest)) + "\n" + juce::String(ots::hex(nonce)) + "\n" + agoraIso() + "\n");
}

bool atualizarCarimbo(const juce::File& alvo, bool usarRede) {
    if (!usarRede) return false;
    bool mudou = false;
    const auto otsFile = arquivoOts(alvo);
    if (!otsFile.existsAsFile() && fila(alvo).existsAsFile()) {
        juce::StringArray linhas;
        linhas.addLines(fila(alvo).loadFileAsString());
        if (linhas.size() >= 2 && alvo.existsAsFile()) {
            const Bytes digestAtual = ots::sha256DoArquivo(alvo);
            const Bytes digest = ots::deHex(linhas[0].toStdString());
            const Bytes nonce = ots::deHex(linhas[1].toStdString());
            if (digest == digestAtual && nonce.size() == 16) {
                auto recibo = ots::novoRecibo(digest, nonce);
                if (ots::enviarParaCalendarios(recibo, ots::calendariosPadrao()) > 0 && gravarBytes(otsFile, ots::serializar(recibo))) {
                    fila(alvo).deleteFile();
                    mudou = true;
                }
            }
        }
    }
    if (otsFile.existsAsFile()) {
        auto recibo = ots::ler(lerBytes(otsFile));
        if (recibo) {
            if (!ots::alturaConfirmada(*recibo) && ots::tentarAtualizar(*recibo)) {
                if (gravarBytes(otsFile, ots::serializar(*recibo))) mudou = true;
            }
            if (const auto altura = ots::alturaConfirmada(*recibo)) {
                const auto inf = info(alvo);
                juce::StringArray l;
                l.addLines(inf.loadFileAsString());
                const bool temData = l.size() >= 2 && l[1].isNotEmpty();
                if (!temData) {
                    const auto data = ots::dataDoBloco(*altura);
                    if (data.isNotEmpty()) { inf.replaceWithText(juce::String(static_cast<juce::int64>(*altura)) + "\n" + data + "\n"); mudou = true; }
                    else if (!inf.existsAsFile()) { inf.replaceWithText(juce::String(static_cast<juce::int64>(*altura)) + "\n\n"); }
                }
            }
        }
    }
    return mudou;
}

EstadoProva estadoDoCarimbo(const juce::File& alvo) {
    EstadoProva e;
    const auto otsFile = arquivoOts(alvo);
    if (otsFile.existsAsFile()) {
        e.estado = Estado::Pendente;
        if (auto r = ots::ler(lerBytes(otsFile))) {
            if (const auto altura = ots::alturaConfirmada(*r)) {
                e.estado = Estado::Confirmada;
                e.bloco = *altura;
                juce::StringArray l;
                l.addLines(info(alvo).loadFileAsString());
                if (l.size() >= 2) e.dataBloco = l[1];
            }
        }
        e.registradoEm = otsFile.getCreationTime().toISO8601(true);
    } else if (fila(alvo).existsAsFile()) {
        e.estado = Estado::NaFila;
        juce::StringArray l;
        l.addLines(fila(alvo).loadFileAsString());
        if (l.size() >= 3) e.registradoEm = l[2];
    }
    return e;
}

bool carimboConfere(const juce::File& alvo) {
    const auto otsFile = arquivoOts(alvo);
    if (!alvo.existsAsFile() || !otsFile.existsAsFile()) return false;
    const auto r = ots::ler(lerBytes(otsFile));
    return r && r->digestArquivo == ots::sha256DoArquivo(alvo);
}

// ── Itens ────────────────────────────────────────────────────────────────────────────────────────

juce::File pastaDeProvas(const juce::File& pastaProject) { return pastaProject.getChildFile("provas"); }
juce::File manifestoDoItem(const juce::File& pastaProject, const std::string& itemId) {
    return pastaDeProvas(pastaProject).getChildFile(juce::String(itemId) + ".registro.json");
}
EstadoProva estadoDoItem(const juce::File& pastaProject, const std::string& itemId) {
    const auto m = manifestoDoItem(pastaProject, itemId);
    if (!m.existsAsFile()) return {};
    return estadoDoCarimbo(m);
}

std::map<std::string, int> estadosDeTodos(const juce::File& pastaProject) {
    std::map<std::string, int> r;
    const auto pasta = pastaDeProvas(pastaProject);
    if (!pasta.isDirectory()) return r;
    for (const auto& f : pasta.findChildFiles(juce::File::findFiles, false, "*.registro.json")) {
        const auto e = estadoDoCarimbo(f);
        if (e.estado == Estado::Nenhum) continue;
        r[f.getFileName().upToFirstOccurrenceOf(".registro.json", false, false).toStdString()] =
            e.estado == Estado::NaFila ? 1 : (e.estado == Estado::Pendente ? 2 : 3);
    }
    return r;
}

juce::var montarManifesto(const juce::String& tipo, const std::vector<EntradaManifesto>& arquivos, const OpcoesComputador& computador, bool usarRede,
                          const juce::String& observacao) {
    auto* raiz = new juce::DynamicObject();
    juce::var v(raiz);
    raiz->setProperty("formato", "bkr-matriz-registro/1");
    raiz->setProperty("aplicativo", "BKR Matriz Mini " + juce::String(JUCE_APPLICATION_VERSION_STRING));
    raiz->setProperty("tipo", tipo);
    juce::Array<juce::var> lista;
    for (const auto& a : arquivos) {
        auto* o = new juce::DynamicObject();
        o->setProperty("nome", a.nome);
        o->setProperty("sha256", a.sha256);
        lista.add(juce::var(o));
    }
    raiz->setProperty("arquivos", lista);
    const auto agora = juce::Time::getCurrentTime();
    raiz->setProperty("data_hora_local", agora.toISO8601(true));
    raiz->setProperty("fuso_horario", agora.getTimeZone());
    if (observacao.isNotEmpty()) raiz->setProperty("observacao", observacao);
    if (computador.algum()) {
        auto* c = new juce::DynamicObject();
        if (computador.nome) {
            c->setProperty("nome", juce::SystemStats::getComputerName());
            c->setProperty("sistema", juce::SystemStats::getOperatingSystemName());
        }
        if (computador.ip) c->setProperty("ip_publico", usarRede ? ipPublico() : juce::String());
        if (computador.mac) c->setProperty("mac", macDoComputador());
        c->setProperty("nota", matriz::i18n::t("light.prova.computerNote"));
        raiz->setProperty("computador", juce::var(c));
    }
    return v;
}

bool gravarManifestoECarimbar(const juce::File& destinoJson, const juce::var& manifesto, bool usarRede) {
    destinoJson.getParentDirectory().createDirectory();
    if (!destinoJson.replaceWithText(juce::JSON::toString(manifesto, false), false, false, "\n")) return false;
    return carimbarArquivo(destinoJson, usarRede);
}

bool registrarItem(matriz::model::Project& projeto, const std::string& itemId, const OpcoesComputador& computador, bool usarRede, juce::String* erro) {
    const auto falhar = [&](const char* chave) { if (erro) *erro = matriz::i18n::t(chave); return false; };
    bool conversa = false;
    if (!ehConversaOuDoc(projeto, itemId, &conversa)) return falhar("light.prova.err.notEligible");
    const auto manifesto = manifestoDoItem(projeto.pasta(), itemId);
    if (manifesto.existsAsFile()) return falhar("light.prova.err.already");

    std::vector<EntradaManifesto> entradas;
    const auto principal = arquivoPrincipalDoItem(projeto, itemId);
    if (!principal) return falhar("light.prova.err.noFile");
    if (!conversa || principal->arquivo.hasFileExtension("zip")) {
        entradas.push_back({principal->nomeOriginal, principal->sha256});
    } else {
        // Conversa vinda de pasta ou .txt: o .txt e cada mídia, cada uma com o seu hash.
        entradas.push_back({principal->nomeOriginal, principal->sha256});
        if (auto info = matriz::whatsapp::lerInfoConversa(projeto.registroLeitura(), itemId)) {
            if (!info->txtItemId.empty() && info->txtItemId != itemId)
                if (auto t = arquivoPrincipalDoItem(projeto, info->txtItemId)) entradas.push_back({t->nomeOriginal, t->sha256});
            for (const auto& m : info->midias) {
                if (m.midiaItemId.empty()) continue;
                if (auto a = arquivoPrincipalDoItem(projeto, m.midiaItemId)) entradas.push_back({m.nomeOriginal.isNotEmpty() ? m.nomeOriginal : a->nomeOriginal, a->sha256});
            }
        }
    }
    const auto m = montarManifesto(conversa ? "whatsapp-conversa" : "documento", entradas, computador, usarRede);
    if (!gravarManifestoECarimbar(manifesto, m, usarRede)) return falhar("light.prova.err.write");
    colocarAoLadoDoArquivo(projeto, itemId);
    return true;
}

void colocarAoLadoDe(const juce::File& pastaProject, const std::string& itemId, const juce::File& arquivoNoBackup) {
    try {
        const auto manifesto = manifestoDoItem(pastaProject, itemId);
        if (!manifesto.existsAsFile() || !arquivoNoBackup.existsAsFile()) return;
        const auto base = arquivoNoBackup.getFileName();
        const auto pasta = arquivoNoBackup.getParentDirectory();
        const auto copiar = [&](const juce::File& de, const juce::String& sufixo) {
            const auto para = pasta.getChildFile(base + sufixo);
            if (de.existsAsFile()) de.copyFileTo(para);
            else para.deleteFile();
        };
        copiar(manifesto, ".registro.json");
        copiar(arquivoOts(manifesto), ".registro.json.ots");
        copiar(fila(manifesto), ".registro.json.ots.fila");
        copiar(info(manifesto), ".registro.json.ots.info");
    } catch (...) {
    }
}

void colocarAoLadoDoArquivo(matriz::model::Project& projeto, const std::string& itemId) {
    const auto principal = arquivoPrincipalDoItem(projeto, itemId);
    if (principal) colocarAoLadoDe(projeto.pasta(), itemId, principal->arquivo);
}

void colocarTodasAoLado(matriz::model::Project& projeto) {
    const auto pasta = pastaDeProvas(projeto.pasta());
    if (!pasta.isDirectory()) return;
    for (const auto& f : pasta.findChildFiles(juce::File::findFiles, false, "*.registro.json"))
        colocarAoLadoDoArquivo(projeto, f.getFileName().upToFirstOccurrenceOf(".registro.json", false, false).toStdString());
}

juce::String textoComoVerificar() { return matriz::i18n::t("light.prova.howToVerify.file"); }

std::optional<juce::File> exportarProvaDoItem(matriz::model::Project& projeto, const std::string& itemId, const juce::File& pastaPai, juce::String* erro) {
    const auto falhar = [&](const char* chave) -> std::optional<juce::File> { if (erro) *erro = matriz::i18n::t(chave); return std::nullopt; };
    const auto manifesto = manifestoDoItem(projeto.pasta(), itemId);
    if (!manifesto.existsAsFile()) return falhar("light.prova.err.noProof");
    const auto principal = arquivoPrincipalDoItem(projeto, itemId);
    if (!principal) return falhar("light.prova.err.noFile");
    const auto nomeBase = principal->arquivo.getFileNameWithoutExtension();
    auto destino = pastaPai.getChildFile(matriz::i18n::t("light.prova.exportFolder").replace("{name}", nomeBase));
    for (int i = 2; destino.exists(); ++i) destino = pastaPai.getChildFile(matriz::i18n::t("light.prova.exportFolder").replace("{name}", nomeBase) + " (" + juce::String(i) + ")");
    if (!destino.createDirectory()) return falhar("light.prova.err.write");
    principal->arquivo.copyFileTo(destino.getChildFile(principal->arquivo.getFileName()));
    manifesto.copyFileTo(destino.getChildFile("registro.json"));
    if (arquivoOts(manifesto).existsAsFile()) arquivoOts(manifesto).copyFileTo(destino.getChildFile("registro.json.ots"));
    else if (fila(manifesto).existsAsFile()) fila(manifesto).copyFileTo(destino.getChildFile("registro.json.ots.fila"));
    if (info(manifesto).existsAsFile()) info(manifesto).copyFileTo(destino.getChildFile("registro.json.ots.info"));
    destino.getChildFile(matriz::i18n::t("light.prova.howToVerify.fileName")).replaceWithText(textoComoVerificar());
    return destino;
}

// Certificado em PDF (uma página A4): o que foi carimbado, o hash, o estado do carimbo e como verificar.
bool exportarProvaPdf(matriz::model::Project& projeto, const std::string& itemId, const juce::File& destinoPdf, juce::String* erro) {
    const auto falhar = [&](const char* chave) { if (erro) *erro = matriz::i18n::t(chave); return false; };
    const auto manifesto = manifestoDoItem(projeto.pasta(), itemId);
    if (!manifesto.existsAsFile()) return falhar("light.prova.err.noProof");
    const auto principal = arquivoPrincipalDoItem(projeto, itemId);
    if (!principal) return falhar("light.prova.err.noFile");
    const auto est = estadoDoCarimbo(manifesto);
    const auto json = juce::JSON::parse(manifesto.loadFileAsString());
    const bool pt = matriz::i18n::localeAtivo() == "pt_BR";
    const auto tr = [pt](const char* p, const char* e) { return juce::String::fromUTF8(pt ? p : e); };

    juce::Image pagina(juce::Image::RGB, kPaginaLarguraPx, kPaginaAlturaPx, true);
    {
        juce::Graphics g(pagina);
        g.fillAll(juce::Colour(0xfff6f2e9));
        const juce::Colour tinta(0xff151515), mudo(0xff5b574f), azul(0xff004eaa);
        g.setColour(tinta);
        g.fillRect(0, 0, kPaginaLarguraPx, 14);
        int y = 90;
        g.setColour(azul);
        g.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)));
        g.drawText("OpenTimeStamps", 90, y, kPaginaLarguraPx - 180, 36, juce::Justification::centredLeft, false);
        y += 56;
        g.setColour(tinta);
        g.setFont(juce::Font(juce::FontOptions(54.0f, juce::Font::bold)));
        g.drawFittedText(matriz::i18n::t("auto.provadata.e4952e"), 90, y, kPaginaLarguraPx - 180, 70, juce::Justification::centredLeft, 1);
        y += 110;
        g.fillRect(90, y, kPaginaLarguraPx - 180, 3);
        y += 40;

        const auto linha = [&](const juce::String& rotulo, const juce::String& valor, bool mono = false) {
            g.setColour(mudo);
            g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
            g.drawText(rotulo.toUpperCase(), 90, y, kPaginaLarguraPx - 180, 26, juce::Justification::centredLeft, false);
            y += 30;
            g.setColour(tinta);
            g.setFont(mono ? juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::plain))
                           : juce::Font(juce::FontOptions(30.0f, juce::Font::plain)));
            g.drawFittedText(valor, 90, y, kPaginaLarguraPx - 180, mono ? 70 : 80, juce::Justification::topLeft, 3);
            y += (mono ? 84 : 90);
        };
        linha(matriz::i18n::t("auto.provadata.fc3ee5"), principal->nomeOriginal);
        linha("SHA-256 " + matriz::i18n::t("auto.provadata.9d227c"), principal->sha256, true);
        linha(matriz::i18n::t("auto.provadata.e6ad18"), json.isObject() ? json["data_hora_local"].toString() : juce::String());
        linha(matriz::i18n::t("auto.provadata.ddd4d9"), textoEstado(est).isNotEmpty() ? textoEstado(est) : matriz::i18n::t("auto.provadata.2e72fc"));
        if (est.dataBloco.isNotEmpty()) linha(matriz::i18n::t("auto.provadata.22a052"), est.dataBloco);
        linha(matriz::i18n::t("auto.provadata.a988c6"), sha256Hexa(manifesto), true);

        y += 10;
        g.setColour(mudo);
        g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::plain)));
        g.drawFittedText(textoComoVerificar(), 90, y, kPaginaLarguraPx - 180, kPaginaAlturaPx - y - 120, juce::Justification::topLeft, 40);
        g.setColour(mudo);
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::plain)));
        g.drawText("opentimestamps.org  -  BKR Matriz Mini", 90, kPaginaAlturaPx - 80, kPaginaLarguraPx - 180, 28, juce::Justification::centredLeft, false);
    }
    if (!escreverPdfDePaginas({ pagina }, destinoPdf, "OpenTimeStamps - " + principal->nomeOriginal, "BKR Matriz Mini")) return falhar("light.prova.err.write");
    return true;
}

// ── Textos ───────────────────────────────────────────────────────────────────────────────────────

juce::String dataCurtaDoBloco(const juce::String& d) {
    if (d.length() < 10) return {};
    return d.substring(8, 10) + "/" + d.substring(5, 7) + "/" + d.substring(0, 4);
}

juce::String hashCurto(const juce::String& sha) { return sha.substring(0, 16); }

juce::String textoEstado(const EstadoProva& e) {
    switch (e.estado) {
        case Estado::NaFila: return matriz::i18n::t("light.prova.state.queued");
        case Estado::Pendente: return matriz::i18n::t("light.prova.state.pending");
        case Estado::Confirmada: {
            auto s = matriz::i18n::t("light.prova.state.confirmed").replace("{block}", juce::String(static_cast<juce::int64>(e.bloco)));
            const auto d = dataCurtaDoBloco(e.dataBloco);
            return s.replace("{date}", d.isNotEmpty() ? d : juce::String("—"));
        }
        default: return {};
    }
}

// ── Atualizador ───────────────────────────────────────────────────────────────────────────────────

void registrarProjetoParaAtualizacao(const juce::File& pastaProject) {
    std::lock_guard<std::mutex> lk(gMutexAtualizacao);
    for (const auto& p : gProjetos) if (p == pastaProject) return;
    gProjetos.push_back(pastaProject);
}

void registrarPastaParaAtualizacaoExtra(const juce::File& pasta) {
    auto l = pastasExtra();
    if (l.contains(pasta.getFullPathName())) return;
    l.add(pasta.getFullPathName());
    while (l.size() > 200) l.remove(0);  // só as mais recentes
    matriz::app::gravarPreferenciaTexto("prova_pastas_extra", l.joinIntoString("\n"));
}

void iniciarAtualizadorEmBackground() { relogio().iniciar(); }

void pararAtualizadorEmBackground() {
    relogio().parar();
    // Dá alguns segundos para a rodada em andamento terminar; se a rede estiver lenta, o processo encerra mesmo assim.
    for (int i = 0; i < 30 && gEmExecucao.load(); ++i) juce::Thread::sleep(100);
}

void atualizarAgora(std::function<void(bool)> aoTerminar) {
    if (gEmExecucao.exchange(true)) return;
    std::vector<juce::File> copia;
    {
        std::lock_guard<std::mutex> lk(gMutexAtualizacao);
        copia = gProjetos;
    }
    juce::Thread::launch([copia, aoTerminar] {
        bool mudou = false;
        try { mudou = rodadaDeAtualizacao(copia); } catch (...) {}
        gEmExecucao = false;
        if (aoTerminar) juce::MessageManager::callAsync([aoTerminar, mudou] { aoTerminar(mudou); });
    });
}

} // namespace matriz::provadata
