#include "BackupWorkspaceComponent.h"
#include "BackupFileSelectorDialog.h"
#include "BackupSyncDialog.h"
#include "BackupScanProgressDialog.h"
#include "SyncDestinationDialog.h"
#include "PublishHtmlDialog.h"
#include "ExportZipDialog.h"
#include "../Sync/SyncEngine.h"
#include <AssetsBinaryData.h>
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Catalogo/CatalogoProxies.h"
#include "../Catalogo/CatalogSiteExport.h"
#include "../Consolidacao/MetadadoEmbutido.h"
#include "../Vault/Resolucao.h"
#include "../Vault/Volume.h"
#include "../Preservation/Preservation.h"
#include "ProgressoGlobal.h"
#include "../Model/ProjectLog.h"
#include "../Vault/DeviceUsageLog.h"

namespace matriz::ui {

namespace {

class GoogleDriveIconButton : public juce::Button {
public:
    GoogleDriveIconButton() : juce::Button("GoogleDrive") {
        img_ = juce::ImageFileFormat::loadFrom(AssetsBinaryData::googledrive_png, AssetsBinaryData::googledrive_pngSize);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        const auto& tk = tema();
        auto r = getLocalBounds().toFloat();
        
        g.setColour(shouldDrawButtonAsDown ? tk.painelAlt.darker(0.1f)
                    : (shouldDrawButtonAsHighlighted ? tk.painelAlt.brighter(0.15f) : tk.painelAlt));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xff1a73e8) : tk.borda);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

        if (img_.isValid()) {
            auto iconArea = r.reduced(4.0f);
            g.drawImageWithin(img_, (int)iconArea.getX(), (int)iconArea.getY(),
                              (int)iconArea.getWidth(), (int)iconArea.getHeight(),
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
        } else {
            g.setColour(juce::Colour(0xff1a73e8));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText("GD", getLocalBounds(), juce::Justification::centred);
        }
    }
private:
    juce::Image img_;
};

void desenharIconeNuvem(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour cor) {
    juce::Path cloud;
    float x = r.getX();
    float y = r.getY();
    float w = r.getWidth();
    float h = r.getHeight();

    cloud.startNewSubPath(x + w * 0.20f, y + h * 0.82f);
    cloud.lineTo(x + w * 0.80f, y + h * 0.82f);
    cloud.cubicTo(x + w * 0.94f, y + h * 0.82f, x + w * 1.00f, y + h * 0.65f, x + w * 0.92f, y + h * 0.52f);
    cloud.cubicTo(x + w * 0.94f, y + h * 0.38f, x + w * 0.80f, y + h * 0.32f, x + w * 0.72f, y + h * 0.36f);
    cloud.cubicTo(x + w * 0.66f, y + h * 0.12f, x + w * 0.36f, y + h * 0.12f, x + w * 0.30f, y + h * 0.34f);
    cloud.cubicTo(x + w * 0.20f, y + h * 0.30f, x + w * 0.06f, y + h * 0.38f, x + w * 0.06f, y + h * 0.56f);
    cloud.cubicTo(x + w * 0.04f, y + h * 0.72f, x + w * 0.12f, y + h * 0.82f, x + w * 0.20f, y + h * 0.82f);
    cloud.closeSubPath();

    g.setColour(cor);
    g.fillPath(cloud);
}

bool ehDestinoNuvem(const juce::String& caminho, const juce::String& rotulo, const std::string& papel) {
    if (papel == "ORIGINAL") return false;
    const juce::String& p = caminho;
    const juce::String& r = rotulo;
    return (p.contains("CloudStorage") ||
            p.containsIgnoreCase("Google Drive") || p.containsIgnoreCase("GoogleDrive") ||
            r.containsIgnoreCase("Google Drive") || r.containsIgnoreCase("GoogleDrive") ||
            p.containsIgnoreCase("Dropbox") || r.containsIgnoreCase("Dropbox") ||
            p.containsIgnoreCase("OneDrive") || r.containsIgnoreCase("OneDrive") ||
            p.containsIgnoreCase("iCloud") || p.containsIgnoreCase("Mobile Documents") || r.containsIgnoreCase("iCloud") ||
            p.containsIgnoreCase("pCloud") || r.containsIgnoreCase("pCloud") ||
            p.containsIgnoreCase("Box Sync") || r.containsIgnoreCase("Box") ||
            r.containsIgnoreCase("Cloud") || r.containsIgnoreCase("Nuvem"));
}

} // namespace

class BackupWorkspaceComponent::PreviaLista : public juce::Component {
public:
    enum class ModoPrioridade {
        Nenhum = 0,
        PriorizarIdenticos = 1,  // 🟢
        PriorizarPendentes = 2,  // 🔴
        PriorizarOrfaos = 3      // ⚪
    };

    struct LinhaExibicao {
        enum class Tipo { Normal, Orfao };
        Tipo tipo = Tipo::Normal;

        // Item Normal
        std::string itemId;
        std::string arquivoId;
        juce::String nomeOriginal;
        juce::String caminhoRelativoDestino;
        bool emConflito = false;
        bool jaConsolidado = false;
        matriz::consolidacao::StatusArquivoBackup status = matriz::consolidacao::StatusArquivoBackup::Vermelho;

        // Item Órfão
        juce::String caminhoRelativoOrfao;
        juce::int64 tamanhoBytesOrfao = 0;
    };

    void definirPlano(const matriz::consolidacao::PlanoConsolidacao& plano) {
        plano_ = &plano;
        isCatalogMode_ = false;
        colecoes_.clear();
        reconstruirLinhas();
    }

    void definirScan(const matriz::consolidacao::ResultadoScanBackup& scan) {
        statusPorItem_.clear();
        for (const auto& isb : scan.itensGrid) {
            statusPorItem_[isb.arquivoId] = isb.status;
            if (!isb.itemId.empty()) statusPorItem_[isb.itemId] = isb.status;
        }
        reconstruirLinhas();
    }

    void definirOrfaos(const std::vector<matriz::consolidacao::ArquivoOrfao>& orfaos,
                       const juce::File& destFolder) {
        orfaos_ = orfaos;
        destFolder_ = destFolder;
        reconstruirLinhas();
    }

    void definirColecoesCatalogo(const std::vector<ProjetoAberto::ColecaoLink>& colecoes,
                                 const juce::File& destFolder) {
        colecoes_ = colecoes;
        destFolder_ = destFolder;
        isCatalogMode_ = true;
        plano_ = nullptr;
        orfaos_.clear();
        reconstruirLinhas();
    }

    void definirModoPrioridade(ModoPrioridade modo) {
        modoPrioridade_ = modo;
        reconstruirLinhas();
    }

    void reconstruirLinhas() {
        linhasExibicao_.clear();
        if (isCatalogMode_) {
            setSize(getWidth(), std::max(60, static_cast<int>(colecoes_.size()) * 36 + 10));
            repaint();
            return;
        }

        if (plano_) {
            for (const auto& item : plano_->itens) {
                LinhaExibicao l;
                l.tipo = LinhaExibicao::Tipo::Normal;
                l.itemId = item.itemId;
                l.arquivoId = item.arquivoId;
                l.nomeOriginal = item.nomeOriginal;
                l.caminhoRelativoDestino = item.caminhoRelativoDestino;
                l.emConflito = item.emConflito;
                l.jaConsolidado = item.jaConsolidado;

                auto itStatus = statusPorItem_.find(item.arquivoId);
                if (itStatus != statusPorItem_.end()) {
                    l.status = itStatus->second;
                } else {
                    itStatus = statusPorItem_.find(item.itemId);
                    if (itStatus != statusPorItem_.end()) {
                        l.status = itStatus->second;
                    } else {
                        l.status = item.jaConsolidado ? matriz::consolidacao::StatusArquivoBackup::Verde : matriz::consolidacao::StatusArquivoBackup::Vermelho;
                    }
                }
                linhasExibicao_.push_back(std::move(l));
            }
        }

        for (const auto& orf : orfaos_) {
            LinhaExibicao l;
            l.tipo = LinhaExibicao::Tipo::Orfao;
            l.caminhoRelativoOrfao = orf.caminhoRelativo;
            l.tamanhoBytesOrfao = orf.tamanhoBytes;
            linhasExibicao_.push_back(std::move(l));
        }

        if (modoPrioridade_ == ModoPrioridade::PriorizarPendentes) {
            std::stable_sort(linhasExibicao_.begin(), linhasExibicao_.end(), [](const LinhaExibicao& a, const LinhaExibicao& b) {
                int pA = (a.tipo == LinhaExibicao::Tipo::Normal && (a.status == matriz::consolidacao::StatusArquivoBackup::Vermelho || a.emConflito)) ? 0 : 1;
                int pB = (b.tipo == LinhaExibicao::Tipo::Normal && (b.status == matriz::consolidacao::StatusArquivoBackup::Vermelho || b.emConflito)) ? 0 : 1;
                return pA < pB;
            });
        } else if (modoPrioridade_ == ModoPrioridade::PriorizarIdenticos) {
            std::stable_sort(linhasExibicao_.begin(), linhasExibicao_.end(), [](const LinhaExibicao& a, const LinhaExibicao& b) {
                int pA = (a.tipo == LinhaExibicao::Tipo::Normal && a.status == matriz::consolidacao::StatusArquivoBackup::Verde && !a.emConflito) ? 0 : 1;
                int pB = (b.tipo == LinhaExibicao::Tipo::Normal && b.status == matriz::consolidacao::StatusArquivoBackup::Verde && !b.emConflito) ? 0 : 1;
                return pA < pB;
            });
        } else if (modoPrioridade_ == ModoPrioridade::PriorizarOrfaos) {
            std::stable_sort(linhasExibicao_.begin(), linhasExibicao_.end(), [](const LinhaExibicao& a, const LinhaExibicao& b) {
                int pA = (a.tipo == LinhaExibicao::Tipo::Orfao) ? 0 : 1;
                int pB = (b.tipo == LinhaExibicao::Tipo::Orfao) ? 0 : 1;
                return pA < pB;
            });
        }

        setSize(getWidth(), std::max(60, static_cast<int>(linhasExibicao_.size()) * 26));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        if (isCatalogMode_) {
            if (colecoes_.empty()) {
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
                g.drawText(isPt ? juce::String::fromUTF8("Nenhuma coleção vinculada a este catálogo.") : "No collections linked to this catalog.", getLocalBounds(), juce::Justification::centred);
                return;
            }

            int y = 4;
            for (const auto& c : colecoes_) {
                auto linha = juce::Rectangle<int>(8, y, getWidth() - 16, 30);
                g.setColour(tk.painelAlt);
                g.fillRoundedRectangle(linha.toFloat(), 4.0f);
                g.setColour(tk.borda);
                g.drawRoundedRectangle(linha.toFloat(), 4.0f, 1.0f);

                auto r = linha.reduced(8, 0);

                // Badge
                auto badge = juce::Rectangle<int>(r.getX(), r.getY() + 6, 56, 18);
                g.setColour(c.valido ? tk.estadoQcOk : juce::Colour(0xfff97316));
                g.fillRoundedRectangle(badge.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
                g.drawText(c.valido ? "ONLINE" : "OFFLINE", badge, juce::Justification::centred);
                r.removeFromLeft(64);

                // Collection Name
                g.setColour(tk.textoPrimario);
                g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
                g.drawText(c.nome, r.removeFromLeft(200), juce::Justification::centredLeft, true);

                // Assets & Size
                g.setColour(tk.textoSecundario);
                g.setFont(juce::Font(juce::FontOptions(11.5f)));
                juce::String details = juce::String(c.totalAssets) + (isPt ? juce::String::fromUTF8(" itens  |  ") : " assets  |  ") +
                                       juce::File::descriptionOfSizeInBytes(c.totalBytes);
                g.drawText(details, r.removeFromLeft(160), juce::Justification::centredLeft);

                // Target Destination
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                juce::File raiz = matriz::model::normalizarParaRaizDestino(destFolder_);
                juce::String target = "->  " + raiz.getChildFile("Media").getChildFile(c.nome).getFullPathName();
                g.drawText(target, r, juce::Justification::centredLeft, true);

                y += 34;
            }
            return;
        }

        if (linhasExibicao_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhum item para backup.") : "No items to backup.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        int y = 0;
        const int itemH = 26;
        for (const auto& linha : linhasExibicao_) {
            juce::Rectangle<int> rLinha(0, y, getWidth(), itemH);

            if (linha.tipo == LinhaExibicao::Tipo::Normal) {
                if (linha.emConflito) {
                    g.setColour(tk.perigo.withAlpha(0.12f));
                    g.fillRect(rLinha);
                } else if (linha.status == matriz::consolidacao::StatusArquivoBackup::Verde) {
                    g.setColour(tk.painelAlt.withAlpha(0.35f));
                    g.fillRect(rLinha);
                }

                auto r = rLinha.reduced(6, 2);
                bool isVerde = (linha.status == matriz::consolidacao::StatusArquivoBackup::Verde);

                // Dot
                auto dotArea = r.removeFromLeft(16).withSizeKeepingCentre(8, 8);
                g.setColour(isVerde ? juce::Colour(0xff22c55e) : juce::Colour(0xffef4444));
                g.fillEllipse(dotArea.toFloat());
                r.removeFromLeft(4);

                // Status badge on the right (with comfortable margin from scrollbar)
                juce::String statusTxt;
                juce::Colour statusCol;
                if (linha.emConflito) {
                    statusTxt = isPt ? juce::String::fromUTF8("CONFLITO") : "CONFLICT";
                    statusCol = tk.perigo;
                } else if (isVerde) {
                    statusTxt = isPt ? juce::String::fromUTF8("Idêntico") : "Up to date";
                    statusCol = juce::Colour(0xff22c55e);
                } else {
                    statusTxt = isPt ? juce::String::fromUTF8("Pendente") : "Pending";
                    statusCol = juce::Colour(0xffef4444);
                }

                r.removeFromRight(20); // Margem para não colidir com scrollbar
                int statusW = 95;
                auto statusArea = r.removeFromRight(statusW);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f, juce::Font::bold)));
                g.setColour(statusCol);
                g.drawText(statusTxt, statusArea, juce::Justification::centredRight, false);
                r.removeFromRight(12);

                // Main Text: "nomeOriginal  →  caminhoRelativoDestino"
                g.setColour(linha.emConflito ? tk.perigo : (isVerde ? tk.textoSecundario : tk.textoPrimario));
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                juce::String texto = linha.nomeOriginal + juce::String::fromUTF8("  →  ") + linha.caminhoRelativoDestino;
                g.drawText(texto, r, juce::Justification::centredLeft, true);

            } else {
                // Linha Órfão
                auto r = rLinha.reduced(6, 2);

                // Dot ⚪
                auto dotArea = r.removeFromLeft(16).withSizeKeepingCentre(8, 8);
                g.setColour(juce::Colour(0xff9ca3af));
                g.fillEllipse(dotArea.toFloat());
                r.removeFromLeft(4);

                // Size & Status badge on the right
                r.removeFromRight(20);
                juce::String sizeTxt = juce::File::descriptionOfSizeInBytes(linha.tamanhoBytesOrfao);
                int statusW = 95;
                auto statusArea = r.removeFromRight(statusW);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));
                g.setColour(tk.textoTerciario);
                g.drawText(sizeTxt, statusArea, juce::Justification::centredRight, false);
                r.removeFromRight(12);

                // Main text: caminhoRelativo
                g.setColour(tk.textoTerciario);
                g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
                juce::String texto = linha.caminhoRelativoOrfao + (isPt ? juce::String::fromUTF8("  [Órfão no destino]") : "  [Orphan in destination]");
                g.drawText(texto, r, juce::Justification::centredLeft, true);
            }

            y += itemH;
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (isCatalogMode_) return;
        const int itemH = 26;
        int idx = e.y / itemH;
        if (idx >= 0 && idx < static_cast<int>(linhasExibicao_.size())) {
            const auto& linha = linhasExibicao_[static_cast<size_t>(idx)];
            if (linha.tipo == LinhaExibicao::Tipo::Normal) {
                if (!linha.itemId.empty() && aoAbrirNoGrid) {
                    aoAbrirNoGrid({linha.itemId});
                }
            } else if (linha.tipo == LinhaExibicao::Tipo::Orfao) {
                juce::File targetFile;
                if (destFolder_.isDirectory() && linha.caminhoRelativoOrfao.isNotEmpty()) {
                    targetFile = destFolder_.getChildFile(linha.caminhoRelativoOrfao);
                }
                if (targetFile.existsAsFile()) {
                    targetFile.revealToUser();
                } else if (destFolder_.isDirectory()) {
                    destFolder_.revealToUser();
                }
            }
        }
    }

    std::function<void(const std::set<std::string>&)> aoAbrirNoGrid;

private:
    const matriz::consolidacao::PlanoConsolidacao* plano_ = nullptr;
    std::unordered_map<std::string, matriz::consolidacao::StatusArquivoBackup> statusPorItem_;
    std::vector<matriz::consolidacao::ArquivoOrfao> orfaos_;
    std::vector<LinhaExibicao> linhasExibicao_;
    ModoPrioridade modoPrioridade_ = ModoPrioridade::Nenhum;
    std::vector<ProjetoAberto::ColecaoLink> colecoes_;
    juce::File destFolder_;
    bool isCatalogMode_ = false;
};

class BackupWorkspaceComponent::LegendaStatusComponent : public juce::Component {
public:
    LegendaStatusComponent() {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    std::function<void(PreviaLista::ModoPrioridade)> aoMudarFiltro;

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        auto area = getLocalBounds();
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena - 0.5f)));

        struct ItemLegenda {
            PreviaLista::ModoPrioridade modo;
            juce::Colour cor;
            juce::String texto;
        };

        std::vector<ItemLegenda> itens = {
            { PreviaLista::ModoPrioridade::PriorizarIdenticos, juce::Colour(0xff22c55e), isPt ? juce::String::fromUTF8("Idêntico (SHA-256)") : "Up to date (SHA-256)" },
            { PreviaLista::ModoPrioridade::PriorizarPendentes, juce::Colour(0xffef4444), isPt ? juce::String::fromUTF8("Pendente / Divergente") : "Pending / Modified" },
            { PreviaLista::ModoPrioridade::PriorizarOrfaos, juce::Colour(0xff9ca3af), isPt ? juce::String::fromUTF8("Órfão no destino") : "Orphan in destination" }
        };

        pillRects_.clear();
        int x = 0;
        for (const auto& it : itens) {
            int textW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), it.texto);
            int pillW = 8 + 8 + 5 + textW + 8;
            auto pillRect = juce::Rectangle<int>(x, 0, pillW, area.getHeight());
            pillRects_.push_back({ pillRect, it.modo });

            bool isSelected = (modoAtivo_ == it.modo);

            if (isSelected) {
                g.setColour(tk.acento.withAlpha(0.20f));
                g.fillRoundedRectangle(pillRect.toFloat(), 4.0f);
                g.setColour(tk.acento);
                g.drawRoundedRectangle(pillRect.toFloat().reduced(0.5f), 4.0f, 1.5f);
            } else {
                g.setColour(tk.painelAlt.withAlpha(0.6f));
                g.fillRoundedRectangle(pillRect.toFloat(), 4.0f);
                g.setColour(tk.borda.withAlpha(0.6f));
                g.drawRoundedRectangle(pillRect.toFloat().reduced(0.5f), 4.0f, 1.0f);
            }

            auto dotRect = juce::Rectangle<float>((float)x + 7.0f, ((float)area.getHeight() - 7.0f) / 2.0f, 7.0f, 7.0f);
            g.setColour(it.cor);
            g.fillEllipse(dotRect);

            g.setColour(isSelected ? tk.textoPrimario : tk.textoSecundario);
            auto textRect = juce::Rectangle<int>(x + 18, 0, textW + 4, area.getHeight());
            g.drawText(it.texto, textRect, juce::Justification::centredLeft, false);

            x += pillW + 8;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (const auto& pr : pillRects_) {
            if (pr.rect.contains(e.getPosition())) {
                if (modoAtivo_ == pr.modo) {
                    modoAtivo_ = PreviaLista::ModoPrioridade::Nenhum;
                } else {
                    modoAtivo_ = pr.modo;
                }
                repaint();
                if (aoMudarFiltro) aoMudarFiltro(modoAtivo_);
                break;
            }
        }
    }

    void resetarFiltro() {
        modoAtivo_ = PreviaLista::ModoPrioridade::Nenhum;
        repaint();
    }

private:
    struct PillInfo {
        juce::Rectangle<int> rect;
        PreviaLista::ModoPrioridade modo;
    };
    std::vector<PillInfo> pillRects_;
    PreviaLista::ModoPrioridade modoAtivo_ = PreviaLista::ModoPrioridade::Nenhum;
};

class BackupWorkspaceComponent::ConfigContainerComponent : public juce::Component {
public:
    explicit ConfigContainerComponent(BackupWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        for (const auto& cartao : cartoes_) {
            g.setColour(tk.painel);
            g.fillRoundedRectangle(cartao.toFloat(), tk.raioMedio);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(cartao.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
        }
    }

    void resized() override {
        const auto& tk = tema();
        cartoes_.clear();

        int largura = getWidth();
        int totalH = getHeight();
        if (largura <= 0 || totalH <= 0) return;

        bool isCatalogMode = (owner_.projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

        const int padCartaoX = 10;
        const int padCartaoY = 8;
        const int alturaControle = 26;
        const int alturaLinhaToggle = 22;
        const int alturaCabecalhoSecao = 20;
        const int numCartoes = isCatalogMode ? 2 : 4;
        const int gapCartoes = 8;
        const int espacosTotais = (numCartoes - 1) * gapCartoes;
        const int availableForCards = totalH - espacosTotais;

        int card1H = 0, card2H = 0, card3H = 0, card4H = 0;

        if (isCatalogMode) {
            int minH2 = alturaCabecalhoSecao + 2 + 36 + 4 + alturaControle + 2 + 20 + padCartaoY * 2;
            int minH4 = alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 2) + padCartaoY * 2;
            int minTotal = minH2 + minH4;

            if (availableForCards >= minTotal) {
                int extra = availableForCards - minTotal;
                int extra2 = static_cast<int>(extra * 0.55f);
                int extra4 = extra - extra2;
                card2H = minH2 + extra2;
                card4H = minH4 + extra4;
            } else {
                float ratio = static_cast<float>(availableForCards) / static_cast<float>(std::max(1, minTotal));
                card2H = std::max(60, static_cast<int>(minH2 * ratio));
                card4H = std::max(40, availableForCards - card2H);
            }
        } else {
            bool temColecoes = (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible());
            bool temEditarSelecao = (owner_.btnEditarSelecao_ && owner_.btnEditarSelecao_->isVisible());
            bool temEditorHierarquia = (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible());
            bool temEditCustomPrefixo = (owner_.editPrefixo_ && owner_.editPrefixo_->isVisible());
            int extraH1 = (temColecoes || temEditarSelecao) ? (4 + alturaControle) : 0;
            int minH1 = alturaCabecalhoSecao + 2 + alturaControle + extraH1 + padCartaoY * 2;
            int minH2 = alturaCabecalhoSecao + 2 + 32 + 4 + alturaControle + 2 + 20 + padCartaoY * 2;
            int minH3 = alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 2) + 3 + alturaControle + (temEditorHierarquia ? (3 + alturaControle) : 0) + (4 + alturaControle) + (temEditCustomPrefixo ? (3 + alturaControle) : 0) + padCartaoY * 2;
            int minH4 = alturaCabecalhoSecao + 2 + (alturaLinhaToggle * 5) + padCartaoY * 2;
            int minTotal = minH1 + minH2 + minH3 + minH4;

            if (availableForCards >= minTotal) {
                int extra = availableForCards - minTotal;
                int extra1 = static_cast<int>(extra * 0.05f);
                int extra2 = static_cast<int>(extra * 0.60f); // DESTINATION: 38% → 60%
                int extra3 = static_cast<int>(extra * 0.08f); // ORGANIZATION: 37% → 8% (mínimo)
                int extra4 = extra - extra1 - extra2 - extra3;
                card1H = minH1 + extra1;
                card2H = minH2 + extra2;
                card3H = minH3 + extra3;
                card4H = minH4 + extra4;
            } else {
                float ratio = static_cast<float>(availableForCards) / static_cast<float>(std::max(1, minTotal));
                card1H = std::max(46, static_cast<int>(minH1 * ratio));
                card2H = std::max(80, static_cast<int>(minH2 * ratio));
                card3H = std::max(80, static_cast<int>(minH3 * ratio));
                card4H = std::max(60, availableForCards - card1H - card2H - card3H);
            }
        }

        int currY = 0;
        auto abrirCartao = [&](int altura) {
            auto cartao = juce::Rectangle<int>(0, currY, largura, altura);
            cartoes_.push_back(cartao);
            currY += altura + gapCartoes;
            return cartao.reduced(padCartaoX, padCartaoY);
        };

        // 1. SOURCE (Collection Mode only)
        if (!isCatalogMode && owner_.comboSource_) {
            auto dentro = abrirCartao(card1H);
            if (owner_.labelSource_) owner_.labelSource_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            dentro.removeFromTop(2);
            owner_.comboSource_->setBounds(dentro.removeFromTop(alturaControle));
            if (owner_.comboColecoes_ && owner_.comboColecoes_->isVisible()) {
                dentro.removeFromTop(4);
                owner_.comboColecoes_->setBounds(dentro.removeFromTop(alturaControle));
            }
            if (owner_.btnEditarSelecao_ && owner_.btnEditarSelecao_->isVisible()) {
                dentro.removeFromTop(4);
                owner_.btnEditarSelecao_->setBounds(dentro.removeFromTop(alturaControle));
            }
        }

        // 2. DESTINATION (Both Collection & Catalog Modes)
        if (owner_.listVaults_) {
            auto dentro = abrirCartao(card2H);
            if (owner_.labelDest_) owner_.labelDest_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            dentro.removeFromTop(2);

            int alturaDestInfo = 20;
            int reservedBottom = alturaControle + 4 + alturaDestInfo;
            int listH = std::max(24, dentro.getHeight() - reservedBottom - 4);
            owner_.listVaults_->setBounds(dentro.removeFromTop(listH));
            dentro.removeFromTop(4);

            // Browse and Google Drive buttons side by side
            {
                auto linhaBrowse = dentro.removeFromTop(alturaControle);
                int btnGdW = 32;
                if (owner_.btnGoogleDriveDest_) {
                    owner_.btnGoogleDriveDest_->setBounds(linhaBrowse.removeFromRight(btnGdW));
                    linhaBrowse.removeFromRight(tk.espacoPequeno);
                }
                if (owner_.btnBrowseVault_) owner_.btnBrowseVault_->setBounds(linhaBrowse);
            }
            dentro.removeFromTop(2);
            if (owner_.labelDestInfo_) owner_.labelDestInfo_->setBounds(dentro.removeFromTop(std::min(alturaDestInfo, dentro.getHeight())));
        }

        // 3. ORGANIZATION (Collection Mode only)
        if (!isCatalogMode && owner_.comboOrg_) {
            auto dentro = abrirCartao(card3H);
            if (owner_.labelOrg_) owner_.labelOrg_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            dentro.removeFromTop(2);
            if (owner_.togglePreservarEstrutura_) owner_.togglePreservarEstrutura_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (owner_.toggleUsarEstruturaMapa_) owner_.toggleUsarEstruturaMapa_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            dentro.removeFromTop(3);
            owner_.comboOrg_->setBounds(dentro.removeFromTop(alturaControle));
            if (owner_.btnEditarHierarquia_ && owner_.btnEditarHierarquia_->isVisible()) {
                dentro.removeFromTop(3);
                owner_.btnEditarHierarquia_->setBounds(dentro.removeFromTop(alturaControle));
            }
            if (owner_.comboModoPrefixo_) {
                dentro.removeFromTop(4);
                auto linhaPrefixo = dentro.removeFromTop(alturaControle);
                if (owner_.labelPrefixo_) {
                    int labelW = std::min(90, static_cast<int>(linhaPrefixo.getWidth() * 0.35f));
                    owner_.labelPrefixo_->setBounds(linhaPrefixo.removeFromLeft(labelW));
                    linhaPrefixo.removeFromLeft(4);
                }
                owner_.comboModoPrefixo_->setBounds(linhaPrefixo);
                if (owner_.editPrefixo_ && owner_.editPrefixo_->isVisible()) {
                    dentro.removeFromTop(3);
                    owner_.editPrefixo_->setBounds(dentro.removeFromTop(alturaControle));
                }
            }
        }

        // 4. OPTIONS (Both Collection & Catalog Modes)
        if (owner_.toggleVerificarChecksum_) {
            int lastCardH = std::max(40, totalH - currY);
            auto dentro = abrirCartao(lastCardH);
            if (owner_.labelOpcoes_) owner_.labelOpcoes_->setBounds(dentro.removeFromTop(alturaCabecalhoSecao));
            dentro.removeFromTop(2);
            owner_.toggleVerificarChecksum_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (owner_.toggleGerarCatalogo_) owner_.toggleGerarCatalogo_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
            if (!isCatalogMode) {
                if (owner_.toggleEmbutirMetadados_) {
                    owner_.toggleEmbutirMetadados_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
                }
                if (owner_.toggleAutoResolverConflitos_) {
                    owner_.toggleAutoResolverConflitos_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
                }
                if (owner_.toggleForcarRebackup_) {
                    owner_.toggleForcarRebackup_->setBounds(dentro.removeFromTop(alturaLinhaToggle));
                }
            }
        }

        alturaCalculada_ = cartoes_.empty() ? totalH : cartoes_.back().getBottom();
        repaint();
    }

    int calcularAlturaNecessaria() const {
        return alturaCalculada_;
    }

private:
    BackupWorkspaceComponent& owner_;
    std::vector<juce::Rectangle<int>> cartoes_;
    int alturaCalculada_ = 500;
};

class BackupWorkspaceComponent::CatalogBackupContainerComponent : public juce::Component {
public:
    CatalogBackupContainerComponent(BackupWorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        bool isLight = (tk.fundo.getBrightness() > 0.5f);
        juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
        g.fillAll(bg);
        bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

        int y = 20;
        int w = getWidth() - 32;

        // 1. Global Backup Summary (Top 4 KPI Cards)
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("RESUMO GLOBAL DO BACKUP") : "GLOBAL BACKUP SUMMARY", 16, y, w, 22, juce::Justification::left);
        y += 28;

        int cardW = (w - 36) / 4;
        int cardH = 70;
        int cx = 16;

        auto drawCard = [&](const juce::String& title, const juce::String& val, const juce::String& sub, juce::Colour color) {
            auto r = juce::Rectangle<int>(cx, y, cardW, cardH);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            auto strip = r.removeFromLeft(4);
            g.setColour(color);
            g.fillRoundedRectangle(strip.toFloat(), 3.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);

            auto inner = r.reduced(10, 6);
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(title, inner.removeFromTop(14), juce::Justification::left);

            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
            g.drawText(val, inner.removeFromTop(20), juce::Justification::left);

            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawText(sub, inner, juce::Justification::left);

            cx += cardW + 12;
        };

        drawCard(isPt ? juce::String::fromUTF8("DADOS TOTAIS") : "TOTAL DATA",
                 juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes),
                 juce::String(owner_.catalogBackupTotal_.totalAssets) + (isPt ? juce::String::fromUTF8(" itens") : " assets"), juce::Colour(0xff3b82f6));
        drawCard(isPt ? juce::String::fromUTF8("COM BACKUP") : "BACKED UP",
                 juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes),
                 isPt ? juce::String::fromUTF8("100% protegido") : "100% protected", juce::Colour(0xff10b981));
        drawCard(isPt ? juce::String::fromUTF8("AUSENTES") : "MISSING",
                 "0 B", isPt ? juce::String::fromUTF8("0 itens ausentes") : "0 assets missing", juce::Colour(0xff64748b));
        drawCard(isPt ? juce::String::fromUTF8("REQUER ATENÇÃO") : "NEEDS ATTENTION",
                 juce::String(owner_.catalogBackupTotal_.needsAttention),
                 isPt ? juce::String::fromUTF8("itens para revisar") : "items need review",
                 owner_.catalogBackupTotal_.needsAttention > 0 ? juce::Colour(0xfff97316) : tk.textoSecundario);

        y += cardH + 24;

        // 2. Collections Overview Section Header
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("STATUS DO BACKUP DAS COLEÇÕES") : "COLLECTIONS BACKUP STATUS", 16, y, w, 22, juce::Justification::left);
        y += 28;

        // Table Header
        auto headerRect = juce::Rectangle<int>(16, y, w, 28);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(headerRect.toFloat(), 4.0f);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));

        int col1W = 260;
        int col2W = 120;
        int col3W = 110;
        int col4W = 120;
        
        auto hInner = headerRect.reduced(12, 0);
        g.drawText(isPt ? juce::String::fromUTF8("COLEÇÃO") : "COLLECTION", hInner.removeFromLeft(col1W), juce::Justification::centredLeft);
        g.drawText(isPt ? juce::String::fromUTF8("TAMANHO EM DISCO") : "STORAGE SIZE", hInner.removeFromLeft(col2W), juce::Justification::centredLeft);
        g.drawText(isPt ? juce::String::fromUTF8("ITENS") : "ASSETS", hInner.removeFromLeft(col3W), juce::Justification::centredLeft);
        g.drawText("STATUS", hInner.removeFromLeft(col4W), juce::Justification::centredLeft);
        g.drawText(isPt ? juce::String::fromUTF8("LOCALIZAÇÃO") : "LOCATION", hInner, juce::Justification::centredLeft);

        y += 32;

        if (owner_.catalogBackupItems_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(isPt ? juce::String::fromUTF8("Nenhuma coleção vinculada a este catálogo ainda.") : "No collections linked to this catalog yet.", 16, y, w, 30, juce::Justification::left);
            return;
        }

        // Collection Rows
        for (const auto& item : owner_.catalogBackupItems_) {
            auto rowRect = juce::Rectangle<int>(16, y, w, 36);
            g.setColour(tk.painel);
            g.fillRoundedRectangle(rowRect.toFloat(), 4.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(rowRect.toFloat(), 4.0f, 1.0f);

            auto rInner = rowRect.reduced(12, 0);
            
            // Name
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.drawText(item.name, rInner.removeFromLeft(col1W), juce::Justification::centredLeft, true);

            // Size
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::File::descriptionOfSizeInBytes(item.sizeBytes), rInner.removeFromLeft(col2W), juce::Justification::centredLeft);

            // Assets
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::String(item.totalAssets), rInner.removeFromLeft(col3W), juce::Justification::centredLeft);

            // Status Badge
            auto badgeArea = rInner.removeFromLeft(col4W);
            auto badge = juce::Rectangle<int>(badgeArea.getX(), badgeArea.getY() + 8, 70, 20);
            juce::Colour badgeCol = (item.status == "READY") ? tk.estadoQcOk : (item.status == "WARNING" ? tk.alerta : tk.perigo);
            g.setColour(badgeCol);
            g.fillRoundedRectangle(badge.toFloat(), 3.0f);
            g.setColour(tk.textoSobreAcento);
            g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
            juce::String statusTxt = (item.status == "READY") ? (isPt ? juce::String::fromUTF8("PRONTO") : "READY") : item.status;
            g.drawText(statusTxt, badge, juce::Justification::centred);

            // Path
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(item.path, rInner, juce::Justification::centredLeft, true);

            y += 40;
        }

        // 3. Catalog Total Row
        y += 8;
        auto totalRow = juce::Rectangle<int>(16, y, w, 40);
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(totalRow.toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(totalRow.toFloat(), 4.0f, 1.5f);

        auto tInner = totalRow.reduced(12, 0);
        g.setColour(tk.acento);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("TOTAL DO CATÁLOGO") : "CATALOG TOTAL", tInner.removeFromLeft(col1W), juce::Justification::centredLeft);

        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(juce::File::descriptionOfSizeInBytes(owner_.catalogBackupTotal_.sizeBytes), tInner.removeFromLeft(col2W), juce::Justification::centredLeft);
        g.drawText(juce::String(owner_.catalogBackupTotal_.totalAssets), tInner.removeFromLeft(col3W), juce::Justification::centredLeft);

        auto badgeArea = tInner.removeFromLeft(col4W);
        auto badge = juce::Rectangle<int>(badgeArea.getX(), badgeArea.getY() + 9, 70, 22);
        g.setColour(tk.estadoQcOk);
        g.fillRoundedRectangle(badge.toFloat(), 3.0f);
        g.setColour(tk.textoSobreAcento);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(isPt ? juce::String::fromUTF8("PRONTO") : "READY", badge, juce::Justification::centred);
    }

    void recalculateHeight() {
        int h = 260 + static_cast<int>(owner_.catalogBackupItems_.size()) * 40 + 80;
        setSize(getParentWidth(), std::max(h, getParentHeight()));
    }

private:
    BackupWorkspaceComponent& owner_;
};

void BackupWorkspaceComponent::carregarColecoesBackupCatalogo() {
    catalogBackupItems_.clear();
    catalogBackupTotal_ = CatalogBackupItem{};
    catalogBackupTotal_.name = "CATALOG TOTAL";

    auto colecoes = projeto_.listarColecoesLinkadas();
    for (const auto& c : colecoes) {
        CatalogBackupItem item;
        item.name = c.nome;
        item.path = c.caminhoProjeto;

        if (!c.valido) {
            item.status = "OFFLINE";
        } else {
            item.sizeBytes = c.totalBytes;
            item.totalAssets = c.totalAssets;
            item.status = "READY";

            juce::File colDir(c.caminhoProjeto);
            juce::File dbFile = matriz::model::Project::resolverPastaProjeto(colDir).getChildFile("registro.sqlite");
            if (dbFile.existsAsFile()) {
                try {
                    matriz::db::Database colDb(dbFile.getFullPathName().toStdString());
                    auto stmtRev = colDb.prepare(
                        "SELECT COUNT(id) FROM item "
                        "WHERE (ano IS NULL OR ano = 0) "
                        "   OR (source_media IS NULL OR TRIM(source_media) = '') "
                        "   OR (collection_type IS NULL OR TRIM(collection_type) = '')");
                    if (stmtRev.step()) {
                        item.needsAttention = static_cast<uint64_t>(stmtRev.columnInt(0));
                        if (item.needsAttention > 0 && item.status == "READY") {
                            item.status = "WARNING";
                        }
                    }
                } catch (...) {}
            }
        }

        catalogBackupTotal_.sizeBytes += item.sizeBytes;
        catalogBackupTotal_.totalAssets += item.totalAssets;
        catalogBackupTotal_.needsAttention += item.needsAttention;
        catalogBackupItems_.push_back(item);
    }
}

void BackupWorkspaceComponent::carregarOpcoesContent() {
    opcoesContent_.clear();

    // Standard CONTENT taxonomy from metadata sheets across all categories
    static const std::vector<std::string> kPadroesContent = {
        // Audio
        "Album", "EP", "Single", "Compilation", "Soundtrack", "Stems", "Multitracks",
        "Sample Pack", "Sample", "Preset", "DAW Session", "Field Recording", "Sound FX", "MIDI",
        "Artist Catalog", "Artist Backup",
        // Video
        "Raw Footage", "Home Video", "Music Video", "Film", "Documentary",
        "Corporate Video", "Commercial", "Live Performance", "NLE Project", "Social Media Video",
        // Image
        "Photo", "Artwork", "Album Cover", "Poster", "Press / Promotional", "Image Edit Project",
        "Graphics", "Logo", "3D",
        // Docs
        "Documentation", "Book", "Contract", "Manual", "Report", "Reference", "Technical Documentation",
        "Spreadsheet", "Planilha"
    };

    std::map<std::string, int> contagens;
    int semConteudo = 0;

    try {
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT COALESCE(NULLIF(TRIM(i.collection_type), ''), "
            "       (SELECT TRIM(valor) FROM item_campo WHERE item_id = i.id AND campo_id = 'collection_type' AND valor IS NOT NULL AND TRIM(valor) <> '' LIMIT 1), "
            "       '') AS ctype, COUNT(DISTINCT i.id) "
            "FROM item i "
            "WHERE i.projeto_id = ? "
            "GROUP BY ctype");
        stmt.bind(1, matriz::db::Value::of(projeto_.projeto().projetoId()));
        while (stmt.step()) {
            std::string ct = stmt.columnText(0);
            int cnt = stmt.columnInt(1);
            if (ct.empty()) {
                semConteudo += cnt;
            } else {
                contagens[ct] += cnt;
            }
        }
    } catch (...) {}

    // 1. Add all standard options from metadata sheets
    std::set<std::string> jaAdicionados;
    for (const auto& opt : kPadroesContent) {
        int cnt = 0;
        auto it = contagens.find(opt);
        if (it != contagens.end()) cnt = it->second;
        opcoesContent_.push_back({opt, juce::String(opt), cnt});
        jaAdicionados.insert(opt);
    }

    // 2. Add any custom options that exist in the database but aren't in the standard list
    for (const auto& [ct, cnt] : contagens) {
        if (jaAdicionados.find(ct) == jaAdicionados.end()) {
            opcoesContent_.push_back({ct, juce::String(ct), cnt});
        }
    }

    // 3. Add "No content defined" / "Sem conteúdo definido"
    juce::String rotuloSem = matriz::i18n::t("backup.sem_conteudo");
    opcoesContent_.push_back({"__empty__", rotuloSem, semConteudo});
}

juce::String BackupWorkspaceComponent::rotuloOpcaoSelecionados() const {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (selectedItemIds_.empty()) {
        return isPt ? juce::String::fromUTF8("Selecionar Arquivos...") : "Select Files...";
    }
    return (isPt ? juce::String::fromUTF8("Selecionar Arquivos (") : "Select Files (")
           + juce::String(static_cast<int>(selectedItemIds_.size())) + ")";
}

void BackupWorkspaceComponent::abrirJanelaSelecionarArquivos() {
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    BackupFileSelectorDialog::exibirModal(projeto_, selectedItemIds_, this, [safeThis](const std::set<std::string>& novosIds) {
        if (!safeThis) return;
        safeThis->selectedItemIds_ = novosIds;
        safeThis->whatOption_ = WhatOption::SelectedAssets;
        if (safeThis->comboSource_) {
            safeThis->comboSource_->changeItemText(3, safeThis->rotuloOpcaoSelecionados());
            safeThis->comboSource_->setSelectedId(3, juce::dontSendNotification);
        }
        if (safeThis->btnEditarSelecao_) {
            safeThis->btnEditarSelecao_->setVisible(true);
        }
        safeThis->atualizarResumo();
        safeThis->resized();
    });
}

BackupWorkspaceComponent::BackupWorkspaceComponent(ProjetoAberto& projeto, const std::set<std::string>& selectedItemIds)
    : projeto_(projeto), selectedItemIds_(selectedItemIds)
{
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    carregarDestinosBackup();
    carregarOpcoesContent();

    const auto& tk = tema();

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    // === TITLE ===
    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText(isCatalogMode ? matriz::i18n::t("backup.titulo_catalogo") : matriz::i18n::t("backup.titulo_configuracao"), juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelTitulo_);

    // === CONFIG CONTAINER & VIEWPORT ===
    configViewport_ = std::make_unique<juce::Viewport>();
    configViewport_->setScrollBarsShown(true, false);
    configContainer_ = std::make_unique<ConfigContainerComponent>(*this);
    configViewport_->setViewedComponent(configContainer_.get(), false);
    addAndMakeVisible(*configViewport_);

    // === SELECTION section ===
    labelSource_ = std::make_unique<juce::Label>();
    labelSource_->setText(matriz::i18n::t("backup.secao_origem"), juce::dontSendNotification);
    labelSource_->setTooltip(isPt ? juce::String::fromUTF8("Seleção dos itens para o backup") : "Selection of assets to backup");
    labelSource_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelSource_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelSource_);

    comboSource_ = std::make_unique<juce::ComboBox>();
    comboSource_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboSource_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboSource_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboSource_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboSource_->setTooltip(isPt ? juce::String::fromUTF8("Escolha quais itens incluir nesta execução do backup") : "Choose which assets to include in this backup run");
    comboSource_->addItem(isCatalogMode ? matriz::i18n::t("backup.origem_todos_catalogo") : matriz::i18n::t("backup.origem_todos_projeto"), 1);
    comboSource_->addItem(matriz::i18n::t("backup.origem_intake"), 2);
    comboSource_->addItem(rotuloOpcaoSelecionados(), 3);
    comboSource_->addItem(matriz::i18n::t("backup.origem_sem_backup"), 4);
    comboSource_->addItem(matriz::i18n::t("backup.origem_de_conteudo"), 5);
    comboSource_->setSelectedId(1, juce::dontSendNotification);
    whatOption_ = WhatOption::Everything;
    comboSource_->onChange = [this] {
        int id = comboSource_->getSelectedId();
        if (id == 3) {
            whatOption_ = WhatOption::SelectedAssets;
            if (comboColecoes_) comboColecoes_->setVisible(false);
            if (btnEditarSelecao_) btnEditarSelecao_->setVisible(true);
            resized();
            abrirJanelaSelecionarArquivos();
            return;
        }
        whatOption_ = static_cast<WhatOption>(id - 1);
        if (comboColecoes_) comboColecoes_->setVisible(id == 5);
        if (btnEditarSelecao_) btnEditarSelecao_->setVisible(id == 3);
        resized();
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*comboSource_);

    btnEditarSelecao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS...") : "SELECT FILES...");
    btnEditarSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnEditarSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnEditarSelecao_->setTooltip(isPt ? juce::String::fromUTF8("Abrir janela para selecionar arquivos do backup") : "Open window to choose backup files");
    btnEditarSelecao_->onClick = [this] {
        abrirJanelaSelecionarArquivos();
    };
    configContainer_->addChildComponent(*btnEditarSelecao_);
    if (!selectedItemIds_.empty()) {
        btnEditarSelecao_->setVisible(true);
    }

    comboColecoes_ = std::make_unique<juce::ComboBox>();
    comboColecoes_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboColecoes_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboColecoes_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboColecoes_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    for (size_t i = 0; i < opcoesContent_.size(); ++i)
        comboColecoes_->addItem(opcoesContent_[i].rotulo + " (" + juce::String(opcoesContent_[i].contagem) + ")", static_cast<int>(i + 1));
    if (!opcoesContent_.empty()) comboColecoes_->setSelectedId(1, juce::dontSendNotification);
    comboColecoes_->onChange = [this] {
        selectedContentIdx_ = comboColecoes_->getSelectedItemIndex();
        atualizarResumo();
    };
    configContainer_->addChildComponent(*comboColecoes_);

    // === DESTINATION section ===
    labelDest_ = std::make_unique<juce::Label>();
    labelDest_->setText(matriz::i18n::t("backup.secao_destino"), juce::dontSendNotification);
    labelDest_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelDest_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelDest_);

    listVaults_ = std::make_unique<juce::ListBox>();
    listVaults_->setModel(this);
    listVaults_->setColour(juce::ListBox::backgroundColourId, tk.painelAlt);
    listVaults_->setColour(juce::ListBox::outlineColourId, tk.borda);
    listVaults_->setRowHeight(32);
    configContainer_->addAndMakeVisible(*listVaults_);

    btnBrowseVault_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("+ Adicionar Destino...") : "+ Add Destination...");
    btnBrowseVault_->setTooltip(isPt ? juce::String::fromUTF8("Criar ou vincular um destino de backup") : "Create or link a backup destination folder");
    aplicarEstiloBotao(*btnBrowseVault_, false);
    btnBrowseVault_->onClick = [this, isPt] {
        auto chooser = std::make_shared<juce::FileChooser>(
            isPt ? juce::String::fromUTF8("Selecione a pasta para o novo Destino")
                 : "Select folder for the new Destination");
        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis, chooser](const juce::FileChooser& fc) {
                                  if (!safeThis) return;
                                  juce::File folder = fc.getResult();
                                  if (folder == juce::File()) return;
                                  safeThis->criarNovoClone(folder);
                              });
    };
    configContainer_->addAndMakeVisible(*btnBrowseVault_);

    // Botão Google Drive como destino
    bool isPtGDest = isPt;
    btnGoogleDriveDest_ = std::make_unique<GoogleDriveIconButton>();
    btnGoogleDriveDest_->setTooltip(isPtGDest
        ? juce::String::fromUTF8("Criar destino no Google Drive (requer Google Drive para Desktop)")
        : "Create destination in Google Drive (requires Google Drive for Desktop)");
    btnGoogleDriveDest_->onClick = [this, isPtGDest] {
        auto gdFolder = detectarPastaGoogleDrive();
        if (gdFolder.isDirectory()) {
            auto chooser = std::make_shared<juce::FileChooser>(
                isPtGDest ? juce::String::fromUTF8("Selecione ou crie uma subpasta no Google Drive para o Destino")
                          : "Select or create a subfolder in Google Drive for the Destination",
                gdFolder);
            juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [safeThis, chooser](const juce::FileChooser& fc) {
                                      if (!safeThis) return;
                                      juce::File subpasta = fc.getResult();
                                      if (subpasta == juce::File()) return;
                                      safeThis->criarNovoClone(subpasta);
                                  });
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Google Drive",
                isPtGDest
                    ? juce::String::fromUTF8("Pasta do Google Drive não encontrada.\nInstale o Google Drive para Desktop em drive.google.com/drive/download")
                    : "Google Drive folder not found.\nInstall Google Drive for Desktop from drive.google.com/drive/download");
        }
    };
    configContainer_->addAndMakeVisible(*btnGoogleDriveDest_);

    labelDestInfo_ = std::make_unique<juce::Label>();
    labelDestInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
    labelDestInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    labelDestInfo_->setMinimumHorizontalScale(0.85f);
    configContainer_->addAndMakeVisible(*labelDestInfo_);

    // === FOLDER STRUCTURE section ===
    labelOrg_ = std::make_unique<juce::Label>();
    labelOrg_->setText(matriz::i18n::t("backup.secao_organizacao"), juce::dontSendNotification);
    labelOrg_->setTooltip(isPt ? juce::String::fromUTF8("Defina como as pastas e arquivos serão estruturados no backup") : "Define how folders and files will be structured in the backup");
    labelOrg_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOrg_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelOrg_);

    togglePreservarEstrutura_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.preservar_estrutura"));
    togglePreservarEstrutura_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    togglePreservarEstrutura_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    togglePreservarEstrutura_->setTooltip(matriz::i18n::t("backup.preservar_estrutura_dica"));
    togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);

    toggleUsarEstruturaMapa_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.usar_estrutura_mapa"));
    toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleUsarEstruturaMapa_->setTooltip(matriz::i18n::t("backup.usar_estrutura_mapa_dica"));
    toggleUsarEstruturaMapa_->setToggleState(true, juce::dontSendNotification);

    auto atualizarEstadoOrganizacao = [this] {
        bool usaOriginal = togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState();
        if (comboOrg_) comboOrg_->setEnabled(!usaOriginal);
        if (btnEditarHierarquia_) {
            bool isCustom = (comboOrg_ && comboOrg_->getSelectedId() == 5);
            btnEditarHierarquia_->setVisible(isCustom && !usaOriginal);
            btnEditarHierarquia_->setEnabled(isCustom && !usaOriginal);
        }
        resized();
        atualizarResumo();
    };

    togglePreservarEstrutura_->onClick = [this, atualizarEstadoOrganizacao] {
        if (togglePreservarEstrutura_->getToggleState()) {
            if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(false, juce::dontSendNotification);
        }
        atualizarEstadoOrganizacao();
        if (togglePreservarEstrutura_->getToggleState() && !plano_.podeConsolidar() && !plano_.nomesEmConflito.empty()) {
            mostrarPopupConflitoPreservacao();
        }
    };

    toggleUsarEstruturaMapa_->onClick = [this, atualizarEstadoOrganizacao] {
        if (toggleUsarEstruturaMapa_->getToggleState()) {
            if (togglePreservarEstrutura_) togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);
            if (comboOrg_) comboOrg_->setSelectedId(1, juce::dontSendNotification);
        }
        atualizarEstadoOrganizacao();
    };

    configContainer_->addAndMakeVisible(*togglePreservarEstrutura_);
    configContainer_->addAndMakeVisible(*toggleUsarEstruturaMapa_);

    comboOrg_ = std::make_unique<juce::ComboBox>();
    comboOrg_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboOrg_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboOrg_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboOrg_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboOrg_->addItem(matriz::i18n::t("backup.manter_organizacao"), 1);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia") : "By media type", 2);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por ano") : "By year", 3);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia + ano") : "By media type + year", 4);
    comboOrg_->addItem(isPt ? juce::String::fromUTF8("Personalizado (editor visual)") : "Custom (visual editor)", 5);
    comboOrg_->setSelectedId(1, juce::dontSendNotification);
    comboOrg_->setEnabled(true);
    comboOrg_->setTooltip("Choose directory naming/organization structure pattern");
    comboOrg_->onChange = [this, atualizarEstadoOrganizacao] {
        int id = comboOrg_->getSelectedId();
        if (id == 1) {
            if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(true, juce::dontSendNotification);
        } else {
            if (toggleUsarEstruturaMapa_) toggleUsarEstruturaMapa_->setToggleState(false, juce::dontSendNotification);
        }
        if (togglePreservarEstrutura_) togglePreservarEstrutura_->setToggleState(false, juce::dontSendNotification);
        atualizarEstadoOrganizacao();
    };
    configContainer_->addAndMakeVisible(*comboOrg_);

    hierarquiaCustom_ = matriz::consolidacao::hierarquiaPadrao();
    btnEditarHierarquia_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("ABRIR EDITOR VISUAL") : "OPEN VISUAL EDITOR");
    btnEditarHierarquia_->setColour(juce::TextButton::buttonColourId, tk.acento);
    btnEditarHierarquia_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    btnEditarHierarquia_->setTooltip("Open visual interactive editor to design folder naming tree");
    btnEditarHierarquia_->onClick = [this] {
        new HierarquiaEditorWindow(hierarquiaCustom_, [this](const matriz::consolidacao::HierarquiaBackup& h) {
            hierarquiaCustom_ = h;
            atualizarResumo();
        });
    };
    configContainer_->addChildComponent(*btnEditarHierarquia_);

    prefixoAuto_ = "BKR";
    {
        auto stmtPref = projeto_.projeto().registro().prepare("SELECT prefixo_nomenclatura FROM projeto LIMIT 1");
        if (stmtPref.step() && !stmtPref.columnIsNull(0)) {
            juce::String val = stmtPref.columnText(0);
            if (val.isNotEmpty())
                prefixoAuto_ = val;
        }
    }
    prefixoCustomizado_ = prefixoAuto_;

    labelPrefixo_ = std::make_unique<juce::Label>();
    labelPrefixo_->setText(matriz::i18n::t("backup.prefixo_arquivos"), juce::dontSendNotification);
    labelPrefixo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelPrefixo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelPrefixo_);

    comboModoPrefixo_ = std::make_unique<juce::ComboBox>();
    comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_nenhum"), 1);
    comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_auto").replace("{prefix}", prefixoAuto_), 2);
    comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_custom"), 3);
    comboModoPrefixo_->setSelectedId(1, juce::dontSendNotification); // Default: NO PREFIX (1)
    modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
    comboModoPrefixo_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboModoPrefixo_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboModoPrefixo_->setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboModoPrefixo_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboModoPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_modo_nenhum_dica"));
    comboModoPrefixo_->onChange = [this] {
        int id = comboModoPrefixo_->getSelectedId();
        if (id == 1) {
            modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Nenhum;
            if (editPrefixo_) editPrefixo_->setVisible(false);
        } else if (id == 2) {
            modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Auto;
            if (editPrefixo_) editPrefixo_->setVisible(false);
        } else if (id == 3) {
            modoPrefixo_ = matriz::consolidacao::ModoPrefixoArquivo::Custom;
            if (editPrefixo_) editPrefixo_->setVisible(true);
        }
        if (configContainer_) configContainer_->resized();
        atualizarResumo();
    };
    configContainer_->addAndMakeVisible(*comboModoPrefixo_);

    editPrefixo_ = std::make_unique<juce::TextEditor>();
    editPrefixo_->setText(prefixoCustomizado_, juce::dontSendNotification);
    editPrefixo_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    editPrefixo_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    editPrefixo_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    editPrefixo_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
    editPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_arquivos_dica"));
    editPrefixo_->setVisible(false);
    editPrefixo_->onTextChange = [this] {
        prefixoCustomizado_ = editPrefixo_->getText().trim();
        atualizarResumo();
    };
    configContainer_->addChildComponent(*editPrefixo_);

    // === OPTIONS section ===
    labelOpcoes_ = std::make_unique<juce::Label>();
    labelOpcoes_->setText(matriz::i18n::t("backup.secao_opcoes"), juce::dontSendNotification);
    labelOpcoes_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelOpcoes_->setColour(juce::Label::textColourId, tk.textoSecundario);
    configContainer_->addAndMakeVisible(*labelOpcoes_);

    toggleVerificarChecksum_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.verificar_checksum"));
    toggleVerificarChecksum_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleVerificarChecksum_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleVerificarChecksum_->setTooltip("Enable reading back copied files to verify SHA-256 integrity");
    toggleVerificarChecksum_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleVerificarChecksum_);

    toggleGerarCatalogo_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.gerar_banco_sqlite"));
    toggleGerarCatalogo_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleGerarCatalogo_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleGerarCatalogo_->setTooltip("Enable SQLite database summary file generation in target folder");
    toggleGerarCatalogo_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleGerarCatalogo_);

    toggleEmbutirMetadados_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.embutir_metadados"));
    toggleEmbutirMetadados_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleEmbutirMetadados_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleEmbutirMetadados_->setTooltip("Enable embedding Dublin Core and technical tags directly into media headers");
    toggleEmbutirMetadados_->setToggleState(true, juce::dontSendNotification);
    configContainer_->addAndMakeVisible(*toggleEmbutirMetadados_);

    toggleAutoResolverConflitos_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.auto_resolver_conflitos"));
    toggleAutoResolverConflitos_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleAutoResolverConflitos_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleAutoResolverConflitos_->setTooltip(matriz::i18n::t("backup.auto_resolver_conflitos"));
    toggleAutoResolverConflitos_->setToggleState(true, juce::dontSendNotification);
    toggleAutoResolverConflitos_->onClick = [this] { atualizarResumo(); };
    configContainer_->addAndMakeVisible(*toggleAutoResolverConflitos_);

    toggleForcarRebackup_ = std::make_unique<juce::ToggleButton>(matriz::i18n::t("backup.forcar_rebackup"));
    toggleForcarRebackup_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
    toggleForcarRebackup_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    toggleForcarRebackup_->setTooltip(matriz::i18n::t("backup.forcar_rebackup_dica"));
    toggleForcarRebackup_->setToggleState(false, juce::dontSendNotification);
    toggleForcarRebackup_->onClick = [this] { atualizarResumo(); };
    configContainer_->addAndMakeVisible(*toggleForcarRebackup_);

    // === PREVIEW section ===
    labelResumo_ = std::make_unique<juce::Label>();
    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*labelResumo_);

    barraLegenda_ = std::make_unique<LegendaStatusComponent>();
    barraLegenda_->aoMudarFiltro = [this](PreviaLista::ModoPrioridade modo) {
        if (listPrevia_) listPrevia_->definirModoPrioridade(modo);
    };
    addAndMakeVisible(*barraLegenda_);

    listPrevia_ = std::make_unique<PreviaLista>();
    listPrevia_->aoAbrirNoGrid = [this](const std::set<std::string>& ids) {
        if (aoAbrirNoGrid) aoAbrirNoGrid(ids);
    };
    listPreviaViewport_ = std::make_unique<juce::Viewport>();
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
    addAndMakeVisible(*listPreviaViewport_);

    // === PROGRESS ===
    barraProgresso_ = std::make_unique<juce::ProgressBar>(progressoValor_);
    addChildComponent(*barraProgresso_);

    labelProgressoStatus_ = std::make_unique<juce::Label>();
    labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    labelProgressoStatus_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelProgressoStatus_->setJustificationType(juce::Justification::centred);
    addChildComponent(*labelProgressoStatus_);

    // === BUTTONS ===
    btnStartBackup_ = std::make_unique<juce::TextButton>(matriz::i18n::t("backup.btn_fazer_backup"));
    aplicarEstiloBotao(*btnStartBackup_, true);
    btnStartBackup_->setTooltip("Begin incremental backup process (sends only pending/modified files)");
    btnStartBackup_->onClick = [this] {
        if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState() &&
            !plano_.podeConsolidar() && !plano_.nomesEmConflito.empty()) {
            mostrarPopupConflitoPreservacao();
            return;
        }
        executarBackupAcao(false);
    };
    addAndMakeVisible(*btnStartBackup_);

    btnSyncDestino_ = std::make_unique<juce::TextButton>("BACKUP SYNC...");
    aplicarEstiloBotao(*btnSyncDestino_, false);
    btnSyncDestino_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnSyncDestino_->setTooltip(isPt ? juce::String::fromUTF8("Comparar destino ativo com outro disco/pasta para sincronização manual")
                                     : "Compare active destination with another drive/folder for manual sync review");
    btnSyncDestino_->onClick = [this] { iniciarSyncComOutroDestino(); };
    addAndMakeVisible(*btnSyncDestino_);

    btnPublishHtml_ = std::make_unique<juce::TextButton>(matriz::i18n::t("backup.btn_publicar_html"));
    aplicarEstiloBotao(*btnPublishHtml_, false);
    btnPublishHtml_->setColour(juce::TextButton::textColourOffId, tk.acento);
    btnPublishHtml_->setTooltip("Publish static HTML website preview / catalog");
    btnPublishHtml_->onClick = [this] { publicarHtml(); };
    addChildComponent(*btnPublishHtml_);

    btnExportZip_ = std::make_unique<juce::TextButton>(juce::String::formatted(matriz::i18n::t("backup.exportar_zip").toRawUTF8(), 0));
    aplicarEstiloBotao(*btnExportZip_, false);
    btnExportZip_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff0077ff));
    btnExportZip_->setTooltip(isPt ? juce::String::fromUTF8("Exportar itens marcados com K como arquivo ZIP")
                                   : "Export assets marked with K as ZIP package");
    btnExportZip_->onClick = [this] {
        ExportZipDialog::exibirModal(projeto_);
    };
    addChildComponent(*btnExportZip_);

    btnLimparZip_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("×"));
    aplicarEstiloBotao(*btnLimparZip_, false);
    btnLimparZip_->setTooltip(matriz::i18n::t("backup.limpar_zip_dica"));
    btnLimparZip_->onClick = [this] {
        projeto_.limparMarcacoes(ProjetoAberto::TipoMarcacao::Zip);
        atualizarBotoesListas();
    };
    addChildComponent(*btnLimparZip_);

    btnSendToPrint_ = std::make_unique<juce::TextButton>(juce::String::formatted(matriz::i18n::t("backup.enviar_print").toRawUTF8(), 0));
    aplicarEstiloBotao(*btnSendToPrint_, false);
    btnSendToPrint_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff6b00));
    btnSendToPrint_->setTooltip(isPt ? juce::String::fromUTF8("Enviar itens marcados com P para impressão (em breve)")
                                     : "Send assets marked with P to print (coming soon)");
    btnSendToPrint_->setEnabled(false);
    addChildComponent(*btnSendToPrint_);

    btnLimparPrint_ = std::make_unique<juce::TextButton>(juce::String::fromUTF8("×"));
    aplicarEstiloBotao(*btnLimparPrint_, false);
    btnLimparPrint_->setTooltip(matriz::i18n::t("backup.limpar_print_dica"));
    btnLimparPrint_->onClick = [this] {
        projeto_.limparMarcacoes(ProjetoAberto::TipoMarcacao::Print);
        atualizarBotoesListas();
    };
    addChildComponent(*btnLimparPrint_);

    btnCancelarExecucao_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
    aplicarEstiloBotao(*btnCancelarExecucao_, false);
    btnCancelarExecucao_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnCancelarExecucao_->setTooltip("Abort current backup task safely");
    btnCancelarExecucao_->onClick = [this] {
        if (cancelamento_) cancelamento_->pedir();
    };
    btnCancelarExecucao_->setVisible(false);
    addAndMakeVisible(*btnCancelarExecucao_);

    btnDone_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("CONCLUÍDO") : "DONE");
    aplicarEstiloBotao(*btnDone_, false);
    btnDone_->setTooltip("Return to Home screen");
    btnDone_->onClick = [this] {
        if (aoConcluir) aoConcluir();
        else if (aoVoltarHome) aoVoltarHome();
    };
    addChildComponent(*btnDone_);

    btnOpenCatalog_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("ABRIR PASTA DO BACKUP") : "OPEN BACKUP FOLDER");
    aplicarEstiloBotao(*btnOpenCatalog_, true);
    btnOpenCatalog_->setTooltip(isPt ? juce::String::fromUTF8("Abrir pasta do backup no Finder") : "Open backup folder in Finder");
    btnOpenCatalog_->onClick = [this] {
        juce::File pastaAlvo = matriz::model::normalizarParaRaizDestino(resolvedDestFolder_);
        if (!pastaAlvo.exists()) pastaAlvo = projeto_.projeto().raiz();
        if (!pastaAlvo.exists()) pastaAlvo = projeto_.projeto().pastaMedia();
        if (!pastaAlvo.exists()) pastaAlvo = projeto_.projeto().pasta();
        if (pastaAlvo.exists()) {
            if (!pastaAlvo.startAsProcess()) {
                pastaAlvo.revealToUser();
            }
        }
    };
    addChildComponent(*btnOpenCatalog_);

    btnExportJanela_ = std::make_unique<juce::TextButton>(matriz::i18n::t("backup.btn_exportar_metadados"));
    aplicarEstiloBotao(*btnExportJanela_, false);
    btnExportJanela_->setTooltip(isPt ? juce::String::fromUTF8("Exportar catálogo de metadados como CSV/XLS") : "Export metadata catalog as CSV/XLS");
    btnExportJanela_->onClick = [this] { mostrarJanelaExportar(); };
    addChildComponent(*btnExportJanela_);

    addChildComponent(overlay_);

    EventBus::obterInstancia().registrarListener(this);
    carregarDestinoAtivoInicial();
    atualizarResumo();
    atualizarBotoesListas();
}

BackupWorkspaceComponent::~BackupWorkspaceComponent() {
    EventBus::obterInstancia().removerListener(this);
}

void BackupWorkspaceComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    if (labelTitulo_) {
        labelTitulo_->setText(isCatalogMode ? matriz::i18n::t("backup.titulo_catalogo") : matriz::i18n::t("backup.titulo_configuracao"), juce::dontSendNotification);
        labelTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        labelTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnSyncDestino_) {
        btnSyncDestino_->setButtonText("BACKUP SYNC...");
    }
    if (labelSource_) {
        labelSource_->setText(matriz::i18n::t("backup.secao_origem"), juce::dontSendNotification);
        labelSource_->setTooltip(isPt ? juce::String::fromUTF8("Seleção dos itens para o backup") : "Selection of assets to backup");
        labelSource_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelSource_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (comboSource_) {
        comboSource_->setTooltip(isPt ? juce::String::fromUTF8("Escolha quais itens incluir nesta execução do backup") : "Choose which assets to include in this backup run");
        int sel = comboSource_->getSelectedId();
        comboSource_->clear(juce::dontSendNotification);
        comboSource_->addItem(isCatalogMode ? matriz::i18n::t("backup.origem_todos_catalogo") : matriz::i18n::t("backup.origem_todos_projeto"), 1);
        comboSource_->addItem(matriz::i18n::t("backup.origem_intake"), 2);
        comboSource_->addItem(rotuloOpcaoSelecionados(), 3);
        comboSource_->addItem(matriz::i18n::t("backup.origem_sem_backup"), 4);
        comboSource_->addItem(matriz::i18n::t("backup.origem_de_conteudo"), 5);
        if (sel > 0) comboSource_->setSelectedId(sel, juce::dontSendNotification);

        comboSource_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboSource_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboSource_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboSource_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (btnEditarSelecao_) {
        btnEditarSelecao_->setButtonText(isPt ? juce::String::fromUTF8("SELECIONAR ARQUIVOS...") : "SELECT FILES...");
        btnEditarSelecao_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnEditarSelecao_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }
    if (comboColecoes_) {
        int sel = comboColecoes_->getSelectedId();
        carregarOpcoesContent();
        comboColecoes_->clear(juce::dontSendNotification);
        for (size_t i = 0; i < opcoesContent_.size(); ++i)
            comboColecoes_->addItem(opcoesContent_[i].rotulo + " (" + juce::String(opcoesContent_[i].contagem) + ")", static_cast<int>(i + 1));
        if (sel > 0 && sel <= static_cast<int>(opcoesContent_.size()))
            comboColecoes_->setSelectedId(sel, juce::dontSendNotification);
        else if (!opcoesContent_.empty())
            comboColecoes_->setSelectedId(1, juce::dontSendNotification);

        comboColecoes_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboColecoes_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboColecoes_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboColecoes_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (labelDest_) {
        labelDest_->setText(matriz::i18n::t("backup.secao_destino"), juce::dontSendNotification);
        labelDest_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelDest_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (btnBrowseVault_) {
        btnBrowseVault_->setButtonText(isPt ? juce::String::fromUTF8("+ Adicionar Destino...") : "+ Add Destination...");
        btnBrowseVault_->setTooltip(isPt ? juce::String::fromUTF8("Criar ou vincular um destino de backup") : "Create or link a backup destination folder");
    }
    if (btnGoogleDriveDest_) {
        btnGoogleDriveDest_->setTooltip(isPt
            ? juce::String::fromUTF8("Criar destino no Google Drive (requer Google Drive para Desktop)")
            : "Create destination in Google Drive (requires Google Drive for Desktop)");
    }
    if (listVaults_) {
        listVaults_->setColour(juce::ListBox::backgroundColourId, tk.painelAlt);
        listVaults_->setColour(juce::ListBox::outlineColourId, tk.borda);
        listVaults_->repaint();
    }
    if (labelDestInfo_) {
        labelDestInfo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        labelDestInfo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (labelOrg_) {
        labelOrg_->setText(matriz::i18n::t("backup.secao_organizacao"), juce::dontSendNotification);
        labelOrg_->setTooltip(isPt ? juce::String::fromUTF8("Defina como as pastas e arquivos serão estruturados no backup") : "Define how folders and files will be structured in the backup");
        labelOrg_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelOrg_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (togglePreservarEstrutura_) {
        togglePreservarEstrutura_->setButtonText(matriz::i18n::t("backup.preservar_estrutura"));
        togglePreservarEstrutura_->setTooltip(matriz::i18n::t("backup.preservar_estrutura_dica"));
        togglePreservarEstrutura_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        togglePreservarEstrutura_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleUsarEstruturaMapa_) {
        toggleUsarEstruturaMapa_->setButtonText(matriz::i18n::t("backup.usar_estrutura_mapa"));
        toggleUsarEstruturaMapa_->setTooltip(matriz::i18n::t("backup.usar_estrutura_mapa_dica"));
        toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleUsarEstruturaMapa_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (comboOrg_) {
        int sel = comboOrg_->getSelectedId();
        comboOrg_->clear(juce::dontSendNotification);
        comboOrg_->addItem(matriz::i18n::t("backup.manter_organizacao"), 1);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia") : "By media type", 2);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por ano") : "By year", 3);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Por tipo de mídia + ano") : "By media type + year", 4);
        comboOrg_->addItem(isPt ? juce::String::fromUTF8("Personalizado (editor visual)") : "Custom (visual editor)", 5);
        if (sel > 0) comboOrg_->setSelectedId(sel, juce::dontSendNotification);

        comboOrg_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboOrg_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboOrg_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboOrg_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    }
    if (btnEditarHierarquia_) {
        btnEditarHierarquia_->setButtonText(isPt ? juce::String::fromUTF8("ABRIR EDITOR VISUAL") : "OPEN VISUAL EDITOR");
        btnEditarHierarquia_->setColour(juce::TextButton::buttonColourId, tk.acento);
        btnEditarHierarquia_->setColour(juce::TextButton::textColourOffId, tk.textoSobreAcento);
    }
    if (labelPrefixo_) {
        labelPrefixo_->setText(matriz::i18n::t("backup.prefixo_arquivos"), juce::dontSendNotification);
        labelPrefixo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        labelPrefixo_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (comboModoPrefixo_) {
        int sel = comboModoPrefixo_->getSelectedId();
        comboModoPrefixo_->clear(juce::dontSendNotification);
        comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_nenhum"), 1);
        comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_auto").replace("{prefix}", prefixoAuto_), 2);
        comboModoPrefixo_->addItem(matriz::i18n::t("backup.prefixo_modo_custom"), 3);
        comboModoPrefixo_->setSelectedId(sel > 0 ? sel : 1, juce::dontSendNotification);
        comboModoPrefixo_->setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
        comboModoPrefixo_->setColour(juce::ComboBox::textColourId, juce::Colours::black);
        comboModoPrefixo_->setColour(juce::ComboBox::outlineColourId, tk.borda);
        comboModoPrefixo_->setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
        comboModoPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_modo_nenhum_dica"));
    }
    if (editPrefixo_) {
        editPrefixo_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        editPrefixo_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
        editPrefixo_->setColour(juce::TextEditor::outlineColourId, tk.borda);
        editPrefixo_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
        editPrefixo_->setTooltip(matriz::i18n::t("backup.prefixo_arquivos_dica"));
    }
    if (labelOpcoes_) {
        labelOpcoes_->setText(matriz::i18n::t("backup.secao_opcoes"), juce::dontSendNotification);
        labelOpcoes_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        labelOpcoes_->setColour(juce::Label::textColourId, tk.textoSecundario);
    }
    if (toggleVerificarChecksum_) {
        toggleVerificarChecksum_->setButtonText(matriz::i18n::t("backup.verificar_checksum"));
        toggleVerificarChecksum_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleVerificarChecksum_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleGerarCatalogo_) {
        toggleGerarCatalogo_->setButtonText(matriz::i18n::t("backup.gerar_banco_sqlite"));
        toggleGerarCatalogo_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleGerarCatalogo_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleEmbutirMetadados_) {
        toggleEmbutirMetadados_->setButtonText(matriz::i18n::t("backup.embutir_metadados"));
        toggleEmbutirMetadados_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleEmbutirMetadados_->setColour(juce::ToggleButton::tickColourId, tk.acento);
    }
    if (toggleAutoResolverConflitos_) {
        toggleAutoResolverConflitos_->setButtonText(matriz::i18n::t("backup.auto_resolver_conflitos"));
        toggleAutoResolverConflitos_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleAutoResolverConflitos_->setColour(juce::ToggleButton::tickColourId, tk.acento);
        toggleAutoResolverConflitos_->setTooltip(matriz::i18n::t("backup.auto_resolver_conflitos"));
    }
    if (toggleForcarRebackup_) {
        toggleForcarRebackup_->setButtonText(matriz::i18n::t("backup.forcar_rebackup"));
        toggleForcarRebackup_->setColour(juce::ToggleButton::textColourId, tk.textoPrimario);
        toggleForcarRebackup_->setColour(juce::ToggleButton::tickColourId, tk.acento);
        toggleForcarRebackup_->setTooltip(matriz::i18n::t("backup.forcar_rebackup_dica"));
    }
    if (labelResumo_) {
        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (labelProgressoStatus_) {
        labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        labelProgressoStatus_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (btnStartBackup_) {
        btnStartBackup_->setButtonText(matriz::i18n::t("backup.btn_fazer_backup"));
        aplicarEstiloBotao(*btnStartBackup_, true);
    }
    if (btnPublishHtml_) {
        btnPublishHtml_->setButtonText(matriz::i18n::t("backup.btn_publicar_html"));
        aplicarEstiloBotao(*btnPublishHtml_, false);
        btnPublishHtml_->setColour(juce::TextButton::textColourOffId, tk.acento);
    }
    if (btnCancelarExecucao_) {
        btnCancelarExecucao_->setButtonText(isPt ? juce::String::fromUTF8("CANCELAR") : "CANCEL");
        aplicarEstiloBotao(*btnCancelarExecucao_, false);
        btnCancelarExecucao_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    }
    if (btnDone_) {
        btnDone_->setButtonText(isPt ? juce::String::fromUTF8("CONCLUÍDO") : "DONE");
        aplicarEstiloBotao(*btnDone_, false);
    }
    if (btnOpenCatalog_) {
        btnOpenCatalog_->setButtonText(isPt ? juce::String::fromUTF8("ABRIR PASTA DO BACKUP") : "OPEN BACKUP FOLDER");
        btnOpenCatalog_->setTooltip(isPt ? juce::String::fromUTF8("Abrir pasta do backup no Finder") : "Open backup folder in Finder");
        aplicarEstiloBotao(*btnOpenCatalog_, true);
    }
    if (btnExportJanela_) {
        btnExportJanela_->setButtonText(matriz::i18n::t("backup.btn_exportar_metadados"));
        aplicarEstiloBotao(*btnExportJanela_, false);
    }
    if (btnExportZip_) {
        aplicarEstiloBotao(*btnExportZip_, false);
        btnExportZip_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff0077ff));
    }
    if (btnLimparZip_) {
        aplicarEstiloBotao(*btnLimparZip_, false);
    }
    if (btnSendToPrint_) {
        aplicarEstiloBotao(*btnSendToPrint_, false);
        btnSendToPrint_->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff6b00));
    }
    if (btnLimparPrint_) {
        aplicarEstiloBotao(*btnLimparPrint_, false);
    }
    if (btnBrowseVault_) {
        btnBrowseVault_->setButtonText(matriz::i18n::t("backup.escolher_pasta"));
        aplicarEstiloBotao(*btnBrowseVault_, false);
    }

    if (listPrevia_) listPrevia_->repaint();
    atualizarResumo();
    atualizarBotoesListas();
    repaint();
}

void BackupWorkspaceComponent::mostrarJanelaExportar() {
    struct JanelaExportarMetadata : public juce::DialogWindow {
        JanelaExportarMetadata(const juce::String& title, juce::Colour bg)
            : juce::DialogWindow(title, bg, true) {}

        void closeButtonPressed() override {
            setVisible(false);
            exitModalState(0);
        }
    };

    int modalH = 300;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
    auto janela = std::make_shared<JanelaExportarMetadata>(isPt ? juce::String::fromUTF8("EXPORTAR METADADOS") : "EXPORT METADATA", tema().painel);

    struct PainelExportar : public juce::Component {
        PainelExportar(BackupWorkspaceComponent& parent, std::shared_ptr<juce::DialogWindow> win)
            : win_(std::move(win)) {
            const auto& tk = tema();
            bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

            lblTitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("EXPORTAR METADADOS") : "EXPORT METADATA");
            lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
            lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
            lblTitulo_->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*lblTitulo_);

            auto criarBotao = [&](const juce::String& text, std::function<void()> cb) {
                auto b = std::make_unique<juce::TextButton>(text);
                parent.aplicarEstiloBotao(*b, false);
                b->onClick = [this, cb] {
                    if (win_) win_->exitModalState(0);
                    if (cb) cb();
                };
                addAndMakeVisible(*b);
                return b;
            };

            btnCsvFull_ = criarBotao(isPt ? juce::String::fromUTF8("CSV (COMPLETO)") : "CSV (FULL)", [&parent] { parent.exportarCsv(); });
            btnXls_ = criarBotao("XLS", [&parent] { parent.exportarXls(); });
            btnDublinCore_ = criarBotao(isPt ? juce::String::fromUTF8("CSV (DUBLIN CORE)") : "CSV (DUBLIN CORE)", [&parent] { parent.exportarDublinCore(); });
            btnChecksums_ = criarBotao(isPt ? juce::String::fromUTF8("HASHES / CHECKSUMS") : "CHECKSUMS", [&parent] { parent.exportarChecksums(); });

            setSize(460, 300);
        }

        void resized() override {
            auto area = getLocalBounds().reduced(24, 20);
            lblTitulo_->setBounds(area.removeFromTop(32));
            area.removeFromTop(14);

            int btnH = 38;
            btnCsvFull_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnXls_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnDublinCore_->setBounds(area.removeFromTop(btnH));
            area.removeFromTop(10);
            btnChecksums_->setBounds(area.removeFromTop(btnH));
        }

    private:
        std::shared_ptr<juce::DialogWindow> win_;
        std::unique_ptr<juce::Label> lblTitulo_;
        std::unique_ptr<juce::TextButton> btnCsvFull_;
        std::unique_ptr<juce::TextButton> btnXls_;
        std::unique_ptr<juce::TextButton> btnDublinCore_;
        std::unique_ptr<juce::TextButton> btnChecksums_;
    };

    auto painel = std::make_unique<PainelExportar>(*this, janela);

    janela->setContentOwned(painel.release(), true);
    janela->setResizable(false, false);
    janela->centreWithSize(460, modalH);
    janela->setVisible(true);
    janela->enterModalState(true);
}

void BackupWorkspaceComponent::publicarHtml() {
    PublishHtmlDialog::exibirModal(projeto_);
}

void BackupWorkspaceComponent::aoItemAlterado(const EventoItemAlterado&) {
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<BackupWorkspaceComponent>(this)] {
        if (safe != nullptr) {
            safe->atualizarBotoesListas();
        }
    });
}

void BackupWorkspaceComponent::atualizarBotoesListas() {
    const size_t countZip = projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Zip);
    const size_t countPrint = projeto_.contarMarcacoes(ProjetoAberto::TipoMarcacao::Print);

    if (btnExportZip_) {
        btnExportZip_->setButtonText(juce::String::formatted(matriz::i18n::t("backup.exportar_zip").toRawUTF8(), (int)countZip));
        btnExportZip_->setEnabled(countZip > 0);
    }
    if (btnLimparZip_) {
        btnLimparZip_->setEnabled(countZip > 0);
        btnLimparZip_->setTooltip(matriz::i18n::t("backup.limpar_zip_dica"));
    }
    if (btnSendToPrint_) {
        btnSendToPrint_->setButtonText(juce::String::formatted(matriz::i18n::t("backup.enviar_print").toRawUTF8(), (int)countPrint));
        btnSendToPrint_->setEnabled(false);
    }
    if (btnLimparPrint_) {
        btnLimparPrint_->setEnabled(countPrint > 0);
        btnLimparPrint_->setTooltip(matriz::i18n::t("backup.limpar_print_dica"));
    }
}

void BackupWorkspaceComponent::exportarCsv() {
    juce::FileChooser fc("Export BKR Full CSV Package...", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    if (fc.browseForDirectory()) {
        juce::File targetDir = fc.getResult();

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String err;
        bool ok = false;
        ProgressThread thread("EXPORT METADATA - CSV (FULL)", [this, &ids, &targetDir, &ok, &err](ProgressThread* t) {
            t->setStatusMessage("Generating BKR Full CSV Package...");
            t->setProgress(0.4);
            ok = projeto_.exportarFullCsvPacote(ids, targetDir, err);
            t->setProgress(1.0);
        });
        thread.runThread();

        if (ok) {
            juce::File pkgDir = targetDir.getFileName() == "BKR_Full_Export" ? targetDir : targetDir.getChildFile("BKR_Full_Export");
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("EXPORT FULL CSV")
                    .withMessage("BKR Full CSV Package exported and validated successfully!\n\nPackage location:\n" + pkgDir.getFullPathName() + "\n\nContents:\n- BKR_FULL.csv\n- BKR_FULL.schema.json\n- manifest.json")
                    .withButton("OK"),
                static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        } else {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("EXPORT FULL CSV FAILED")
                    .withMessage("BKR Full CSV Export failed validation:\n" + err)
                    .withButton("OK"),
                static_cast<juce::ModalComponentManager::Callback*>(nullptr));
        }
    }
}

void BackupWorkspaceComponent::exportarXls() {
    juce::FileChooser fc("Export XLS Spreadsheet...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.xls");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("xls");

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String xls;
        ProgressThread thread("EXPORT METADATA - XLS", [this, &ids, &xls](ProgressThread* t) {
            t->setStatusMessage("Generating XLS Spreadsheet (" + juce::String(static_cast<int>(ids.size())) + " items)...");
            t->setProgress(0.5);
            xls = projeto_.exportarXlsXml(ids);
            t->setProgress(1.0);
        });
        thread.runThread();

        targetFile.replaceWithText(xls);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT XLS")
                .withMessage("XLS Spreadsheet exported successfully to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarCsvPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_full_report.csv");

    std::vector<std::string> ids;
    for (const auto& item : plano_.itens) ids.push_back(item.itemId);
    if (ids.empty()) {
        try {
            auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
            while (stmt.step()) ids.push_back(stmt.columnText(0));
        } catch (...) {}
    }

    auto csv = projeto_.exportarFullCsv(ids);
    targetFile.replaceWithText(csv);
}

void BackupWorkspaceComponent::exportarXlsPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_catalog.xls");

    std::vector<std::string> ids;
    for (const auto& item : plano_.itens) ids.push_back(item.itemId);
    if (ids.empty()) {
        try {
            auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
            while (stmt.step()) ids.push_back(stmt.columnText(0));
        } catch (...) {}
    }

    auto xls = projeto_.exportarXlsXml(ids);
    targetFile.replaceWithText(xls);
}

void BackupWorkspaceComponent::exportarDublinCore() {
    juce::FileChooser fc("Export DC-CSV Report...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.csv");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("csv");

        std::vector<std::string> ids;
        for (const auto& item : plano_.itens) ids.push_back(item.itemId);
        if (ids.empty()) {
            try {
                auto stmt = projeto_.projeto().registro().prepare("SELECT id FROM item");
                while (stmt.step()) ids.push_back(stmt.columnText(0));
            } catch (...) {}
        }

        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String csv;
        ProgressThread thread("EXPORT METADATA - CSV (DUBLIN CORE)", [this, &ids, &csv](ProgressThread* t) {
            t->setStatusMessage("Generating Dublin Core CSV (" + juce::String(static_cast<int>(ids.size())) + " items)...");
            t->setProgress(0.5);
            csv = projeto_.exportarDublinCoreCsv(ids);
            t->setProgress(1.0);
        });
        thread.runThread();

        targetFile.replaceWithText(csv);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT DC-CSV")
                .withMessage("DC-CSV Report exported to:\n" + targetFile.getFullPathName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

juce::String BackupWorkspaceComponent::gerarManifestChecksumsBackup(const std::function<void(int, int)>& onProgress) {
    juce::String manifest;
    int total = static_cast<int>(plano_.itens.size());
    int feito = 0;

    for (const auto& item : plano_.itens) {
        if (onProgress) onProgress(feito, total);

        juce::String hash;
        try {
            auto stmt = projeto_.projeto().registro().prepare("SELECT checksum_sha256 FROM arquivo WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(item.arquivoId));
            if (stmt.step() && !stmt.columnIsNull(0)) {
                hash = stmt.columnText(0);
            }
        } catch (...) {}

        if (hash.isEmpty()) {
            auto resolvido = matriz::vault::resolverArquivo(projeto_.projeto().registro(), item.arquivoId, projeto_.projeto().pasta());
            juce::File srcFile = resolvido ? *resolvido : projeto_.projeto().pasta().getChildFile(item.nomeOriginal);
            if (srcFile.existsAsFile()) {
                hash = juce::SHA256(srcFile).toHexString().toLowerCase();
            }
        }

        juce::String relPath = item.caminhoRelativoDestino;
        if (relPath.isEmpty()) relPath = item.nomeOriginal;

        if (hash.isEmpty()) hash = "0000000000000000000000000000000000000000000000000000000000000000";
        manifest += hash + "  " + relPath + "\n";
        feito++;
    }

    if (onProgress) onProgress(total, total);
    return manifest;
}

void BackupWorkspaceComponent::exportarChecksums() {
    juce::FileChooser fc("Export Checksum Manifest...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.sha256");
    if (fc.browseForFileToSave(true)) {
        juce::File targetFile = fc.getResult();
        if (targetFile.getFileExtension().isEmpty()) targetFile = targetFile.withFileExtension("sha256");

        bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
        struct ProgressThread : public juce::ThreadWithProgressWindow {
            ProgressThread(const juce::String& title, std::function<void(ProgressThread*)> work)
                : juce::ThreadWithProgressWindow(title, true, true), work_(work) {}
            void run() override { if (work_) work_(this); }
            std::function<void(ProgressThread*)> work_;
        };

        juce::String manifest;
        ProgressThread thread("EXPORT METADATA - CHECKSUMS", [this, &manifest, isCatalogMode](ProgressThread* t) {
            if (isCatalogMode || plano_.itens.empty()) {
                t->setStatusMessage("Generating SHA-256 Checksum Manifest...");
                t->setProgress(0.5);
                manifest = projeto_.exportarFixityManifest({}, "SHA-256");
                t->setProgress(1.0);
            } else {
                manifest = gerarManifestChecksumsBackup([t](int feito, int total) {
                    if (total > 0) {
                        t->setStatusMessage("Generating SHA-256 Checksum: " + juce::String(feito + 1) + " of " + juce::String(total) + "...");
                        t->setProgress(static_cast<double>(feito) / total);
                    }
                });
            }
        });
        thread.runThread();

        targetFile.replaceWithText(manifest);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("EXPORT CHECKSUMS")
                .withMessage("Checksum manifest exported to:\n" + targetFile.getFullPathName()
                             + "\n\nVerify with:  sha256sum -c " + targetFile.getFileName())
                .withButton("OK"),
            static_cast<juce::ModalComponentManager::Callback*>(nullptr));
    }
}

void BackupWorkspaceComponent::exportarDublinCorePara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + "_dublin_core.csv");
    std::vector<std::string> ids;
    for (const auto& item : plano_.itens) ids.push_back(item.itemId);
    auto csv = projeto_.exportarDublinCoreCsv(ids);
    targetFile.replaceWithText(csv.toStdString());
}

void BackupWorkspaceComponent::exportarChecksumsPara(const juce::File& destFolder) {
    juce::String projectName = juce::String(projeto_.projeto().nome());
    juce::File targetFile = destFolder.getChildFile(projectName + ".sha256");
    juce::String manifest = gerarManifestChecksumsBackup(nullptr);
    targetFile.replaceWithText(manifest);
}

void BackupWorkspaceComponent::aplicarEstiloBotao(juce::TextButton& botao, bool primario) {
    const auto& tk = tema();
    botao.setColour(juce::TextButton::buttonColourId, primario ? tk.acento : tk.painelAlt);
    botao.setColour(juce::TextButton::buttonOnColourId, tk.acento);
    botao.setColour(juce::TextButton::textColourOffId, primario ? tk.textoSobreAcento : tk.textoPrimario);
    botao.setColour(juce::TextButton::textColourOnId, tk.textoSobreAcento);
}

std::set<std::string> BackupWorkspaceComponent::obterItensSelecionadosPeloCriterio() {
    std::set<std::string> out;
    auto& db = projeto_.projeto().registro();

    if (whatOption_ == WhatOption::Everything) {
        auto stmt = db.prepare("SELECT id FROM item");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::Intake) {
        auto stmt = db.prepare("SELECT id FROM item WHERE criado_em >= datetime('now', '-24 hours')");
        while (stmt.step()) out.insert(stmt.columnText(0));
    } else if (whatOption_ == WhatOption::SelectedAssets) {
        return selectedItemIds_;
    } else if (whatOption_ == WhatOption::NeedsBackup) {
        return projeto_.itensDaColecaoEmbutida("vulneraveis");
    } else if (whatOption_ == WhatOption::Collection) {
        if (selectedContentIdx_ >= 0 && selectedContentIdx_ < static_cast<int>(opcoesContent_.size())) {
            const auto& opt = opcoesContent_[static_cast<size_t>(selectedContentIdx_)];
            if (opt.chave == "__empty__") {
                auto stmt = db.prepare(
                    "SELECT i.id FROM item i "
                    "WHERE (i.collection_type IS NULL OR TRIM(i.collection_type) = '') "
                    "  AND NOT EXISTS (SELECT 1 FROM item_campo ic WHERE ic.item_id = i.id AND ic.campo_id = 'collection_type' AND ic.valor IS NOT NULL AND TRIM(ic.valor) <> '')");
                while (stmt.step()) out.insert(stmt.columnText(0));
            } else {
                auto stmt = db.prepare(
                    "SELECT i.id FROM item i "
                    "WHERE i.collection_type = ? "
                    "   OR ( (i.collection_type IS NULL OR TRIM(i.collection_type) = '') "
                    "        AND EXISTS (SELECT 1 FROM item_campo ic WHERE ic.item_id = i.id AND ic.campo_id = 'collection_type' AND ic.valor = ?) )");
                stmt.bind(1, matriz::db::Value::of(opt.chave));
                stmt.bind(2, matriz::db::Value::of(opt.chave));
                while (stmt.step()) out.insert(stmt.columnText(0));
            }
        }
    }
    return out;
}

void BackupWorkspaceComponent::atualizarResumo() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        for (const auto& c : colecoes) {
            if (c.valido) {
                totalAssets += c.totalAssets;
                totalBytes += c.totalBytes;
            }
        }

        juce::String summary = (isPt ? juce::String::fromUTF8("COLEÇÕES (") : "COLLECTIONS (") + juce::String(static_cast<int>(colecoes.size())) + ")" +
                               (isPt ? juce::String::fromUTF8("  |  Itens Totais: ") : "  |  Total Assets: ") + juce::String(totalAssets) +
                               (isPt ? juce::String::fromUTF8("  |  Espaço: ") : "  |  Space: ") + juce::File::descriptionOfSizeInBytes(totalBytes);

        if (resolvedDestFolder_.getFullPathName().isEmpty()) {
            summary += isPt ? juce::String::fromUTF8("  -  (Selecione uma pasta de destino acima)") : "  -  (Select a destination folder above)";
            btnStartBackup_->setEnabled(false);
        } else if (colecoes.empty()) {
            summary += isPt ? juce::String::fromUTF8("  -  (Nenhuma coleção vinculada a este catálogo)") : "  -  (No collections linked to this catalog)";
            btnStartBackup_->setEnabled(false);
        } else {
            btnStartBackup_->setEnabled(true);
        }

        labelResumo_->setText(summary, juce::dontSendNotification);
        listPrevia_->definirColecoesCatalogo(colecoes, resolvedDestFolder_);
        listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);
        return;
    }

    if (resolvedDestFolder_.getFullPathName().isEmpty()) {
        labelResumo_->setText(isPt ? juce::String::fromUTF8("Selecione um destino para ver a prévia do backup.") : "Select a destination to see backup preview.", juce::dontSendNotification);
        plano_.itens.clear();
        listPrevia_->definirPlano(plano_);
        btnStartBackup_->setEnabled(false);
        return;
    }

    std::set<std::string> itemIds = obterItensSelecionadosPeloCriterio();

    matriz::consolidacao::HierarquiaBackup h;
    if (togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState()) {
        h = { matriz::consolidacao::NivelHierarquia::EstruturaOriginal };
    } else if (toggleUsarEstruturaMapa_ && toggleUsarEstruturaMapa_->getToggleState()) {
        h = { matriz::consolidacao::NivelHierarquia::PastaManual };
    } else {
        int orgId = comboOrg_ ? comboOrg_->getSelectedId() : 1;
        if (orgId == 1) h = { matriz::consolidacao::NivelHierarquia::PastaManual };
        else if (orgId == 2) h = { matriz::consolidacao::NivelHierarquia::TipoMidia };
        else if (orgId == 3) h = { matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 4) h = { matriz::consolidacao::NivelHierarquia::TipoMidia, matriz::consolidacao::NivelHierarquia::Ano };
        else if (orgId == 5) h = hierarquiaCustom_;
    }

    bool autoResolver = toggleAutoResolverConflitos_ ? toggleAutoResolverConflitos_->getToggleState() : true;
    bool forcarRebackup = toggleForcarRebackup_ ? toggleForcarRebackup_->getToggleState() : false;

    juce::File destinoRaiz = matriz::model::normalizarParaRaizDestino(
        resolvedDestFolder_.isDirectory() ? resolvedDestFolder_ : projeto_.projeto().raiz());
    juce::File destinoMedia = destinoRaiz.getChildFile("Media");

    plano_ = matriz::consolidacao::planejarConsolidacao(
        projeto_.projeto().registro(), projeto_.projeto().pasta(), destinoMedia, h, {}, modoPrefixo_, prefixoCustomizado_,
        autoResolver, forcarRebackup);

    std::vector<matriz::consolidacao::ItemPlanejado> filtrados;
    juce::int64 sz = 0; // space to copy
    juce::int64 totalSz = 0; // total backup size
    for (auto& item : plano_.itens) {
        if (itemIds.count(item.itemId)) {
            filtrados.push_back(item);
            if (!item.jaConsolidado) sz += item.tamanhoBytes;
            totalSz += item.tamanhoBytes;
        }
    }
    plano_.itens = std::move(filtrados);
    plano_.espacoNecessarioBytes = sz;

    juce::String summary = (isPt ? juce::String::fromUTF8("Itens: ") : "Assets: ") + juce::String(static_cast<int>(plano_.itens.size()));
    if (itemIds.size() > plano_.itens.size()) {
        summary += isPt ? (juce::String::fromUTF8(" (de ") + juce::String(static_cast<int>(itemIds.size())) + juce::String::fromUTF8(" no catálogo)"))
                        : (" (of " + juce::String(static_cast<int>(itemIds.size())) + " in catalog)");
    }
    summary += (isPt ? juce::String::fromUTF8(" | Espaço: ") : " | Space: ") + juce::File::descriptionOfSizeInBytes(totalSz);

    if (plano_.conflitosAutoResolvidos > 0) {
        summary += (isPt ? juce::String::fromUTF8(" | ") : " | ") +
                   matriz::i18n::t("backup.conflitos_resolvidos").replace("{n}", juce::String(plano_.conflitosAutoResolvidos));
    }

    int semArquivo = static_cast<int>(itemIds.size() - plano_.itens.size());
    if (semArquivo > 0) {
        summary += isPt ? (juce::String::fromUTF8("  -  (") + juce::String(semArquivo) + juce::String::fromUTF8(" itens de metadado não têm arquivos físicos)"))
                        : ("  -  (" + juce::String(semArquivo) + " metadata items have no physical files)");
    }

    // Botão desabilitado sem explicação é indistinguível de botão ausente —
    // o motivo vai junto do resumo sempre que o backup não puder rodar.
    bool pronto = plano_.podeConsolidar() && !plano_.itens.empty();
    if (plano_.itens.empty())
        summary += isPt ? juce::String::fromUTF8("  -  NÃO É POSSÍVEL EXECUTAR: nenhum item corresponde à seleção.")
                        : "  -  CANNOT RUN: no assets match the selection.";
    else if (!plano_.podeConsolidar()) {
        bool preservando = togglePreservarEstrutura_ && togglePreservarEstrutura_->getToggleState();
        if (preservando) {
            summary += isPt ? juce::String::fromUTF8("  -  (Conflito de nomes detectado na estrutura original)")
                            : "  -  (Naming conflict detected in original structure)";
        } else {
            int qtd = static_cast<int>(plano_.nomesEmConflito.size());
            summary += isPt ? (juce::String::fromUTF8("  -  NÃO É POSSÍVEL EXECUTAR: ") + juce::String(qtd) +
                               juce::String::fromUTF8(" arquivo(s) com conflito de nomes no destino. Resolva as duplicatas ou altere a estrutura de pastas."))
                            : ("  -  CANNOT RUN: " + juce::String(qtd) +
                               " file(s) with naming conflicts at destination. Resolve duplicates or change folder structure.");
        }
    } else if (sz == 0 && !plano_.itens.empty() && !forcarRebackup) {
        summary += isPt ? juce::String::fromUTF8("  (Destino 100% atualizado)")
                        : "  (Destination 100% up to date)";
    }

    labelResumo_->setText(summary, juce::dontSendNotification);

    listPrevia_->definirPlano(plano_);
    listPreviaViewport_->setViewedComponent(listPrevia_.get(), false);

    btnStartBackup_->setEnabled(pronto);
}

void BackupWorkspaceComponent::mostrarPopupConflitoPreservacao() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    int qtd = static_cast<int>(plano_.nomesEmConflito.size());

    PainelOverlay::Config cfg;
    cfg.titulo = isPt ? juce::String::fromUTF8("CONFLITO DE ARQUIVOS NO DESTINO")
                      : "DESTINATION FILE CONFLICT";

    juce::String msg;
    if (isPt) {
        msg = juce::String::fromUTF8(
            "Ao optar por 'Preservar estrutura original de pastas', foram identificados ")
            + juce::String(qtd) + juce::String::fromUTF8(" arquivo(s) cujos nomes geram colisão no destino.\n\n"
            "O processo de backup não pode sobrescrever arquivos com o mesmo nome.\n\n"
            "Deseja ir para a aba de Duplicatas para escanear, validar e resolver os arquivos conflitantes?");
    } else {
        msg = juce::String(
            "When choosing 'Preserve original folder structure', ")
            + juce::String(qtd) + " file(s) collide at the destination with identical names and paths.\n\n"
            "The backup process cannot overwrite destination files.\n\n"
            "Would you like to go to the Duplicates tab to scan, inspect, and resolve these conflicting files?";
    }
    cfg.mensagem = msg;
    cfg.botoes = {
        { isPt ? "IR PARA DUPLICATAS" : "GO TO DUPLICATES", 1, true, false },
        { isPt ? "VOLTAR" : "RETURN", 2, false, true }
    };

    overlay_.mostrar(cfg, [this](PainelOverlay::Resultado res) {
        if (res.botaoId == 1) {
            if (aoPedirIrParaDuplicatas) {
                aoPedirIrParaDuplicatas();
            }
        }
    });
}

juce::File BackupWorkspaceComponent::detectarPastaGoogleDrive() {
    // Caminho moderno (Google Drive Desktop ≥ v55, macOS 12+)
    juce::File base = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                          .getChildFile("Library/CloudStorage");
    if (base.isDirectory()) {
        for (auto& child : base.findChildFiles(juce::File::findDirectories, false, "GoogleDrive-*")) {
            auto myDrive = child.getChildFile("My Drive");
            if (myDrive.isDirectory()) return myDrive;
            if (child.isDirectory()) return child;
        }
    }
    // Caminho legado (Google Drive pré-2021)
    juce::File legacy = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                            .getChildFile("Google Drive");
    if (legacy.isDirectory()) return legacy;
    return juce::File(); // não encontrado
}

void BackupWorkspaceComponent::iniciarBackup() {
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);

    estado_ = Estado::Running;
    executando_ = true;
    cancelamento_->rearmar();
    progressoValor_ = 0.0;

    btnStartBackup_->setEnabled(false);
    barraProgresso_->setVisible(true);
    labelProgressoStatus_->setVisible(true);
    labelProgressoStatus_->setText("Preparing copy operations...", juce::dontSendNotification);

    auto cancelamento = cancelamento_;
    auto plano = plano_;
    auto& projeto = projeto_;
    bool gerarCatalogo = toggleGerarCatalogo_->getToggleState();
    bool embutirMeta = !isCatalogMode && toggleEmbutirMetadados_->getToggleState();

    juce::File destinoRaiz = matriz::model::normalizarParaRaizDestino(
        resolvedDestFolder_.isDirectory() ? resolvedDestFolder_ : (isCatalogMode ? resolvedDestFolder_ : projeto.projeto().raiz()));
    juce::File destinoMedia = destinoRaiz.getChildFile("Media");
    destinoRaiz.createDirectory();
    destinoMedia.createDirectory();
    matriz::model::sanitizarEstruturaDestino(destinoRaiz);

    // Coloca os arquivos do projeto (.mtz ou .bkm) DENTRO da pasta principal do backup (Tarefa 11)
    juce::File pastaBackupRaiz = destinoRaiz;
    pastaBackupRaiz.createDirectory();

    juce::String extProj = isCatalogMode ? ".bkm" : ".mtz";
    juce::String nomeDoProjeto = juce::String::fromUTF8(projeto.projeto().nome().c_str());
    juce::File arqProjBackup = pastaBackupRaiz.getChildFile(juce::File::createLegalFileName(nomeDoProjeto) + extProj);
    juce::File arqProjOrig = projeto.projeto().raiz().getChildFile(juce::File::createLegalFileName(nomeDoProjeto) + extProj);
    if (arqProjOrig.existsAsFile()) {
        arqProjOrig.copyFileTo(arqProjBackup);
    } else {
        juce::DynamicObject::Ptr projObj = new juce::DynamicObject();
        projObj->setProperty("formato", 1);
        projObj->setProperty("modo", isCatalogMode ? "catalogo" : "preservacao");
        projObj->setProperty("nome", nomeDoProjeto);
        projObj->setProperty("projeto_id", juce::String(projeto.projeto().projetoId()));
        projObj->setProperty("destination_id", juce::String(projeto.projeto().destinationId()));
        projObj->setProperty("criado_em", juce::Time::getCurrentTime().toISO8601(true));
        arqProjBackup.replaceWithText(juce::JSON::toString(juce::var(projObj.get()), false));
    }
    // Garante destination.json e Project/ com SQLite no backup
    juce::File destJsonBackup = pastaBackupRaiz.getChildFile("destination.json");
    juce::File destJsonOrig = projeto.projeto().raiz().getChildFile("destination.json");
    if (destJsonOrig.existsAsFile()) {
        destJsonOrig.copyFileTo(destJsonBackup);
    } else {
        matriz::model::DestinationInfo dInfo;
        dInfo.formato = 1;
        dInfo.destinationId = matriz::model::novoUuid();
        dInfo.projetoId = projeto.projeto().projetoId();
        dInfo.papel = "CLONE";
        dInfo.rotulo = pastaBackupRaiz.getFileName().toStdString();
        dInfo.revisao = projeto.projeto().revisao();
        dInfo.ultimaEdicaoUtc = matriz::model::agoraIso8601();
        dInfo.criadoEm = matriz::model::agoraIso8601();
        dInfo.gravarEmArquivo(destJsonBackup);
    }

    juce::File projectSubBackup = pastaBackupRaiz.getChildFile("Project");
    if (!projectSubBackup.exists()) projectSubBackup.createDirectory();
    juce::File regBackup = projectSubBackup.getChildFile("registro.sqlite");
    juce::File indBackup = projectSubBackup.getChildFile("indice.sqlite");
    juce::File regOrig = projeto.projeto().pasta().getChildFile("registro.sqlite");
    juce::File indOrig = projeto.projeto().pasta().getChildFile("indice.sqlite");
    if (regOrig.existsAsFile()) regOrig.copyFileTo(regBackup);
    if (indOrig.existsAsFile()) indOrig.copyFileTo(indBackup);

    if (isCatalogMode) {
        auto colecoes = projeto.listarColecoesLinkadas();
        ProgressoGlobal::obterInstancia().iniciarTarefa(
            "backup",
            "Consolidating Catalog Collections",
            static_cast<int>(colecoes.size()),
            [cancelamento] { cancelamento->pedir(); },
            "Preparing catalog backup...");

        resized();
        repaint();

        juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

        juce::MessageManager::callAsync([safeThis, cancelamento, colecoes, destinoRaiz, destinoMedia, &projeto,
                                          gerarCatalogo]() {
            if (!safeThis) return;

            int copiado = 0;
            int falhas = 0;
            std::vector<juce::String> falhasLista;

            destinoMedia.createDirectory();

            for (size_t i = 0; i < colecoes.size(); ++i) {
                if (cancelamento->pedido()) break;
                const auto& c = colecoes[i];
                if (!c.valido) continue;

                juce::File colOrigem(c.caminhoProjeto);
                juce::File colDestino = destinoMedia.getChildFile(c.nome);

                if (safeThis) {
                    safeThis->progressoValor_ = static_cast<double>(i) / std::max<size_t>(1, colecoes.size());
                    safeThis->labelProgressoStatus_->setText("Backing up collection " + juce::String(i + 1) + " of " + juce::String(colecoes.size()) + ": " + c.nome + "...", juce::dontSendNotification);
                    ProgressoGlobal::obterInstancia().atualizarProgresso("backup", static_cast<int>(i), "Backing up: " + c.nome);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                }

                if (colOrigem.isDirectory()) {
                    if (colDestino.exists()) colDestino.deleteRecursively();
                    if (!colOrigem.copyDirectoryTo(colDestino)) {
                        falhas++;
                        falhasLista.push_back("Failed to copy collection folder: " + c.nome);
                    } else {
                        copiado++;
                    }
                }
            }

            if (!safeThis) return;

            // Copy catalog database as well into Project/
            juce::File catOrigemDb = projeto.projeto().pasta().getChildFile("registro.sqlite");
            if (catOrigemDb.existsAsFile()) {
                juce::File projectSubBackup = destinoRaiz.getChildFile("Project");
                projectSubBackup.createDirectory();
                catOrigemDb.copyFileTo(projectSubBackup.getChildFile("registro.sqlite"));
            }

            safeThis->copiadoCount_ = copiado;
            safeThis->verificadoCount_ = copiado;
            safeThis->falhasCount_ = falhas;
            safeThis->falhasLista_ = falhasLista;

            safeThis->registrarDestinoBackup(destinoRaiz, "Catalog Backup", copiado, 0, falhas, cancelamento->pedido());

            safeThis->executando_ = false;
            safeThis->estado_ = Estado::Done;
            safeThis->progressoValor_ = 1.0;

            const bool houveFalha = (falhas > 0);
            juce::String msgFinal = cancelamento->pedido() ? "Backup cancelled."
                                                           : (houveFalha ? "Catalog backup completed with errors."
                                                                         : "Catalog backup completed successfully (" + juce::String(copiado) + " collections).");
            ProgressoGlobal::obterInstancia().concluirTarefa("backup", msgFinal);

            safeThis->labelProgressoStatus_->setText(msgFinal, juce::dontSendNotification);
            safeThis->labelProgressoStatus_->setColour(
                juce::Label::textColourId,
                houveFalha ? tema().perigo : (cancelamento->pedido() ? tema().alerta : tema().estadoQcOk));

            safeThis->mostrarControlesConfig(false);
            safeThis->resized();
            safeThis->repaint();
        });
        return;
    }

    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "backup",
        "Consolidating Backup",
        static_cast<int>(plano.itens.size()),
        [cancelamento] { cancelamento->pedir(); },
        "Preparing copy operations...");

    resized();
    repaint();

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);

    juce::MessageManager::callAsync([safeThis, cancelamento, plano, destinoRaiz, destinoMedia, &projeto,
                                      gerarCatalogo, embutirMeta]() {
        if (!safeThis) return;

        auto resultado = matriz::consolidacao::executarConsolidacao(
            projeto.projeto().registro(),
            projeto.projeto().pasta(),
            destinoMedia,
            plano,
            [safeThis, cancelamento](int feito, int total) {
                if (!safeThis) return false;
                safeThis->progressoValor_ = static_cast<double>(feito) / std::max(1, total);
                safeThis->labelProgressoStatus_->setText("Copying: " + juce::String(feito) + " of " + juce::String(total) + " assets...", juce::dontSendNotification);
                ProgressoGlobal::obterInstancia().atualizarProgresso(
                    "backup", feito, "Copying " + juce::String(feito) + " of " + juce::String(total) + " assets...");
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                return !cancelamento->pedido() && safeThis != nullptr;
            }
        );

        if (!safeThis) return;

        safeThis->copiadoCount_ = resultado.consolidados;
        safeThis->verificadoCount_ = resultado.consolidados + resultado.pulados;
        safeThis->falhasCount_ = static_cast<int>(resultado.falhas.size());
        safeThis->falhasLista_.clear();
        for (const auto& f : resultado.falhas)
            safeThis->falhasLista_.push_back(f);

        if (gerarCatalogo && !resultado.cancelado && safeThis) {
            safeThis->labelProgressoStatus_->setText("Generating HTML Catalog...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", "Generating HTML Catalog...");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (!safeThis) return;
            juce::File catDestDir = projeto.projeto().pasta().getChildFile("catalogo");
            catDestDir.createDirectory();
            auto resCatalogo = matriz::catalogo::gerar(
                projeto.projeto().registro(),
                projeto.projeto().indice(),
                projeto.projeto().pasta(),
                catDestDir,
                [safeThis, cancelamento](int feito, int total) {
                    if (!safeThis) return false;
                    safeThis->labelProgressoStatus_->setText("Cataloging: " + juce::String(feito) + " of " + juce::String(total) + " files...", juce::dontSendNotification);
                    ProgressoGlobal::obterInstancia().atualizarProgresso(
                        "backup", feito, "Cataloging " + juce::String(feito) + " of " + juce::String(total) + " files...");
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                    return !cancelamento->pedido() && safeThis != nullptr;
                }
            );
            (void)resCatalogo;
        }

        if (embutirMeta && !resultado.cancelado && safeThis) {
            safeThis->labelProgressoStatus_->setText("Embedding metadata into backup files...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", "Embedding metadata...");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (!safeThis) return;
            // Só os metadados aqui. Marcadores NÃO: executarConsolidacao já
            // chamou embutirMarcadoresNoBackup no fim, e rodar de novo anexa
            // um segundo par de chunks cue/LIST/iXML no mesmo WAV.
            int metadados = matriz::consolidacao::embutirMetadadosNoBackup(
                projeto.projeto().registro(), destinoMedia);
            (void)metadados;
        }

        if (!safeThis) return;

        // Auto-export CSV, XLS, BKM to Project/relatorios folder
        if (!resultado.cancelado) {
            safeThis->labelProgressoStatus_->setText("Exporting CSV, XLS, Dublin Core and checksums...", juce::dontSendNotification);
            ProgressoGlobal::obterInstancia().atualizarDetalhe("backup", "Exporting CSV, XLS & checksums...");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (safeThis) {
                juce::File relatoriosDir = safeThis->projeto_.projeto().pasta().getChildFile("relatorios");
                relatoriosDir.createDirectory();
                safeThis->exportarCsvPara(relatoriosDir);
                safeThis->exportarXlsPara(relatoriosDir);
                safeThis->exportarDublinCorePara(relatoriosDir);
                safeThis->exportarChecksumsPara(relatoriosDir);
            }
        }

        // Increment and confirm revision on active project
        if (!resultado.cancelado) {
            safeThis->projeto_.projeto().confirmarRevisao();
        }

        // Auto-mirroring to online clones
        if (!resultado.cancelado) {
            safeThis->labelProgressoStatus_->setText("Mirroring to connected clones...", juce::dontSendNotification);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            if (safeThis) {
                auto espelhoRes = matriz::sync::SyncEngine::executarEspelhamentoAutomatico(safeThis->projeto_.projeto());
                (void)espelhoRes;
            }
        }

        safeThis->executando_ = false;
        safeThis->estado_ = Estado::Done;

        // A barra parava no último valor reportado (86%) ao lado de "concluído
        // com sucesso" — dois sinais contraditórios na mesma tela.
        safeThis->progressoValor_ = 1.0;

        const bool houveFalha = safeThis->falhasCount_ > 0;
        juce::String rotuloSugerido = safeThis->selectedDestinoIdx_ >= 0 && safeThis->selectedDestinoIdx_ < static_cast<int>(safeThis->destinosBackup_.size())
            ? safeThis->destinosBackup_[static_cast<size_t>(safeThis->selectedDestinoIdx_)].rotulo
            : (safeThis->googleDriveComoDestino_ ? juce::String("Google Drive") : destinoRaiz.getFileName());
        safeThis->registrarDestinoBackup(destinoRaiz, rotuloSugerido, safeThis->copiadoCount_, resultado.pulados, safeThis->falhasCount_, resultado.cancelado);

        juce::String msgFinal = resultado.cancelado ? "Backup cancelled"
                                                   : (houveFalha ? "Backup finished with " + juce::String(safeThis->falhasCount_) + " errors"
                                                                 : "Backup completed: " + juce::String(safeThis->copiadoCount_) + " assets consolidated");
        ProgressoGlobal::obterInstancia().concluirTarefa("backup", msgFinal);

        // A linha de destaque diz o QUE aconteceu; a de baixo, os números.
        safeThis->labelProgressoStatus_->setText(
            resultado.cancelado ? "Backup cancelled."
                                : (houveFalha ? "Backup finished with errors."
                                              : "Backup completed successfully."),
            juce::dontSendNotification);
        safeThis->labelProgressoStatus_->setColour(
            juce::Label::textColourId,
            houveFalha ? tema().perigo : (resultado.cancelado ? tema().alerta : tema().estadoQcOk));

        juce::String finalMsg;
        finalMsg << safeThis->copiadoCount_ << " copied   |   "
                 << safeThis->verificadoCount_ << " verified";
        if (houveFalha) finalMsg << "   |   " << safeThis->falhasCount_ << " failed";
        finalMsg << "\n" << destinoRaiz.getFullPathName();
        if (houveFalha) {
            finalMsg << "\n";
            int mostradas = 0;
            for (const auto& f : safeThis->falhasLista_) {
                if (mostradas++ >= 4) break;
                finalMsg << "\n" << f;
            }
            if (safeThis->falhasLista_.size() > 4)
                finalMsg << "\n... and " << static_cast<int>(safeThis->falhasLista_.size()) - 4 << " more.";
        }
        safeThis->labelResumo_->setText(finalMsg, juce::dontSendNotification);

        safeThis->labelTitulo_->setText(resultado.cancelado ? "Backup Cancelled" : "Backup Complete",
                                         juce::dontSendNotification);
        safeThis->btnStartBackup_->setVisible(false);
        if (safeThis->btnCancelarExecucao_) safeThis->btnCancelarExecucao_->setVisible(false);
        safeThis->btnDone_->setVisible(true);
        safeThis->resized();
    });
}

void BackupWorkspaceComponent::carregarDestinosBackup() {
    destinosBackup_.clear();
    projeto_.sincronizarBackupDestinoDeHistorico();

    auto& db = projeto_.projeto().registro();
    try {
        auto stmt = db.prepare("SELECT id, rotulo, destino_path, papel, ativo FROM backup_destino WHERE ativo = 1 ORDER BY criado_em ASC");
        while (stmt.step()) {
            DestinoBackupItem d;
            d.id = stmt.columnText(0);
            d.rotulo = juce::String::fromUTF8(stmt.columnText(1).c_str());
            d.caminho = juce::String::fromUTF8(stmt.columnText(2).c_str());
            d.papel = stmt.columnIsNull(3) ? "CLONE" : stmt.columnText(3);
            d.online = juce::File(d.caminho).isDirectory();
            d.ativo = (juce::File(d.caminho) == projeto_.projeto().raiz());
            destinosBackup_.push_back(std::move(d));
        }
    } catch (...) {}

    // Garante que a pasta oficial criada no início do projeto (DESTINATION Original) esteja sempre listada
    juce::File raizProjeto = projeto_.projeto().raiz();
    bool encontrouRaiz = false;
    for (auto& d : destinosBackup_) {
        if (juce::File(d.caminho) == raizProjeto) {
            encontrouRaiz = true;
            d.ativo = true;
            if (projeto_.projeto().papel() == "ORIGINAL" || d.papel != "CLONE") {
                d.papel = "ORIGINAL";
            }
            break;
        }
    }

    if (!encontrouRaiz && raizProjeto.isDirectory()) {
        DestinoBackupItem d;
        d.id = projeto_.projeto().destinationId();
        d.rotulo = raizProjeto.getFileName();
        d.caminho = raizProjeto.getFullPathName();
        d.papel = "ORIGINAL";
        d.online = true;
        d.ativo = true;
        destinosBackup_.push_back(std::move(d));
    }

    // Remove entradas que são subpastas da raiz do projeto — ex: Media/ auto-registrada como clone.
    destinosBackup_.erase(
        std::remove_if(destinosBackup_.begin(), destinosBackup_.end(),
            [&raizProjeto](const DestinoBackupItem& d) {
                return juce::File(d.caminho).isAChildOf(raizProjeto);
            }),
        destinosBackup_.end());

    // MAIN ORIGINAL aparece sempre em 1º lugar no topo da lista
    std::stable_partition(destinosBackup_.begin(), destinosBackup_.end(),
                          [](const DestinoBackupItem& d) { return d.papel == "ORIGINAL"; });

    for (const auto& d : destinosBackup_) {
        if (d.online) {
            matriz::model::sanitizarEstruturaDestino(juce::File(d.caminho));
        }
    }

    if (listVaults_) {
        listVaults_->updateContent();
        listVaults_->repaint();
    }
}

void BackupWorkspaceComponent::adicionarOuAtivarDestino(const juce::File& pasta, const juce::String& rotuloSugerido) {
    if (pasta == juce::File()) return;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    auto dentroDeMediaOuProject = [](const juce::File& f) {
        juce::File cur = f;
        while (cur != juce::File() && cur != cur.getParentDirectory()) {
            if (cur.getFileName().equalsIgnoreCase("Media") || cur.getFileName().equalsIgnoreCase("Project"))
                return true;
            cur = cur.getParentDirectory();
        }
        return false;
    };

    if (dentroDeMediaOuProject(pasta) || pasta.isAChildOf(projeto_.projeto().raiz()) || pasta.isAChildOf(projeto_.projeto().pasta())) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Destino Inválido") : juce::String("Invalid Destination"),
            isPt ? juce::String::fromUTF8("A pasta de destino não pode se chamar 'Media' ou 'Project', nem estar dentro de uma pasta Media ou Project.")
                 : "The destination folder cannot be named 'Media' or 'Project', nor be inside a Media or Project folder.");
        return;
    }

    auto& db = projeto_.projeto().registro();
    std::string destPath = pasta.getFullPathName().toStdString();
    std::string rotulo = rotuloSugerido.trim().isNotEmpty() ? rotuloSugerido.toStdString() : pasta.getFileName().toStdString();
    if (rotulo.empty()) rotulo = "Backup";

    try {
        auto stmtCheck = db.prepare("SELECT id, rotulo, ativo FROM backup_destino WHERE destino_path = ?");
        stmtCheck.bind(1, matriz::db::Value::of(destPath));
        if (stmtCheck.step()) {
            int ativo = stmtCheck.columnInt(2);
            if (ativo == 0) {
                db.run("UPDATE backup_destino SET ativo = 1 WHERE destino_path = ?", {matriz::db::Value::of(destPath)});
            }
        } else {
            std::string novoId = matriz::model::novoUuid();
            std::string agora = matriz::model::agoraIso8601();
            db.run("INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, ultima_revisao_conhecida) VALUES (?, ?, ?, ?, 'CLONE', 1, ?, ?)",
                   {matriz::db::Value::of(novoId), matriz::db::Value::of(novoId), matriz::db::Value::of(destPath),
                    matriz::db::Value::of(rotulo), matriz::db::Value::of(agora),
                    matriz::db::Value::of(static_cast<int64_t>(projeto_.projeto().revisao()))});

            // Garante que destination.json exista no destino registrado
            juce::File destJson = pasta.getChildFile("destination.json");
            if (!destJson.existsAsFile()) {
                matriz::model::DestinationInfo dInfo;
                dInfo.formato = 1;
                dInfo.destinationId = novoId;
                dInfo.projetoId = projeto_.projeto().projetoId();
                dInfo.papel = "CLONE";
                dInfo.rotulo = rotulo;
                dInfo.revisao = projeto_.projeto().revisao();
                dInfo.ultimaEdicaoUtc = agora;
                dInfo.criadoEm = agora;
                dInfo.gravarEmArquivo(destJson);
            }

            // Log: Backup Version Created
            matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
            juce::StringArray details;
            details.add("Label: " + juce::String::fromUTF8(rotulo.c_str()));
            details.add("Path: " + pasta.getFullPathName());
            pLog.appendEntry("Backup Version Created", details);
        }
    } catch (...) {}
}

void BackupWorkspaceComponent::criarNovoClone(const juce::File& folder) {
    if (folder == juce::File()) return;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    auto dentroDeMediaOuProject = [](const juce::File& f) {
        juce::File cur = f;
        while (cur != juce::File() && cur != cur.getParentDirectory()) {
            if (cur.getFileName().equalsIgnoreCase("Media") || cur.getFileName().equalsIgnoreCase("Project"))
                return true;
            cur = cur.getParentDirectory();
        }
        return false;
    };

    if (dentroDeMediaOuProject(folder) || folder.isAChildOf(projeto_.projeto().raiz()) || folder.isAChildOf(projeto_.projeto().pasta())) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Destino Inválido") : juce::String("Invalid Destination"),
            isPt ? juce::String::fromUTF8("A pasta de destino não pode se chamar 'Media' ou 'Project', nem estar dentro de uma pasta Media ou Project.")
                 : "The destination folder cannot be named 'Media' or 'Project', nor be inside a Media or Project folder.");
        return;
    }

    if (folder == projeto_.projeto().raiz() || folder == projeto_.projeto().pasta()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Destino Inválido") : juce::String("Invalid Destination"),
            isPt ? juce::String::fromUTF8("A pasta de destino não pode ser a própria pasta do projeto atual.")
                 : "The destination folder cannot be the active project's own folder.");
        return;
    }

    juce::File destJsonFile = folder.getChildFile("destination.json");
    if (destJsonFile.existsAsFile()) {
        // Already a destination
        auto destInfoOpt = matriz::model::DestinationInfo::lerDeArquivo(destJsonFile);
        if (destInfoOpt.has_value()) {
            if (destInfoOpt->destinationId == projeto_.projeto().destinationId()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    isPt ? juce::String::fromUTF8("Destino Existente") : juce::String("Existing Destination"),
                    isPt ? juce::String::fromUTF8("Esta pasta já é o próprio DESTINATION ativo.")
                         : "This folder is already the active DESTINATION.");
                return;
            }
            // Link existing destination
            auto& db = projeto_.projeto().registro();
            try {
                auto stmtCheck = db.prepare("SELECT id FROM backup_destino WHERE destino_path = ?");
                stmtCheck.bind(1, matriz::db::Value::of(folder.getFullPathName().toStdString()));
                if (stmtCheck.step()) {
                    db.run(
                        "UPDATE backup_destino SET ativo = 1, destination_id = ?, papel = ?, "
                        "ultima_revisao_conhecida = ?, ultimo_visto_em = ?, ultima_edicao_conhecida = ? WHERE destino_path = ?",
                        {matriz::db::Value::of(destInfoOpt->destinationId),
                         matriz::db::Value::of(destInfoOpt->papel.empty() ? "CLONE" : destInfoOpt->papel),
                         matriz::db::Value::of(static_cast<int64_t>(destInfoOpt->revisao)),
                         matriz::db::Value::of(matriz::model::agoraIso8601()),
                         matriz::db::Value::of(destInfoOpt->ultimaEdicaoUtc),
                         matriz::db::Value::of(folder.getFullPathName().toStdString())});
                } else {
                    db.run(
                        "INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                        "ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                        "VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?, ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(destInfoOpt->destinationId),
                         matriz::db::Value::of(folder.getFullPathName().toStdString()),
                         matriz::db::Value::of(folder.getFileName().toStdString()),
                         matriz::db::Value::of(destInfoOpt->papel.empty() ? "CLONE" : destInfoOpt->papel),
                         matriz::db::Value::of(matriz::model::agoraIso8601()),
                         matriz::db::Value::of(static_cast<int64_t>(destInfoOpt->revisao)),
                         matriz::db::Value::of(matriz::model::agoraIso8601()),
                         matriz::db::Value::of(destInfoOpt->ultimaEdicaoUtc)});
                }
            } catch (...) {}

            carregarDestinosBackup();
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                isPt ? juce::String::fromUTF8("Destino Vinculado") : juce::String("Destination Linked"),
                isPt ? juce::String::fromUTF8("Destino existente vinculado com sucesso ao projeto.")
                     : "Existing destination linked successfully to the project.");
            return;
        }
    }

    if (folder.exists() && !folder.findChildFiles(juce::File::findFilesAndDirectories, false).isEmpty()) {
        bool ok = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Pasta Não Vazia") : juce::String("Folder Not Empty"),
            isPt ? juce::String::fromUTF8("A pasta selecionada não está vazia. Deseja criar o destino nesta pasta?")
                 : "The selected folder is not empty. Do you want to create the destination here?",
            isPt ? juce::String::fromUTF8("Continuar") : juce::String("Proceed"),
            isPt ? juce::String::fromUTF8("Cancelar") : juce::String("Cancel"));
        if (!ok) return;
    }

    // Check disk space
    juce::int64 espacoNecessario = 0;
    for (auto& f : projeto_.projeto().pasta().findChildFiles(juce::File::findFiles, true)) {
        espacoNecessario += f.getSize();
    }
    for (auto& f : projeto_.projeto().pastaMedia().findChildFiles(juce::File::findFiles, true)) {
        espacoNecessario += f.getSize();
    }
    juce::int64 espacoLivre = folder.getBytesFreeOnVolume();
    if (espacoLivre > 0 && espacoLivre < espacoNecessario) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            isPt ? juce::String::fromUTF8("Espaço Insuficiente") : juce::String("Insufficient Disk Space"),
            isPt ? juce::String::fromUTF8("Espaço livre em disco insuficiente para criar o destino.\nNecessário: ") +
                   juce::File::descriptionOfSizeInBytes(espacoNecessario) + juce::String::fromUTF8("\nDisponível: ") +
                   juce::File::descriptionOfSizeInBytes(espacoLivre)
                 : "Insufficient disk space to create destination.\nRequired: " +
                   juce::File::descriptionOfSizeInBytes(espacoNecessario) + "\nAvailable: " +
                   juce::File::descriptionOfSizeInBytes(espacoLivre));
        return;
    }

    // Perform backup creation
    ProgressoGlobal::obterInstancia().iniciarTarefa(
        "clone",
        isPt ? "Criando Destino de Backup" : "Creating Backup Destination",
        100,
        nullptr,
        isPt ? "Copiando banco de dados e arquivos..." : "Copying database and project files...");

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    juce::File pastaProjeto = projeto_.projeto().pasta();
    juce::File pastaMedia = projeto_.projeto().pastaMedia();
    juce::File raizProjeto = projeto_.projeto().raiz();
    std::string projId = projeto_.projeto().projetoId();
    std::string destId = projeto_.projeto().destinationId();
    int64_t projRev = static_cast<int64_t>(projeto_.projeto().revisao());

    // Assegura que o projeto de origem esteja perfeitamente higienizado antes do clone
    matriz::model::sanitizarEstruturaDestino(raizProjeto);

    juce::Thread::launch([safeThis, folder, isPt, pastaProjeto, pastaMedia, raizProjeto, projId, destId, projRev]() {
        try {
            folder.createDirectory();
            juce::File cloneProject = folder.getChildFile("Project");
            cloneProject.createDirectory();
            juce::File cloneMedia = folder.getChildFile("Media");
            cloneMedia.createDirectory();

            // Safe copy of databases
            juce::File dbReg = cloneProject.getChildFile("registro.sqlite");
            dbReg.deleteFile();
            juce::File srcReg = pastaProjeto.getChildFile("registro.sqlite");
            if (srcReg.existsAsFile()) {
                matriz::db::Database srcDb(srcReg.getFullPathName().toStdString());
                srcDb.copiarSeguroPara(dbReg.getFullPathName().toStdString());
            }

            juce::File dbInd = cloneProject.getChildFile("indice.sqlite");
            dbInd.deleteFile();
            juce::File srcInd = pastaProjeto.getChildFile("indice.sqlite");
            if (srcInd.existsAsFile()) {
                matriz::db::Database srcDbInd(srcInd.getFullPathName().toStdString());
                srcDbInd.copiarSeguroPara(dbInd.getFullPathName().toStdString());
            }

            // Copy other project files
            for (auto& child : pastaProjeto.findChildFiles(juce::File::findFilesAndDirectories, false)) {
                juce::String name = child.getFileName();
                if (name == "registro.sqlite" || name == "indice.sqlite" ||
                    name.startsWith("registro.sqlite-") || name.startsWith("indice.sqlite-") ||
                    name == ".sync_em_andamento") {
                    continue;
                }
                if (child.isDirectory()) {
                    child.copyDirectoryTo(cloneProject.getChildFile(name));
                } else {
                    child.copyFileTo(cloneProject.getChildFile(name));
                }
            }

            // Copy media files (only genuine media, never system/project folders)
            if (pastaMedia.isDirectory()) {
                for (auto& child : pastaMedia.findChildFiles(juce::File::findFilesAndDirectories, false)) {
                    juce::String name = child.getFileName();
                    if (name == ".DS_Store" || name == "_lixeira" || name == "destination.json" ||
                        name == "Project" || name == "log" || name == "relatorios" ||
                        name == "catalogo" || name == "Media") {
                        continue;
                    }
                    if (child.isDirectory()) {
                        child.copyDirectoryTo(cloneMedia.getChildFile(name));
                    } else {
                        child.copyFileTo(cloneMedia.getChildFile(name));
                    }
                }
            }

            // Copy project file (.mtz / .bkm) to root and Project/ of clone
            for (auto& fProj : raizProjeto.findChildFiles(juce::File::findFiles, false, "*.mtz;*.bkm")) {
                fProj.copyFileTo(folder.getChildFile(fProj.getFileName()));
                fProj.copyFileTo(cloneProject.getChildFile(fProj.getFileName()));
            }

            // Write destination.json in clone
            matriz::model::DestinationInfo cloneInfo;
            cloneInfo.destinationId = matriz::model::novoUuid();
            cloneInfo.projetoId = projId;
            cloneInfo.papel = "CLONE";
            cloneInfo.rotulo = folder.getFileName().toStdString();
            cloneInfo.revisao = projRev;
            cloneInfo.ultimaEdicaoUtc = matriz::model::agoraIso8601();
            cloneInfo.criadoEm = matriz::model::agoraIso8601();
            cloneInfo.gravarEmArquivo(folder.getChildFile("destination.json"));

            // Rigorously sanitize clone structure
            matriz::model::sanitizarEstruturaDestino(folder);

            // Register clone in active project's backup_destino
            {
                matriz::db::Database activeDb(pastaProjeto.getChildFile("registro.sqlite").getFullPathName().toStdString());
                auto stmtCheck = activeDb.prepare("SELECT id FROM backup_destino WHERE destino_path = ?");
                stmtCheck.bind(1, matriz::db::Value::of(folder.getFullPathName().toStdString()));
                if (stmtCheck.step()) {
                    activeDb.run(
                        "UPDATE backup_destino SET ativo = 1, destination_id = ?, papel = 'CLONE', "
                        "ultima_revisao_conhecida = ?, ultimo_visto_em = ?, ultima_edicao_conhecida = ? WHERE destino_path = ?",
                        {matriz::db::Value::of(cloneInfo.destinationId),
                         matriz::db::Value::of(static_cast<int64_t>(cloneInfo.revisao)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(folder.getFullPathName().toStdString())});
                } else {
                    activeDb.run(
                        "INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                        "ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                        "VALUES (?, ?, ?, ?, 'CLONE', 1, ?, ?, ?, ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(cloneInfo.destinationId),
                         matriz::db::Value::of(folder.getFullPathName().toStdString()),
                         matriz::db::Value::of(folder.getFileName().toStdString()),
                         matriz::db::Value::of(cloneInfo.criadoEm),
                         matriz::db::Value::of(static_cast<int64_t>(cloneInfo.revisao)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc)});
                }
            }

            // Register active destination inside clone's backup_destino
            try {
                matriz::db::Database cloneDb(dbReg.getFullPathName().toStdString());
                auto stmtCheck = cloneDb.prepare("SELECT id FROM backup_destino WHERE destino_path = ?");
                stmtCheck.bind(1, matriz::db::Value::of(raizProjeto.getFullPathName().toStdString()));
                if (stmtCheck.step()) {
                    cloneDb.run(
                        "UPDATE backup_destino SET ativo = 1, destination_id = ?, papel = 'ORIGINAL', "
                        "ultima_revisao_conhecida = ?, ultimo_visto_em = ?, ultima_edicao_conhecida = ? WHERE destino_path = ?",
                        {matriz::db::Value::of(destId),
                         matriz::db::Value::of(static_cast<int64_t>(projRev)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(raizProjeto.getFullPathName().toStdString())});
                } else {
                    cloneDb.run(
                        "INSERT INTO backup_destino (id, destination_id, destino_path, rotulo, papel, ativo, criado_em, "
                        "ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
                        "VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?, ?)",
                        {matriz::db::Value::of(matriz::model::novoUuid()),
                         matriz::db::Value::of(destId),
                         matriz::db::Value::of(raizProjeto.getFullPathName().toStdString()),
                         matriz::db::Value::of(raizProjeto.getFileName().toStdString()),
                         matriz::db::Value::of(std::string("ORIGINAL")),
                         matriz::db::Value::of(cloneInfo.criadoEm),
                         matriz::db::Value::of(static_cast<int64_t>(projRev)),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc),
                         matriz::db::Value::of(cloneInfo.ultimaEdicaoUtc)});
                }
            } catch (...) {}

            // Logs
            matriz::model::ProjectLog activeLog(pastaProjeto);
            activeLog.appendEntry("Clone Created", {"Path: " + folder.getFullPathName(), "Destination ID: " + juce::String(cloneInfo.destinationId)});

            matriz::model::ProjectLog cloneLog(cloneProject);
            cloneLog.appendEntry("Clone Initialized", {"Origin: " + raizProjeto.getFullPathName()});

            juce::MessageManager::callAsync([safeThis, folderPath = folder.getFullPathName(), isPt]() {
                ProgressoGlobal::obterInstancia().concluirTarefa("clone", isPt ? "Destino criado com sucesso." : "Destination created successfully.");
                if (safeThis) {
                    safeThis->carregarDestinosBackup();
                    for (int i = 0; i < static_cast<int>(safeThis->destinosBackup_.size()); ++i) {
                        if (safeThis->destinosBackup_[static_cast<size_t>(i)].caminho == folderPath) {
                            safeThis->selectedDestinoIdx_ = i;
                            if (safeThis->listVaults_) safeThis->listVaults_->selectRow(i);
                            safeThis->resolvedDestFolder_ = juce::File(folderPath);
                            safeThis->customDestFolder_ = safeThis->resolvedDestFolder_;
                            safeThis->dispararScanDestino(true);
                            break;
                        }
                    }
                }

                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    isPt ? "Destino Criado" : "Destination Created",
                    isPt ? juce::String::fromUTF8("Novo destino de backup criado com sucesso em:\n") + folderPath
                         : "New backup destination created successfully at:\n" + folderPath);
            });
        } catch (const std::exception& e) {
            juce::String erroMsg(e.what());
            juce::MessageManager::callAsync([isPt, erroMsg]() {
                ProgressoGlobal::obterInstancia().concluirTarefa("clone", isPt ? "Erro ao criar destino." : "Error creating destination.");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    isPt ? "Erro ao Criar Destino" : "Destination Creation Error",
                    erroMsg);
            });
        }
    });
}

void BackupWorkspaceComponent::desvincularDestino(const DestinoBackupItem& dest) {
    if (dest.papel == "ORIGINAL") return;
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        matriz::i18n::t("backup.desvincular"),
        matriz::i18n::t("backup.desvincular_confirmacao"),
        matriz::i18n::t("backup.desvincular"),
        isPt ? "Cancelar" : "Cancel",
        nullptr,
        juce::ModalCallbackFunction::create([this, dest](int result) {
            if (result != 1) return;

            auto& db = projeto_.projeto().registro();
            if (dest.id.empty()) {
                db.run("UPDATE backup_destino SET ativo = 0 WHERE destino_path = ?",
                       {matriz::db::Value::of(dest.caminho.toStdString())});
            } else {
                db.run("UPDATE backup_destino SET ativo = 0 WHERE id = ?",
                       {matriz::db::Value::of(dest.id)});
            }

            matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
            juce::StringArray details;
            details.add("Label: " + dest.rotulo);
            details.add("Path: " + dest.caminho);
            pLog.appendEntry("Backup Version Unlinked", details);

            carregarDestinosBackup();
            carregarDestinoAtivoInicial();
        }));
}

// --- ListBoxModel implementation for Destinations list ---

int BackupWorkspaceComponent::getNumRows() {
    return static_cast<int>(destinosBackup_.size());
}

void BackupWorkspaceComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber >= static_cast<int>(destinosBackup_.size())) return;
    const auto& tk = tema();
    const auto& dest = destinosBackup_[static_cast<size_t>(rowNumber)];
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (rowIsSelected) {
        g.fillAll(tk.acento.withAlpha(0.18f));
        g.setColour(tk.bordaFoco);
        g.drawRect(0, 0, width, height, 1);
    } else {
        g.fillAll(rowNumber % 2 == 0 ? tk.painel : tk.painelAlt);
    }

    int curX = 8;
    bool isOriginal = (dest.papel == "ORIGINAL");

    // Papel tag [MAIN ORIGINAL] or [DESTINATION]
    int tagW = isOriginal ? 104 : (isPt ? 72 : 88);
    juce::Rectangle<int> papelArea(curX, (height - 20) / 2, tagW, 20);
    if (isOriginal) {
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(papelArea.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle(papelArea.toFloat().reduced(0.5f), 4.0f, 1.2f);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("MAIN ORIGINAL", papelArea, juce::Justification::centred, true);
    } else {
        g.setColour(tk.painel.withAlpha(0.6f));
        g.fillRoundedRectangle(papelArea.toFloat(), 4.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(papelArea.toFloat().reduced(0.5f), 4.0f, 1.0f);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(isPt ? "DESTINO" : "DESTINATION", papelArea, juce::Justification::centred, true);
    }
    curX += tagW + 10;

    // Online/Offline status badge on the far right
    int statusW = 68;
    juce::Rectangle<int> statusArea(width - statusW - 8, (height - 18) / 2, statusW, 18);
    g.setColour(dest.online ? tk.estadoQcOk.withAlpha(0.85f) : tk.textoTerciario.withAlpha(0.5f));
    g.fillRoundedRectangle(statusArea.toFloat(), 4.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    g.drawText(dest.online ? "ONLINE" : "OFFLINE", statusArea, juce::Justification::centred, true);

    // Cloud icon on right side before status area, if cloud destination
    bool ehNuvem = ehDestinoNuvem(dest.caminho, dest.rotulo, dest.papel);
    int labelRight = statusArea.getX() - 10;
    if (ehNuvem) {
        labelRight -= 24;
        juce::Rectangle<float> cloudRect(static_cast<float>(labelRight + 4), static_cast<float>(height - 12) / 2.0f, 16.0f, 12.0f);
        juce::Colour cloudCol = (tk.fundo.getBrightness() > 0.5f) ? juce::Colour(0xff0284c7) : juce::Colour(0xff38bdf8);
        desenharIconeNuvem(g, cloudRect, cloudCol);
    }

    // Volume name gets all the breathing room between tag and status badge
    int labelW = std::max(20, labelRight - curX);
    g.setColour(isOriginal ? tk.textoPrimario : tk.textoPrimario.withAlpha(0.9f));
    auto fontRotulo = juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo - 0.5f, juce::Font::bold));
    g.setFont(fontRotulo);
    g.drawText(dest.rotulo, curX, 0, labelW, height, juce::Justification::centredLeft, true);
}

void BackupWorkspaceComponent::listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) {
    if (rowNumber >= 0 && rowNumber < static_cast<int>(destinosBackup_.size())) {
        selectedDestinoIdx_ = rowNumber;
        if (listVaults_) listVaults_->selectRow(rowNumber);

        const auto& dest = destinosBackup_[static_cast<size_t>(rowNumber)];
        juce::String loc = dest.caminho;
        resolvedDestFolder_ = matriz::model::normalizarParaRaizDestino(juce::File(loc));
        customDestFolder_ = resolvedDestFolder_;
        bool isPt = matriz::i18n::localeAtivo().startsWith("pt");
        juce::String tipoStr = (dest.papel == "ORIGINAL")
            ? (isPt ? juce::String::fromUTF8("[MAIN ORIGINAL - Destino Oficial]") : "[MAIN ORIGINAL - Official Destination]")
            : (isPt ? juce::String::fromUTF8("[DESTINO - Cópia de Backup]") : "[DESTINATION - Backup Copy]");
        juce::String textoInfo = (isPt ? juce::String::fromUTF8("Selecionado: ") : "Selected: ") + loc + "  " + tipoStr;
        labelDestInfo_->setText(textoInfo, juce::dontSendNotification);
        labelDestInfo_->setTooltip(textoInfo);
        dispararScanDestino(true);

        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addSectionHeader(dest.caminho);
            juce::String itemTxt = isPt ? juce::String::fromUTF8("Revelar no Finder") : "Reveal in Finder";
            juce::File f(dest.caminho);
            menu.addItem(1, itemTxt, dest.online && f.exists());

            juce::String copyTxt = isPt ? juce::String::fromUTF8("Copiar Caminho") : "Copy Path";
            menu.addItem(3, copyTxt, true);

            if (dest.papel != "ORIGINAL") {
                menu.addSeparator();
                juce::String unlinkTxt = isPt ? juce::String::fromUTF8("Desvincular do Backup...")
                                              : "Unlink from Backup...";
                menu.addItem(2, unlinkTxt, true);
            }

            juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, dest](int res) {
                if (!safeThis) return;
                if (res == 1) {
                    juce::File alvo(dest.caminho);
                    if (alvo.exists()) {
                        alvo.revealToUser();
                    }
                } else if (res == 2) {
                    safeThis->desvincularDestino(dest);
                } else if (res == 3) {
                    juce::SystemClipboard::copyTextToClipboard(dest.caminho);
                }
            });
        }
    }
}

static juce::String formatarEspacoLivre(juce::int64 bytes) {
    if (bytes <= 0) return "0 B";
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    if (bytes < 1024LL * 1024LL * 1024LL * 1024LL) return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    return juce::String(bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0), 2) + " TB";
}

juce::String BackupWorkspaceComponent::getTooltipForRow(int rowNumber) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(destinosBackup_.size())) return {};
    const auto& dest = destinosBackup_[static_cast<size_t>(rowNumber)];
    bool isPt = matriz::i18n::localeAtivo().startsWith("pt");

    juce::File caminhoF(dest.caminho);
    auto infoVol = matriz::vault::descreverVolume(caminhoF);
    juce::String nomeVolume = infoVol.nome.empty()
        ? (infoVol.hardware.volumeLabel.empty() ? caminhoF.getFileName() : juce::String(infoVol.hardware.volumeLabel))
        : juce::String(infoVol.nome);
    if (nomeVolume.isEmpty()) nomeVolume = "Macintosh HD";

    juce::String caminhoCompleto = dest.caminho;
    if (caminhoCompleto.isEmpty()) caminhoCompleto = caminhoF.getFullPathName();

    juce::int64 freeBytes = caminhoF.getBytesFreeOnVolume();
    juce::String freeSpaceStr = formatarEspacoLivre(freeBytes);

    if (isPt) {
        return juce::String::fromUTF8("Nome do Volume: ") + nomeVolume + "\n" +
               juce::String::fromUTF8("Espaço Livre: ") + freeSpaceStr + "\n" +
               juce::String::fromUTF8("Caminho Completo: ") + caminhoCompleto;
    }
    return "Volume Name: " + nomeVolume + "\n" +
           "Free Space: " + freeSpaceStr + "\n" +
           "Full Path: " + caminhoCompleto;
}

void BackupWorkspaceComponent::carregarDestinoAtivoInicial() {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    resolvedDestFolder_ = projeto_.projeto().raiz();
    customDestFolder_ = resolvedDestFolder_;
    selectedDestinoIdx_ = -1;

    for (size_t i = 0; i < destinosBackup_.size(); ++i) {
        if (destinosBackup_[i].caminho == projeto_.projeto().raiz().getFullPathName() ||
            (selectedDestinoIdx_ < 0 && destinosBackup_[i].papel == "ORIGINAL")) {
            selectedDestinoIdx_ = static_cast<int>(i);
            if (destinosBackup_[i].caminho == projeto_.projeto().raiz().getFullPathName()) break;
        }
    }
    if (selectedDestinoIdx_ < 0 && !destinosBackup_.empty()) {
        selectedDestinoIdx_ = 0;
    }

    if (selectedDestinoIdx_ >= 0 && selectedDestinoIdx_ < static_cast<int>(destinosBackup_.size())) {
        if (listVaults_) listVaults_->selectRow(selectedDestinoIdx_);
        const auto& dest = destinosBackup_[static_cast<size_t>(selectedDestinoIdx_)];
        resolvedDestFolder_ = matriz::model::normalizarParaRaizDestino(juce::File(dest.caminho));
        customDestFolder_ = resolvedDestFolder_;
        if (labelDestInfo_) {
            juce::String tipoStr = (dest.papel == "ORIGINAL")
                ? (isPt ? juce::String::fromUTF8("[MAIN ORIGINAL - Destino Oficial]") : "[MAIN ORIGINAL - Official Destination]")
                : (isPt ? juce::String::fromUTF8("[DESTINO - Cópia de Backup]") : "[DESTINATION - Backup Copy]");
            juce::String activeInfo = (isPt ? juce::String::fromUTF8("Destino Ativo: ") : "Active Destination: ")
                                    + dest.caminho + "  " + tipoStr;
            labelDestInfo_->setText(activeInfo, juce::dontSendNotification);
            labelDestInfo_->setTooltip(activeInfo);
        }
    }

    dispararScanDestino(false);
    atualizarResumo();
}

void BackupWorkspaceComponent::dispararScanDestino(bool forcado) {
    (void)forcado;
    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    if (isCatalogMode) {
        atualizarResumo();
        return;
    }

    juce::File destinoRaiz = matriz::model::normalizarParaRaizDestino(resolvedDestFolder_);
    juce::File targetDir = destinoRaiz.getChildFile("Media");
    if (!targetDir.isDirectory() || plano_.itens.empty()) {
        scanResult_.itensGrid.clear();
        scanResult_.orfaos.clear();
        scanResult_.totalVerdes = 0;
        scanResult_.totalVermelhos = static_cast<int>(plano_.itens.size());
        scanResult_.totalOrfaos = 0;
        for (const auto& ip : plano_.itens) {
            matriz::consolidacao::ItemStatusBackup isb;
            isb.itemId = ip.itemId;
            isb.arquivoId = ip.arquivoId;
            isb.codigoAcervo = ip.codigoAcervo;
            isb.nomeArquivo = ip.nomeOriginal;
            isb.caminhoRelativoDestino = ip.caminhoRelativoDestino;
            isb.tamanhoBytes = ip.tamanhoBytes;
            isb.status = matriz::consolidacao::StatusArquivoBackup::Vermelho;
            scanResult_.itensGrid.push_back(std::move(isb));
        }
        if (listPrevia_) {
            listPrevia_->definirScan(scanResult_);
            listPrevia_->definirOrfaos(scanResult_.orfaos, targetDir);
        }
        atualizarResumo();
        return;
    }

    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    BackupScanProgressDialog::showModal(
        projeto_.projeto().registro(),
        projeto_.projeto().pasta(),
        targetDir,
        plano_,
        [safeThis, targetDir](const matriz::consolidacao::ResultadoScanBackup& result) {
            if (!safeThis) return;
            safeThis->scanResult_ = result;
            safeThis->scanRealizado_ = true;
            if (safeThis->listPrevia_) {
                safeThis->listPrevia_->definirScan(result);
                safeThis->listPrevia_->definirOrfaos(result.orfaos, targetDir);
            }
            safeThis->atualizarResumo();
        }
    );
}

void BackupWorkspaceComponent::executarBackupAcao(bool forcarOverride) {
    if (toggleForcarRebackup_)
        toggleForcarRebackup_->setToggleState(forcarOverride, juce::dontSendNotification);
    iniciarBackup();
}

void BackupWorkspaceComponent::iniciarSyncComOutroDestino() {
    juce::Component::SafePointer<BackupWorkspaceComponent> safeThis(this);
    SyncDestinationDialog::abrirModal(projeto_.projeto(), [safeThis] {
        if (safeThis) safeThis->recarregar();
    });
}

void BackupWorkspaceComponent::registrarDestinoBackup(const juce::File& destFolder, const juce::String& rotuloSugerido,
                                                      int copiado, int pulados, int falhas, bool cancelado)
{
    if (cancelado) return;

    adicionarOuAtivarDestino(destFolder, rotuloSugerido);

    std::string rotulo = rotuloSugerido.trim().isNotEmpty() ? rotuloSugerido.toStdString() : destFolder.getFileName().toStdString();
    if (rotulo.empty()) rotulo = "Backup";

    // Log: Backup Completed
    matriz::model::ProjectLog pLog(projeto_.projeto().pasta());
    juce::StringArray details;
    details.add("Destination: " + juce::String::fromUTF8(rotulo.c_str()) + " (" + destFolder.getFullPathName() + ")");
    details.add(toggleForcarRebackup_ && toggleForcarRebackup_->getToggleState() ? "Mode: Force Override" : "Mode: Incremental");
    details.add("Copied: " + juce::String(copiado));
    details.add("Skipped: " + juce::String(pulados));
    details.add("Failures: " + juce::String(falhas));
    pLog.appendEntry("Backup Completed", details);

    // Register in Device Usage Log (persists to DB and mirrors to destination external drive)
    try {
        auto& db = projeto_.projeto().registro();
        std::string vId = matriz::vault::obterOuCriarVaultParaDestino(db, destFolder, projeto_.projeto().projetoId());

        juce::StringArray arquivosCopiados;
        juce::int64 bytesCopiados = 0;
        for (const auto& item : scanResult_.itensGrid) {
            arquivosCopiados.add(item.caminhoRelativoDestino);
            bytesCopiados += item.tamanhoBytes;
        }

        juce::String detalhesUso = "Backup: " + juce::String(copiado) + " copied, "
                                 + juce::String(pulados) + " skipped, "
                                 + juce::String(falhas) + " failed";

        matriz::vault::registrarUsoDoDispositivo(
            db,
            projeto_.projeto().pasta(),
            vId,
            "BACKUP",
            copiado,
            bytesCopiados,
            arquivosCopiados,
            detalhesUso.toStdString());
    } catch (...) {}

    // Update scanResult_ in memory
    for (auto& isb : scanResult_.itensGrid) {
        isb.status = matriz::consolidacao::StatusArquivoBackup::Verde;
    }
    scanResult_.totalVerdes = static_cast<int>(scanResult_.itensGrid.size());
    scanResult_.totalVermelhos = 0;
    if (listPrevia_) listPrevia_->definirScan(scanResult_);
}

void BackupWorkspaceComponent::mostrarControlesConfig(bool mostrar) {
    if (configViewport_) configViewport_->setVisible(mostrar);
    if (listPreviaViewport_) listPreviaViewport_->setVisible(mostrar);
    if (labelResumo_) labelResumo_->setVisible(mostrar);
    if (barraLegenda_) barraLegenda_->setVisible(mostrar && projeto_.projeto().modo() != matriz::model::Modo::Catalogo);
    if (comboColecoes_) comboColecoes_->setVisible(mostrar && comboSource_->getSelectedId() == 5);
    if (btnEditarHierarquia_) btnEditarHierarquia_->setVisible(mostrar && comboOrg_->getSelectedId() == 5);
}

void BackupWorkspaceComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    bool isLight = (tk.fundo.getBrightness() > 0.5f);
    juce::Colour bg = (isLight ? tk.fundo.darker(0.30f) : tk.fundo.brighter(0.30f)).brighter(0.30f);
    g.fillAll(bg);

    // Cabeçalho e rodapé como faixas, separados por fio de 1px
    g.setColour(tk.borda);
    if (!faixaCabecalho_.isEmpty())
        g.fillRect(faixaCabecalho_.getX(), faixaCabecalho_.getBottom() - 1, faixaCabecalho_.getWidth(), 1);
    if (!faixaRodape_.isEmpty())
        g.fillRect(faixaRodape_.getX(), faixaRodape_.getY(), faixaRodape_.getWidth(), 1);

    if (!cartaoPrevia_.isEmpty() && estado_ == Estado::Config) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartaoPrevia_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartaoPrevia_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }

    if (!cartaoCentral_.isEmpty()) {
        g.setColour(tk.painel);
        g.fillRoundedRectangle(cartaoCentral_.toFloat(), tk.raioMedio);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cartaoCentral_.toFloat().reduced(0.5f), tk.raioMedio, 1.0f);
    }
}

void BackupWorkspaceComponent::resized() {
    const auto& tk = tema();
    cartaoPrevia_ = {};
    cartaoCentral_ = {};

    const bool emAndamento = (estado_ == Estado::Running || estado_ == Estado::Done);
    mostrarControlesConfig(!emAndamento);

    auto area = getLocalBounds();

    // ---- Cabeçalho ----
    faixaCabecalho_ = area.removeFromTop(64);
    if (labelTitulo_) {
        auto rCabecalho = faixaCabecalho_.reduced(tk.espacoGrande * 2, 0)
                                         .withTrimmedBottom(tk.espacoMedio)
                                         .removeFromBottom(34);
        labelTitulo_->setBounds(rCabecalho);
    }

    // ---- Rodapé ----
    faixaRodape_ = area.removeFromBottom(72);
    auto botoes = faixaRodape_.reduced(tk.espacoGrande * 2, tk.espacoGrande);
    const int alturaBotao = 40;
    botoes = botoes.withSizeKeepingCentre(botoes.getWidth(), alturaBotao);

    bool isCatalogMode = (projeto_.projeto().modo() == matriz::model::Modo::Catalogo);
    bool temItens = !plano_.itens.empty();
    if (isCatalogMode) {
        auto colecoes = projeto_.listarColecoesLinkadas();
        for (const auto& c : colecoes) {
            if (c.valido && c.totalAssets > 0) {
                temItens = true;
                break;
            }
        }
    }

    if (estado_ == Estado::Done) {
        btnDone_->setBounds(botoes.removeFromRight(110));
        btnDone_->setVisible(true);
        btnStartBackup_->setVisible(false);
        if (btnSyncDestino_) btnSyncDestino_->setVisible(false);
        if (btnPublishHtml_) btnPublishHtml_->setVisible(false);
        if (btnExportZip_) btnExportZip_->setVisible(false);
        if (btnLimparZip_) btnLimparZip_->setVisible(false);
        if (btnSendToPrint_) btnSendToPrint_->setVisible(false);
        if (btnLimparPrint_) btnLimparPrint_->setVisible(false);
        if (btnCancelarExecucao_) btnCancelarExecucao_->setVisible(false);
        botoes.removeFromRight(tk.espacoPainel);
        if (btnOpenCatalog_) {
            btnOpenCatalog_->setBounds(botoes.removeFromRight(200));
            btnOpenCatalog_->setVisible(true);
        }
    } else {
        if (btnOpenCatalog_) btnOpenCatalog_->setVisible(false);
        btnDone_->setVisible(false);
        if (btnCancelarExecucao_) btnCancelarExecucao_->setVisible(false);
        btnStartBackup_->setBounds(botoes.removeFromRight(170));
        btnStartBackup_->setVisible(true);
        botoes.removeFromRight(tk.espacoPainel);
        if (btnSyncDestino_) {
            btnSyncDestino_->setBounds(botoes.removeFromRight(190));
            btnSyncDestino_->setVisible(true);
        }
        botoes.removeFromRight(tk.espacoPainel);
        if (btnPublishHtml_) {
            btnPublishHtml_->setBounds(botoes.removeFromRight(160));
            btnPublishHtml_->setVisible(true);
            btnPublishHtml_->setEnabled(temItens);
        }
        botoes.removeFromRight(tk.espacoPainel);
        if (btnLimparZip_) {
            btnLimparZip_->setBounds(botoes.removeFromRight(26));
            btnLimparZip_->setVisible(true);
        }
        if (btnExportZip_) {
            btnExportZip_->setBounds(botoes.removeFromRight(145));
            btnExportZip_->setVisible(true);
        }
        botoes.removeFromRight(tk.espacoPainel);
        if (btnLimparPrint_) {
            btnLimparPrint_->setBounds(botoes.removeFromRight(26));
            btnLimparPrint_->setVisible(true);
        }
        if (btnSendToPrint_) {
            btnSendToPrint_->setBounds(botoes.removeFromRight(155));
            btnSendToPrint_->setVisible(true);
        }
    }

    if (btnExportJanela_) {
        botoes.removeFromRight(tk.espacoPainel);
        btnExportJanela_->setBounds(botoes.removeFromRight(170));
        btnExportJanela_->setVisible(true);
        btnExportJanela_->setEnabled(temItens);
    }

    // ---- Running / Done: um cartão só, centrado ----
    if (emAndamento) {
        const int larguraCartao = std::min(760, area.getWidth() - tk.espacoGrande * 2);
        auto corpo = area.reduced(tk.espacoGrande, tk.espacoGrande);
        cartaoCentral_ = corpo.withSizeKeepingCentre(larguraCartao, std::min(320, corpo.getHeight()));

        auto dentro = cartaoCentral_.reduced(tk.espacoGrande * 2, tk.espacoGrande);

        labelProgressoStatus_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        labelProgressoStatus_->setJustificationType(juce::Justification::centred);
        labelProgressoStatus_->setBounds(dentro.removeFromTop(40));
        dentro.removeFromTop(tk.espacoMedio);

        if (barraProgresso_->isVisible()) {
            barraProgresso_->setBounds(dentro.removeFromTop(24));
            dentro.removeFromTop(tk.espacoMedio);
        }

        if (estado_ == Estado::Running && btnCancelarExecucao_) {
            btnCancelarExecucao_->setBounds(dentro.removeFromBottom(28).withSizeKeepingCentre(120, 28));
            btnCancelarExecucao_->setVisible(true);
            dentro.removeFromBottom(tk.espacoPequeno);
        } else if (btnCancelarExecucao_) {
            btnCancelarExecucao_->setVisible(false);
        }

        labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        labelResumo_->setColour(juce::Label::textColourId, tk.textoSecundario);
        labelResumo_->setJustificationType(juce::Justification::centredTop);
        labelResumo_->setBounds(dentro);
        return;
    }

    // ---- Config: duas colunas justificadas à esquerda, com prévia expandida até a direita ----
    auto corpo = area.reduced(tk.espacoGrande, tk.espacoMedio);

    const int larguraConfig = std::min(420, std::max(340, static_cast<int>(corpo.getWidth() * 0.32f)));
    auto colunaConfig = corpo.removeFromLeft(larguraConfig);
    corpo.removeFromLeft(tk.espacoMedio);
    auto colunaPrevia = corpo;

    if (configViewport_ && configContainer_) {
        configViewport_->setBounds(colunaConfig);
        configViewport_->setScrollBarsShown(false, false);
        configContainer_->setBounds(0, 0, colunaConfig.getWidth(), colunaConfig.getHeight());
        configContainer_->resized();
    }

    // PREVIEW — cartão único ocupando a coluna inteira com lista unificada de arquivos
    cartaoPrevia_ = colunaPrevia;
    const int padCartao = tk.espacoPainel + 2;
    const int alturaCabecalhoSecao = 26;
    auto dentroPrevia = colunaPrevia.reduced(padCartao, padCartao);

    labelResumo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
    labelResumo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    labelResumo_->setJustificationType(juce::Justification::centredLeft);
    labelResumo_->setBounds(dentroPrevia.removeFromTop(alturaCabecalhoSecao));

    dentroPrevia.removeFromTop(4);

    if (barraLegenda_) {
        barraLegenda_->setBounds(dentroPrevia.removeFromTop(20));
        barraLegenda_->setVisible(!isCatalogMode);
    }
    dentroPrevia.removeFromTop(tk.espacoPequeno);

    listPreviaViewport_->setBounds(dentroPrevia);
    if (listPrevia_) listPrevia_->setSize(dentroPrevia.getWidth(), listPrevia_->getHeight());

    overlay_.setBounds(getLocalBounds());
}

void BackupWorkspaceComponent::recarregar() {
    carregarOpcoesContent();
    carregarColecoesBackupCatalogo();
    carregarDestinosBackup();
    carregarDestinoAtivoInicial();
    atualizarResumo();
    atualizarBotoesListas();
    dispararScanDestino(false);
    repaint();
}

} // namespace matriz::ui
