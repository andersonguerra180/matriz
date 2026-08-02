#pragma once

#include <JuceHeader.h>

#include <string>

// MD5 e SHA-256 do arquivo inteiro (§7.2 estágio 1, §5.4). Sempre os dois —
// SHA-256 é o checksum de integridade principal (§12.2 fixity), MD5 fica
// como segundo checksum de compatibilidade com fluxos institucionais que
// ainda o exigem.

namespace matriz::ingest {

struct Checksums {
    std::string md5;
    std::string sha256;
};

Checksums calcularChecksums(const juce::File& arquivo);

} // namespace matriz::ingest
