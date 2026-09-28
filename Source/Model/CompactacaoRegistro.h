#pragma once

#include <JuceHeader.h>

#include <string>

namespace matriz::model {

// Compactação do registro.sqlite de um projeto já existente (o ingest novo
// já não grava nada disto):
//  - miniaturas em blob (cache_arquivo.miniatura) — a oficial é a de
//    .miniaturas/, na pasta do projeto;
//  - EXIF: no JSON técnico ficam só as chaves que a ficha usa
//    (ehChaveExifGuardada) e a seção automática [OTHER METADATA] sai das
//    notas (o EXIF inteiro continua no arquivo; "GET EXIF" traz de volta);
//  - e reescreve o arquivo inteiro contínuo (VACUUM INTO): o banco que
//    cresceu aos poucos num HD fica fragmentado e lê várias vezes mais
//    devagar que um arquivo novo.
// O original nunca é alterado: todo o trabalho é feito numa cópia, que só
// substitui o original depois de integrity_check "ok" e contagens iguais;
// o original fica ao lado como registro.antes-compactacao-<data>.sqlite.
// O projeto NÃO pode estar aberto no app.
struct ResultadoCompactacao {
    bool ok = false;
    std::string erro;
    juce::int64 bytesAntes = 0;
    juce::int64 bytesDepois = 0;
    int miniaturasRemovidas = 0;
    int jsonsLimpos = 0;
    int notasLimpas = 0;
    juce::File copiaOriginal;
};

ResultadoCompactacao compactarRegistro(const juce::File& pastaProject);

// Remove a seção automática [OTHER METADATA] (cabeçalho + conteúdo até a
// próxima seção) de um texto de notas; o resto fica byte a byte igual.
std::string removerOutraMetadataDasNotas(const std::string& notas, bool* mudou = nullptr);

} // namespace matriz::model
