#include "CompactacaoRegistro.h"

#include "../Db/Database.h"
#include "../Ingest/LeituraTecnica.h"
#include "NotasEstruturadas.h"

#include <vector>

namespace matriz::model {

using matriz::db::Value;

namespace {

std::string literalSql(const juce::File& f) {
    return "'" + f.getFullPathName().replace("'", "''").toStdString() + "'";
}

juce::int64 contar(matriz::db::Database& db, const char* tabela) {
    auto st = db.prepare(std::string("SELECT COUNT(*) FROM ") + tabela);
    return st.step() ? st.columnInt(0) : -1;
}

std::string integridade(matriz::db::Database& db) {
    auto st = db.prepare("PRAGMA integrity_check");
    return st.step() ? st.columnText(0) : std::string("sem resposta");
}

// Remove -wal/-shm/-journal órfãos de um arquivo de banco já fechado.
void apagarAuxiliares(const juce::File& banco) {
    for (const char* sufixo : {"-wal", "-shm", "-journal"})
        juce::File(banco.getFullPathName() + sufixo).deleteFile();
}

} // namespace

std::string removerOutraMetadataDasNotas(const std::string& notas, bool* mudou) {
    if (mudou) *mudou = false;
    const std::string cabecalho = std::string("[") + kOutraMetadataTitulo + "]";
    std::vector<std::string> mantidas;
    bool naSecaoAutomatica = false;
    size_t inicio = 0;
    while (true) {
        size_t fim = notas.find('\n', inicio);
        const bool ultima = fim == std::string::npos;
        if (ultima) fim = notas.size();
        const std::string linha = notas.substr(inicio, fim - inicio);
        if (!linha.empty() && linha.front() == '[' && linha.back() == ']')
            naSecaoAutomatica = linha == cabecalho;
        if (naSecaoAutomatica) {
            if (mudou) *mudou = true;
        } else {
            mantidas.push_back(linha);
        }
        if (ultima) break;
        inicio = fim + 1;
    }
    std::string out;
    for (size_t i = 0; i < mantidas.size(); ++i) {
        if (i) out += '\n';
        out += mantidas[i];
    }
    // Seções são separadas por linha em branco: sem a automática, sobraria
    // uma linha vazia na ponta.
    while (!out.empty() && out.back() == '\n') out.pop_back();
    while (!out.empty() && out.front() == '\n') out.erase(out.begin());
    // Sem a seção, sobra só espaço? Então não há nota nenhuma.
    if (juce::String(out).trim().isEmpty()) out.clear();
    return out;
}

ResultadoCompactacao compactarRegistro(const juce::File& pastaProject) {
    ResultadoCompactacao r;
    const juce::File original = pastaProject.getChildFile("registro.sqlite");
    const juce::File copia = pastaProject.getChildFile("registro.compactando.sqlite");
    const juce::File final_ = pastaProject.getChildFile("registro.compactado.sqlite");
    if (!original.existsAsFile()) {
        r.erro = "registro.sqlite not found in " + pastaProject.getFullPathName().toStdString();
        return r;
    }
    r.bytesAntes = original.getSize();
    for (const auto& f : {copia, final_}) { f.deleteFile(); apagarAuxiliares(f); }

    try {
        juce::int64 itensAntes = 0, arquivosAntes = 0;
        // 1) Cópia do original como arquivo (leitura sequencial — um VACUUM
        //    INTO direto do original lia as páginas fora de ordem e levava
        //    ~20 min num banco fragmentado). O original nem é aberto pelo
        //    SQLite. Um -wal com conteúdo significaria projeto aberto/não
        //    fechado direito: aí não mexe.
        if (juce::File(original.getFullPathName() + "-wal").getSize() > 0)
            throw std::runtime_error("registro.sqlite-wal is not empty (project open or not closed cleanly)");
        if (!original.copyFileTo(copia)) throw std::runtime_error("could not copy registro.sqlite");
        {
            // 2) Limpeza na cópia, numa transação só.
            matriz::db::Database db(copia.getFullPathName().toStdString());
            itensAntes = contar(db, "item");
            arquivosAntes = contar(db, "arquivo");
            db.exec("BEGIN IMMEDIATE");

            {
                auto st = db.prepare("SELECT COUNT(*) FROM cache_arquivo WHERE miniatura IS NOT NULL");
                if (st.step()) r.miniaturasRemovidas = static_cast<int>(st.columnInt(0));
            }
            db.exec("UPDATE cache_arquivo SET miniatura = NULL WHERE miniatura IS NOT NULL");

            // JSON técnico: o objeto bruto.exif é refeito só com as chaves
            // guardadas (json_group_object) — o resto do JSON fica igual.
            {
                std::string lista;
                for (const char* c : matriz::ingest::kChavesExifGuardadasLista()) {
                    if (!lista.empty()) lista += ", ";
                    lista += "'" + std::string(c) + "'";
                }
                auto st = db.prepare(
                    "SELECT COUNT(*) FROM arquivo WHERE json_valid(caracteristicas_tecnicas_json) "
                    "AND json_type(caracteristicas_tecnicas_json, '$.bruto.exif') = 'object'");
                if (st.step()) r.jsonsLimpos = static_cast<int>(st.columnInt(0));
                db.exec("UPDATE arquivo SET caracteristicas_tecnicas_json = json_set(caracteristicas_tecnicas_json, "
                        "'$.bruto.exif', (SELECT json_group_object(j.key, j.value) FROM "
                        "json_each(arquivo.caracteristicas_tecnicas_json, '$.bruto.exif') j WHERE j.key IN (" + lista + "))) "
                        "WHERE json_valid(caracteristicas_tecnicas_json) "
                        "AND json_type(caracteristicas_tecnicas_json, '$.bruto.exif') = 'object'");
            }

            // Notas: sai a seção automática [OTHER METADATA] inteira.
            {
                std::vector<std::pair<std::string, std::string>> novas;
                auto st = db.prepare("SELECT id, notas_livres FROM item WHERE notas_livres LIKE ?");
                st.bind(1, Value::of(std::string("%[") + kOutraMetadataTitulo + "]%"));
                while (st.step()) {
                    bool mudou = false;
                    std::string limpa = removerOutraMetadataDasNotas(st.columnText(1), &mudou);
                    if (mudou) novas.push_back({st.columnText(0), std::move(limpa)});
                }
                for (const auto& [id, texto] : novas)
                    db.run("UPDATE item SET notas_livres = ? WHERE id = ?",
                           {texto.empty() ? Value::null() : Value::of(texto), Value::of(id)});
                r.notasLimpas = static_cast<int>(novas.size());
            }

            db.exec("COMMIT");
            // 3) Arquivo final, contínuo e sem as páginas liberadas.
            db.exec("VACUUM INTO " + literalSql(final_));
        }
        copia.deleteFile();
        apagarAuxiliares(copia);

        {
            // 4) Conferência antes de trocar qualquer coisa.
            matriz::db::Database db(final_.getFullPathName().toStdString());
            const std::string integ = integridade(db);
            if (integ != "ok") throw std::runtime_error("integrity_check: " + integ);
            if (contar(db, "item") != itensAntes || contar(db, "arquivo") != arquivosAntes)
                throw std::runtime_error("item/arquivo counts differ after compaction");
            db.exec("PRAGMA journal_mode=DELETE");  // fecha sem -wal/-shm ao lado
        }
        apagarAuxiliares(final_);

        // 5) Troca: o original fica guardado ao lado, intacto.
        r.copiaOriginal = pastaProject.getChildFile(
            "registro.antes-compactacao-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".sqlite");
        if (!original.moveFileTo(r.copiaOriginal)) throw std::runtime_error("could not rename the original");
        for (const char* sufixo : {"-wal", "-shm"}) {
            juce::File aux(original.getFullPathName() + sufixo);
            if (aux.existsAsFile()) aux.moveFileTo(juce::File(r.copiaOriginal.getFullPathName() + sufixo));
        }
        if (!final_.moveFileTo(original)) {
            r.copiaOriginal.moveFileTo(original);  // desfaz: volta o original
            throw std::runtime_error("could not put the compacted file in place");
        }
        r.bytesDepois = original.getSize();
        r.ok = true;
    } catch (const std::exception& e) {
        r.erro = e.what();
        for (const auto& f : {copia, final_}) { f.deleteFile(); apagarAuxiliares(f); }
    }
    return r;
}

} // namespace matriz::model
