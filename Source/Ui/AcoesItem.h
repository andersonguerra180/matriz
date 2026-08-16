#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

// Ações sobre um arquivo ou sobre a seleção inteira — o mesmo conjunto
// servido pelo menu de botão direito na grade e pelo painel direito, pra as
// duas portas fazerem exatamente a mesma coisa (item 5/6 das correções de
// operação: "resolver tudo no mesmo lugar").
//
// Toda ação aqui vale igual pra 1 item ou pra N: quem chama passa sempre um
// vector, e não existe caminho "só funciona com um selecionado".
//
// NENHUMA ação deste módulo apaga arquivo em disco. "Remover desta lista" e
// "Remover do backup" mexem só no registro e no plano de organização, e os
// diálogos dizem isso com todas as letras — é a garantia que o operador
// precisa ter pra usar o software sem medo num HD que ele não quer perder.

namespace matriz::ui::acoes {

// O que a UI precisa fazer em resposta a uma ação, e que este módulo não
// tem como fazer sozinho (não conhece MosaicoComponent nem a ficha).
struct Ganchos {
    // Algo mudou no banco — recarregar grade/árvore/filtros/faixa de apoio.
    std::function<void()> aoMudarDados;
    // Abrir o painel direito com a seleção atual (Categorizar).
    std::function<void()> aoAbrirDetalhes;
    // Filtrar a grade por um conjunto de itens (Ver duplicatas).
    std::function<void(std::set<std::string>)> aoFiltrarItens;
};

// Monta o menu de contexto pra `itemIds` (nunca vazio). Os rótulos mudam de
// singular pra plural conforme a quantidade.
juce::PopupMenu construirMenu(ProjetoAberto& projeto, const std::vector<std::string>& itemIds);

// Executa o item de menu escolhido. `resultado` é o id devolvido por
// PopupMenu::showMenuAsync; 0 (nada escolhido) é ignorado com segurança.
void executar(int resultado, ProjetoAberto& projeto, std::vector<std::string> itemIds, Ganchos ganchos);

// --- As mesmas ações, chamáveis direto ---
//
// O painel direito oferece botões dedicados em vez de um menu; usar estas
// funções (as MESMAS que `executar` chama) é o que garante que clicar em
// "Renomear" no painel e escolher "Renomear…" no botão direito façam
// exatamente a mesma coisa, incluindo as confirmações.
void renomear(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos);
void mudarTipo(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos);
void removerDoBackup(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos);
// Item 9 — escolhe uma imagem e a torna a miniatura do(s) item(ns).
void definirCapa(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos);
// Abre o menu de pastas da BACKUP ancorado no componente dado.
void enviarParaPasta(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos,
                      juce::Component* ancora);

void renomearEmLote(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, Ganchos ganchos);

} // namespace matriz::ui::acoes
