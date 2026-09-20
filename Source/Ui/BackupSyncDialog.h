#pragma once

#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

namespace matriz::ui {

struct BackupVersionRef {
    std::string id;
    juce::String rotulo;
    juce::String destinoPath;
    int totalItens = 0;
    juce::String ultimoBackup;
};

struct SyncDivergenceItem {
    enum class Tipo {
        Ausente,
        Divergente
    };

    Tipo tipo = Tipo::Ausente;
    juce::File arquivoFonte;
    juce::File arquivoAlvo;
    juce::String caminhoRelativoFonte;
    juce::String caminhoRelativoAlvo;
    juce::String nomeExibicao;
    std::string itemId;
    std::string pastaId;
    std::string arquivoId;
    std::string sha256Fonte;
    std::string sha256Alvo;
    bool selecionado = false; // Checkbox desmarcado por padrão
};

class BackupSyncDialog {
public:
    static void showSyncDialog(ProjetoAberto& projeto,
                               const std::vector<BackupVersionRef>& versoes,
                               std::function<void()> onConcluido);

    static void showSyncWithDestinationDialog(ProjetoAberto& projeto,
                                              const juce::File& destinoAtivo,
                                              const juce::File& segundoDestino,
                                              std::function<void()> onConcluido);
};

} // namespace matriz::ui
