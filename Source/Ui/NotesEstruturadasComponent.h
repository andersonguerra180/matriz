#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

#include "../Model/NotasEstruturadas.h"

// Editor estruturado do campo NOTES (item "NOTES — estrutura de metadados e
// notas"): mostra a seção automática OTHER METADATA (somente leitura, só
// aparece se houver conteúdo) seguida das seções criadas pelo usuário via
// "+ ADD NOTE", cada uma colapsável independentemente. Por baixo continua
// sendo um único texto em notas_livres — ver Source/Model/NotasEstruturadas.h.

namespace matriz::ui {

class NotesEstruturadasComponent : public juce::Component {
public:
    explicit NotesEstruturadasComponent(bool isPt);
    ~NotesEstruturadasComponent() override;

    void setTexto(const std::string& texto);
    std::string getTexto() const;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Disparado em tempo real a cada edição confirmada (perda de foco/Enter
    // fora de uma seção multi-linha) ou ao criar/remover uma seção — quem
    // usa este componente decide o que fazer (salvar no banco, notificar
    // aoAplicarSucesso etc.), este componente só serializa/mantém o texto.
    std::function<void()> onCommit;

private:
    struct LinhaSecao {
        matriz::model::SecaoNota dados;
        bool colapsada = true;
        std::unique_ptr<juce::TextButton> cabecalho;
        std::unique_ptr<juce::TextButton> botaoRemover; // ausente para automatica==true
        std::unique_ptr<juce::Label> corpoSomenteLeitura;
        std::unique_ptr<juce::TextEditor> corpoEditavel;
    };

    void reconstruirLinhas();
    void atualizarTextoCabecalho(LinhaSecao& linha);
    void commit();

    bool isPt_ = false;
    std::vector<std::unique_ptr<LinhaSecao>> linhas_;
    std::unique_ptr<juce::TextButton> botaoAddNote_;

    std::unique_ptr<juce::Component> conteudoInterno_;
    std::unique_ptr<juce::Viewport> viewport_;
};

} // namespace matriz::ui
