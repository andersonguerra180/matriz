#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "NomesCanonicos.h"

#include <map>
#include <set>

#include "Project.h"
#include "ProjectLog.h"

namespace matriz::model::nomes {

using matriz::db::Value;

namespace {

std::string aparar(const std::string& s) {
    return juce::String::fromUTF8(s.c_str()).trim().toStdString();
}

// Padrão LIKE pra pré-filtrar pela chave: ASCII vai escapado (o LIKE do
// SQLite já ignora maiúsculas em ASCII), qualquer outro caractere vira '_'
// (um caractere qualquer). A conferência de verdade é chave() em C++.
std::string padraoLike(const std::string& chaveNome) {
    juce::String out;
    for (auto c : juce::String::fromUTF8(chaveNome.c_str())) {
        if (c < 128) {
            if (c == '%' || c == '_' || c == '\\') out << '\\';
            out << juce::String::charToString(c);
        } else {
            out << '_';
        }
    }
    return out.toStdString();
}

bool tabelaExiste(matriz::db::Database& db, const char* nome) {
    try {
        auto st = db.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?");
        st.bind(1, Value::of(std::string(nome)));
        return st.step();
    } catch (...) {
        return false;
    }
}

struct Parte {
    std::string texto;  // como está (sem aparar)
    char sep = 0;       // ',' ';' ou 0 no último
};

std::vector<Parte> partirComSeparadores(const std::string& valor) {
    std::vector<Parte> partes;
    Parte atual;
    bool aspas = false;
    for (char c : valor) {  // separadores e aspas são ASCII: seguro byte a byte em UTF-8
        if (c == '"') aspas = !aspas;
        if (!aspas && (c == ',' || c == ';')) {
            atual.sep = c;
            partes.push_back(std::move(atual));
            atual = {};
            continue;
        }
        atual.texto += c;
    }
    partes.push_back(std::move(atual));
    return partes;
}

}  // namespace

std::string chave(const std::string& nome) {
    const juce::String limpo = juce::String::fromUTF8(nome.c_str()).trim();
#if JUCE_MAC
    // juce::String::toLowerCase usa towlower, que depende do locale do
    // processo — no app (locale "C") "É" continuava "É". CoreFoundation
    // conhece Unicode inteiro; NFC junta "é" digitado com "é" decomposto
    // (NFD, como o macOS guarda nomes de arquivo).
    if (!limpo.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.,&'()/")) {
        CFStringRef cf = limpo.toCFString();
        CFMutableStringRef m = CFStringCreateMutableCopy(nullptr, 0, cf);
        CFStringNormalize(m, kCFStringNormalizationFormC);
        CFStringLowercase(m, nullptr);
        const juce::String r = juce::String::fromCFString(m);
        CFRelease(m);
        CFRelease(cf);
        return r.toStdString();
    }
#elif JUCE_WINDOWS
    if (!limpo.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.,&'()/")) {
        std::wstring wstr = limpo.toWideCharPointer();
        // 1. Normalizar para NFC
        int nfcLen = NormalizeString(NormalizationC, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0);
        if (nfcLen > 0) {
            std::wstring nfcBuf(static_cast<size_t>(nfcLen), L'\0');
            NormalizeString(NormalizationC, wstr.c_str(), static_cast<int>(wstr.length()), nfcBuf.data(), nfcLen);
            // 2. Lowercase invariante de locale
            int lowerLen = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, nfcBuf.c_str(), nfcLen, nullptr, 0, nullptr, nullptr, 0);
            if (lowerLen > 0) {
                std::wstring lowerBuf(static_cast<size_t>(lowerLen), L'\0');
                LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, nfcBuf.c_str(), nfcLen, lowerBuf.data(), lowerLen, nullptr, nullptr, 0);
                return juce::String(lowerBuf.c_str()).toStdString();
            }
            return juce::String(nfcBuf.c_str()).toStdString();
        }
    }
#endif
    return limpo.toLowerCase().toStdString();
}

std::vector<std::string> dividirSubjects(const std::string& valor) {
    std::vector<std::string> out;
    for (auto& p : partirComSeparadores(valor)) {
        auto t = aparar(p.texto);
        if (!t.empty()) out.push_back(std::move(t));
    }
    return out;
}

std::string canonizarListaSubjects(const std::string& valor,
                                   const std::function<std::string(const std::string&)>& canonico) {
    auto partes = partirComSeparadores(valor);
    struct Mantido { std::string texto; char sep; };
    std::vector<Mantido> mantidos;
    std::set<std::string> vistos;
    bool mudou = false;
    for (auto& p : partes) {
        const std::string t = aparar(p.texto);
        if (t.empty()) { mudou = mudou || partes.size() > 1; continue; }
        std::string c = canonico(t);
        if (c.empty()) c = t;
        if (!vistos.insert(chave(c)).second) { mudou = true; continue; }
        if (c != t) mudou = true;
        mantidos.push_back({c, p.sep});
    }
    if (!mudou) return valor;
    std::string out;
    for (size_t i = 0; i < mantidos.size(); ++i) {
        out += mantidos[i].texto;
        if (i + 1 < mantidos.size()) {
            out += mantidos[i].sep != 0 ? mantidos[i].sep : ',';
            out += ' ';
        }
    }
    return out;
}

std::string tagCanonica(matriz::db::Database& registro, const std::string& nome) {
    const std::string limpo = aparar(nome);
    if (limpo.empty()) return limpo;
    const bool temPessoas = tabelaExiste(registro, "collection_person");
    // Atalho indexado: a grafia exata já gravada é a canônica (depois da
    // migração só existe uma grafia por nome). É o caso comum no lote — N
    // itens recebendo a mesma tag — e evita a varredura abaixo por item.
    auto existeExata = [&](const char* sql) {
        try {
            auto st = registro.prepare(sql);
            st.bind(1, Value::of(limpo));
            return st.step();
        } catch (...) {
            return false;
        }
    };
    if ((temPessoas && existeExata("SELECT 1 FROM collection_person WHERE nome = ? LIMIT 1")) ||
        existeExata("SELECT 1 FROM item_tag WHERE tag = ? LIMIT 1"))
        return limpo;
    const std::string k = chave(limpo);
    const std::string padrao = padraoLike(k);
    auto procurar = [&](const std::string& sql) -> std::string {
        try {
            auto st = registro.prepare(sql);
            st.bind(1, Value::of(padrao));
            while (st.step()) {
                std::string v = st.columnText(0);
                if (chave(v) == k) return aparar(v);
            }
        } catch (...) {}
        return {};
    };
    // Lista PEOPLE primeiro (nome escolhido de propósito), depois as tags.
    if (temPessoas)
        if (auto v = procurar("SELECT nome FROM collection_person WHERE nome LIKE ? ESCAPE '\\' ORDER BY criado_em, rowid");
            !v.empty())
            return v;
    if (auto v = procurar("SELECT tag FROM item_tag WHERE tag LIKE ? ESCAPE '\\' ORDER BY rowid"); !v.empty()) return v;
    return limpo;
}

std::string subjectCanonico(matriz::db::Database& registro, const std::string& nome) {
    const std::string limpo = aparar(nome);
    if (limpo.empty()) return limpo;
    const std::string k = chave(limpo);
    const std::string padrao = "%" + padraoLike(k) + "%";
    auto procurar = [&](const std::string& sql) -> std::string {
        try {
            auto st = registro.prepare(sql);
            st.bind(1, Value::of(padrao));
            while (st.step())
                for (auto& t : dividirSubjects(st.columnText(0)))
                    if (chave(t) == k) return t;
        } catch (...) {}
        return {};
    };
    if (auto v = procurar("SELECT dc_subject FROM item WHERE dc_subject LIKE ? ESCAPE '\\' ORDER BY rowid"); !v.empty())
        return v;
    if (auto v = procurar("SELECT valor FROM item_campo WHERE campo_id = 'dc_subject' AND valor LIKE ? ESCAPE '\\' "
                          "ORDER BY rowid");
        !v.empty())
        return v;
    return limpo;
}

std::string subjectsCanonicos(matriz::db::Database& registro, const std::string& valor) {
    return canonizarListaSubjects(valor, [&registro](const std::string& t) { return subjectCanonico(registro, t); });
}

// ---------------------------------------------------------------------------
// Vocabulário em memória
// ---------------------------------------------------------------------------

namespace {

// Grafias na ordem da primeira ocorrência (repetidas exatas colapsadas).
std::vector<std::string> grafiasEmOrdem(matriz::db::Database& db, Vocabulario::Tipo tipo) {
    std::vector<std::string> out;
    std::set<std::string> vistas;
    auto somar = [&](const std::string& v) {
        if (!v.empty() && vistas.insert(v).second) out.push_back(v);
    };
    try {
        if (tipo == Vocabulario::Tipo::Tags) {
            if (tabelaExiste(db, "collection_person")) {
                auto st = db.prepare("SELECT nome FROM collection_person ORDER BY criado_em, rowid");
                while (st.step()) somar(st.columnText(0));
            }
            auto st = db.prepare("SELECT tag, MIN(rowid) AS o FROM item_tag GROUP BY tag ORDER BY o");
            while (st.step()) somar(st.columnText(0));
        } else {
            auto st = db.prepare("SELECT dc_subject FROM item WHERE COALESCE(dc_subject, '') <> '' ORDER BY rowid");
            while (st.step())
                for (auto& t : dividirSubjects(st.columnText(0))) somar(t);
            auto st2 = db.prepare("SELECT valor FROM item_campo WHERE campo_id = 'dc_subject' "
                                  "AND COALESCE(valor, '') <> '' ORDER BY rowid");
            while (st2.step())
                for (auto& t : dividirSubjects(st2.columnText(0))) somar(t);
        }
    } catch (...) {}
    return out;
}

}  // namespace

Vocabulario Vocabulario::carregar(matriz::db::Database& registro, Tipo tipo) {
    Vocabulario v;
    for (const auto& g : grafiasEmOrdem(registro, tipo)) {
        const std::string t = aparar(g);
        if (!t.empty()) v.porChave_.emplace(chave(t), t);
    }
    return v;
}

std::string Vocabulario::canonico(const std::string& nome) {
    const std::string t = aparar(nome);
    if (t.empty()) return t;
    return porChave_.emplace(chave(t), t).first->second;
}

std::string Vocabulario::listaSubjects(const std::string& valor) {
    return canonizarListaSubjects(valor, [this](const std::string& t) { return canonico(t); });
}

// ---------------------------------------------------------------------------
// Migração
// ---------------------------------------------------------------------------

namespace {

std::vector<GrupoUnificacao> agrupar(const std::vector<std::string>& grafias, GrupoUnificacao::Tipo tipo) {
    std::map<std::string, size_t> indice;
    std::vector<std::vector<std::string>> porChave;
    for (const auto& g : grafias) {
        const std::string k = chave(g);
        if (k.empty()) continue;
        auto [it, novo] = indice.emplace(k, porChave.size());
        if (novo) porChave.emplace_back();
        porChave[it->second].push_back(g);
    }
    std::vector<GrupoUnificacao> out;
    for (auto& lista : porChave) {
        GrupoUnificacao grupo;
        grupo.tipo = tipo;
        grupo.canonico = aparar(lista.front());
        for (auto& g : lista)
            if (g != grupo.canonico) grupo.variantes.push_back(g);
        if (!grupo.variantes.empty()) out.push_back(std::move(grupo));
    }
    return out;
}

}  // namespace

std::vector<GrupoUnificacao> levantarUnificacao(matriz::db::Database& registro) {
    auto grupos = agrupar(grafiasEmOrdem(registro, Vocabulario::Tipo::Tags), GrupoUnificacao::Tipo::Tags);
    const size_t nTags = grupos.size();
    auto subjects = agrupar(grafiasEmOrdem(registro, Vocabulario::Tipo::Subjects), GrupoUnificacao::Tipo::Subjects);
    for (auto& g : subjects) grupos.push_back(std::move(g));

    // Itens por grupo.
    try {
        for (size_t i = 0; i < nTags; ++i) {
            std::set<std::string> ids;
            auto st = registro.prepare("SELECT item_id FROM item_tag WHERE tag = ?");
            for (const auto& v : grupos[i].variantes) {
                st.reset();
                st.bind(1, Value::of(v));
                while (st.step()) ids.insert(st.columnText(0));
            }
            grupos[i].itens = static_cast<int>(ids.size());
        }
        if (grupos.size() > nTags) {
            std::map<std::string, size_t> grupoDaVariante;
            for (size_t i = nTags; i < grupos.size(); ++i)
                for (const auto& v : grupos[i].variantes) grupoDaVariante[v] = i;
            std::vector<std::set<std::string>> ids(grupos.size());
            auto contar = [&](const std::string& sql) {
                auto st = registro.prepare(sql);
                while (st.step())
                    for (auto& t : dividirSubjects(st.columnText(1)))
                        if (auto it = grupoDaVariante.find(t); it != grupoDaVariante.end())
                            ids[it->second].insert(st.columnText(0));
            };
            contar("SELECT id, dc_subject FROM item WHERE COALESCE(dc_subject, '') <> ''");
            contar("SELECT item_id, valor FROM item_campo WHERE campo_id = 'dc_subject' AND COALESCE(valor, '') <> ''");
            for (size_t i = nTags; i < grupos.size(); ++i) grupos[i].itens = static_cast<int>(ids[i].size());
        }
    } catch (...) {}
    return grupos;
}

ResultadoUnificacao aplicarUnificacao(matriz::db::Database& registro, const juce::File& pastaProjeto) {
    ResultadoUnificacao r;
    const auto grupos = levantarUnificacao(registro);
    if (grupos.empty()) { r.ok = true; return r; }

    // 1. Backup do registro antes de mexer (API de backup do SQLite: cópia
    //    consistente mesmo com o banco aberto em WAL).
    r.backup = pastaProjeto.getChildFile("registro.antes-unificacao-" +
                                         juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".sqlite");
    try {
        registro.copiarSeguroPara(r.backup.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        r.erro = juce::String("Backup failed: ") + e.what();
        return r;
    }
    if (!r.backup.existsAsFile()) {
        r.erro = "Backup failed: " + r.backup.getFullPathName();
        return r;
    }

    std::map<std::string, std::string> tagParaCanonica;       // variante exata → canônica
    std::map<std::string, std::string> subjectPorChave;       // chave → canônico
    for (const auto& g : grupos) {
        if (g.tipo == GrupoUnificacao::Tipo::Tags) {
            for (const auto& v : g.variantes) tagParaCanonica[v] = g.canonico;
        } else {
            subjectPorChave[chave(g.canonico)] = g.canonico;
        }
    }
    auto subjectCanon = [&](const std::string& t) {
        auto it = subjectPorChave.find(chave(t));
        return it != subjectPorChave.end() ? it->second : t;
    };
    auto temVarianteDeSubject = [&](const std::string& valor) {
        for (auto& t : dividirSubjects(valor)) {
            auto it = subjectPorChave.find(chave(t));
            if (it != subjectPorChave.end() && it->second != t) return true;
        }
        return false;
    };

    std::set<std::string> itensAlterados;
    try {
        registro.exec("BEGIN IMMEDIATE");

        // 2. Tags: cada item com uma variante fica com UMA linha, a canônica.
        //    DELETE + INSERT (não UPDATE) pra os gatilhos manterem busca_fts/_map.
        for (const auto& [variante, canon] : tagParaCanonica) {
            std::vector<std::string> ids;
            {
                auto st = registro.prepare("SELECT item_id FROM item_tag WHERE tag = ?");
                st.bind(1, Value::of(variante));
                while (st.step()) ids.push_back(st.columnText(0));
            }
            registro.run("DELETE FROM item_tag WHERE tag = ?", {Value::of(variante)});
            for (const auto& id : ids) {
                registro.run("INSERT OR IGNORE INTO item_tag (id, item_id, tag) VALUES (?, ?, ?)",
                             {Value::of(novoUuid()), Value::of(id), Value::of(canon)});
                itensAlterados.insert(id);
            }
        }
        // Lista PEOPLE: uma linha por nome, com a grafia canônica.
        if (tabelaExiste(registro, "collection_person")) {
            for (const auto& g : grupos) {
                if (g.tipo != GrupoUnificacao::Tipo::Tags) continue;
                std::vector<std::string> grafias = g.variantes;
                grafias.push_back(g.canonico);
                std::vector<long long> rowids;
                auto st = registro.prepare("SELECT rowid FROM collection_person WHERE nome = ? ORDER BY criado_em, rowid");
                for (const auto& n : grafias) {
                    st.reset();
                    st.bind(1, Value::of(n));
                    while (st.step()) rowids.push_back(st.columnInt(0));
                }
                if (rowids.empty()) continue;
                const long long manter = *std::min_element(rowids.begin(), rowids.end());
                for (auto rid : rowids)
                    if (rid != manter) registro.run("DELETE FROM collection_person WHERE rowid = ?", {Value::of(rid)});
                registro.run("UPDATE collection_person SET nome = ? WHERE rowid = ?",
                             {Value::of(g.canonico), Value::of(manter)});
            }
        }

        // 3. Subjects: reescreve a lista de quem tem alguma variante
        //    (item.dc_subject e o espelho em item_campo).
        if (!subjectPorChave.empty()) {
            struct Linha { std::string id, itemId, valor; };
            std::vector<Linha> linhas;
            {
                auto st = registro.prepare("SELECT id, id, dc_subject FROM item WHERE COALESCE(dc_subject, '') <> ''");
                while (st.step()) linhas.push_back({st.columnText(0), st.columnText(1), st.columnText(2)});
            }
            for (const auto& l : linhas) {
                if (!temVarianteDeSubject(l.valor)) continue;
                registro.run("UPDATE item SET dc_subject = ? WHERE id = ?",
                             {Value::of(canonizarListaSubjects(l.valor, subjectCanon)), Value::of(l.id)});
                itensAlterados.insert(l.itemId);
            }
            linhas.clear();
            {
                auto st = registro.prepare("SELECT id, item_id, valor FROM item_campo WHERE campo_id = 'dc_subject' "
                                           "AND COALESCE(valor, '') <> ''");
                while (st.step()) linhas.push_back({st.columnText(0), st.columnText(1), st.columnText(2)});
            }
            for (const auto& l : linhas) {
                if (!temVarianteDeSubject(l.valor)) continue;
                registro.run("UPDATE item_campo SET valor = ? WHERE id = ?",
                             {Value::of(canonizarListaSubjects(l.valor, subjectCanon)), Value::of(l.id)});
                itensAlterados.insert(l.itemId);
            }
            // Histórico do autocomplete: sem isso ele voltaria a sugerir "show".
            if (tabelaExiste(registro, "autocomplete_historico")) {
                std::vector<std::string> valores;
                {
                    auto st = registro.prepare("SELECT valor FROM autocomplete_historico WHERE campo_id = 'dc_subject'");
                    while (st.step()) valores.push_back(st.columnText(0));
                }
                for (const auto& v : valores) {
                    if (!temVarianteDeSubject(v)) continue;
                    registro.run("UPDATE OR IGNORE autocomplete_historico SET valor = ? WHERE campo_id = 'dc_subject' "
                                 "AND valor = ?",
                                 {Value::of(canonizarListaSubjects(v, subjectCanon)), Value::of(v)});
                    registro.run("DELETE FROM autocomplete_historico WHERE campo_id = 'dc_subject' AND valor = ?",
                                 {Value::of(v)});
                }
            }
        }
        registro.exec("COMMIT");
    } catch (const std::exception& e) {
        try { registro.exec("ROLLBACK"); } catch (...) {}
        r.erro = e.what();
        return r;
    }

    r.ok = true;
    r.grupos = static_cast<int>(grupos.size());
    r.itensAlterados = static_cast<int>(itensAlterados.size());
    try {
        juce::StringArray linhas;
        linhas.add("Backup of the registry before the change: " + r.backup.getFileName());
        linhas.add("Groups unified: " + juce::String(r.grupos) + ", items changed: " + juce::String(r.itensAlterados));
        for (const auto& g : grupos) {
            juce::StringArray vs;
            for (const auto& v : g.variantes) vs.add("\"" + juce::String::fromUTF8(v.c_str()) + "\"");
            linhas.add(juce::String(g.tipo == GrupoUnificacao::Tipo::Tags ? "Tag/Person: " : "Subject: ") +
                       vs.joinIntoString(" + ") + " -> \"" + juce::String::fromUTF8(g.canonico.c_str()) + "\" (" +
                       juce::String(g.itens) + " items)");
        }
        ProjectLog(pastaProjeto).appendEntry("Names Unified (case-insensitive)", linhas);
    } catch (...) {}
    return r;
}

}  // namespace matriz::model::nomes
