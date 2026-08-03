#include "ConfiguracoesProjetoDialogo.h"

#include "../I18n/Strings.h"
#include "../Model/Project.h"
#include "Tokens.h"

#include <JuceHeader.h>

namespace matriz::ui {

namespace {

struct EstadoProjetoRow {
    std::string id, nome, modo;
    bool permitirSobrescrever = false;
    bool avisoJaConfirmado = false;
};

EstadoProjetoRow lerEstado(matriz::db::Database& registro) {
    EstadoProjetoRow e;
    auto stmt = registro.prepare(
        "SELECT id, nome, modo, permitir_sobrescrever_master, aviso_sobrescrita_confirmado_em FROM projeto LIMIT 1");
    if (stmt.step()) {
        e.id = stmt.columnText(0);
        e.nome = stmt.columnText(1);
        e.modo = stmt.columnText(2);
        e.permitirSobrescrever = stmt.columnInt(3) != 0;
        e.avisoJaConfirmado = !stmt.columnIsNull(4);
    }
    return e;
}

void gravarPermitirSobrescrever(matriz::db::Database& registro, const std::string& projetoId, bool ligado,
                                 bool marcarAvisoConfirmado) {
    if (marcarAvisoConfirmado) {
        registro.run("UPDATE projeto SET permitir_sobrescrever_master = ?, aviso_sobrescrita_confirmado_em = ? WHERE id = ?",
                      {matriz::db::Value::of(ligado), matriz::db::Value::of(matriz::model::agoraIso8601()),
                       matriz::db::Value::of(projetoId)});
    } else {
        registro.run("UPDATE projeto SET permitir_sobrescrever_master = ? WHERE id = ?",
                      {matriz::db::Value::of(ligado), matriz::db::Value::of(projetoId)});
    }
}

} // namespace

void mostrarDialogoConfiguracoesProjeto(ProjetoAberto& projeto, std::function<void()> aoFechar) {
    auto estado = std::make_shared<EstadoProjetoRow>(lerEstado(projeto.projeto().registro()));

    auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("configuracoes_projeto.titulo"), juce::String(),
                                                        juce::MessageBoxIconType::NoIcon);

    auto infoLabel = std::make_shared<juce::Label>();
    infoLabel->setText(juce::String(estado->nome) + "  ·  " +
                            (estado->modo == "catalogo" ? matriz::i18n::t("dialogo_novo_projeto.campo_modo_catalogo")
                                                          : matriz::i18n::t("dialogo_novo_projeto.campo_modo_preservacao")),
                        juce::dontSendNotification);
    infoLabel->setColour(juce::Label::textColourId, tema().textoPrimario);
    infoLabel->setSize(420, 24);
    janela->addCustomComponent(infoLabel.get());

    auto toggle = std::make_shared<juce::ToggleButton>(matriz::i18n::t("configuracoes_projeto.campo_permitir_sobrescrever"));
    toggle->setToggleState(estado->permitirSobrescrever, juce::dontSendNotification);
    toggle->setSize(420, 28);
    toggle->setColour(juce::ToggleButton::textColourId, tema().textoPrimario);
    janela->addCustomComponent(toggle.get());

    janela->addButton(matriz::i18n::t("configuracoes_projeto.botao_fechar"), 1);

    // Mantém tudo vivo até o AlertWindow fechar.
    struct EstadoVivo {
        std::shared_ptr<juce::AlertWindow> janela;
        std::shared_ptr<juce::Label> infoLabel;
        std::shared_ptr<juce::ToggleButton> toggle;
        std::shared_ptr<EstadoProjetoRow> estado;
    };
    auto vivo = std::make_shared<EstadoVivo>();
    vivo->janela = janela;
    vivo->infoLabel = infoLabel;
    vivo->toggle = toggle;
    vivo->estado = estado;

    toggle->onClick = [vivo, &projeto] {
        bool ligarAgora = vivo->toggle->getToggleState();

        if (!ligarAgora) {
            gravarPermitirSobrescrever(projeto.projeto().registro(), vivo->estado->id, false, false);
            vivo->estado->permitirSobrescrever = false;
            return;
        }

        if (vivo->estado->avisoJaConfirmado) {
            // Já confirmou antes — "não incomoda mais" (P1).
            gravarPermitirSobrescrever(projeto.projeto().registro(), vivo->estado->id, true, false);
            vivo->estado->permitirSobrescrever = true;
            return;
        }

        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(matriz::i18n::t("configuracoes_projeto.aviso_sobrescrever_titulo"))
                .withMessage(matriz::i18n::t("configuracoes_projeto.aviso_sobrescrever_corpo"))
                .withButton(matriz::i18n::t("configuracoes_projeto.aviso_sobrescrever_confirmar"))
                .withButton(matriz::i18n::t("configuracoes_projeto.aviso_sobrescrever_cancelar")),
            [vivo, &projeto](int resultado) {
                bool confirmou = resultado == 1;
                if (confirmou) {
                    gravarPermitirSobrescrever(projeto.projeto().registro(), vivo->estado->id, true, true);
                    vivo->estado->permitirSobrescrever = true;
                    vivo->estado->avisoJaConfirmado = true;
                } else {
                    vivo->toggle->setToggleState(false, juce::dontSendNotification);
                }
            });
    };

    janela->enterModalState(true, juce::ModalCallbackFunction::create([vivo, aoConcluir = std::move(aoFechar)](int) {
                                 if (aoConcluir) aoConcluir();
                             }));
}

} // namespace matriz::ui
