#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "ProjetoAberto.h"

// "Metadados originais" — o que já vinha DENTRO do arquivo (EXIF, codec,
// duração, taxa de amostragem, dimensões, datas), somente leitura.
//
// Existe pra desfazer uma ambiguidade que o painel direito tinha: tudo ali
// era campo editável, então não dava pra saber o que o software LEU do
// arquivo e o que uma pessoa DIGITOU. Agora são duas seções separadas e
// rotuladas — esta, imutável, e a ficha, que é o que vai pro backup.
//
// Nunca escreve nada. Não há caminho de código daqui pro banco.

namespace matriz::ui {

class MetadadosOriginaisComponent : public juce::Component {
public:
    explicit MetadadosOriginaisComponent(ProjetoAberto& projeto);

    // "" = nenhum item selecionado. Item sem arquivo associado (ou cujo
    // ingest falhou antes da leitura técnica) mostra o estado vazio
    // explicando isso, nunca uma seção em branco sem motivo.
    void mostrarItem(const std::string& itemId);

    // Colapsada, sobra só o cabeçalho clicável. Começa colapsada: quem abre
    // o painel quase sempre quer EDITAR a ficha, e o dado técnico empurraria
    // os campos editáveis pra fora da tela.
    bool estaColapsada() const { return colapsada_; }
    void alternarColapso();

    // Altura que o componente quer ocupar — o pai (FichaPanelComponent) usa
    // pra dividir o espaço entre esta seção e a ficha.
    int alturaDesejada() const;

    std::function<void()> aoMudarAltura;

    // Introspecção pra teste: rótulos das linhas atualmente montadas.
    std::vector<juce::String> rotulosParaTeste() const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int kAlturaCabecalho = 28;
    static constexpr int kAlturaLinha = 20;
    static constexpr int kMaxLinhasVisiveis = 10;

private:
    struct Linha {
        juce::String rotulo;
        juce::String valor;
    };

    void reconstruir(const std::string& itemId);

    ProjetoAberto& projeto_;
    std::vector<Linha> linhas_;
    bool colapsada_ = true;
    bool semArquivo_ = false;
};

} // namespace matriz::ui
