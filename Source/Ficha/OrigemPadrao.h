#pragma once

#include <optional>
#include <string>

// Default determinístico do campo "origem" (Digital/Analógico, §4.2) por
// tipo de mídia. tipo_midia só fica disponível depois do ingest — não há
// LeituraTecnicaResultado acessível no momento da classificação (um item
// pode ter múltiplos arquivos), então o sinal usado aqui é só o tipo em si,
// não EXIF de câmera nem outro metadado por arquivo. Suficiente pra cobrir
// "material vindo de captura de fita/vinil/cassete nasce analógico" — sinal
// mais fino (EXIF) fica para uma fatia futura.

namespace matriz::ficha {

// nullopt = tipo ambíguo (pode nascer digital ou analógico) — campo fica
// vazio, o humano decide.
std::optional<std::string> origemPadraoParaTipo(const std::string& tipoMidia);

} // namespace matriz::ficha
