#pragma once

#include <JuceHeader.h>
#include "../Db/Database.h"
#include "AnalyticsTypes.h"
#include <vector>
#include <string>

namespace matriz::analytics {

class SavedAnalyses {
public:
    static void garantirTabela(matriz::db::Database& db);
    static bool salvarAnalise(matriz::db::Database& db, const std::string& nome, const AnalyticsQuery& query);
    static std::vector<std::pair<std::string, AnalyticsQuery>> listarAnalises(matriz::db::Database& db);
    static bool removerAnalise(matriz::db::Database& db, const std::string& nome);
    static std::vector<std::pair<std::string, AnalyticsQuery>> obterPresetsPadrao();
};

} // namespace matriz::analytics
