#pragma once

#include <JuceHeader.h>
#include "ProjetoAberto.h"

namespace matriz::ui {

class ArvoreBackupComponent : public juce::Component {
public:
    explicit ArvoreBackupComponent(ProjetoAberto& projeto);
    ~ArvoreBackupComponent() override = default;

    void recarregar();
    void moverItemParaPasta(const std::string& itemId, const std::string& novaPasta);
    void selecionarERenomearPasta(const std::string& pastaId);

    void criarNovaPasta(const std::string& nome, const std::optional<std::string>& pastaPaiId = std::nullopt);
    void renomearPastaSelecionada(const std::string& pastaId, const std::string& novoNome);
    void apagarPastaSelecionada(const std::string& pastaId);
    void conectarPastas(const std::string& pastaFilhoId, const std::optional<std::string>& novaPastaPaiId);
    void alternarAtivoPasta(const std::string& pastaId);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::vector<juce::String> obterListaPastas() const;

private:
    struct FolderNode {
        std::string id;
        juce::String nome;
        std::string pastaPaiId;
        int contagemItens = 0;
        juce::int64 tamanhoBytes = 0;
        juce::Rectangle<int> bounds;
        bool ativo = true;
        bool selecionado = false;
    };

    ProjetoAberto& projeto_;
    std::vector<FolderNode> nodes_;
    int nodeDragIndice_ = -1;
    juce::Point<int> arrastoOffset_;

    float zoom_ = 1.0f;
    juce::Point<float> panOffset_{0.0f, 0.0f};
    bool panning_ = false;
    juce::Point<int> panStart_;

    std::unique_ptr<juce::TextButton> btnCriarPasta_;
    std::unique_ptr<juce::TextButton> btnRenomearPasta_;
    std::unique_ptr<juce::TextButton> btnApagarPasta_;
    std::unique_ptr<juce::TextButton> btnImportarEstrutura_;
    std::unique_ptr<juce::TextButton> btnAutoArranjar_;
    std::unique_ptr<juce::TextButton> btnZoomIn_;
    std::unique_ptr<juce::TextButton> btnZoomOut_;
    std::unique_ptr<juce::TextButton> btnZoomFit_;

    std::unique_ptr<juce::TextEditor> inlineEditor_;
    std::string editingNodeId_;

    void recalcularNodes();
    void autoArranjar();
    void desenharLinhaConexaoN8n(juce::Graphics& g, juce::Point<float> p1, juce::Point<float> p2, bool ativo, bool rascunho = false) const;
    bool ehDescendente(const std::string& noPaiId, const std::string& noFilhoId) const;

    void aplicarZoom(float novoZoom, juce::Point<float> centro);
    juce::Point<float> screenToCanvas(juce::Point<int> screen) const;
    juce::Point<int> canvasToScreen(juce::Point<float> canvas) const;
    void iniciarEdicaoInline(int nodeIndex);
    void finalizarEdicaoInline();
    void desenharMinimap(juce::Graphics& g) const;
    juce::Rectangle<int> minimapBounds() const;

    std::string socketDragParentId_;
    juce::Point<float> socketDragPos_;

    bool minimapDragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArvoreBackupComponent)
};

} // namespace matriz::ui
