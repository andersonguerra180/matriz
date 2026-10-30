#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace matriz::ui {

class ProjetoAberto;

// Painel SOMENTE-LEITURA com tudo que o ingest/leitura técnica sabe sobre o arquivo de um item, em cards
// (ARQUIVO · TÉCNICO · CÂMERA/EXIF · DATA ORIGINAL · LOCALIZAÇÃO/GPS · DESCRITIVO). Campo ausente não aparece;
// card sem campo não aparece. Usado na coluna direita do INTAKE e na lateral da janela de preview.
//
// Thread: a LEITURA (banco + disco) roda numa thread de fundo (lerCards); os componentes só são montados
// na message thread. Trocar de item rápido descarta os resultados antigos (geração).
class PainelMetadadosArquivo : public juce::Component {
public:
    struct Linha {
        juce::String rotulo;
        juce::String valor;
    };
    struct Card {
        juce::String titulo;
        std::vector<Linha> linhas;
    };

    // Resumo de uma pasta (janela Explorer · Folder Map: nada selecionado = a pasta atual; ou o cartão de pasta selecionado).
    struct ResumoPasta {
        juce::String nome, caminho, vault;  // caminho: "Acervo › Site › IMAGES"
        int arquivos = 0, subpastas = 0;
        juce::int64 bytes = 0;
        int video = 0, audio = 0, foto = 0, documento = 0;
        juce::String maisAntigo, maisRecente;  // "AAAA-MM-DD"
    };

    explicit PainelMetadadosArquivo(ProjetoAberto& projeto);
    ~PainelMetadadosArquivo() override;

    // Visual "ficha fixa" (STRUCTURE): nome em destaque, tamanho legível, caminho mono, datas juntas,
    // Location/Descriptive em grade 2 x 2. Mesmos dados; só muda a apresentação.
    void definirLayoutCompacto(bool compacto);

    // itemId vazio: mostra a frase discreta de "nada selecionado".
    void mostrarItem(const std::string& itemId);
    // Só no visual compacto (estilo explorer): resumo da pasta, sem ler o banco (quem chama já tem os números).
    void mostrarResumoPasta(const ResumoPasta& resumo);

    // Leitura pura (sem UI): segura para thread de fundo.
    static std::vector<Card> lerCards(ProjetoAberto& projeto, const std::string& itemId);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Conteudo;
    void aplicarCards(std::vector<Card> cards, int geracao);

    ProjetoAberto& projeto_;
    bool compacto_ = false;
    juce::Label lblVazio_;
    juce::Viewport viewport_;
    std::unique_ptr<Conteudo> conteudo_;
    std::atomic<int> geracao_{0};
    juce::ThreadPool pool_{1};  // declarado por último entre os membros usados pelo job: destruído (e esperado) primeiro

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelMetadadosArquivo)
};

} // namespace matriz::ui
