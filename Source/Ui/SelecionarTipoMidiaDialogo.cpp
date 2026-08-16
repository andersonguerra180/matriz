#include "SelecionarTipoMidiaDialogo.h"

#include "../Ficha/CatalogoDeFichas.h"
#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "../Ingest/LeituraTecnica.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {
constexpr int kBotaoConfirmar = 1;
constexpr int kBotaoCancelar = 0;
} // namespace

// Tipos de mídia descobertos em tempo de execução em fichas/*.yaml (§6.1:
// "adicionar tipo novo é escrever um arquivo, não recompilar") — nenhuma
// lista de ids hardcoded aqui. Cada ficha declara seu próprio `modos`
// (archive e/ou catalog) e `ordem` (posição de exibição); ficha sem
// `modos` fica disponível nos dois. Um YAML inválido em fichas/ derruba o
// diálogo inteiro (listarTiposPorModo lança, não pula em silêncio) —
// falha visível é preferível a um tipo sumir sem explicação.
std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveis(ProjetoAberto& projeto) {
    std::vector<TipoMidiaOpcao> out;
    std::string modo = projeto.projeto().modo() == matriz::model::Modo::Catalogo ? "catalog" : "archive";
    for (auto& info : matriz::ficha::listarTiposPorModo(MATRIZ_FICHAS_DIR, modo)) {
        juce::String rotulo = matriz::ficha::rotuloTipo(info.id, info.definicao.rotulo.empty() ? info.id : info.definicao.rotulo);
        out.push_back({info.id, rotulo, info.definicao.categoria});
    }
    return out;
}

std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveisParaExtensoes(ProjetoAberto& projeto, const std::vector<std::string>& extensoes) {
    auto todos = listarTiposMidiaDisponiveis(projeto);
    if (extensoes.empty()) return todos;

    std::map<matriz::ingest::CategoriaMidia, int> contagem;
    for (const auto& ext : extensoes) {
        auto cat = matriz::ingest::categoriaPorExtensao(juce::String(ext));
        contagem[cat]++;
    }

    matriz::ingest::CategoriaMidia dominante = matriz::ingest::CategoriaMidia::Desconhecida;
    int maxC = 0;
    for (const auto& [c, count] : contagem) {
        if (count > maxC) {
            maxC = count;
            dominante = c;
        }
    }

    if (dominante == matriz::ingest::CategoriaMidia::Desconhecida) return todos;

    std::set<std::string> aceitos;
    if (dominante == matriz::ingest::CategoriaMidia::Audio) {
        aceitos = {"fita_rolo", "cassete", "vinil", "dat", "minidisc", "cd", "digital_audio", "field_recording", "sound_effects"};
    } else if (dominante == matriz::ingest::CategoriaMidia::Video) {
        aceitos = {"filme", "video", "dvd", "vhs", "umatic", "betacam", "betamax", "digital_video"};
    } else if (dominante == matriz::ingest::CategoriaMidia::Imagem) {
        aceitos = {"foto", "cover_art", "negativo", "slide"};
    } else if (dominante == matriz::ingest::CategoriaMidia::Documento || dominante == matriz::ingest::CategoriaMidia::Texto) {
        aceitos = {"documento", "sample", "release"};
    }

    if (aceitos.empty()) return todos;

    std::vector<TipoMidiaOpcao> filtrados;
    for (const auto& op : todos) {
        if (aceitos.count(op.id)) filtrados.push_back(op);
    }
    return filtrados.empty() ? todos : filtrados;
}

PainelOverlay::Config configSelecionarTipoMidia(const std::vector<TipoMidiaOpcao>& opcoes,
                                                 int quantidadeArquivos) {
    PainelOverlay::Config config;
    config.titulo = matriz::i18n::t("ingest.selecionar_tipo_titulo");
    config.mensagem =
        matriz::i18n::t("ingest.selecionar_tipo_corpo").replace("{n}", juce::String(quantidadeArquivos));

    std::vector<const TipoMidiaOpcao*> digitais, analogicos, outros;
    for (auto& o : opcoes) {
        if (o.categoria == "digital") digitais.push_back(&o);
        else if (o.categoria == "analog") analogicos.push_back(&o);
        else outros.push_back(&o);
    }
    int idx = 0;
    if (!digitais.empty()) {
        config.cabecalhos.push_back({idx, "DIGITAL"});
        for (auto* o : digitais) { config.opcoes.push_back(o->rotulo); ++idx; }
    }
    if (!analogicos.empty()) {
        config.cabecalhos.push_back({idx, "ANALOG"});
        for (auto* o : analogicos) { config.opcoes.push_back(o->rotulo); ++idx; }
    }
    for (auto* o : outros) { config.opcoes.push_back(o->rotulo); ++idx; }

    config.opcaoSelecionada = 0;
    config.botoes = {
        {matriz::i18n::t("comum.cancelar"), kBotaoCancelar, false, true},
        {matriz::i18n::t("ingest.selecionar_tipo_confirmar"), kBotaoConfirmar, true, false},
    };
    return config;
}

void mostrarSelecionarTipoMidia(PainelOverlay& overlay, std::vector<TipoMidiaOpcao> opcoes,
                                 int quantidadeArquivos,
                                 std::function<void(std::optional<std::string>)> aoConcluir) {
    if (opcoes.empty()) {
        if (aoConcluir) aoConcluir(std::nullopt);
        return;
    }

    // Build reordered ID list matching the ComboBox order (digital, analog, other).
    std::vector<std::string> idsOrdenados;
    for (auto& o : opcoes) if (o.categoria == "digital") idsOrdenados.push_back(o.id);
    for (auto& o : opcoes) if (o.categoria == "analog") idsOrdenados.push_back(o.id);
    for (auto& o : opcoes) if (o.categoria != "digital" && o.categoria != "analog") idsOrdenados.push_back(o.id);

    overlay.mostrar(configSelecionarTipoMidia(opcoes, quantidadeArquivos),
                    [idsOrdenados, aoConcluir](PainelOverlay::Resultado r) {
                        if (!aoConcluir) return;
                        if (r.botaoId != kBotaoConfirmar || r.opcaoSelecionada < 0 ||
                            r.opcaoSelecionada >= static_cast<int>(idsOrdenados.size())) {
                            aoConcluir(std::nullopt);
                            return;
                        }
                        aoConcluir(idsOrdenados[static_cast<size_t>(r.opcaoSelecionada)]);
                    });
}

} // namespace matriz::ui
