#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
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

// SHA-256 incremental (a cópia verificada calcula o hash enquanto lê, e a
// verificação relê o temporário em blocos que podem ser cancelados).
class Sha256Parcial {
public:
    Sha256Parcial();
    ~Sha256Parcial();
    Sha256Parcial(const Sha256Parcial&) = delete;
    Sha256Parcial& operator=(const Sha256Parcial&) = delete;
    void atualizar(const void* dados, size_t n);
    std::string finalizar();  // hex minúsculo; só uma vez

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// SHA-256 do arquivo, lido em blocos de 1 MiB. `continuar` (opcional) é chamado
// entre os blocos: devolver false aborta (resultado vazio, `*cancelado` = true).
// Arquivo ilegível também devolve "".
std::string sha256DoArquivo(const juce::File& arquivo, const std::function<bool()>& continuar = {},
                            bool* cancelado = nullptr);

} // namespace matriz::ingest
