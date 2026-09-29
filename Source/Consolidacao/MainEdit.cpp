#include "MainEdit.h"

#include <filesystem>
#include <set>

#include "../Ingest/Checksum.h"
#include "../Model/Project.h"
#include "../Model/ProjectLog.h"
#include "Consolidacao.h"

namespace matriz::mainedit {

using matriz::db::Value;

namespace {

// ---------------------------------------------------------------- utilidades

juce::String jstr(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

std::string agora() { return matriz::model::agoraIso8601(); }

void logar(const ContextoMain& ctx, const juce::String& titulo, std::initializer_list<juce::String> detalhes) {
    try {
        juce::StringArray arr;
        for (const auto& d : detalhes) arr.add(d);
        matriz::model::ProjectLog(ctx.pastaProjeto).appendEntry("MAIN EDIT: " + titulo, arr);
    } catch (...) {}
}

// Rename atômico (mesmo volume). NUNCA usa File::moveFileTo: ele apaga o alvo
// antes de mover — num rename só de caixa (APFS insensível) apagaria o próprio
// arquivo. Recusa qualquer alvo existente; nunca sobrescreve.
bool renomearSeguro(const juce::File& de, const juce::File& para, std::string& erro) {
    if (!de.exists()) { erro = "source not found: " + de.getFullPathName().toStdString(); return false; }
    const bool soCaixa = de != para && de.getFullPathName().equalsIgnoreCase(para.getFullPathName());
    if (para.exists() && !soCaixa) { erro = "target already exists: " + para.getFullPathName().toStdString(); return false; }
    para.getParentDirectory().createDirectory();
    std::error_code ec;
    std::filesystem::rename(de.getFullPathName().toStdString(), para.getFullPathName().toStdString(), ec);
    if (ec) { erro = ec.message() + ": " + para.getFullPathName().toStdString(); return false; }
    return true;
}

bool nomeDeArquivoValido(const juce::String& n) {
    if (n.trim().isEmpty() || n == "." || n == "..") return false;
    if (n.containsAnyOf("/\\:") || n.toUTF8().sizeInBytes() > 255) return false;
    return true;
}

// Caminho relativo a Media/ -> arquivo; recusa "..", absoluto e qualquer coisa fora de Media/.
bool caminhoRelativoSeguro(const juce::String& rel) {
    if (rel.isEmpty() || rel.startsWithChar('/') || rel.contains("..")) return false;
    return true;
}

juce::var novoObjeto() { return juce::var(new juce::DynamicObject()); }
juce::String paraJson(const juce::var& v) { return juce::JSON::toString(v, true); }

template <class F>
void transacao(const ContextoMain& ctx, F&& f) {
    matriz::db::Database::Trava trava(*ctx.registro);
    ctx.registro->exec("BEGIN IMMEDIATE");
    try {
        f();
        ctx.registro->exec("COMMIT");
    } catch (...) {
        try { ctx.registro->exec("ROLLBACK"); } catch (...) {}
        throw;
    }
}

struct LinhaRegistro {
    bool achou = false;
    std::string id, itemId, pastaId, arquivoId, checksum, destinoPath, destinoId;
    juce::String caminho;  // relativo a Media/
};

LinhaRegistro lerLinha(const ContextoMain& ctx, const std::string& registroId) {
    LinhaRegistro l;
    auto st = ctx.registro->prepare(
        "SELECT id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, checksum_sha256, "
        "COALESCE(destino_path, ''), COALESCE(destino_id, '') FROM consolidacao_registro WHERE id = ?");
    st.bind(1, Value::of(registroId));
    if (!st.step()) return l;
    l.achou = true;
    l.id = st.columnText(0);
    l.itemId = st.columnText(1);
    l.pastaId = st.columnText(2);
    l.arquivoId = st.columnText(3);
    l.caminho = jstr(st.columnText(4));
    l.checksum = st.columnText(5);
    l.destinoPath = st.columnText(6);
    l.destinoId = st.columnText(7);
    return l;
}

// ---------------------------------------------------------------- journal

std::string abrirJournal(const ContextoMain& ctx, const juce::String& op, const juce::var& payload) {
    const std::string id = matriz::model::novoUuid();
    ctx.registro->run("INSERT INTO main_edit_journal (id, op, estado, payload, criado_em) VALUES (?, ?, 'pendente', ?, ?)",
                      {Value::of(id), Value::of(op.toStdString()), Value::of(paraJson(payload).toStdString()),
                       Value::of(agora())});
    return id;
}

void fecharJournal(const ContextoMain& ctx, const std::string& id, const char* estado) {
    ctx.registro->run("UPDATE main_edit_journal SET estado = ?, concluido_em = ? WHERE id = ?",
                      {Value::of(std::string(estado)), Value::of(agora()), Value::of(id)});
}

// ------------------------------------------------- completar no banco (idempotente)

void completarArquivo(const ContextoMain& ctx, const juce::var& p, const std::string& jid, bool mover) {
    const std::string registroId = p["registroId"].toString().toStdString();
    const std::string deRel = p["deRel"].toString().toStdString();
    const std::string paraRel = p["paraRel"].toString().toStdString();
    const std::string sha = p["sha"].toString().toStdString();
    transacao(ctx, [&] {
        if (mover) {
            ctx.registro->run("UPDATE consolidacao_registro SET caminho_relativo_destino = ?, pasta_id = ? WHERE id = ?",
                              {Value::of(paraRel), Value::of(p["pastaPara"].toString().toStdString()), Value::of(registroId)});
            if (ctx.mapaMoverItem)
                ctx.mapaMoverItem(p["itemId"].toString().toStdString(), p["pastaDe"].toString().toStdString(),
                                  p["pastaPara"].toString().toStdString());
        } else {
            ctx.registro->run("UPDATE consolidacao_registro SET caminho_relativo_destino = ? WHERE id = ?",
                              {Value::of(paraRel), Value::of(registroId)});
        }
        matriz::consolidacao::anotarMovePendentePraClones(*ctx.registro, "arquivo", deRel, paraRel, sha);
        if (auto* comp = p["companheiros"].getArray())
            for (auto& c : *comp)
                matriz::consolidacao::anotarMovePendentePraClones(*ctx.registro, "arquivo", c["de"].toString().toStdString(), c["para"].toString().toStdString(), "");
        fecharJournal(ctx, jid, "aplicado");
    });
    logar(ctx, mover ? "File moved" : "File renamed",
          {"Item: " + p["codigo"].toString(), "From: " + jstr(deRel), "To: " + jstr(paraRel), "SHA-256: " + jstr(sha)});
}

void completarPasta(const ContextoMain& ctx, const juce::var& p, const std::string& jid, bool renomear) {
    const std::string pastaId = p["pastaId"].toString().toStdString();
    const std::string deDir = p["deDir"].toString().toStdString();
    const std::string paraDir = p["paraDir"].toString().toStdString();
    transacao(ctx, [&] {
        // Reaponta todo caminho registrado dentro da pasta (prefixo exato, sem LIKE:
        // "_" e "%" em nomes de pasta são curingas).
        const std::string prefDe = deDir + "/";
        const std::string prefPara = paraDir + "/";
        ctx.registro->run(
            "UPDATE consolidacao_registro SET caminho_relativo_destino = ? || substr(caminho_relativo_destino, ?) "
            "WHERE substr(caminho_relativo_destino, 1, ?) = ?",
            {Value::of(prefPara), Value::of(static_cast<long long>(juce::String(prefDe).length() + 1)),
             Value::of(static_cast<long long>(juce::String(prefDe).length())), Value::of(prefDe)});
        if (renomear) {
            ctx.registro->run("UPDATE acervo_pasta SET nome = ?, atualizado_em = ? WHERE id = ?",
                              {Value::of(p["novoNome"].toString().toStdString()), Value::of(agora()), Value::of(pastaId)});
        } else {
            const std::string paiPara = p["paiPara"].toString().toStdString();
            ctx.registro->run("UPDATE acervo_pasta SET pasta_pai_id = ?, atualizado_em = ? WHERE id = ?",
                              {paiPara.empty() ? Value::null() : Value::of(paiPara), Value::of(agora()), Value::of(pastaId)});
        }
        if (static_cast<bool>(p["existia"])) matriz::consolidacao::anotarMovePendentePraClones(*ctx.registro, "pasta", deDir, paraDir, "");
        fecharJournal(ctx, jid, "aplicado");
    });
    logar(ctx, renomear ? "Folder renamed" : "Folder moved", {"From: " + jstr(deDir), "To: " + jstr(paraDir)});
}

void inserirQuarentena(const ContextoMain& ctx, const juce::var& p, const char* motivo) {
    ctx.registro->run(
        "INSERT OR IGNORE INTO quarentena_item (id, item_id, arquivo_id, pasta_id, caminho_original, caminho_quarentena, "
        "motivo, checksum_sha256, tamanho_bytes, destino_path, destino_id, criado_em) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {Value::of(p["quarentenaId"].toString().toStdString()), Value::of(p["itemId"].toString().toStdString()),
         Value::of(p["arquivoId"].toString().toStdString()), Value::of(p["pastaId"].toString().toStdString()),
         Value::of(p["deRel"].toString().toStdString()), Value::of(p["quarentenaRel"].toString().toStdString()),
         Value::of(std::string(motivo)), Value::of(p["antigoSha"].toString().toStdString()),
         Value::of(static_cast<long long>(static_cast<juce::int64>(p["antigoTam"]))),
         Value::of(p["destinoPath"].toString().toStdString()), Value::of(p["destinoId"].toString().toStdString()),
         Value::of(agora())});
}

void completarDeletar(const ContextoMain& ctx, const juce::var& p, const std::string& jid) {
    transacao(ctx, [&] {
        inserirQuarentena(ctx, p, "deletado");
        ctx.registro->run("DELETE FROM consolidacao_registro WHERE id = ?", {Value::of(p["registroId"].toString().toStdString())});
        fecharJournal(ctx, jid, "aplicado");
    });
    logar(ctx, "File moved to quarantine (deleted)",
          {"Item: " + p["codigo"].toString(), "Path: " + p["deRel"].toString(), "SHA-256: " + p["antigoSha"].toString()});
}

void completarSubstituir(const ContextoMain& ctx, const juce::var& p, const std::string& jid) {
    transacao(ctx, [&] {
        inserirQuarentena(ctx, p, "substituido");
        ctx.registro->run(
            "UPDATE consolidacao_registro SET checksum_sha256 = ?, caminho_relativo_destino = ?, consolidado_em = ? WHERE id = ?",
            {Value::of(p["novoSha"].toString().toStdString()), Value::of(p["paraRel"].toString().toStdString()),
             Value::of(agora()), Value::of(p["registroId"].toString().toStdString())});
        fecharJournal(ctx, jid, "aplicado");
    });
    logar(ctx, "File replaced (old version to quarantine)",
          {"Item: " + p["codigo"].toString(), "Path: " + p["paraRel"].toString(),
           "Old SHA-256: " + p["antigoSha"].toString(), "New SHA-256: " + p["novoSha"].toString()});
}

void completarRestaurar(const ContextoMain& ctx, const juce::var& p, const std::string& jid) {
    const std::string qid = p["quarentenaId"].toString().toStdString();
    transacao(ctx, [&] {
        if (p["motivo"].toString() == "deletado") {
            ctx.registro->run(
                "INSERT OR IGNORE INTO consolidacao_registro (id, item_id, pasta_id, arquivo_id, caminho_relativo_destino, "
                "checksum_sha256, consolidado_em, destino_path, destino_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                {Value::of(matriz::model::novoUuid()), Value::of(p["itemId"].toString().toStdString()),
                 Value::of(p["pastaId"].toString().toStdString()), Value::of(p["arquivoId"].toString().toStdString()),
                 Value::of(p["paraRel"].toString().toStdString()), Value::of(p["antigoSha"].toString().toStdString()),
                 Value::of(agora()), Value::of(p["destinoPath"].toString().toStdString()),
                 Value::of(p["destinoId"].toString().toStdString())});
        }
        ctx.registro->run("UPDATE quarentena_item SET restaurado_em = ? WHERE id = ?", {Value::of(agora()), Value::of(qid)});
        fecharJournal(ctx, jid, "aplicado");
    });
    logar(ctx, "File restored from quarantine", {"From: _QUARENTENA/" + p["quarentenaRel"].toString(), "To: " + p["paraRel"].toString()});
}

// ------------------------------------------------- estado do disco (recuperação)

enum class EstadoFs { Concluido, NaoIniciado, Conflito };

EstadoFs estadoMovimento(const juce::File& de, const juce::File& para) {
    const bool temDe = de.exists(), temPara = para.exists();
    if (!temDe && temPara) return EstadoFs::Concluido;
    if (temDe && !temPara) return EstadoFs::NaoIniciado;
    // rename só de caixa: os dois caminhos são o mesmo arquivo (FS insensível a caixa) —
    // o banco passa a refletir o nome novo, que é o que o usuário pediu.
    if (temDe && temPara && de.getFullPathName().equalsIgnoreCase(para.getFullPathName())) return EstadoFs::Concluido;
    return EstadoFs::Conflito;
}

juce::File arq(const ContextoMain& ctx, const juce::String& rel) { return ctx.media().getChildFile(rel); }
juce::File arqQuarentena(const ContextoMain& ctx, const juce::String& rel) { return ctx.quarentena().getChildFile(rel); }

// Nome único dentro da quarentena: "<uuid curto>_<nome original>".
juce::String nomeNaQuarentena(const juce::File& original) {
    return jstr(matriz::model::novoUuid()).substring(0, 8) + "_" + original.getFileName();
}

// Monta a parte comum do payload de quarentena (deletar / substituir).
juce::var payloadQuarentena(const LinhaRegistro& l, const juce::String& quarentenaRel, const juce::File& arquivoNoMain,
                            const juce::String& codigo) {
    juce::var p = novoObjeto();
    p.getDynamicObject()->setProperty("registroId", jstr(l.id));
    p.getDynamicObject()->setProperty("itemId", jstr(l.itemId));
    p.getDynamicObject()->setProperty("pastaId", jstr(l.pastaId));
    p.getDynamicObject()->setProperty("arquivoId", jstr(l.arquivoId));
    p.getDynamicObject()->setProperty("codigo", codigo);
    p.getDynamicObject()->setProperty("deRel", l.caminho);
    p.getDynamicObject()->setProperty("quarentenaId", jstr(matriz::model::novoUuid()));
    p.getDynamicObject()->setProperty("quarentenaRel", quarentenaRel);
    p.getDynamicObject()->setProperty("antigoSha", jstr(l.checksum));
    p.getDynamicObject()->setProperty("antigoTam", static_cast<juce::int64>(arquivoNoMain.getSize()));
    p.getDynamicObject()->setProperty("destinoPath", jstr(l.destinoPath));
    p.getDynamicObject()->setProperty("destinoId", jstr(l.destinoId));
    return p;
}

juce::String codigoDoItem(const ContextoMain& ctx, const std::string& itemId) {
    try {
        auto st = ctx.registro->prepare("SELECT COALESCE(codigo_acervo, '') FROM item WHERE id = ?");
        st.bind(1, Value::of(itemId));
        if (st.step()) return jstr(st.columnText(0));
    } catch (...) {}
    return {};
}

Resultado falha(const std::string& erro) {
    Resultado r;
    r.erro = erro;
    return r;
}

}  // namespace

// ============================================================ arquivos

Resultado renomearArquivo(const ContextoMain& ctx, const std::string& registroId, const juce::String& novoNomeBruto) {
    try {
        const auto l = lerLinha(ctx, registroId);
        if (!l.achou) return falha("file is not registered in the MAIN");
        if (!caminhoRelativoSeguro(l.caminho)) return falha("unsafe path in the registry");
        const juce::File atual = arq(ctx, l.caminho);
        if (!atual.existsAsFile()) return falha("file not found in the MAIN: " + l.caminho.toStdString());

        juce::String novoNome = novoNomeBruto.trim();
        if (!nomeDeArquivoValido(novoNome)) return falha("invalid file name");
        if (!novoNome.containsChar('.') && atual.getFileExtension().isNotEmpty()) novoNome += atual.getFileExtension();

        const juce::File alvo = atual.getParentDirectory().getChildFile(novoNome);
        if (alvo == atual) return falha("the name did not change");
        const bool soCaixa = alvo.getFullPathName().equalsIgnoreCase(atual.getFullPathName());
        if (alvo.exists() && !soCaixa) return falha("a file with that name already exists in this folder");

        const juce::String deRel = l.caminho;
        const juce::String paraRel = (l.caminho.contains("/") ? l.caminho.upToLastOccurrenceOf("/", true, false) : juce::String()) + novoNome;

        // Companheiros que viajam junto: sidecar XMP ("<arquivo>.ext.xmp", ver
        // MetadadoEmbutido) e capa (mesmo nome-base, extensão da capa).
        std::vector<std::pair<juce::String, juce::String>> nomesComp;  // {nome antes, nome depois}
        nomesComp.emplace_back(atual.getFileName() + ".xmp", alvo.getFileName() + ".xmp");
        try {
            auto stc = ctx.registro->prepare("SELECT caminho_relativo FROM arquivo WHERE item_id = ? AND papel = 'capa_frente' LIMIT 1");
            stc.bind(1, Value::of(l.itemId));
            if (stc.step()) {
                const auto e = juce::File(jstr(stc.columnText(0))).getFileExtension();
                if (e.isNotEmpty() && !e.equalsIgnoreCase(atual.getFileExtension()))
                    nomesComp.emplace_back(atual.getFileNameWithoutExtension() + e, alvo.getFileNameWithoutExtension() + e);
            }
        } catch (...) {}
        const juce::String dirDe = deRel.contains("/") ? deRel.upToLastOccurrenceOf("/", true, false) : juce::String();
        const juce::String dirPara = paraRel.contains("/") ? paraRel.upToLastOccurrenceOf("/", true, false) : juce::String();
        juce::Array<juce::var> companheiros;
        std::vector<std::pair<juce::File, juce::File>> movs;
        movs.emplace_back(atual, alvo);
        for (auto& [nDe, nPara] : nomesComp) {
            juce::File cDe = atual.getParentDirectory().getChildFile(nDe);
            juce::File cPara = alvo.getParentDirectory().getChildFile(nPara);
            if (cDe.existsAsFile() && !cPara.exists() && cDe != atual) {
                movs.emplace_back(cDe, cPara);
                juce::var c = novoObjeto();
                c.getDynamicObject()->setProperty("de", dirDe + nDe);
                c.getDynamicObject()->setProperty("para", dirPara + nPara);
                companheiros.add(c);
            }
        }

        juce::var p = novoObjeto();
        auto* o = p.getDynamicObject();
        o->setProperty("registroId", jstr(registroId));
        o->setProperty("itemId", jstr(l.itemId));
        o->setProperty("codigo", codigoDoItem(ctx, l.itemId));
        o->setProperty("deRel", deRel);
        o->setProperty("paraRel", paraRel);
        o->setProperty("sha", jstr(l.checksum));
        o->setProperty("companheiros", companheiros);

        const std::string jid = abrirJournal(ctx, "rename_file", p);
        std::vector<std::pair<juce::File, juce::File>> feitos;
        std::string erro;
        for (auto& [de, para] : movs) {
            if (!renomearSeguro(de, para, erro)) {
                for (auto it = feitos.rbegin(); it != feitos.rend(); ++it) { std::string e2; renomearSeguro(it->second, it->first, e2); }
                fecharJournal(ctx, jid, "cancelado");
                return falha(erro);
            }
            feitos.emplace_back(de, para);
        }
        try {
            completarArquivo(ctx, p, jid, false);
        } catch (const std::exception& e) {
            return falha(std::string("the file was renamed but the registry update failed; it will be completed when the project reopens: ") + e.what());
        }
        Resultado r;
        r.ok = true; r.idJournal = jid; r.deRel = deRel; r.paraRel = paraRel;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

Resultado moverArquivo(const ContextoMain& ctx, const std::string& registroId, const std::string& novaPastaId) {
    try {
        if (!ctx.mainUsaMapa) return falha("moving files is only allowed when the MAIN uses a folder map");
        const auto l = lerLinha(ctx, registroId);
        if (!l.achou) return falha("file is not registered in the MAIN");
        if (!caminhoRelativoSeguro(l.caminho)) return falha("unsafe path in the registry");
        const juce::File atual = arq(ctx, l.caminho);
        if (!atual.existsAsFile()) return falha("file not found in the MAIN: " + l.caminho.toStdString());

        const juce::String pastaFis = matriz::consolidacao::caminhoFisicoDaPasta(*ctx.registro, novaPastaId);
        if (pastaFis.isEmpty()) return falha("destination folder not found in the MAIN's folder map");
        const juce::String paraRel = pastaFis + "/" + atual.getFileName();
        const juce::File alvo = arq(ctx, paraRel);
        if (alvo == atual) return falha("the file is already in that folder");
        if (alvo.exists()) return falha("a file with that name already exists in the destination folder");

        juce::var p = novoObjeto();
        auto* o = p.getDynamicObject();
        o->setProperty("registroId", jstr(registroId));
        o->setProperty("itemId", jstr(l.itemId));
        o->setProperty("codigo", codigoDoItem(ctx, l.itemId));
        o->setProperty("deRel", l.caminho);
        o->setProperty("paraRel", paraRel);
        o->setProperty("sha", jstr(l.checksum));
        o->setProperty("pastaDe", jstr(l.pastaId));
        o->setProperty("pastaPara", jstr(novaPastaId));
        o->setProperty("companheiros", juce::Array<juce::var>());

        const std::string jid = abrirJournal(ctx, "move_file", p);
        std::string erro;
        if (!renomearSeguro(atual, alvo, erro)) {
            fecharJournal(ctx, jid, "cancelado");
            return falha(erro);
        }
        try {
            completarArquivo(ctx, p, jid, true);
        } catch (const std::exception& e) {
            return falha(std::string("the file was moved but the registry update failed; it will be completed when the project reopens: ") + e.what());
        }
        Resultado r;
        r.ok = true; r.idJournal = jid; r.deRel = l.caminho; r.paraRel = paraRel;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

Resultado deletarArquivo(const ContextoMain& ctx, const std::string& registroId) {
    try {
        const auto l = lerLinha(ctx, registroId);
        if (!l.achou) return falha("file is not registered in the MAIN");
        if (!caminhoRelativoSeguro(l.caminho)) return falha("unsafe path in the registry");
        const juce::File atual = arq(ctx, l.caminho);
        if (!atual.existsAsFile()) return falha("file not found in the MAIN: " + l.caminho.toStdString());

        const juce::String qRel = nomeNaQuarentena(atual);
        juce::var p = payloadQuarentena(l, qRel, atual, codigoDoItem(ctx, l.itemId));
        const std::string jid = abrirJournal(ctx, "delete_file", p);
        std::string erro;
        ctx.quarentena().createDirectory();
        if (!renomearSeguro(atual, arqQuarentena(ctx, qRel), erro)) {
            fecharJournal(ctx, jid, "cancelado");
            return falha(erro);
        }
        try {
            completarDeletar(ctx, p, jid);
        } catch (const std::exception& e) {
            return falha(std::string("the file is in quarantine but the registry update failed; it will be completed when the project reopens: ") + e.what());
        }
        Resultado r;
        r.ok = true; r.idJournal = jid; r.idQuarentena = p["quarentenaId"].toString().toStdString();
        r.deRel = l.caminho;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

Resultado substituirArquivo(const ContextoMain& ctx, const std::string& registroId, const juce::File& novoArquivo) {
    try {
        const auto l = lerLinha(ctx, registroId);
        if (!l.achou) return falha("file is not registered in the MAIN");
        if (!caminhoRelativoSeguro(l.caminho)) return falha("unsafe path in the registry");
        if (!novoArquivo.existsAsFile()) return falha("the replacement file was not found");
        const juce::File atual = arq(ctx, l.caminho);
        if (!atual.existsAsFile()) return falha("file not found in the MAIN: " + l.caminho.toStdString());
        // A nova versão nunca pode ser o próprio arquivo do MAIN nem estar dentro da quarentena.
        if (novoArquivo == atual || novoArquivo.isAChildOf(ctx.quarentena()))
            return falha("choose a replacement file from outside the MAIN's own copy");

        // Mesmo caminho e nome-base; extensão da nova versão.
        const juce::String ext = novoArquivo.getFileExtension();
        const juce::File alvo = atual.getParentDirectory().getChildFile(atual.getFileNameWithoutExtension() + ext);
        if (alvo != atual && alvo.exists()) return falha("a file with the new name already exists in this folder");
        const juce::String paraRel = (l.caminho.contains("/") ? l.caminho.upToLastOccurrenceOf("/", true, false) : juce::String())
                                     + alvo.getFileName();

        const juce::String qRel = nomeNaQuarentena(atual);
        const juce::File tmp = alvo.getSiblingFile(alvo.getFileName() + ".mtz-tmp");
        if (tmp.exists()) tmp.deleteFile();

        // Hash da nova versão calculado ANTES de mexer no MAIN: vai no journal
        // pra a recuperação saber reconhecer a cópia completa.
        const auto novoHash = matriz::ingest::calcularChecksums(novoArquivo);
        juce::var p = payloadQuarentena(l, qRel, atual, codigoDoItem(ctx, l.itemId));
        p.getDynamicObject()->setProperty("paraRel", paraRel);
        p.getDynamicObject()->setProperty("novoSha", jstr(novoHash.sha256));
        p.getDynamicObject()->setProperty("origemNova", novoArquivo.getFullPathName());
        const std::string jid = abrirJournal(ctx, "replace_file", p);

        std::string erro;
        ctx.quarentena().createDirectory();
        if (!renomearSeguro(atual, arqQuarentena(ctx, qRel), erro)) {
            fecharJournal(ctx, jid, "cancelado");
            return falha(erro);
        }
        auto desfazerQuarentena = [&] { std::string e2; renomearSeguro(arqQuarentena(ctx, qRel), atual, e2); };
        if (!novoArquivo.copyFileTo(tmp) || !tmp.existsAsFile() || tmp.getSize() != novoArquivo.getSize()) {
            tmp.deleteFile();
            desfazerQuarentena();
            fecharJournal(ctx, jid, "cancelado");
            return falha("copying the new version failed; the original was put back");
        }
        if (matriz::ingest::calcularChecksums(tmp).sha256 != novoHash.sha256) {
            tmp.deleteFile();
            desfazerQuarentena();
            fecharJournal(ctx, jid, "cancelado");
            return falha("the copied file does not match the new version's hash; the original was put back");
        }
        if (!renomearSeguro(tmp, alvo, erro)) {
            tmp.deleteFile();
            desfazerQuarentena();
            fecharJournal(ctx, jid, "cancelado");
            return falha(erro);
        }
        try {
            completarSubstituir(ctx, p, jid);
        } catch (const std::exception& e) {
            return falha(std::string("the file was replaced but the registry update failed; it will be completed when the project reopens: ") + e.what());
        }
        Resultado r;
        r.ok = true; r.idJournal = jid; r.idQuarentena = p["quarentenaId"].toString().toStdString();
        r.deRel = l.caminho; r.paraRel = paraRel;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

// ============================================================ pastas

Resultado criarPastaFisica(const ContextoMain& ctx, const std::string& pastaId) {
    try {
        if (!ctx.mainUsaMapa) return falha("folders are only editable when the MAIN uses a folder map");
        const juce::String rel = matriz::consolidacao::caminhoFisicoDaPasta(*ctx.registro, pastaId);
        if (rel.isEmpty() || !caminhoRelativoSeguro(rel)) return falha("folder not found in the MAIN's folder map");
        const juce::File dir = arq(ctx, rel);
        if (!dir.isDirectory() && !dir.createDirectory().wasOk()) return falha("could not create the folder on disk");
        logar(ctx, "Folder created", {"Path: " + rel});
        Resultado r;
        r.ok = true; r.paraRel = rel;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

namespace {

Resultado moverOuRenomearPasta(const ContextoMain& ctx, const std::string& pastaId, bool renomear,
                               const juce::String& novoNomeBruto, const std::string& novoPaiId) {
    try {
        if (!ctx.mainUsaMapa) return falha("folders are only editable when the MAIN uses a folder map");
        std::string paiAtual, nomeAtual;
        {
            auto st = ctx.registro->prepare("SELECT COALESCE(pasta_pai_id, ''), nome FROM acervo_pasta WHERE id = ?");
            st.bind(1, Value::of(pastaId));
            if (!st.step()) return falha("folder not found in the MAIN's folder map");
            paiAtual = st.columnText(0);
            nomeAtual = st.columnText(1);
        }
        const juce::String deDir = matriz::consolidacao::caminhoFisicoDaPasta(*ctx.registro, pastaId);
        if (deDir.isEmpty() || !caminhoRelativoSeguro(deDir)) return falha("unsafe folder path");

        juce::String novoNome = renomear ? novoNomeBruto.trim() : jstr(nomeAtual);
        if (!nomeDeArquivoValido(novoNome)) return falha("invalid folder name");
        const std::string paiPara = renomear ? paiAtual : novoPaiId;

        if (!renomear) {
            // Nunca dentro de si mesma nem de um descendente.
            std::string cur = paiPara;
            std::set<std::string> vistos;
            while (!cur.empty() && !vistos.count(cur)) {
                if (cur == pastaId) return falha("a folder cannot be moved inside itself");
                vistos.insert(cur);
                auto st = ctx.registro->prepare("SELECT COALESCE(pasta_pai_id, '') FROM acervo_pasta WHERE id = ?");
                st.bind(1, Value::of(cur));
                cur = st.step() ? st.columnText(0) : std::string();
            }
        }
        const juce::String prefPai = matriz::consolidacao::caminhoFisicoDaPasta(*ctx.registro, paiPara);
        const juce::String paraDir = (prefPai.isEmpty() ? juce::String() : prefPai + "/") +
                                     matriz::consolidacao::segmentoDePastaSeguro(novoNome);
        if (paraDir == deDir) return falha("nothing to change");

        // Pasta irmã com o mesmo nome no mapa (ou no disco) = colisão.
        {
            auto st = ctx.registro->prepare(
                "SELECT 1 FROM acervo_pasta WHERE id <> ? AND COALESCE(pasta_pai_id, '') = ? AND lower(nome) = lower(?) "
                "AND mapa_id = (SELECT mapa_id FROM acervo_pasta WHERE id = ?)");
            st.bind(1, Value::of(pastaId));
            st.bind(2, Value::of(paiPara));
            st.bind(3, Value::of(novoNome.toStdString()));
            st.bind(4, Value::of(pastaId));
            if (st.step()) return falha("a folder with that name already exists here");
        }
        const juce::File origem = arq(ctx, deDir);
        const juce::File destino = arq(ctx, paraDir);
        const bool existia = origem.isDirectory();
        const bool soCaixa = destino.getFullPathName().equalsIgnoreCase(origem.getFullPathName());
        if (destino.exists() && !soCaixa) return falha("a folder with that name already exists on disk");

        juce::var p = novoObjeto();
        auto* o = p.getDynamicObject();
        o->setProperty("pastaId", jstr(pastaId));
        o->setProperty("deDir", deDir);
        o->setProperty("paraDir", paraDir);
        o->setProperty("novoNome", novoNome);
        o->setProperty("paiDe", jstr(paiAtual));
        o->setProperty("paiPara", jstr(paiPara));
        o->setProperty("existia", existia);

        const std::string jid = abrirJournal(ctx, renomear ? "rename_folder" : "move_folder", p);
        if (existia) {
            std::string erro;
            if (!renomearSeguro(origem, destino, erro)) {
                fecharJournal(ctx, jid, "cancelado");
                return falha(erro);
            }
        }
        try {
            completarPasta(ctx, p, jid, renomear);
        } catch (const std::exception& e) {
            return falha(std::string("the folder was changed on disk but the registry update failed; it will be completed when the project reopens: ") + e.what());
        }
        Resultado r;
        r.ok = true; r.idJournal = jid; r.deRel = deDir; r.paraRel = paraDir;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

}  // namespace

Resultado renomearPasta(const ContextoMain& ctx, const std::string& pastaId, const juce::String& novoNome) {
    return moverOuRenomearPasta(ctx, pastaId, true, novoNome, {});
}

Resultado moverPasta(const ContextoMain& ctx, const std::string& pastaId, const std::string& novoPaiId) {
    return moverOuRenomearPasta(ctx, pastaId, false, {}, novoPaiId);
}

// ============================================================ quarentena

std::vector<ItemQuarentena> listarQuarentena(const ContextoMain& ctx) {
    std::vector<ItemQuarentena> out;
    try {
        auto st = ctx.registro->prepare(
            "SELECT id, caminho_original, motivo, criado_em, tamanho_bytes, checksum_sha256 FROM quarentena_item "
            "WHERE restaurado_em IS NULL AND esvaziado_em IS NULL ORDER BY criado_em DESC");
        while (st.step()) {
            ItemQuarentena i;
            i.id = st.columnText(0);
            i.caminhoOriginal = jstr(st.columnText(1));
            i.motivo = jstr(st.columnText(2));
            i.criadoEm = jstr(st.columnText(3));
            i.tamanhoBytes = static_cast<juce::int64>(st.columnInt(4));
            i.checksum = st.columnText(5);
            out.push_back(std::move(i));
        }
    } catch (...) {}
    return out;
}

ResumoQuarentena resumoQuarentena(const ContextoMain& ctx) {
    ResumoQuarentena r;
    try {
        auto st = ctx.registro->prepare(
            "SELECT COUNT(*), COALESCE(SUM(tamanho_bytes), 0) FROM quarentena_item WHERE restaurado_em IS NULL AND esvaziado_em IS NULL");
        if (st.step()) { r.itens = static_cast<int>(st.columnInt(0)); r.bytes = static_cast<juce::int64>(st.columnInt(1)); }
    } catch (...) {}
    return r;
}

Resultado restaurar(const ContextoMain& ctx, const std::string& quarentenaId, const juce::String& destinoAlternativoRel) {
    try {
        juce::var p = novoObjeto();
        auto* o = p.getDynamicObject();
        juce::String qRel, original;
        {
            auto st = ctx.registro->prepare(
                "SELECT item_id, arquivo_id, pasta_id, caminho_original, caminho_quarentena, motivo, checksum_sha256, "
                "destino_path, destino_id FROM quarentena_item WHERE id = ? AND restaurado_em IS NULL AND esvaziado_em IS NULL");
            st.bind(1, Value::of(quarentenaId));
            if (!st.step()) return falha("item not found in the quarantine");
            o->setProperty("itemId", jstr(st.columnText(0)));
            o->setProperty("arquivoId", jstr(st.columnText(1)));
            o->setProperty("pastaId", jstr(st.columnText(2)));
            original = jstr(st.columnText(3));
            qRel = jstr(st.columnText(4));
            o->setProperty("motivo", jstr(st.columnText(5)));
            o->setProperty("antigoSha", jstr(st.columnText(6)));
            o->setProperty("destinoPath", jstr(st.columnText(7)));
            o->setProperty("destinoId", jstr(st.columnText(8)));
        }
        const juce::File naQuarentena = arqQuarentena(ctx, qRel);
        if (!naQuarentena.existsAsFile()) return falha("the quarantined file is missing from _QUARENTENA");

        const juce::String paraRel = destinoAlternativoRel.isNotEmpty() ? destinoAlternativoRel : original;
        if (!caminhoRelativoSeguro(paraRel)) return falha("unsafe destination path");
        const juce::File alvo = arq(ctx, paraRel);
        if (alvo.exists()) {
            Resultado r;
            r.destinoOcupado = true;
            r.erro = "there is already a file at that path; choose another destination";
            return r;
        }
        o->setProperty("quarentenaId", jstr(quarentenaId));
        o->setProperty("quarentenaRel", qRel);
        o->setProperty("paraRel", paraRel);
        // Restaurar uma versão SUBSTITUÍDA vira arquivo avulso no MAIN (o registro segue
        // apontando pra versão nova). Só 'deletado' volta ao registro, no caminho restaurado.

        const std::string jid = abrirJournal(ctx, "restore_file", p);
        std::string erro;
        if (!renomearSeguro(naQuarentena, alvo, erro)) {
            fecharJournal(ctx, jid, "cancelado");
            return falha(erro);
        }
        try {
            completarRestaurar(ctx, p, jid);
        } catch (const std::exception& e) {
            return falha(std::string("the file was restored but the registry update failed; it will be completed when the project reopens: ") + e.what());
        }
        Resultado r;
        r.ok = true; r.idJournal = jid; r.paraRel = paraRel;
        return r;
    } catch (const std::exception& e) {
        return falha(e.what());
    }
}

ResultadoEsvaziar esvaziarQuarentena(const ContextoMain& ctx, const AoProgredirEdicao& aoProgredir) {
    ResultadoEsvaziar res;
    struct Linha { std::string id; juce::String rel; juce::int64 tam = 0; };
    std::vector<Linha> linhas;
    try {
        auto st = ctx.registro->prepare(
            "SELECT id, caminho_quarentena, tamanho_bytes FROM quarentena_item WHERE restaurado_em IS NULL AND esvaziado_em IS NULL");
        while (st.step()) linhas.push_back({st.columnText(0), jstr(st.columnText(1)), static_cast<juce::int64>(st.columnInt(2))});
    } catch (const std::exception& e) {
        res.falhas.push_back(e.what());
        return res;
    }
    const int total = static_cast<int>(linhas.size());
    int feito = 0;
    for (auto& l : linhas) {
        if (aoProgredir && !aoProgredir(feito, total)) { res.cancelado = true; break; }
        ++feito;
        try {
            const juce::File f = arqQuarentena(ctx, l.rel);
            if (f.exists() && !f.deleteFile()) throw std::runtime_error("could not delete " + l.rel.toStdString());
            ctx.registro->run("UPDATE quarentena_item SET esvaziado_em = ? WHERE id = ?", {Value::of(agora()), Value::of(l.id)});
            ++res.apagados;
            res.bytes += l.tam;
        } catch (const std::exception& e) {
            res.falhas.push_back(e.what());
        }
    }
    logar(ctx, "Quarantine emptied",
          {"Files deleted: " + juce::String(res.apagados), "Size: " + juce::File::descriptionOfSizeInBytes(res.bytes),
           "Failures: " + juce::String(static_cast<int>(res.falhas.size()))});
    return res;
}

// ============================================================ recuperação

ResultadoRecuperacao recuperarJournal(const ContextoMain& ctx) {
    ResultadoRecuperacao res;
    struct Pendente { std::string id, op, payload; };
    std::vector<Pendente> pend;
    try {
        auto st = ctx.registro->prepare("SELECT id, op, payload FROM main_edit_journal WHERE estado = 'pendente' ORDER BY criado_em");
        while (st.step()) pend.push_back({st.columnText(0), st.columnText(1), st.columnText(2)});
    } catch (...) { return res; }

    for (auto& j : pend) {
        try {
            const juce::var p = juce::JSON::parse(jstr(j.payload));
            EstadoFs estado = EstadoFs::Conflito;
            if (j.op == "rename_file" || j.op == "move_file") {
                estado = estadoMovimento(arq(ctx, p["deRel"].toString()), arq(ctx, p["paraRel"].toString()));
                if (estado == EstadoFs::Concluido) {
                    // companheiros: melhor esforço
                    if (auto* comp = p["companheiros"].getArray())
                        for (auto& c : *comp) {
                            std::string e2;
                            if (arq(ctx, c["de"].toString()).exists() && !arq(ctx, c["para"].toString()).exists())
                                renomearSeguro(arq(ctx, c["de"].toString()), arq(ctx, c["para"].toString()), e2);
                        }
                    completarArquivo(ctx, p, j.id, j.op == "move_file");
                }
            } else if (j.op == "rename_folder" || j.op == "move_folder") {
                if (!static_cast<bool>(p["existia"])) estado = EstadoFs::Concluido;
                else estado = estadoMovimento(arq(ctx, p["deDir"].toString()), arq(ctx, p["paraDir"].toString()));
                if (estado == EstadoFs::Concluido) completarPasta(ctx, p, j.id, j.op == "rename_folder");
            } else if (j.op == "delete_file") {
                estado = estadoMovimento(arq(ctx, p["deRel"].toString()), arqQuarentena(ctx, p["quarentenaRel"].toString()));
                // (de = MAIN, para = quarentena): Concluido quando o arquivo já saiu do MAIN e está na quarentena
                if (estado == EstadoFs::Concluido) completarDeletar(ctx, p, j.id);
            } else if (j.op == "replace_file") {
                const juce::File destinoNovo = arq(ctx, p["paraRel"].toString());
                const juce::File q = arqQuarentena(ctx, p["quarentenaRel"].toString());
                const juce::File tmp = destinoNovo.getSiblingFile(destinoNovo.getFileName() + ".mtz-tmp");
                if (destinoNovo.existsAsFile() && q.existsAsFile() &&
                    jstr(matriz::ingest::calcularChecksums(destinoNovo).sha256) == p["novoSha"].toString()) {
                    tmp.deleteFile();
                    estado = EstadoFs::Concluido;
                    completarSubstituir(ctx, p, j.id);
                } else if (q.existsAsFile() && !destinoNovo.exists()) {
                    // Copia incompleta: devolve o original ao lugar.
                    tmp.deleteFile();
                    std::string e2;
                    estado = renomearSeguro(q, arq(ctx, p["deRel"].toString()), e2) ? EstadoFs::NaoIniciado : EstadoFs::Conflito;
                } else if (!q.exists() && arq(ctx, p["deRel"].toString()).existsAsFile()) {
                    tmp.deleteFile();
                    estado = EstadoFs::NaoIniciado;
                } else {
                    estado = EstadoFs::Conflito;
                }
            } else if (j.op == "restore_file") {
                estado = estadoMovimento(arqQuarentena(ctx, p["quarentenaRel"].toString()), arq(ctx, p["paraRel"].toString()));
                if (estado == EstadoFs::Concluido) completarRestaurar(ctx, p, j.id);
            }
            if (estado == EstadoFs::Concluido) {
                ++res.retomadas;
            } else if (estado == EstadoFs::NaoIniciado) {
                fecharJournal(ctx, j.id, "cancelado");
                ++res.desfeitas;
                logar(ctx, "Interrupted operation undone (nothing had changed)", {"Operation: " + jstr(j.op)});
            } else {
                fecharJournal(ctx, j.id, "conflito");
                const std::string msg = "Interrupted MAIN operation needs attention (" + j.op + "): " + j.payload;
                res.conflitos.push_back(msg);
                logar(ctx, "Interrupted operation needs attention", {"Operation: " + jstr(j.op), jstr(j.payload)});
            }
        } catch (const std::exception& e) {
            res.conflitos.push_back("Could not recover MAIN operation " + j.op + ": " + e.what());
        }
    }
    return res;
}

}  // namespace matriz::mainedit
