#include "DataEvento.h"

#include <cctype>

namespace matriz::ficha {

namespace {

std::string aparar(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool soDigitos(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

int diasDoMes(int mes, int ano) {
    static const int dias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mes == 2 && ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0)) return 29;
    return dias[mes - 1];
}

std::string doisDigitos(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

}  // namespace

std::string DataEvento::texto() const {
    if (ano.empty()) return {};
    std::string t = ano;
    if (!mes.empty()) {
        t += "-" + mes;
        if (!dia.empty()) t += "-" + dia;
    }
    return t;
}

std::optional<DataEvento> dataEventoDeTexto(const std::string& textoBruto) {
    const std::string t = aparar(textoBruto);
    if (t.size() < 4 || !soDigitos(t.substr(0, 4))) return std::nullopt;
    const std::string ano = t.substr(0, 4);
    auto separador = [](char c) { return c == '-' || c == ':' || c == '/' || c == '.'; };
    std::string mes, dia;
    if (t.size() >= 7 && separador(t[4]) && soDigitos(t.substr(5, 2))) {
        mes = t.substr(5, 2);
        if (t.size() >= 10 && separador(t[7]) && soDigitos(t.substr(8, 2))) dia = t.substr(8, 2);
    }
    if (auto valida = validarDataEvento(dia, mes, ano)) return valida;
    // Mês/dia que não existem (ex.: "2019:00:00" de câmera sem relógio): fica só o ano.
    return validarDataEvento({}, {}, ano);
}

std::optional<DataEvento> validarDataEvento(const std::string& diaBruto, const std::string& mesBruto, const std::string& anoBruto) {
    const std::string dia = aparar(diaBruto), mes = aparar(mesBruto), ano = aparar(anoBruto);
    DataEvento d;
    if (dia.empty() && mes.empty() && ano.empty()) return d;
    if (ano.size() != 4 || !soDigitos(ano)) return std::nullopt;
    const int a = std::stoi(ano);
    if (a < 1) return std::nullopt;
    d.ano = ano;
    if (mes.empty()) {
        if (!dia.empty()) return std::nullopt;
        return d;
    }
    if (mes.size() > 2 || !soDigitos(mes)) return std::nullopt;
    const int m = std::stoi(mes);
    if (m < 1 || m > 12) return std::nullopt;
    d.mes = doisDigitos(m);
    if (dia.empty()) return d;
    if (dia.size() > 2 || !soDigitos(dia)) return std::nullopt;
    const int di = std::stoi(dia);
    if (di < 1 || di > diasDoMes(m, a)) return std::nullopt;
    d.dia = doisDigitos(di);
    return d;
}

} // namespace matriz::ficha
