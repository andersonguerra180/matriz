#include "WhatsappConversa.h"

#include <algorithm>
#include <set>
#include <string>

namespace matriz::whatsapp {

namespace {

// ── Normalização ─────────────────────────────────────────────────────────────────────────────
// Remove os invisíveis que o WhatsApp injeta antes de anexos e horas (U+200E/200F, marcas de direção, BOM)
// e troca os espaços "especiais" (U+202F antes de AM/PM, U+00A0) por espaço comum. Trabalha em bytes UTF-8:
// tudo o que o cabeçalho usa é ASCII, e o resto da linha é reconvertido sem perda.
std::string normalizarBytes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xE2 && i + 2 < s.size()) {
            const auto b1 = static_cast<unsigned char>(s[i + 1]);
            const auto b2 = static_cast<unsigned char>(s[i + 2]);
            if (b1 == 0x80 && (b2 == 0x8E || b2 == 0x8F || (b2 >= 0xAA && b2 <= 0xAE))) { i += 3; continue; }
            if (b1 == 0x80 && b2 == 0xAF) { out += ' '; i += 3; continue; }        // U+202F
            if (b1 == 0x80 && b2 == 0x89) { out += ' '; i += 3; continue; }        // U+2009
            if (b1 == 0x81 && b2 >= 0xA6 && b2 <= 0xA9) { i += 3; continue; }     // isolados de direção
        }
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) { out += ' '; i += 2; continue; }
        if (c == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBB &&
            static_cast<unsigned char>(s[i + 2]) == 0xBF) { i += 3; continue; }  // BOM
        out += s[i++];
    }
    return out;
}

bool ehDigito(char c) { return c >= '0' && c <= '9'; }

struct Cabecalho {
    int n1 = 0, n2 = 0, n3 = 0;
    int digitosN1 = 0;
    int hora = 0, minuto = 0, segundo = 0;
    bool temAmPm = false, pm = false;
    bool iphone = false;
    size_t inicioDoResto = 0;
};

bool lerNumero(const std::string& s, size_t& i, int maxDigitos, int& v, int* digitos = nullptr) {
    int n = 0, d = 0;
    while (i < s.size() && ehDigito(s[i]) && d < maxDigitos) { n = n * 10 + (s[i] - '0'); ++i; ++d; }
    if (d == 0) return false;
    v = n;
    if (digitos != nullptr) *digitos = d;
    return true;
}

// "DD/MM/AAAA HH:MM - resto" (Android) ou "[DD/MM/AAAA, HH:MM:SS] resto" (iPhone).
bool lerCabecalho(const std::string& s, Cabecalho& h) {
    size_t i = 0;
    if (s.empty()) return false;
    if (s[0] == '[') { h.iphone = true; i = 1; }
    if (!lerNumero(s, i, 4, h.n1, &h.digitosN1)) return false;
    if (i >= s.size() || (s[i] != '/' && s[i] != '.' && s[i] != '-')) return false;
    ++i;
    if (!lerNumero(s, i, 4, h.n2)) return false;
    if (i >= s.size() || (s[i] != '/' && s[i] != '.' && s[i] != '-')) return false;
    ++i;
    if (!lerNumero(s, i, 4, h.n3)) return false;
    if (i < s.size() && s[i] == ',') ++i;
    if (i >= s.size() || s[i] != ' ') return false;
    while (i < s.size() && s[i] == ' ') ++i;
    if (!lerNumero(s, i, 2, h.hora)) return false;
    if (i >= s.size() || s[i] != ':') return false;
    ++i;
    if (!lerNumero(s, i, 2, h.minuto)) return false;
    if (i < s.size() && s[i] == ':') {
        ++i;
        if (!lerNumero(s, i, 2, h.segundo)) return false;
    }
    // AM/PM ("PM", "p.m.", "p. m.")
    {
        size_t j = i;
        while (j < s.size() && s[j] == ' ') ++j;
        if (j < s.size() && (s[j] == 'a' || s[j] == 'A' || s[j] == 'p' || s[j] == 'P')) {
            const bool ehPm = (s[j] == 'p' || s[j] == 'P');
            size_t k = j + 1;
            while (k < s.size() && (s[k] == '.' || s[k] == ' ')) ++k;
            if (k < s.size() && (s[k] == 'm' || s[k] == 'M')) {
                ++k;
                if (k < s.size() && s[k] == '.') ++k;
                h.temAmPm = true;
                h.pm = ehPm;
                i = k;
            }
        }
    }
    if (h.iphone) {
        if (i >= s.size() || s[i] != ']') return false;
        ++i;
        if (i < s.size() && s[i] == ' ') ++i;
    } else {
        if (s.compare(i, 3, " - ") == 0) i += 3;
        else if (s.compare(i, 5, " \xE2\x80\x93 ") == 0) i += 5;  // travessão
        else return false;
    }
    if (h.hora > 23 || h.minuto > 59 || h.segundo > 59) return false;
    h.inicioDoResto = i;
    return true;
}

juce::String minusculas(const juce::String& s) { return s.toLowerCase(); }

bool contemAlgum(const juce::String& baixo, std::initializer_list<const char*> chaves) {
    for (const auto* c : chaves)
        if (baixo.contains(juce::String::fromUTF8(c))) return true;
    return false;
}

struct Corpo {
    TipoMensagem tipo = TipoMensagem::Texto;
    juce::String anexo;
};

Corpo classificarCorpo(const juce::String& texto, bool temAutor) {
    Corpo c;
    const juce::String t = texto.trim();
    if (t.isEmpty()) { c.tipo = TipoMensagem::SemConteudo; return c; }
    const juce::String baixo = minusculas(t);

    // ligações perdidas (vídeo antes de voz)
    if (contemAlgum(baixo, {"missed video call", "ligação de vídeo perdida", "ligacao de video perdida", "chamada de vídeo perdida",
                            "chamada de video perdida", "videollamada perdida", "llamada de video perdida", "appel vidéo manqué",
                            "verpasster videoanruf"})) {
        c.tipo = TipoMensagem::LigacaoVideo;
        return c;
    }
    if (contemAlgum(baixo, {"missed voice call", "ligação de voz perdida", "ligacao de voz perdida", "chamada de voz perdida",
                            "llamada de voz perdida", "llamada perdida", "appel vocal manqué", "verpasster sprachanruf"})) {
        c.tipo = TipoMensagem::LigacaoVoz;
        return c;
    }

    if (t.startsWithChar('<') && t.endsWithChar('>')) {
        const juce::String dentro = t.substring(1, t.length() - 1).trim();
        const int dp = dentro.indexOf(": ");
        if (dp > 0) {  // iPhone: <anexado: ARQUIVO.ext>
            const juce::String palavra = minusculas(dentro.substring(0, dp).trim());
            if (palavra == "anexado" || palavra == "attached" || palavra == "adjunto" || palavra == "anexo" ||
                palavra == juce::String::fromUTF8("pièce jointe") || palavra == juce::String::fromUTF8("angehängt") ||
                palavra == "allegato" || palavra == "bijlage") {
                c.tipo = TipoMensagem::Anexo;
                c.anexo = dentro.substring(dp + 2).trim();
                return c;
            }
        } else {
            const juce::String d = minusculas(dentro);
            if (contemAlgum(d, {"oculta", "omitted", "omitido", "omitida", "omis", "ausgeschlossen", "ocultada", "omessa"})) {
                c.tipo = TipoMensagem::MidiaOculta;
                return c;
            }
        }
    }

    // Android: ARQUIVO.ext (arquivo anexado)
    if (t.endsWithChar(')')) {
        const int ab = t.lastIndexOf(" (");
        if (ab > 0) {
            const juce::String dentro = minusculas(t.substring(ab + 2, t.length() - 1).trim());
            if (dentro == "arquivo anexado" || dentro == "file attached" || dentro == "archivo adjunto" ||
                dentro == juce::String::fromUTF8("fichier joint") || dentro == juce::String::fromUTF8("datei angehängt") ||
                dentro == "file allegato" || dentro == "bestand toegevoegd") {
                const juce::String nome = t.substring(0, ab).trim();
                if (nome.isNotEmpty()) {
                    c.tipo = TipoMensagem::Anexo;
                    c.anexo = nome;
                    return c;
                }
            }
        }
    }

    if (contemAlgum(baixo, {"mensagem apagada", "esta mensagem foi apagada", "você apagou esta mensagem", "voce apagou esta mensagem",
                            "this message was deleted", "you deleted this message", "mensaje eliminado",
                            "eliminaste este mensaje", "ce message a été supprimé", "diese nachricht wurde gelöscht"}) &&
        t.length() < 80) {
        c.tipo = TipoMensagem::Apagada;
        return c;
    }
    juce::ignoreUnused(temAutor);
    return c;
}

// Sufixos de aviso do sistema que não aparecem na tela da conversa.
juce::String limparAvisoDoSistema(juce::String t) {
    t = t.trim();
    static const char* const sufixos[] = {"Toque para saber mais.", "Toque para saber mais", "Tap to learn more.", "Tap to learn more",
                                          "Click to learn more.", "Click to learn more", "Toca para obtener más información.",
                                          "Appuyez pour en savoir plus."};
    for (const auto* sfx : sufixos) {
        const auto s = juce::String::fromUTF8(sfx);
        if (t.endsWithIgnoreCase(s)) {
            t = t.dropLastCharacters(s.length()).trim();
            break;
        }
    }
    return t;
}

int cmpAutor(const juce::String& a, const juce::String& b) { return a.compareIgnoreCase(b); }

} // namespace

// ── Mensagem ─────────────────────────────────────────────────────────────────────────────────

juce::String Mensagem::dataTexto() const { return juce::String::formatted("%04d-%02d-%02d", ano, mes, dia); }

juce::String Mensagem::dataHoraTexto() const {
    return juce::String::formatted("%04d-%02d-%02d %02d:%02d:%02d", ano, mes, dia, hora, minuto, segundo);
}

long long Mensagem::chaveMinuto() const {
    return (static_cast<long long>(ano) * 10000 + mes * 100 + dia) * 10000 + hora * 100 + minuto;
}

long long Mensagem::chaveDia() const { return static_cast<long long>(ano) * 10000 + mes * 100 + dia; }

// ── Leitura ──────────────────────────────────────────────────────────────────────────────────

namespace {

struct LinhaBruta {
    bool ehCabecalho = false;
    Cabecalho cab;
    std::string conteudo;  // resto (cabeçalho) ou a linha inteira (continuação)
};

std::vector<LinhaBruta> separarLinhas(const std::string& normalizado) {
    std::vector<LinhaBruta> linhas;
    size_t ini = 0;
    const size_t n = normalizado.size();
    while (ini <= n) {
        size_t fim = normalizado.find('\n', ini);
        if (fim == std::string::npos) fim = n;
        std::string l = normalizado.substr(ini, fim - ini);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        LinhaBruta lb;
        Cabecalho h;
        if (lerCabecalho(l, h)) {
            lb.ehCabecalho = true;
            lb.cab = h;
            lb.conteudo = l.substr(h.inicioDoResto);
        } else {
            lb.conteudo = std::move(l);
        }
        linhas.push_back(std::move(lb));
        if (fim >= n) break;
        ini = fim + 1;
    }
    return linhas;
}

} // namespace

ConversaParseada lerConversa(const juce::String& textoCompleto) {
    ConversaParseada r;
    const auto linhas = separarLinhas(normalizarBytes(textoCompleto.toStdString()));

    // 1) ordem da data: o que for inequívoco (um número > 12) decide; senão AM/PM sugere M/D (inglês).
    int evidDiaPrimeiro = 0, evidMesPrimeiro = 0;
    bool algumAmPm = false, algumIphone = false;
    for (const auto& l : linhas) {
        if (!l.ehCabecalho) continue;
        ++r.cabecalhosReconhecidos;
        algumAmPm = algumAmPm || l.cab.temAmPm;
        algumIphone = algumIphone || l.cab.iphone;
        if (l.cab.digitosN1 >= 4) continue;  // AAAA-MM-DD
        if (l.cab.n1 > 12 && l.cab.n2 <= 12) ++evidDiaPrimeiro;
        if (l.cab.n2 > 12 && l.cab.n1 <= 12) ++evidMesPrimeiro;
    }
    r.formatoIphone = algumIphone;
    if (evidDiaPrimeiro > 0 || evidMesPrimeiro > 0) r.diaPrimeiro = evidDiaPrimeiro >= evidMesPrimeiro;
    else r.diaPrimeiro = !algumAmPm;

    // 2) monta as mensagens, na ordem do arquivo
    std::set<std::string> vistos;
    bool temMensagemAberta = false;
    for (const auto& l : linhas) {
        if (l.ehCabecalho) {
            Mensagem m;
            int d, mo, y;
            if (l.cab.digitosN1 >= 4) { y = l.cab.n1; mo = l.cab.n2; d = l.cab.n3; }
            else if (r.diaPrimeiro) { d = l.cab.n1; mo = l.cab.n2; y = l.cab.n3; }
            else { mo = l.cab.n1; d = l.cab.n2; y = l.cab.n3; }
            if (y < 100) y += 2000;
            int h = l.cab.hora;
            if (l.cab.temAmPm) {
                if (l.cab.pm && h < 12) h += 12;
                if (!l.cab.pm && h == 12) h = 0;
            }
            m.ano = y; m.mes = mo; m.dia = d; m.hora = h; m.minuto = l.cab.minuto; m.segundo = l.cab.segundo;

            // "Autor: texto" — o autor vai até o primeiro ": " (sem aspas, razoavelmente curto).
            const juce::String resto = juce::String::fromUTF8(l.conteudo.c_str());
            juce::String autor, corpo;
            bool temAutor = false;
            const int p = resto.indexOf(": ");
            const auto autorValido = [](const juce::String& a) {
                return a.isNotEmpty() && a.length() <= 64 && !a.containsAnyOf(juce::String::fromUTF8("\"“”"));
            };
            if (p > 0 && autorValido(resto.substring(0, p).trim())) {
                autor = resto.substring(0, p).trim();
                corpo = resto.substring(p + 2);
                temAutor = true;
            } else if (resto.endsWithChar(':') && autorValido(resto.dropLastCharacters(1).trim())) {
                autor = resto.dropLastCharacters(1).trim();  // autor sem texto
                temAutor = true;
            }
            if (temAutor) {
                m.autor = autor;
                const Corpo c = classificarCorpo(corpo, true);
                m.tipo = c.tipo;
                m.anexo = c.anexo;
                m.texto = (c.tipo == TipoMensagem::Texto) ? corpo.trim() : (c.tipo == TipoMensagem::SemConteudo ? juce::String() : corpo.trim());
                if (vistos.insert(autor.toLowerCase().toStdString()).second) r.autores.add(autor);
            } else {
                m.tipo = TipoMensagem::Sistema;
                const Corpo c = classificarCorpo(resto, false);
                if (c.tipo == TipoMensagem::LigacaoVoz || c.tipo == TipoMensagem::LigacaoVideo) m.tipo = c.tipo;
                m.texto = limparAvisoDoSistema(resto);
            }
            const auto cd = m.chaveDia();
            if (r.primeiraChaveDia == 0 || cd < r.primeiraChaveDia) r.primeiraChaveDia = cd;
            if (cd > r.ultimaChaveDia) r.ultimaChaveDia = cd;
            r.mensagens.push_back(std::move(m));
            temMensagemAberta = true;
        } else if (temMensagemAberta) {
            // continuação da mensagem anterior (mensagem de várias linhas, legenda de mídia, linha não reconhecida)
            auto& m = r.mensagens.back();
            const juce::String linha = juce::String::fromUTF8(l.conteudo.c_str());
            switch (m.tipo) {
                case TipoMensagem::Anexo:
                case TipoMensagem::MidiaOculta:
                    m.legenda = m.legenda.isEmpty() ? linha : m.legenda + "\n" + linha;
                    break;
                case TipoMensagem::SemConteudo:
                    if (linha.trim().isNotEmpty()) { m.tipo = TipoMensagem::Texto; m.texto = linha; }
                    break;
                case TipoMensagem::Sistema:
                    m.texto = limparAvisoDoSistema(m.texto + "\n" + linha);
                    break;
                default:
                    m.texto = m.texto.isEmpty() ? linha : m.texto + "\n" + linha;
                    break;
            }
        }
        // linhas antes da primeira mensagem são ignoradas
    }
    // idioma: palavras dos avisos e anexos (PT x EN); grupo = 3+ participantes
    {
        int pt = 0, en = 0;
        for (const auto& m : r.mensagens) {
            const juce::String t = (m.texto + " " + m.anexo).toLowerCase();
            if (m.tipo == TipoMensagem::MidiaOculta || m.tipo == TipoMensagem::Anexo || m.tipo == TipoMensagem::Apagada || m.tipo == TipoMensagem::Sistema) {
                if (contemAlgum(t, {"anexado", "mídia", "midia", "apagada", "ligação", "criptografia", "adicionou", "saiu", "criou"})) ++pt;
                if (contemAlgum(t, {"attached", "media omitted", "deleted", "missed", "encryption", "added", "left", "created"})) ++en;
            }
        }
        r.idioma = (en > pt || (pt == en && r.diaPrimeiro == false)) ? "en" : "pt";
        r.grupo = r.autores.size() >= 3;
    }
    // legenda: tira linhas vazias das pontas
    for (auto& m : r.mensagens) {
        if (m.tipo == TipoMensagem::Anexo || m.tipo == TipoMensagem::MidiaOculta) m.legenda = m.legenda.trim();
        if (m.tipo == TipoMensagem::Texto) m.texto = m.texto.trim();
    }
    juce::ignoreUnused(cmpAutor);
    return r;
}

bool primeirasLinhasSaoDeWhatsapp(const juce::String& inicio) {
    const auto linhas = separarLinhas(normalizarBytes(inicio.toStdString()));
    int uteis = 0, cabecalhos = 0;
    bool primeiraEhCabecalho = false;
    for (const auto& l : linhas) {
        if (l.conteudo.empty() && !l.ehCabecalho) continue;
        if (uteis == 0) primeiraEhCabecalho = l.ehCabecalho;
        ++uteis;
        if (l.ehCabecalho) ++cabecalhos;
        if (uteis >= 15) break;
    }
    return uteis > 0 && primeiraEhCabecalho && cabecalhos * 10 >= uteis * 4;
}

bool nomeDeMidiaDoWhatsapp(const juce::String& nome) {
    const juce::String n = nome.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false).toUpperCase();
    for (const char* p : {"IMG-", "VID-", "AUD-", "PTT-", "DOC-", "STK-"})
        if (n.startsWith(p) && n.contains("-WA")) return true;
    return false;
}

juce::String subpastaDeMes(const juce::String& nome) {
    const std::string s = nome.fromLastOccurrenceOf("/", false, false).toStdString();
    const auto valido = [](int a, int m, int d) { return a >= 1990 && a <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31; };
    // AAAAMMDD (8 dígitos seguidos, não colados a outros dígitos)
    for (size_t i = 0; i + 8 <= s.size(); ++i) {
        bool ok = true;
        for (size_t k = 0; k < 8; ++k) ok = ok && ehDigito(s[i + k]);
        if (!ok) continue;
        if (i > 0 && ehDigito(s[i - 1])) continue;
        if (i + 8 < s.size() && ehDigito(s[i + 8])) continue;
        const int a = std::stoi(s.substr(i, 4)), m = std::stoi(s.substr(i + 4, 2)), d = std::stoi(s.substr(i + 6, 2));
        if (valido(a, m, d)) return juce::String::formatted("%04d-%02d", a, m);
    }
    // AAAA-MM-DD
    for (size_t i = 0; i + 10 <= s.size(); ++i) {
        if (!(ehDigito(s[i]) && ehDigito(s[i + 1]) && ehDigito(s[i + 2]) && ehDigito(s[i + 3]) && s[i + 4] == '-' && ehDigito(s[i + 5]) &&
              ehDigito(s[i + 6]) && s[i + 7] == '-' && ehDigito(s[i + 8]) && ehDigito(s[i + 9])))
            continue;
        const int a = std::stoi(s.substr(i, 4)), m = std::stoi(s.substr(i + 5, 2)), d = std::stoi(s.substr(i + 8, 2));
        if (valido(a, m, d)) return juce::String::formatted("%04d-%02d", a, m);
    }
    return "sem-data";
}

// ── Nome do arquivo ──────────────────────────────────────────────────────────────────────────

namespace {

const char* const kPrefixosDeNome[] = {
    "conversa do whatsapp com ", "conversa do whatsapp - ", "whatsapp chat with ", "whatsapp chat - ", "whatsapp chat ",
    "chat de whatsapp con ", "chat de whatsapp - ", "conversación de whatsapp con ", "conversation whatsapp avec ",
    "discussion whatsapp avec ", "whatsapp-chat mit ", "whatsapp chat mit ", "chat whatsapp con ", "whatsapp-chat met ",
    "conversa de whatsapp com ", "chat do whatsapp com "};

} // namespace

bool nomeDeArquivoEhDeExportacao(const juce::String& nomeDoTxt) {
    juce::String nome = nomeDoTxt.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false).trim();
    nome = juce::String::fromUTF8(normalizarBytes(nome.toStdString()).c_str());
    if (!nome.endsWithIgnoreCase(".txt")) return false;
    if (nome.startsWith(".")) return false;
    const juce::String baixo = nome.toLowerCase();
    if (baixo == "_chat.txt") return true;
    for (const auto* p : kPrefixosDeNome)
        if (baixo.startsWith(juce::String::fromUTF8(p))) return true;
    // outros idiomas: "...whatsapp..." no começo do nome
    return baixo.startsWith("whatsapp") || (baixo.contains("whatsapp") && baixo.length() < 90 && baixo.indexOf("whatsapp") <= 24);
}

juce::String nomeDoContatoNoArquivo(const juce::String& nomeDoTxt) {
    juce::String nome = nomeDoTxt.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false).trim();
    nome = juce::String::fromUTF8(normalizarBytes(nome.toStdString()).c_str());
    if (nome.endsWithIgnoreCase(".txt")) nome = nome.dropLastCharacters(4);
    const juce::String baixo = nome.toLowerCase();
    for (const auto* p : kPrefixosDeNome) {
        const auto s = juce::String::fromUTF8(p);
        if (baixo.startsWith(s)) return nome.substring(s.length()).trim();
    }
    return {};
}

// ── Datas ────────────────────────────────────────────────────────────────────────────────────

juce::String nomeDoDia(int ano, int mes, int dia, bool pt) {
    static const char* const mesesPt[] = {"janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho",
                                          "agosto", "setembro", "outubro", "novembro", "dezembro"};
    static const char* const mesesEn[] = {"January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December"};
    if (mes < 1 || mes > 12) return juce::String::formatted("%04d-%02d-%02d", ano, mes, dia);
    if (pt) return juce::String(dia) + " de " + juce::String::fromUTF8(mesesPt[mes - 1]) + " de " + juce::String(ano);
    return juce::String(mesesEn[mes - 1]) + " " + juce::String(dia) + ", " + juce::String(ano);
}

juce::String dataCurta(long long chaveDia) {
    if (chaveDia <= 0) return {};
    const int ano = static_cast<int>(chaveDia / 10000), mes = static_cast<int>((chaveDia / 100) % 100), dia = static_cast<int>(chaveDia % 100);
    return juce::String::formatted("%02d/%02d/%04d", dia, mes, ano);
}

// ── Anexos ───────────────────────────────────────────────────────────────────────────────────

ClasseAnexo classeDoAnexo(const juce::String& nome) {
    const juce::String ext = nome.fromLastOccurrenceOf(".", false, false).toLowerCase();
    static const std::set<std::string> fotos = {"jpg", "jpeg", "png", "gif", "heic", "heif", "bmp", "tif", "tiff"};
    static const std::set<std::string> videos = {"mp4", "mov", "3gp", "mkv", "avi", "m4v", "webm", "mpg", "mpeg"};
    static const std::set<std::string> audios = {"opus", "ogg", "oga", "m4a", "aac", "amr", "mp3", "wav", "flac", "wma", "caf"};
    const std::string e = ext.toStdString();
    if (e == "webp") return ClasseAnexo::Figurinha;
    if (fotos.count(e)) return ClasseAnexo::Foto;
    if (videos.count(e)) return ClasseAnexo::Video;
    if (audios.count(e)) return ClasseAnexo::Audio;
    return ClasseAnexo::Documento;
}

bool ehAudioParaConverter(const juce::String& nome) {
    const juce::String ext = nome.fromLastOccurrenceOf(".", false, false).toLowerCase();
    // .ogg/.oga já tocam nos 3 sistemas: não são reconvertidos
    return ext == "opus" || ext == "m4a" || ext == "aac" || ext == "amr";
}

juce::String chaveDeNome(const juce::String& nome) {
    return juce::String::fromUTF8(normalizarBytes(nome.toStdString()).c_str()).trim().toLowerCase();
}

// ── Marcação *negrito* _itálico_ <b> ──────────────────────────────────────────────────────────

juce::String removerMarcacao(const juce::String& texto, std::vector<TrechoFormatado>& trechos) {
    trechos.clear();
    // trabalha em pontos de código
    std::u32string in;
    for (auto p = texto.getCharPointer(); !p.isEmpty();) in.push_back(static_cast<char32_t>(p.getAndAdvance()));
    std::u32string out;

    const auto ehLimite = [](char32_t c) { return c == U' ' || c == U'\n' || c == U'\t' || c == U'.' || c == U',' || c == U'!' || c == U'?' || c == U':' || c == U';' || c == U'(' || c == U')'; };
    const auto casaTag = [&](size_t i, const char32_t* tag) {
        size_t k = 0;
        while (tag[k] != 0) {
            if (i + k >= in.size() || (in[i + k] | 0x20) != tag[k]) return false;
            ++k;
        }
        return true;
    };

    for (size_t i = 0; i < in.size();) {
        // <b>…</b> e <i>…</i>
        if (in[i] == U'<' && (casaTag(i, U"<b>") || casaTag(i, U"<i>"))) {
            const bool neg = (in[i + 1] | 0x20) == U'b';
            const char32_t* fecha = neg ? U"</b>" : U"</i>";
            size_t j = i + 3;
            bool achou = false;
            for (; j + 3 < in.size() + 0 && j < in.size(); ++j)
                if (casaTag(j, fecha)) { achou = true; break; }
            if (achou) {
                TrechoFormatado t;
                t.inicio = static_cast<int>(out.size());
                out.append(in, i + 3, j - (i + 3));
                t.fim = static_cast<int>(out.size());
                (neg ? t.negrito : t.italico) = true;
                trechos.push_back(t);
                i = j + 4;
                continue;
            }
        }
        if ((in[i] == U'*' || in[i] == U'_') && (i == 0 || ehLimite(in[i - 1])) && i + 1 < in.size() && in[i + 1] != U' ' && in[i + 1] != in[i]) {
            const char32_t m = in[i];
            size_t j = i + 1;
            bool achou = false;
            for (; j < in.size() && in[j] != U'\n'; ++j) {
                if (in[j] == m && in[j - 1] != U' ' && (j + 1 == in.size() || ehLimite(in[j + 1]))) { achou = true; break; }
            }
            if (achou && j > i + 1) {
                TrechoFormatado t;
                t.inicio = static_cast<int>(out.size());
                out.append(in, i + 1, j - (i + 1));
                t.fim = static_cast<int>(out.size());
                (m == U'*' ? t.negrito : t.italico) = true;
                trechos.push_back(t);
                i = j + 1;
                continue;
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    juce::String r;
    for (auto c : out) r += juce::String::charToString(static_cast<juce::juce_wchar>(c));
    return r;
}

} // namespace matriz::whatsapp
