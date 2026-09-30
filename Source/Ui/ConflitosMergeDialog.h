#pragma once

#include <JuceHeader.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace matriz::ui {

// Conflitos de merge lado a lado (pacote de collection, Fase 4): um campo
// por linha, o valor do item mantido já marcado, o operador pode trocar pelo
// outro. Usado na resolução manual de um par (antes de juntar) e na revisão
// do filtro "Merge conflicts" (depois). Não faz I/O — quem chama já leu tudo.
class ConflitosMergeDialog : public juce::Component {
public:
    struct Linha {
        std::string chave;          // devolvida em `trocadas` quando o operador escolhe o outro valor
        std::string campo;          // coluna/campo (define o rótulo)
        juce::String valorMantido;  // já resumido pra leitura
        juce::String valorOutro;
    };

    ConflitosMergeDialog(const juce::String& intro, const juce::String& tituloMantido, const juce::String& tituloOutro,
                         std::vector<Linha> linhas);
    ~ConflitosMergeDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // aoConcluir(confirmado, chaves das linhas em que o OUTRO valor foi escolhido).
    static void mostrar(const juce::String& tituloJanela, const juce::String& intro, const juce::String& tituloMantido,
                        const juce::String& tituloOutro, std::vector<Linha> linhas,
                        std::function<void(bool, std::set<std::string>)> aoConcluir);

    // Rótulo legível do campo (EVENT DATE, CONTENT, GEO LOCATION, TITLE...).
    static juce::String rotuloDoCampo(const std::string& campo);

    // Self-test: escolhe o outro valor na linha `indice` / confirma.
    void escolherOutroParaTeste(int indice);
    void confirmarParaTeste() { confirmar(); }

private:
    class LinhaComponent;
    void confirmar();

    std::vector<Linha> linhas_;
    juce::Label lblIntro_, lblMantido_, lblOutro_;
    juce::Viewport viewport_;
    juce::Component lista_;
    juce::OwnedArray<LinhaComponent> componentes_;
    juce::TextButton btnCancelar_, btnConfirmar_;
    std::function<void(bool, std::set<std::string>)> aoConcluir_;
    bool concluido_ = false;
};

}  // namespace matriz::ui
