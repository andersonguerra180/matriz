#pragma once

#include <JuceHeader.h>

#include <cctype>
#include <cmath>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../Analytics/AssetGeolocation.h"
#include "../Db/Database.h"
#include "../I18n/Strings.h"

// Editar e apagar Favoritos de GEO LOCATION. Compartilhado pelos três menus
// "▾ Favoritos" (ficha de item único, ficha em lote e popup do INTAKE): os itens
// do topo continuam aplicando o favorito; abaixo de um separador entram os
// submenus "Editar favorito" e "Apagar favorito".
//
// Uso:  ... menu.addItem(i + 1, label) ...;  geofav::adicionarGerenciamento(menu, favs);
//       no callback:  if (geofav::tratarResultado(result, favs, db)) return;
namespace matriz::ui::geofav {

constexpr int kBaseEditar = 100000;
constexpr int kBaseApagar = 200000;

inline void adicionarGerenciamento(juce::PopupMenu& menu, const std::vector<matriz::analytics::GeoFavorito>& favs) {
    if (favs.empty()) return;
    auto rotulo = [](const matriz::analytics::GeoFavorito& f) {
        juce::String r(f.nome);
        if (f.city) r += juce::String::fromUTF8(" \xe2\x80\x93 ") + juce::String(*f.city);
        return r;
    };
    juce::PopupMenu editar, apagar;
    for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
        editar.addItem(kBaseEditar + i, rotulo(favs[static_cast<size_t>(i)]));
        apagar.addItem(kBaseApagar + i, rotulo(favs[static_cast<size_t>(i)]));
    }
    menu.addSeparator();
    menu.addSubMenu(juce::String::fromUTF8("\xe2\x9c\x8e ") + matriz::i18n::t("geofav.menu_editar"), editar);
    menu.addSubMenu(juce::String::fromUTF8("\xe2\x9c\x95 ") + matriz::i18n::t("geofav.menu_apagar"), apagar);
}

namespace detalhe {

// "lat, lon" válido (números inteiros do texto, dentro da faixa geográfica).
inline bool lerCoordenadas(const juce::String& texto, double& lat, double& lon) {
    const auto txt = texto.toStdString();
    const auto virgula = txt.find(',');
    if (virgula == std::string::npos) return false;
    auto numero = [](const std::string& s, double& out) {
        try {
            size_t usados = 0;
            out = std::stod(s, &usados);
            while (usados < s.size() && std::isspace(static_cast<unsigned char>(s[usados]))) ++usados;
            return usados == s.size() && std::isfinite(out);
        } catch (...) { return false; }
    };
    double a = 0.0, b = 0.0;
    if (!numero(txt.substr(0, virgula), a) || !numero(txt.substr(virgula + 1), b)) return false;
    if (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0) return false;
    lat = a;
    lon = b;
    return true;
}

inline std::optional<std::string> textoOuNada(const juce::String& s) {
    const auto t = s.trim();
    return t.isEmpty() ? std::nullopt : std::optional<std::string>(t.toStdString());
}

inline void abrirEdicao(matriz::analytics::GeoFavorito fav, matriz::db::Database* db) {
    auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("geofav.editar_titulo"),
                                                       matriz::i18n::t("geofav.editar_msg"),
                                                       juce::MessageBoxIconType::NoIcon);
    juce::String coords;
    if (fav.latitude && fav.longitude) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6) << *fav.latitude << ", " << *fav.longitude;
        coords = ss.str();
    }
    janela->addTextEditor("nome", juce::String(fav.nome), matriz::i18n::t("geofav.campo_nome"));
    janela->addTextEditor("coords", coords, matriz::i18n::t("geofav.campo_coords"));
    janela->addTextEditor("endereco", fav.formattedAddress ? juce::String(*fav.formattedAddress) : juce::String(),
                          matriz::i18n::t("geofav.campo_endereco"));
    janela->addTextEditor("cidade", fav.city ? juce::String(*fav.city) : juce::String(), matriz::i18n::t("geofav.campo_cidade"));
    janela->addTextEditor("estado", fav.stateProvince ? juce::String(*fav.stateProvince) : juce::String(),
                          matriz::i18n::t("geofav.campo_estado"));
    janela->addTextEditor("pais", fav.country ? juce::String(*fav.country) : juce::String(), matriz::i18n::t("geofav.campo_pais"));
    janela->addButton(matriz::i18n::t("geofav.salvar"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("dialogo.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, fav, db](int resultado) mutable {
        if (resultado != 1 || db == nullptr) return;
        const auto nome = janela->getTextEditorContents("nome").trim();
        if (nome.isEmpty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   matriz::i18n::t("geofav.editar_titulo"),
                                                   matriz::i18n::t("geofav.nome_vazio"));
            return;
        }
        fav.nome = nome.toStdString();
        const auto coordsTxt = janela->getTextEditorContents("coords").trim();
        if (coordsTxt.isEmpty()) {
            fav.latitude.reset();
            fav.longitude.reset();
        } else {
            double lat = 0.0, lon = 0.0;
            if (lerCoordenadas(coordsTxt, lat, lon)) {  // inválida: mantém a anterior
                fav.latitude = lat;
                fav.longitude = lon;
            }
        }
        fav.formattedAddress = textoOuNada(janela->getTextEditorContents("endereco"));
        fav.city = textoOuNada(janela->getTextEditorContents("cidade"));
        fav.stateProvince = textoOuNada(janela->getTextEditorContents("estado"));
        fav.country = textoOuNada(janela->getTextEditorContents("pais"));
        try {
            matriz::analytics::GeoFavoritosRepository::salvar(*db, fav);  // mesmo id: upsert
        } catch (...) {}
    }));
}

inline void confirmarApagar(const matriz::analytics::GeoFavorito& fav, matriz::db::Database* db) {
    const auto id = fav.id;
    auto* janela = new juce::AlertWindow(matriz::i18n::t("geofav.apagar_titulo"),
                                         matriz::i18n::t("geofav.apagar_msg").replace("{nome}", juce::String(fav.nome)),
                                         juce::MessageBoxIconType::QuestionIcon);
    janela->addButton(matriz::i18n::t("geofav.apagar_btn"), 1);
    janela->addButton(matriz::i18n::t("dialogo.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    janela->enterModalState(true, juce::ModalCallbackFunction::create([id, db](int resultado) {
        if (resultado != 1 || db == nullptr) return;
        try {
            matriz::analytics::GeoFavoritosRepository::remover(*db, id);  // só a lista; itens que usam o lugar não mudam
        } catch (...) {}
    }), true);
}

} // namespace detalhe

// true se `result` era do gerenciamento (editar/apagar) e já foi tratado.
inline bool tratarResultado(int result, const std::vector<matriz::analytics::GeoFavorito>& favs, matriz::db::Database& db) {
    const int nEditar = result - kBaseEditar;
    const int nApagar = result - kBaseApagar;
    if (nEditar >= 0 && nEditar < static_cast<int>(favs.size())) {
        detalhe::abrirEdicao(favs[static_cast<size_t>(nEditar)], &db);
        return true;
    }
    if (nApagar >= 0 && nApagar < static_cast<int>(favs.size())) {
        detalhe::confirmarApagar(favs[static_cast<size_t>(nApagar)], &db);
        return true;
    }
    return false;
}

} // namespace matriz::ui::geofav
