#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

// Janela do navegador de arquivos estilo Finder (item 3). Junta em volta do
// NavegadorArquivosComponent tudo que o item 3.2 pede: barra lateral com
// volumes/favoritos/recentes, caminho navegável clicável, voltar/avançar/
// subir, busca, filtro por tipo, ordenação, miniatura/prévia do selecionado,
// contagem e tamanho da seleção, e o botão ADD TO BACKUP — que fecha a
// janela ao ser clicado (item 3.1).
//
// A janela não modifica nada em disco (item 3.4): devolve os arquivos
// escolhidos, e quem chamou decide o que fazer (o ingest referencia, a
// cópia acontece no backup).

namespace matriz::ui {

// `manterEstrutura` reflete a escolha visível ao lado do botão (item 3.3):
// true = trazer a subárvore inteira preservando os níveis; false = só os
// arquivos, achatados.
struct ResultadoNavegador {
    std::vector<juce::File> selecionados;
    bool manterEstrutura = true;
};

// aoAdicionar é chamado UMA vez, com a seleção, no clique de ADD TO BACKUP.
// Fechar pela janela (X/Escape) nunca chama.
//
// `ultimaLocalizacao` é onde abrir — o item 3.2 pede que o navegador lembre
// a última localização visitada por projeto; quem chama guarda e devolve.
// Pasta inválida/vazia cai na pasta do usuário.
//
// Devolve o DocumentWindow — produção ignora, o harness de UI usa pra
// snapshot e pra dirigir a janela sem mouse.
std::unique_ptr<juce::DocumentWindow> mostrarNavegadorArquivos(const juce::File& ultimaLocalizacao,
                                                                std::function<void(ResultadoNavegador)> aoAdicionar,
                                                                std::function<void(juce::File)> aoLembrarLocalizacao = {});

} // namespace matriz::ui
