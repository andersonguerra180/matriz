#include "OrigemPadrao.h"

#include <set>

namespace matriz::ficha {

std::optional<std::string> origemPadraoParaTipo(const std::string& tipoMidia) {
    static const std::set<std::string> analogicos = {"cassete", "fita_rolo", "vinil", "dat", "minidisc"};
    static const std::set<std::string> digitais = {"sample", "release", "digital_audio", "digital_video", "3d_file"};

    if (analogicos.count(tipoMidia)) return "Analógico";
    if (digitais.count(tipoMidia)) return "Digital";
    return std::nullopt;
}

} // namespace matriz::ficha
