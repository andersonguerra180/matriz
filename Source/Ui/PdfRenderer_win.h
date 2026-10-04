#pragma once

// Prévia de PDF no Windows via Windows.Data.Pdf (faz parte do SO — nenhuma biblioteca de terceiros). Sem JUCE aqui de
// propósito: os cabeçalhos do C++/WinRT brigam com os do JUCE, então esta interface fala só em tipos padrão.
// Tudo BLOQUEIA (espera as operações assíncronas do WinRT): chamar sempre de thread de fundo, nunca da message thread.

#include <cstdint>
#include <string>
#include <vector>

namespace matriz::ui::pdfwin {

struct InfoPdf {
    int paginas = 0;
    double larguraPagina1 = 0.0;  // em pontos (1/72")
    double alturaPagina1 = 0.0;
};

// true se abriu. Falha (arquivo sumiu, protegido por senha, corrompido) devolve false e o motivo em `erro`.
bool abrir(const std::wstring& caminho, InfoPdf& info, std::string& erro);

// Renderiza a página (1-based) com `larguraPx` de largura (altura proporcional), fundo branco, e devolve um PNG.
bool renderizarPagina(const std::wstring& caminho, int pagina, int larguraPx, std::vector<std::uint8_t>& png,
                      int& larguraOut, int& alturaOut, std::string& erro);

} // namespace matriz::ui::pdfwin
