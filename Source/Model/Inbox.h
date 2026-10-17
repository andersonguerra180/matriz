#pragma once

#include <JuceHeader.h>
#include <vector>

// INBOX: caixa de entrada na RAIZ do MAIN (ao lado de Media/ e Project/). Quem está sem o app guarda material novo
// aqui, em subpastas à vontade; ao reabrir o projeto, Tools > Scan INBOX cataloga tudo (Intake normal, as subpastas
// viram proposta no Folder Map) e o Make Backup MOVE os arquivos para Media/ (sem copiar, a INBOX fica vazia).
// O app NUNCA escaneia a INBOX sozinho. Header só com inline: não precisa entrar em cada alvo do CMake.

namespace matriz::model::inbox {

inline constexpr const char* kNomePasta = "INBOX";
inline constexpr const char* kNomeReadme = "README - LEIA-ME.txt";
inline constexpr int kMinutosParaEstabilizar = 2;  // arquivo mexido há menos que isso pode estar sendo copiado

inline juce::File pasta(const juce::File& raizMain) { return raizMain.getChildFile(kNomePasta); }

inline bool arquivoEstaNaInbox(const juce::File& arquivo, const juce::File& raizMain) {
    return arquivo.isAChildOf(pasta(raizMain));
}

inline juce::String textoReadme() {
    return juce::String::fromUTF8(
        "BKR MATRIZ - LEIA-ME / README\n"
        "==============================\n\n"
        "[PORTUGUÊS]\n\n"
        "Esta pasta é um MAIN do BKR Matriz: o backup protegido do seu acervo. Os arquivos aqui são arquivos\n"
        "comuns; qualquer pessoa abre no Finder, com ou sem o aplicativo.\n\n"
        "O QUE TEM AQUI\n"
        "  Media/     Seus arquivos, organizados pelo app. Não renomeie nem mova arquivos por fora: o catálogo aponta\n"
        "             para eles.\n"
        "  Project/   O catálogo (bancos .sqlite), miniaturas, relatórios e o log do projeto. Não apague.\n"
        "  INBOX/     Caixa de entrada para material novo (veja abaixo).\n"
        "  destination.json e o arquivo .mtz   Identidade deste backup. Não apague.\n\n"
        "INBOX - MATERIAL NOVO, COM OU SEM O APP\n"
        "  Ficou sem o aplicativo? Continue guardando material novo aqui: jogue os arquivos dentro de INBOX/ e\n"
        "  organize em subpastas do jeito que fizer sentido (por exemplo 2027/Show X/). Não escreva dentro de Media/.\n"
        "  Quando voltar a abrir este projeto no BKR Matriz, use o menu Ferramentas > Escanear INBOX (arquivos novos).\n"
        "  O app cataloga tudo (as subpastas viram sugestão de pastas no Folder Map) e, ao rodar o Make Backup,\n"
        "  MOVE os arquivos para dentro de Media/, na organização do projeto. Depois a INBOX fica vazia.\n"
        "  - O app nunca escaneia a INBOX sozinho: só quando você pede.\n"
        "  - Arquivo com o mesmo conteúdo de algo que o app já conhece vira duplicata e fica na INBOX até você\n"
        "    resolver na aba Duplicates.\n"
        "  - Arquivos alterados nos últimos 2 minutos sao ignorados no scan (podem estar sendo copiados).\n\n"
        "ATENÇÃO: dentro de Media/ não crie pastas chamadas log, relatorios, catalogo, Project, Media ou _lixeira.\n"
        "O app move pastas com esses nomes para Project/ ao abrir o projeto.\n\n"
        "------------------------------\n\n"
        "[ENGLISH]\n\n"
        "This folder is a BKR Matriz MAIN: the protected backup of your archive. The files in it are ordinary\n"
        "files; anyone can open them in the Finder, with or without the app.\n\n"
        "WHAT IS HERE\n"
        "  Media/     Your files, organized by the app. Do not rename or move files from outside: the catalog points\n"
        "             to them.\n"
        "  Project/   The catalog (.sqlite databases), thumbnails, reports and the project log. Do not delete.\n"
        "  INBOX/     Inbox for new material (see below).\n"
        "  destination.json and the .mtz file   Identity of this backup. Do not delete.\n\n"
        "INBOX - NEW MATERIAL, WITH OR WITHOUT THE APP\n"
        "  No app at hand? Keep storing new material here: drop the files inside INBOX/ and organize them in\n"
        "  subfolders however you like (for example 2027/Show X/). Do not write inside Media/.\n"
        "  When you open this project in BKR Matriz again, use the menu Tools > Scan INBOX (new files).\n"
        "  The app catalogs everything (your subfolders become suggested folders in the Folder Map) and, when you\n"
        "  run Make Backup, MOVES the files into Media/, in the project's organization. The INBOX is left empty.\n"
        "  - The app never scans the INBOX by itself: only when you ask.\n"
        "  - A file with the same content as something the app already knows becomes a duplicate and stays in the\n"
        "    INBOX until you resolve it in the Duplicates tab.\n"
        "  - Files changed in the last 2 minutes are ignored by the scan (they may still be copying).\n\n"
        "WARNING: inside Media/ do not create folders named log, relatorios, catalogo, Project, Media or _lixeira.\n"
        "The app moves folders with these names to Project/ when it opens the project.\n");
}

// Cria a INBOX e o README na raiz do MAIN se faltarem. Nunca sobrescreve um README editado.
inline void garantir(const juce::File& raizMain) {
    if (!raizMain.isDirectory()) return;
    const juce::File inbox = pasta(raizMain);
    if (!inbox.exists()) inbox.createDirectory();
    const juce::File readme = raizMain.getChildFile(kNomeReadme);
    if (!readme.existsAsFile()) readme.replaceWithText(textoReadme());
}

// .xmp ao lado de uma mídia é sidecar da mídia (não é item à parte), como no Intake.
inline bool ehSidecarDeOutroArquivo(const juce::File& f) {
    if (!f.hasFileExtension("xmp")) return false;
    const juce::File semXmp = f.getSiblingFile(f.getFileNameWithoutExtension());  // IMG_1.CR2.xmp -> IMG_1.CR2
    if (semXmp.existsAsFile() && semXmp.getFileExtension().isNotEmpty()) return true;
    for (const auto& irmao : f.getParentDirectory().findChildFiles(juce::File::findFiles, false, f.getFileNameWithoutExtension() + ".*"))
        if (!irmao.hasFileExtension("xmp")) return true;  // IMG_1.xmp + IMG_1.CR2
    return false;
}

struct Varredura {
    int arquivos = 0;                // elegíveis a entrar
    juce::int64 bytes = 0;
    int recentes = 0;                // mexidos há menos de kMinutosParaEstabilizar
    juce::StringArray exemplosRecentes;
};

// Só metadados de disco (nenhum arquivo é lido). Mesmas exclusões do Intake: ocultos, vazios, caches de áudio e sidecars.
inline Varredura varrer(const juce::File& inbox) {
    Varredura v;
    const auto limite = juce::Time::getCurrentTime() - juce::RelativeTime::minutes(kMinutosParaEstabilizar);
    for (const auto& sub : juce::RangedDirectoryIterator(inbox, true, "*", juce::File::findFiles)) {
        const juce::File f = sub.getFile();
        const juce::String nome = f.getFileName();
        const juce::String ext = f.getFileExtension().trimCharactersAtStart(".").toLowerCase();
        if (nome.startsWith(".") || f.getSize() == 0) continue;
        if (ext == "sfk" || ext == "reapeaks" || ext == "asd") continue;
        if (ehSidecarDeOutroArquivo(f)) continue;
        ++v.arquivos;
        v.bytes += f.getSize();
        if (f.getLastModificationTime() > limite) {
            ++v.recentes;
            if (v.exemplosRecentes.size() < 3) v.exemplosRecentes.add(f.getRelativePathFrom(inbox));
        }
    }
    return v;
}

// Entradas de primeiro nível (subpastas e arquivos soltos) para o Intake: cada subpasta vira uma proposta de pasta no
// Folder Map; "INBOX" em si não vira nível.
inline juce::Array<juce::File> entradasParaIntake(const juce::File& inbox) {
    juce::Array<juce::File> entradas;
    for (const auto& e : juce::RangedDirectoryIterator(inbox, false, "*", juce::File::findFilesAndDirectories)) {
        const juce::File f = e.getFile();
        if (f.getFileName().startsWith(".")) continue;
        entradas.add(f);
    }
    return entradas;
}

// Depois de MOVER os arquivos para Media/: apaga subpastas que ficaram vazias (só com .DS_Store conta como vazia).
// Nunca apaga a própria INBOX. Devolve quantas pastas removeu.
inline int removerSubpastasVazias(const juce::File& inbox) {
    int removidas = 0;
    std::vector<juce::File> pastas;
    for (const auto& e : juce::RangedDirectoryIterator(inbox, true, "*", juce::File::findDirectories)) pastas.push_back(e.getFile());
    std::sort(pastas.begin(), pastas.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFullPathName().length() > b.getFullPathName().length();  // mais fundas primeiro
    });
    for (const auto& d : pastas) {
        bool temAlgo = false;
        for (const auto& e : juce::RangedDirectoryIterator(d, false, "*", juce::File::findFilesAndDirectories)) {
            if (e.getFile().getFileName() == ".DS_Store") continue;
            temAlgo = true;
            break;
        }
        if (temAlgo) continue;
        d.getChildFile(".DS_Store").deleteFile();
        if (d.deleteFile()) ++removidas;
    }
    return removidas;
}

} // namespace matriz::model::inbox
