#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

namespace matriz::ui {

// MAIN EDITOR (Fase 4): janela modal aberta de dentro do MAIN EDIT MODE.
// FILES: árvore do que está registrado no MAIN (lida do banco, numa thread de
// fundo — nunca do disco na message thread) com RENAME / MOVE TO / REPLACE /
// DELETE e CONVERT MAIN TO FOLDER MAP. QUARANTINE: itens em _QUARENTENA com
// RESTORE e EMPTY QUARANTINE. Toda operação roda em thread de fundo
// (ProjetoAberto::editarMain*) e o painel só reflete o resultado.
class MainEditPanel : public juce::Component, private juce::ListBoxModel {
public:
    explicit MainEditPanel(ProjetoAberto& projeto);
    ~MainEditPanel() override;

    // Abre a janela (modal, assíncrona). `aoMudar` roda quando algo que a aba
    // BACKUP mostra pode ter mudado (conversão em folder map, por exemplo).
    static void abrir(ProjetoAberto& projeto, std::function<void()> aoMudar = {});

    // Pede que o usuário digite o nome do projeto (barreira do modo de edição e
    // do esvaziar quarentena). `aoConfirmar` só roda se o nome conferir; se não
    // conferir, avisa e não faz nada.
    static void pedirNomeDoProjeto(ProjetoAberto& projeto, const juce::String& titulo, const juce::String& mensagem,
                                   const juce::String& textoBotao, std::function<void(const juce::String& digitado)> aoConfirmar);

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> aoMudar;

private:
    struct NoMem {
        juce::String nome;
        std::string registroId;  // vazio = pasta
        std::map<juce::String, std::unique_ptr<NoMem>> filhos;
        bool ehArquivo() const { return !registroId.empty(); }
    };
    class ItemArvore;

    void mostrarAba(bool arquivos);
    void carregarArquivos();
    void carregarQuarentena();
    void atualizarBotoes();
    void reconstruirArvore();
    void concluir(const matriz::mainedit::Resultado& r, const juce::String& msgOk = {});
    void avisar(const juce::String& titulo, const juce::String& msg);
    std::string registroSelecionado() const;
    juce::String caminhoSelecionado() const;

    void renomear();
    void mover();
    void substituir();
    void deletar();
    void converter();
    void restaurar();
    void esvaziar();

    // ListBoxModel (aba QUARANTINE)
    int getNumRows() override { return static_cast<int>(quarentena_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selecionada) override;
    void selectedRowsChanged(int) override { atualizarBotoes(); }

    ProjetoAberto& projeto_;
    bool abaArquivos_ = true;
    bool carregando_ = false;
    std::shared_ptr<NoMem> raiz_;
    std::unique_ptr<juce::TreeView> arvore_;
    std::unique_ptr<ItemArvore> itemRaiz_;
    juce::Label infoArquivos_, resumoQuarentena_;
    juce::TextButton btnAbaArquivos_, btnAbaQuarentena_;
    juce::TextButton btnRenomear_, btnMover_, btnSubstituir_, btnDeletar_, btnConverter_;
    juce::TextButton btnRestaurar_, btnEsvaziar_;
    juce::ListBox listaQuarentena_;
    std::vector<matriz::mainedit::ItemQuarentena> quarentena_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(MainEditPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainEditPanel)
};

} // namespace matriz::ui
