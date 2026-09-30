#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// Layout em árvore do Folder Map: pai centralizado verticalmente sobre as filhas,
// uma coluna por profundidade, espaçamento uniforme, sem sobreposição. Usado pelo
// AJUSTAR (autoArranjar) e pelo layout padrão de pastas ainda sem posição salva
// (recalcularNodes). Sem dependência de JUCE — dá pra testar em console.
namespace matriz::ui::layoutarvore {

struct No {
    std::string id;
    std::string paiId;   // "" ou id ausente da lista = raiz
    bool fixo = false;   // já tem posição salva: não muda, mas ancora as filhas sem posição
    int x = 0, y = 0;    // posição salva (só vale se fixo)
};

struct Parametros {
    int larguraNo = 190;
    int alturaNo = 84;
    int gapX = 50;
    int gapY = 26;
    int origemX = 40;    // onde começam as raízes sem posição (sem nenhum nó fixo)
    int origemY = 80;
};

struct Ponto {
    int x = 0, y = 0;
};

// Devolve a posição de cada nó NÃO fixo. Nó sem posição cujo pai tem posição salva
// fica à direita dele, centralizado no y do pai; raiz sem posição empilha na coluna
// da origem (abaixo dos nós fixos, se houver).
inline std::map<std::string, Ponto> calcular(const std::vector<No>& nos, const Parametros& p) {
    std::map<std::string, const No*> porId;
    for (const auto& n : nos) porId[n.id] = &n;

    std::map<std::string, std::vector<std::string>> filhosLivres;  // só filhos sem posição fixa
    std::vector<std::string> raizesGlobais;
    std::map<std::string, std::vector<std::string>> raizesAncoradas;  // pai fixo -> filhas sem posição
    for (const auto& n : nos) {
        if (n.fixo) continue;
        auto pai = porId.find(n.paiId);
        if (n.paiId.empty() || pai == porId.end()) {
            raizesGlobais.push_back(n.id);
        } else if (pai->second->fixo) {
            raizesAncoradas[n.paiId].push_back(n.id);
        } else {
            filhosLivres[n.paiId].push_back(n.id);
        }
    }

    std::map<std::string, int> altura;
    std::function<int(const std::string&)> alturaDaSubarvore = [&](const std::string& id) -> int {
        auto it = filhosLivres.find(id);
        if (it == filhosLivres.end() || it->second.empty()) return altura[id] = p.alturaNo;
        int total = 0;
        for (size_t i = 0; i < it->second.size(); ++i) {
            if (i > 0) total += p.gapY;
            total += alturaDaSubarvore(it->second[i]);
        }
        return altura[id] = std::max(total, p.alturaNo);
    };

    std::map<std::string, Ponto> pos;
    std::function<void(const std::string&, int, int)> posicionar = [&](const std::string& id, int x, int yTopo) {
        auto it = filhosLivres.find(id);
        if (it == filhosLivres.end() || it->second.empty()) {
            pos[id] = {x, yTopo};
            return;
        }
        int cursor = yTopo;
        for (const auto& filho : it->second) {
            posicionar(filho, x + p.larguraNo + p.gapX, cursor);
            cursor += altura[filho] + p.gapY;
        }
        pos[id] = {x, (pos[it->second.front()].y + pos[it->second.back()].y) / 2};
    };

    // Raízes sem pai posicionado: coluna da origem, abaixo de qualquer nó fixo.
    int x0 = p.origemX, y0 = p.origemY;
    bool haFixos = false;
    int minX = 0, maxBottom = 0;
    for (const auto& n : nos) {
        if (!n.fixo) continue;
        minX = haFixos ? std::min(minX, n.x) : n.x;
        maxBottom = haFixos ? std::max(maxBottom, n.y + p.alturaNo) : n.y + p.alturaNo;
        haFixos = true;
    }
    if (haFixos) {
        x0 = minX;
        y0 = std::max(y0, maxBottom + p.gapY * 2);
    }
    int cursor = y0;
    for (const auto& id : raizesGlobais) {
        alturaDaSubarvore(id);
        posicionar(id, x0, cursor);
        cursor += altura[id] + p.gapY;
    }

    // Filhas sem posição de um pai com posição salva: à direita dele, centralizadas no y dele.
    for (const auto& [paiId, filhas] : raizesAncoradas) {
        const No* pai = porId[paiId];
        int total = 0;
        for (size_t i = 0; i < filhas.size(); ++i) {
            if (i > 0) total += p.gapY;
            total += alturaDaSubarvore(filhas[i]);
        }
        int cursorFilhas = pai->y + p.alturaNo / 2 - total / 2;
        for (const auto& id : filhas) {
            posicionar(id, pai->x + p.larguraNo + p.gapX, cursorFilhas);
            cursorFilhas += altura[id] + p.gapY;
        }
    }

    // (0,0) significa "sem posição salva" no banco: nunca devolver exatamente isso.
    for (auto& [id, ponto] : pos)
        if (ponto.x == 0 && ponto.y == 0) ponto.y = 1;
    return pos;
}

}  // namespace matriz::ui::layoutarvore
