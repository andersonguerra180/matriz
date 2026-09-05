#include "DeviceUsageLog.h"
#include "../Db/Database.h"
#include "../Model/Project.h"
#include "DiskIdentity.h"
#include "SmartHealth.h"
#include "Volume.h"

namespace matriz::vault {

namespace {

juce::String sanitizarNomeArquivo(const juce::String& nome) {
    juce::String s = nome.trim();
    if (s.isEmpty()) return "storage_device";
    juce::String clean;
    for (int i = 0; i < s.length(); ++i) {
        juce::juce_wchar c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ') {
            clean << c;
        } else {
            clean << '_';
        }
    }
    return clean.trim().replace(" ", "_");
}

juce::String formatBytes(juce::int64 bytes) {
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 2) + " MB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

} // namespace

void inicializarTabelaUsoDispositivo(matriz::db::Database& db) {
    try {
        db.run(
            "CREATE TABLE IF NOT EXISTS vault_uso_log ("
            "  id                   TEXT PRIMARY KEY,"
            "  vault_id             TEXT NOT NULL REFERENCES vault(id) ON DELETE CASCADE,"
            "  data_dia             TEXT NOT NULL,"
            "  acao                 TEXT NOT NULL,"
            "  operador             TEXT,"
            "  computador           TEXT,"
            "  sistema_operacional  TEXT,"
            "  saude_estado         TEXT,"
            "  smart_status         TEXT,"
            "  total_arquivos       INTEGER,"
            "  total_bytes          INTEGER,"
            "  lista_arquivos       TEXT,"
            "  detalhes             TEXT,"
            "  relatorio_md_caminho TEXT,"
            "  criado_em            TEXT NOT NULL"
            ");", {});
    } catch (...) {}
}

juce::File obterCaminhoRelatorioDispositivo(const juce::File& pastaProjeto,
                                          const std::string& nomeVault,
                                          const std::string& dataDia) {
    juce::String safeDisk = sanitizarNomeArquivo(juce::String::fromUTF8(nomeVault.c_str()));
    juce::File logDiskDir = pastaProjeto.getChildFile("log").getChildFile("disk").getChildFile(safeDisk);
    if (!logDiskDir.isDirectory()) {
        logDiskDir.createDirectory();
    }
    return logDiskDir.getChildFile(safeDisk + "_" + juce::String(dataDia) + ".txt");
}

void registrarUsoDoDispositivo(matriz::db::Database& db,
                              const juce::File& pastaProjeto,
                              const std::string& vaultId,
                              const std::string& acao,
                              int totalArquivos,
                              juce::int64 totalBytes,
                              const juce::StringArray& listaArquivos,
                              const std::string& detalhes,
                              const SmartHealthReport* healthOverride) {
    if (vaultId.empty()) return;
    inicializarTabelaUsoDispositivo(db);

    juce::Time agora = juce::Time::getCurrentTime();
    std::string dataDia = agora.formatted("%Y-%m-%d").toStdString();
    std::string criadoEm = agora.formatted("%Y-%m-%d %H:%M:%S").toStdString();
    std::string horaMinuto = agora.formatted("%H:%M:%S").toStdString();

    std::string operador = juce::SystemStats::getLogonName().toStdString();
    if (operador.empty()) operador = "Operator";
    std::string computador = juce::SystemStats::getComputerName().toStdString();
    std::string sistemaOperacional = juce::SystemStats::getOperatingSystemName().toStdString();

    // Query vault specs
    std::string nomeVault = "Storage Device";
    std::string serialVault;
    std::string modeloVault;
    std::string vendorVault;
    std::string localizacaoVault;
    std::string categoriaVault;
    std::string fsVault;
    juce::int64 capacidadeVault = 0;

    try {
        auto stmtV = db.prepare(
            "SELECT nome, numero_serie, modelo, vendor, localizacao, categoria_dispositivo, sistema_arquivos, capacidade_bytes "
            "FROM vault WHERE id = ?;");
        stmtV.bind(1, matriz::db::Value::of(vaultId));
        if (stmtV.step()) {
            nomeVault = stmtV.columnText(0);
            serialVault = stmtV.columnText(1);
            modeloVault = stmtV.columnText(2);
            vendorVault = stmtV.columnText(3);
            localizacaoVault = stmtV.columnText(4);
            categoriaVault = stmtV.columnText(5);
            fsVault = stmtV.columnText(6);
            capacidadeVault = stmtV.columnInt(7);
        }
    } catch (...) {}

    // Evaluate Drive Health
    SmartHealthReport health;
    if (healthOverride != nullptr) {
        health = *healthOverride;
    } else {
        health = obterUltimoLogOuConsultar(db, vaultId, serialVault, juce::File(localizacaoVault));
    }

    std::string saudeEstado = health.stateLabel.toStdString();
    std::string smartStatus = health.smartStatus.toStdString();

    juce::String listaJunta = listaArquivos.joinIntoString("\n");

    // 1. Resolve text report destination in project folder (.txt)
    juce::File txtReportFile = obterCaminhoRelatorioDispositivo(pastaProjeto, nomeVault, dataDia);
    std::string relatorioTxtCaminho = txtReportFile.getFullPathName().toStdString();

    // 2. Insert record into database
    std::string entryId = matriz::model::novoUuid();
    try {
        auto stmt = db.prepare(
            "INSERT INTO vault_uso_log ("
            "  id, vault_id, data_dia, acao, operador, computador, sistema_operacional, "
            "  saude_estado, smart_status, total_arquivos, total_bytes, lista_arquivos, "
            "  detalhes, relatorio_md_caminho, criado_em) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
        stmt.bind(1, matriz::db::Value::of(entryId));
        stmt.bind(2, matriz::db::Value::of(vaultId));
        stmt.bind(3, matriz::db::Value::of(dataDia));
        stmt.bind(4, matriz::db::Value::of(acao));
        stmt.bind(5, matriz::db::Value::of(operador));
        stmt.bind(6, matriz::db::Value::of(computador));
        stmt.bind(7, matriz::db::Value::of(sistemaOperacional));
        stmt.bind(8, matriz::db::Value::of(saudeEstado));
        stmt.bind(9, matriz::db::Value::of(smartStatus));
        stmt.bind(10, matriz::db::Value::of(totalArquivos));
        stmt.bind(11, matriz::db::Value::of(totalBytes));
        stmt.bind(12, matriz::db::Value::of(listaJunta.toStdString()));
        stmt.bind(13, matriz::db::Value::of(detalhes));
        stmt.bind(14, matriz::db::Value::of(relatorioTxtCaminho));
        stmt.bind(15, matriz::db::Value::of(criadoEm));
        stmt.step();
    } catch (...) {}

    // 3. Build & increment plain text report (.txt)
    juce::String safeDisk = sanitizarNomeArquivo(juce::String::fromUTF8(nomeVault.c_str()));
    juce::String txtContent;

    if (txtReportFile.existsAsFile()) {
        txtContent = txtReportFile.loadFileAsString();
    } else {
        txtContent << "================================================================================\n";
        txtContent << "BKR MATRIZ - DEVICE USAGE & HARDWARE AUDIT LOG\n";
        txtContent << "================================================================================\n";
        txtContent << "Device Name:            " << juce::String::fromUTF8(nomeVault.c_str()) << "\n";
        txtContent << "Log Date:               " << juce::String(dataDia) << "\n";
        txtContent << "Hardware Serial / UUID: " << (serialVault.empty() ? "N/A" : serialVault) << "\n";
        txtContent << "Device Vendor / Model:  " << (vendorVault.empty() ? "" : vendorVault + " ") << modeloVault << "\n";
        txtContent << "Total Capacity:         " << formatBytes(capacidadeVault) << "\n";
        txtContent << "File System:            " << (fsVault.empty() ? "N/A" : fsVault) << "\n";
        txtContent << "Drive Health:           " << saudeEstado << " (SMART Status: " << smartStatus << ")\n";
        txtContent << "================================================================================\n\n";
        txtContent << "TIMELINE SESSIONS:\n";
        txtContent << "--------------------------------------------------------------------------------\n";
    }

    // Append session entry
    txtContent << "[SESSION " << juce::String(horaMinuto) << "] - ACTION: " << juce::String(acao).toUpperCase() << "\n";
    txtContent << "  Timestamp:            " << juce::String(criadoEm) << "\n";
    txtContent << "  Operator:             " << juce::String(operador) << "\n";
    txtContent << "  Workstation:          " << juce::String(computador) << " (" << juce::String(sistemaOperacional) << ")\n";
    txtContent << "  Operation Action:     " << juce::String(acao) << "\n";
    txtContent << "  Drive Health:         " << saudeEstado << " (SMART: " << smartStatus << ")\n";

    if (totalArquivos > 0 || totalBytes > 0) {
        txtContent << "  Volume Processed:     " << juce::String(totalArquivos) << " item(s), " << formatBytes(totalBytes) << "\n";
    }

    if (!detalhes.empty()) {
        txtContent << "  Details / Note:       " << juce::String::fromUTF8(detalhes.c_str()) << "\n";
    }

    if (listaArquivos.size() > 0) {
        txtContent << "  Processed Files:\n";
        int maxListing = juce::jmin(50, listaArquivos.size());
        for (int i = 0; i < maxListing; ++i) {
            txtContent << "    * " << listaArquivos[i] << "\n";
        }
        if (listaArquivos.size() > maxListing) {
            txtContent << "    * ... and " << juce::String(listaArquivos.size() - maxListing) << " additional items\n";
        }
    }
    txtContent << "--------------------------------------------------------------------------------\n\n";

    // Write primary report to project directory
    txtReportFile.getParentDirectory().createDirectory();
    txtReportFile.replaceWithText(txtContent);

    // 4. If drive is a mounted backup destination, also mirror log directly on the backup drive (.txt)
    if (!localizacaoVault.empty()) {
        juce::File driveLoc(localizacaoVault);
        if (driveLoc.isDirectory()) {
            juce::File driveLogDir = driveLoc.getChildFile("log").getChildFile("disk").getChildFile(safeDisk);
            driveLogDir.createDirectory();
            juce::File driveReportFile = driveLogDir.getChildFile(safeDisk + "_" + juce::String(dataDia) + ".txt");
            driveReportFile.replaceWithText(txtContent);
        }
    }
}

std::vector<DeviceUsageEntry> listarHistoricoUsoDoDispositivo(matriz::db::Database& db, const std::string& vaultId) {
    std::vector<DeviceUsageEntry> resultado;
    if (vaultId.empty()) return resultado;
    inicializarTabelaUsoDispositivo(db);

    try {
        auto stmt = db.prepare(
            "SELECT id, vault_id, data_dia, acao, COALESCE(operador, ''), COALESCE(computador, ''), "
            "       COALESCE(sistema_operacional, ''), COALESCE(saude_estado, 'HEALTHY'), "
            "       COALESCE(smart_status, 'PASSED'), COALESCE(total_arquivos, 0), "
            "       COALESCE(total_bytes, 0), COALESCE(lista_arquivos, ''), COALESCE(detalhes, ''), "
            "       COALESCE(relatorio_md_caminho, ''), criado_em "
            "FROM vault_uso_log "
            "WHERE vault_id = ? "
            "ORDER BY criado_em DESC;");
        stmt.bind(1, matriz::db::Value::of(vaultId));

        while (stmt.step()) {
            DeviceUsageEntry e;
            e.id = stmt.columnText(0);
            e.vaultId = stmt.columnText(1);
            e.dataDia = stmt.columnText(2);
            e.acao = stmt.columnText(3);
            e.operador = stmt.columnText(4);
            e.computador = stmt.columnText(5);
            e.sistemaOperacional = stmt.columnText(6);
            e.saudeEstado = stmt.columnText(7);
            e.smartStatus = stmt.columnText(8);
            e.totalArquivos = static_cast<int>(stmt.columnInt(9));
            e.totalBytes = stmt.columnInt(10);
            juce::String filesRaw = stmt.columnText(11);
            if (filesRaw.isNotEmpty()) {
                e.arquivos.addLines(filesRaw);
            }
            e.detalhes = stmt.columnText(12);
            e.relatorioTxtCaminho = stmt.columnText(13);
            e.criadoEm = stmt.columnText(14);
            resultado.push_back(std::move(e));
        }
    } catch (...) {}

    return resultado;
}

} // namespace matriz::vault
