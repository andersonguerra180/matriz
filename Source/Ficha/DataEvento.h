#pragma once

#include <optional>
#include <string>

// EVENT DATE da ficha em três partes (DD | MM | YYYY), com só o ano bastando.
// O ano continua em item.ano (filtros, grade, exports); dia e mês ficam em
// item_campo "evento_dia" / "evento_mes" (dois dígitos, "" = não informado).
// Nasce igual ao DATE CREATED (data do arquivo) — ver IngestArquivo.cpp e
// ProjetoAberto::preencherDataEventoPadrao.

namespace matriz::ficha {

struct DataEvento {
    std::string dia, mes, ano;  // "05", "12", "2019"; "" = vazio
    bool vazia() const { return dia.empty() && mes.empty() && ano.empty(); }
    // "2019-12-05", "2019-12", "2019" ou "" — para comparar e para exibir.
    std::string texto() const;
    bool operator==(const DataEvento& o) const { return dia == o.dia && mes == o.mes && ano == o.ano; }
    bool operator!=(const DataEvento& o) const { return !(*this == o); }
};

// Data de um texto de metadado: "2019-12-05 14:33:00", "2019:12:05 14:33:00" (EXIF), "2019-12-05T14:33:00Z",
// "2019-12" ou "2019". nullopt se não começa por um ano de 4 dígitos.
std::optional<DataEvento> dataEventoDeTexto(const std::string& texto);

// O digitado nas três caixas, validado e normalizado (dia/mês com 2 dígitos). Tudo vazio = data vazia (válida).
// Inválido (nullopt): dia/mês sem ano, dia sem mês, ano que não tem 4 dígitos, mês fora de 1..12, dia que o mês não tem.
std::optional<DataEvento> validarDataEvento(const std::string& dia, const std::string& mes, const std::string& ano);

} // namespace matriz::ficha
