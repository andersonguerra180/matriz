#include "PdfRenderer_win.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <cmath>

namespace matriz::ui::pdfwin {

namespace {

// Cada thread de fundo que chama isto entra no apartamento MTA (onde .get() pode bloquear); sai ao terminar a thread.
struct ApartamentoMta {
    bool iniciou = false;
    ApartamentoMta() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            iniciou = true;
        } catch (...) {
            // a thread já tinha um apartamento (de outro tipo): segue com ele
        }
    }
    ~ApartamentoMta() {
        if (iniciou) winrt::uninit_apartment();
    }
};

void garantirApartamento() {
    static thread_local ApartamentoMta apartamento;
    (void) apartamento;
}

winrt::Windows::Data::Pdf::PdfDocument carregar(const std::wstring& caminho) {
    auto arquivo = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(winrt::hstring(caminho)).get();
    return winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(arquivo).get();
}

} // namespace

bool abrir(const std::wstring& caminho, InfoPdf& info, std::string& erro) {
    try {
        garantirApartamento();
        auto doc = carregar(caminho);
        info.paginas = static_cast<int>(doc.PageCount());
        if (info.paginas > 0) {
            auto tamanho = doc.GetPage(0).Size();
            info.larguraPagina1 = tamanho.Width;
            info.alturaPagina1 = tamanho.Height;
        }
        return info.paginas > 0;
    } catch (const winrt::hresult_error& e) {
        erro = winrt::to_string(e.message());
    } catch (const std::exception& e) {
        erro = e.what();
    } catch (...) {
        erro = "unknown error opening the PDF";
    }
    return false;
}

bool renderizarPagina(const std::wstring& caminho, int pagina, int larguraPx, std::vector<std::uint8_t>& png,
                      int& larguraOut, int& alturaOut, std::string& erro) {
    try {
        garantirApartamento();
        auto doc = carregar(caminho);
        const int total = static_cast<int>(doc.PageCount());
        if (pagina < 1 || pagina > total) {
            erro = "page out of range";
            return false;
        }
        auto pag = doc.GetPage(static_cast<uint32_t>(pagina - 1));
        const auto tamanho = pag.Size();
        if (tamanho.Width <= 0.0f || tamanho.Height <= 0.0f) {
            erro = "page with no size";
            return false;
        }
        const int largura = std::clamp(larguraPx, 16, 4096);
        const int altura = std::clamp(static_cast<int>(std::lround(largura * (tamanho.Height / tamanho.Width))), 16, 8192);

        winrt::Windows::Data::Pdf::PdfPageRenderOptions opcoes;
        opcoes.DestinationWidth(static_cast<uint32_t>(largura));
        opcoes.DestinationHeight(static_cast<uint32_t>(altura));
        opcoes.BackgroundColor(winrt::Windows::UI::Color{255, 255, 255, 255});  // sem isto o fundo sai transparente

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream fluxo;
        pag.RenderToStreamAsync(fluxo, opcoes).get();

        const auto tamanhoBytes = static_cast<uint32_t>(fluxo.Size());
        winrt::Windows::Storage::Streams::DataReader leitor(fluxo.GetInputStreamAt(0));
        leitor.LoadAsync(tamanhoBytes).get();
        png.assign(tamanhoBytes, 0);
        leitor.ReadBytes(winrt::array_view<std::uint8_t>(png.data(), png.data() + png.size()));
        larguraOut = largura;
        alturaOut = altura;
        return !png.empty();
    } catch (const winrt::hresult_error& e) {
        erro = winrt::to_string(e.message());
    } catch (const std::exception& e) {
        erro = e.what();
    } catch (...) {
        erro = "unknown error rendering the PDF page";
    }
    return false;
}

} // namespace matriz::ui::pdfwin

#endif // _WIN32
