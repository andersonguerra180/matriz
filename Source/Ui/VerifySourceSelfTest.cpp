#include "VerifySourceSelfTest.h"

#include <JuceHeader.h>
#include <iostream>

#include "../Ingest/Checksum.h"
#include "../Vault/VerifySource.h"

namespace matriz::ui {

namespace {

int falhas = 0;
void confere(bool ok, const char* o) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << o << "\n";
    if (!ok) ++falhas;
}

juce::File escrever(const juce::File& raiz, const juce::String& rel, const juce::String& conteudo) {
    auto f = raiz.getChildFile(rel);
    f.getParentDirectory().createDirectory();
    f.replaceWithText(conteudo);
    return f;
}

} // namespace

int rodarVerifySourceSelfTest() {
    using matriz::vault::VerifySource;
    falhas = 0;
    auto raiz = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("verifysource-" + juce::Uuid().toString());
    raiz.createDirectory();

    const auto noMain = escrever(raiz, "a/no_main.txt", "ja esta no main");
    const auto rej = escrever(raiz, "a/rejeitado.txt", "foi rejeitado no intake");
    escrever(raiz, "b/faltando.txt", "falta no main");
    escrever(raiz, "b/renomeado_do_main.txt", "ja esta no main");  // mesmo conteudo, outro nome: nao e faltante
    escrever(raiz, "c/faltando_copia.txt", "falta no main");        // mesma copia em outra pasta: lista uma vez
    escrever(raiz, ".oculto/x.txt", "oculto");
    escrever(raiz, "c/.DS_Store", "lixo");
    escrever(raiz, "c/vazio.txt", "");

    matriz::vault::HashesConhecidos h;
    h.noMain.insert(matriz::ingest::sha256DoArquivo(noMain));
    h.rejeitados.insert(matriz::ingest::sha256DoArquivo(rej));

    std::atomic<bool> cancelar{false};
    int ultimoTotal = 0;
    auto r = VerifySource::verificar(raiz, h, cancelar, [&](int, int total, const juce::String&) { ultimoTotal = total; });

    int faltando = 0, rejeitados = 0;
    for (const auto& f : r.faltantes) (f.rejeitadoAntes ? rejeitados : faltando)++;
    confere(!r.cancelado, "scan completo, nao cancelado");
    confere(ultimoTotal == 5, "5 arquivos considerados (oculto, .DS_Store e vazio ficam de fora)");
    confere(r.totalNoMain == 2, "2 ja estao no MAIN (inclui o renomeado)");
    confere(faltando == 1, "1 faltante (a copia repetida nao conta duas vezes)");
    confere(rejeitados == 1, "1 rejeitado antes");

    cancelar = true;
    confere(VerifySource::verificar(raiz, h, cancelar).cancelado, "cancelar interrompe o scan");

    raiz.deleteRecursively();
    std::cout << (falhas == 0 ? "VERIFYSOURCE OK\n" : "VERIFYSOURCE COM FALHAS\n");
    return falhas == 0 ? 0 : 1;
}

} // namespace matriz::ui
