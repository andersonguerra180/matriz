#pragma once

// Verificação headless da ponte UI -> motor de ingestão (MainComponent::
// ingerirArquivos): gera mídia sintética real com ffmpeg, ingere via
// MainComponent de verdade (não um atalho), e confere no banco que item +
// arquivo + checksum + leitura técnica saíram corretos. Roda via
// `MATRIZ --selftest-ingerir-arquivos` (ver Main.cpp).

namespace matriz::ui {

int rodarTestIngerirArquivos();

} // namespace matriz::ui
