#include "ImportarGeoFavoritosDialogo.h"

#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../Analytics/AssetGeolocation.h"
#include "../I18n/Strings.h"
#include "../Model/ProjectLog.h"

#include <set>

namespace matriz::ui {

namespace {

using matriz::analytics::GeoFavorito;
using matriz::analytics::GeoFavoritosRepository;

bool ptBr() { return i18n::localeAtivo().startsWith("pt"); }

juce::String coordenadas(const GeoFavorito& f) {
    if (f.latitude && f.longitude) return juce::String(*f.latitude, 5) + ", " + juce::String(*f.longitude, 5);
    return "-";
}

// Lista com checkbox: clicar na linha alterna. Todos começam marcados.
class ListaFavoritos : public juce::Component, public juce::ListBoxModel {
public:
    explicit ListaFavoritos(std::vector<GeoFavorito> favs) : favs_(std::move(favs)), marcados_(favs_.size(), true) {
        lista_.setModel(this);
        lista_.setRowHeight(26);
        addAndMakeVisible(lista_);
    }
    ~ListaFavoritos() override { lista_.setModel(nullptr); }
    void resized() override { lista_.setBounds(getLocalBounds()); }
    int getNumRows() override { return static_cast<int>(favs_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
        if (row < 0 || row >= static_cast<int>(favs_.size())) return;
        const auto& tk = tema();
        g.fillAll(row % 2 ? tk.painel : tk.painelAlt.withAlpha(0.5f));
        juce::Rectangle<int> caixa(8, (h - 16) / 2, 16, 16);
        g.setColour(marcados_[static_cast<size_t>(row)] ? tk.acento : tk.painel);
        g.fillRoundedRectangle(caixa.toFloat(), 3.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(caixa.toFloat(), 3.0f, 1.0f);
        if (marcados_[static_cast<size_t>(row)]) {
            g.setColour(tk.textoSobreAcento);
            juce::Path p;
            p.startNewSubPath(caixa.getX() + 3.5f, caixa.getCentreY());
            p.lineTo(caixa.getX() + 7.0f, caixa.getBottom() - 4.0f);
            p.lineTo(caixa.getRight() - 3.0f, caixa.getY() + 4.0f);
            g.strokePath(p, juce::PathStrokeType(2.0f));
        }
        auto texto = juce::Rectangle<int>(32, 0, w - 40, h);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
        const auto nome = juce::String::fromUTF8(favs_[static_cast<size_t>(row)].nome.c_str());
        g.drawText(nome, texto.removeFromLeft(juce::jmax(120, (w - 40) / 2)), juce::Justification::centredLeft, true);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(coordenadas(favs_[static_cast<size_t>(row)]), texto, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
        if (row < 0 || row >= static_cast<int>(favs_.size())) return;
        marcados_[static_cast<size_t>(row)] = !marcados_[static_cast<size_t>(row)];
        lista_.repaintRow(row);
    }
    std::vector<GeoFavorito> marcados() const {
        std::vector<GeoFavorito> out;
        for (size_t i = 0; i < favs_.size(); ++i) if (marcados_[i]) out.push_back(favs_[i]);
        return out;
    }
    void marcarTodos(bool v) { std::fill(marcados_.begin(), marcados_.end(), v); lista_.repaint(); }
    int quantos() const { return static_cast<int>(favs_.size()); }

private:
    std::vector<GeoFavorito> favs_;
    std::vector<bool> marcados_;
    juce::ListBox lista_;
};

class ConteudoDialogo : public juce::Component {
public:
    ConteudoDialogo(std::vector<GeoFavorito> favs, juce::String origem, std::function<void(std::vector<GeoFavorito>)> aoImportar,
                    std::function<void()> aoFechar)
        : lista_(std::move(favs)), aoImportar_(std::move(aoImportar)), aoFechar_(std::move(aoFechar)) {
        const bool pt = ptBr();
        titulo_.setText((pt ? juce::String::fromUTF8("Favoritos de GEO LOCATION em: ") : juce::String("GEO LOCATION favorites in: ")) + origem,
                        juce::dontSendNotification);
        titulo_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        titulo_.setColour(juce::Label::textColourId, tema().textoPrimario);
        addAndMakeVisible(titulo_);
        addAndMakeVisible(lista_);
        btnTodos_.setButtonText(pt ? "Marcar todos" : "Select all");
        btnNenhum_.setButtonText(pt ? "Desmarcar todos" : "Select none");
        btnImportar_.setButtonText(pt ? "IMPORTAR MARCADOS" : "IMPORT SELECTED");
        btnCancelar_.setButtonText(pt ? "Cancelar" : "Cancel");
        for (auto* b : {&btnTodos_, &btnNenhum_, &btnImportar_, &btnCancelar_}) addAndMakeVisible(*b);
        btnImportar_.setColour(juce::TextButton::buttonColourId, tema().acento);
        btnImportar_.setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
        btnTodos_.onClick = [this] { lista_.marcarTodos(true); };
        btnNenhum_.onClick = [this] { lista_.marcarTodos(false); };
        btnCancelar_.onClick = [this] { if (aoFechar_) aoFechar_(); };
        btnImportar_.onClick = [this] {
            auto sel = lista_.marcados();
            auto cb = aoImportar_;
            auto fechar = aoFechar_;
            if (fechar) fechar();  // fecha primeiro: o callback pode abrir um alerta
            if (cb) cb(std::move(sel));
        };
        setSize(560, 420);
    }
    void paint(juce::Graphics& g) override { g.fillAll(tema().fundo); }
    void resized() override {
        auto a = getLocalBounds().reduced(14);
        titulo_.setBounds(a.removeFromTop(22));
        a.removeFromTop(6);
        auto rodape = a.removeFromBottom(30);
        btnImportar_.setBounds(rodape.removeFromRight(170));
        rodape.removeFromRight(8);
        btnCancelar_.setBounds(rodape.removeFromRight(90));
        btnTodos_.setBounds(rodape.removeFromLeft(110));
        rodape.removeFromLeft(6);
        btnNenhum_.setBounds(rodape.removeFromLeft(120));
        a.removeFromBottom(8);
        lista_.setBounds(a);
    }

private:
    juce::Label titulo_;
    ListaFavoritos lista_;
    juce::TextButton btnTodos_, btnNenhum_, btnImportar_, btnCancelar_;
    std::function<void(std::vector<GeoFavorito>)> aoImportar_;
    std::function<void()> aoFechar_;
};

void alerta(const juce::String& titulo, const juce::String& msg) {
    auto* w = new juce::AlertWindow(titulo, msg, juce::MessageBoxIconType::InfoIcon);
    w->addButton("OK", 1);
    w->enterModalState(true, nullptr, true);
}

juce::String chave(const std::string& nome) { return juce::String::fromUTF8(nome.c_str()).trim().toLowerCase(); }

}  // namespace

void ImportarGeoFavoritosDialogo::iniciar(ProjetoAberto& projeto, juce::Component*) {
    const bool pt = ptBr();
    auto chooser = std::make_shared<juce::FileChooser>(
        pt ? juce::String::fromUTF8("Escolha a outra coleção (.mtz ou pasta do projeto)") : juce::String("Choose the other collection (.mtz or project folder)"));
    juce::Component::SafePointer<juce::Component> unused;
    ProjetoAberto* proj = &projeto;
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories |
                             juce::FileBrowserComponent::canSelectFiles,
                         [chooser, proj](const juce::FileChooser& fc) {
        const juce::File escolhido = fc.getResult();
        if (escolhido == juce::File()) return;
        const bool pt2 = ptBr();

        const juce::File pastaOutro = matriz::model::Project::resolverPastaProjeto(escolhido);
        const juce::File registroOutro = pastaOutro.getChildFile("registro.sqlite");
        if (!registroOutro.existsAsFile()) {
            alerta(pt2 ? "Importar favoritos" : "Import favorites",
                   pt2 ? juce::String::fromUTF8("Essa pasta não é uma coleção do Matriz.") : juce::String("That folder is not a Matriz collection."));
            return;
        }
        if (registroOutro == proj->projeto().pasta().getChildFile("registro.sqlite")) {
            alerta(pt2 ? "Importar favoritos" : "Import favorites",
                   pt2 ? juce::String::fromUTF8("Escolha OUTRA coleção, não a atual.") : juce::String("Choose ANOTHER collection, not the current one."));
            return;
        }

        // Leitura do outro projeto em background, SOMENTE-LEITURA (nem cria a tabela nele).
        const juce::String nomeOutro = pastaOutro.getParentDirectory().getFileName();
        juce::Thread::launch([proj, registroOutro, nomeOutro]() {
            std::vector<GeoFavorito> favs;
            juce::String erro;
            try {
                matriz::db::Database db(registroOutro.getFullPathName().toStdString(), matriz::db::Database::Modo::SomenteLeitura);
                auto st = db.prepare(
                    "SELECT id, nome, latitude, longitude, formatted_address, city, state_province, country, criado_em "
                    "FROM geo_favoritos ORDER BY nome ASC");
                while (st.step()) {
                    GeoFavorito f;
                    f.id = st.columnText(0);
                    f.nome = st.columnText(1);
                    if (!st.columnIsNull(2)) f.latitude = st.columnReal(2);
                    if (!st.columnIsNull(3)) f.longitude = st.columnReal(3);
                    if (!st.columnIsNull(4)) f.formattedAddress = st.columnText(4);
                    if (!st.columnIsNull(5)) f.city = st.columnText(5);
                    if (!st.columnIsNull(6)) f.stateProvince = st.columnText(6);
                    if (!st.columnIsNull(7)) f.country = st.columnText(7);
                    f.criadoEm = st.columnText(8);
                    favs.push_back(std::move(f));
                }
            } catch (...) {
                // sem a tabela geo_favoritos (coleção sem favoritos) ou banco ilegível: lista vazia
            }
            juce::MessageManager::callAsync([proj, favs = std::move(favs), nomeOutro]() mutable {
                const bool pt3 = ptBr();
                if (favs.empty()) {
                    alerta(pt3 ? "Importar favoritos" : "Import favorites",
                           pt3 ? juce::String::fromUTF8("Essa coleção não tem favoritos de GEO LOCATION.")
                               : juce::String("That collection has no GEO LOCATION favorites."));
                    return;
                }
                auto* janela = new juce::DialogWindow(pt3 ? "Importar favoritos de GEO LOCATION" : "Import GEO LOCATION favorites",
                                                      tema().fundo, true);
                juce::Component::SafePointer<juce::DialogWindow> seguro(janela);
                auto* conteudo = new ConteudoDialogo(
                    std::move(favs), nomeOutro,
                    [proj](std::vector<GeoFavorito> escolhidos) {
                        const bool pt4 = ptBr();
                        if (proj->somenteLeitura()) { ProjetoAberto::avisarSomenteLeitura(); return; }
                        auto& db = proj->projeto().registro();
                        int importados = 0, jaExistiam = 0;
                        try {
                            std::set<juce::String> nomes;
                            for (const auto& f : GeoFavoritosRepository::listar(db)) nomes.insert(chave(f.nome));  // cria a tabela se faltar
                            db.exec("BEGIN IMMEDIATE");
                            try {
                                for (auto f : escolhidos) {
                                    if (!nomes.insert(chave(f.nome)).second) { ++jaExistiam; continue; }
                                    f.id.clear();  // id novo neste projeto
                                    GeoFavoritosRepository::salvar(db, f);
                                    ++importados;
                                }
                                db.exec("COMMIT");
                            } catch (...) {
                                try { db.exec("ROLLBACK"); } catch (...) {}
                                throw;
                            }
                        } catch (const std::exception& e) {
                            alerta(pt4 ? "Importar favoritos" : "Import favorites", juce::String(e.what()));
                            return;
                        }
                        juce::StringArray linhas;
                        linhas.add("Imported: " + juce::String(importados));
                        linhas.add("Already existed (skipped): " + juce::String(jaExistiam));
                        try { matriz::model::ProjectLog(proj->projeto().pasta()).appendEntry("GEO Favorites imported from another collection", linhas); } catch (...) {}
                        alerta(pt4 ? "Importar favoritos" : "Import favorites",
                               pt4 ? juce::String(importados) + juce::String::fromUTF8(" importados, ") + juce::String(jaExistiam) + juce::String::fromUTF8(" já existiam.")
                                   : juce::String(importados) + " imported, " + juce::String(jaExistiam) + " already existed.");
                    },
                    [seguro]() {
                        if (seguro) juce::MessageManager::callAsync([seguro]() { if (seguro) seguro->exitModalState(0); });
                    });
                janela->setContentOwned(conteudo, true);
                janela->centreWithSize(conteudo->getWidth(), conteudo->getHeight() + 28);
                janela->setUsingNativeTitleBar(true);
                janela->setResizable(false, false);
                janela->enterModalState(true, nullptr, true);
            });
        });
    });
}

} // namespace matriz::ui
