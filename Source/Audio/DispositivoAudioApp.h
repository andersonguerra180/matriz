#pragma once

#include <JuceHeader.h>

namespace matriz::audio {

// O dispositivo de saída do app inteiro (preview, timeline, Preferences >
// Audio Device). Antes cada um abria o seu com a saída PADRÃO do sistema —
// e o diálogo Audio Device mexia num gerenciador descartável: o test tone
// tocava na interface escolhida, mas o som do preview ia pra saída padrão
// do macOS (ex.: HDMI da TV).
juce::AudioDeviceManager& dispositivoAudioDoApp();

// Abre uma vez (só quando alguém precisa tocar — o harness headless nunca
// abre placa de som) com a escolha salva nas preferências.
void garantirDispositivoAudioDoApp();

// Grava a escolha atual (chamado ao fechar/aplicar o diálogo Audio Device).
void gravarEscolhaDispositivoAudio();

} // namespace matriz::audio
