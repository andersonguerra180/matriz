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

// ---------------------------------------------------------------------------
// AUTO-ARRANJO com tamanho por nível. Puro (sem JUCE), testável em console.
//  - níveis mais altos da hierarquia ficam maiores, os mais fundos menores;
//  - cartões nunca se sobrepõem (colunas por profundidade, faixas verticais disjuntas por subárvore);
//  - as conexões pai->filho não se cruzam (cada subárvore ocupa a sua própria faixa e o pai fica dentro
//    da faixa dos filhos);
//  - o maior fator de cartão que cabe na área, e a sobra da área vira espaço entre colunas/linhas.
// `escala` é o multiplicador manual do usuário por nó (1 = tamanho calculado).
// ---------------------------------------------------------------------------
struct NoTam {
    std::string id;
    std::string paiId;  // "" ou id ausente da lista = raiz
    double escala = 1.0;
};

struct CaixaAuto {
    int x = 0, y = 0, w = 0, h = 0;
    int direita() const { return x + w; }
    int baixo() const { return y + h; }
};

struct ParametrosAuto {
    int areaLargura = 800, areaAltura = 600;  // área útil do mapa (coordenadas de canvas, zoom 100%)
    int margem = 40;
    int larguraBase = 190, alturaBase = 84;  // nível 0, fator 1, escala 1
    int gapXBase = 50, gapYBase = 26;
    double decaimentoPorNivel = 0.88, pisoNivel = 0.55;
    double fatorMin = 0.3, fatorMax = 2.5;
};

struct ResultadoAuto {
    std::map<std::string, CaixaAuto> caixas;
    std::map<std::string, int> nivel;
    double fator = 1.0;  // fator de cartão escolhido para caber na área
};

inline ResultadoAuto calcularAutoArranjo(const std::vector<NoTam>& nos, const ParametrosAuto& p) {
    ResultadoAuto r;
    const int n = static_cast<int>(nos.size());
    if (n == 0) return r;

    std::map<std::string, int> indice;
    for (int i = 0; i < n; ++i) indice[nos[static_cast<size_t>(i)].id] = i;
    std::vector<std::vector<int>> filhos(static_cast<size_t>(n));
    std::vector<int> raizes;
    for (int i = 0; i < n; ++i) {
        const auto& no = nos[static_cast<size_t>(i)];
        auto pai = indice.find(no.paiId);
        if (no.paiId.empty() || pai == indice.end() || pai->second == i) raizes.push_back(i);
        else filhos[static_cast<size_t>(pai->second)].push_back(i);
    }

    std::vector<int> nivel(static_cast<size_t>(n), 0);
    int nivelMax = 0;
    std::function<void(int, int)> marcar = [&](int i, int d) {
        nivel[static_cast<size_t>(i)] = d;
        nivelMax = std::max(nivelMax, d);
        for (int f : filhos[static_cast<size_t>(i)]) marcar(f, d + 1);
    };
    for (int rz : raizes) marcar(rz, 0);

    auto fatorNivel = [&](int d) { return std::max(p.pisoNivel, std::pow(p.decaimentoPorNivel, d)); };
    std::vector<double> w0(static_cast<size_t>(n)), h0(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double e = std::clamp(nos[static_cast<size_t>(i)].escala, 0.3, 3.0) * fatorNivel(nivel[static_cast<size_t>(i)]);
        w0[static_cast<size_t>(i)] = p.larguraBase * e;
        h0[static_cast<size_t>(i)] = p.alturaBase * e;
    }
    const int colunas = nivelMax + 1;
    std::vector<double> larguraColuna(static_cast<size_t>(colunas), 0.0);
    for (int i = 0; i < n; ++i) {
        auto& c = larguraColuna[static_cast<size_t>(nivel[static_cast<size_t>(i)])];
        c = std::max(c, w0[static_cast<size_t>(i)]);
    }

    struct Disposicao {
        std::vector<double> x, y;
        double largura = 0.0, altura = 0.0;
    };
    auto dispor = [&](double F, double extraX, double extraY) {
        const double gx = p.gapXBase * F + extraX, gy = p.gapYBase * F + extraY;
        std::vector<double> colX(static_cast<size_t>(colunas), 0.0);
        for (int d = 1; d < colunas; ++d)
            colX[static_cast<size_t>(d)] = colX[static_cast<size_t>(d - 1)] + larguraColuna[static_cast<size_t>(d - 1)] * F + gx;

        std::vector<double> faixa(static_cast<size_t>(n), 0.0);
        std::function<double(int)> alturaFaixa = [&](int i) -> double {
            double total = 0.0;
            const auto& fs = filhos[static_cast<size_t>(i)];
            for (size_t k = 0; k < fs.size(); ++k) total += alturaFaixa(fs[k]) + (k > 0 ? gy : 0.0);
            return faixa[static_cast<size_t>(i)] = std::max(total, h0[static_cast<size_t>(i)] * F);
        };

        Disposicao d;
        d.x.assign(static_cast<size_t>(n), 0.0);
        d.y.assign(static_cast<size_t>(n), 0.0);
        std::function<double(int, double)> colocar = [&](int i, double topo) -> double {  // devolve o centro y do nó
            const auto& fs = filhos[static_cast<size_t>(i)];
            const double h = h0[static_cast<size_t>(i)] * F;
            d.x[static_cast<size_t>(i)] = colX[static_cast<size_t>(nivel[static_cast<size_t>(i)])];
            if (fs.empty()) {
                d.y[static_cast<size_t>(i)] = topo + (faixa[static_cast<size_t>(i)] - h) / 2.0;
                return d.y[static_cast<size_t>(i)] + h / 2.0;
            }
            double total = 0.0;
            for (size_t k = 0; k < fs.size(); ++k) total += faixa[static_cast<size_t>(fs[k])] + (k > 0 ? gy : 0.0);
            double cursor = topo + (faixa[static_cast<size_t>(i)] - total) / 2.0;
            double primeiro = 0.0, ultimo = 0.0;
            for (size_t k = 0; k < fs.size(); ++k) {
                const double centro = colocar(fs[k], cursor);
                if (k == 0) primeiro = centro;
                ultimo = centro;
                cursor += faixa[static_cast<size_t>(fs[k])] + gy;
            }
            double y = (primeiro + ultimo) / 2.0 - h / 2.0;
            y = std::clamp(y, topo, topo + faixa[static_cast<size_t>(i)] - h);  // o cartão nunca sai da faixa
            d.y[static_cast<size_t>(i)] = y;
            return y + h / 2.0;
        };

        double topo = 0.0;
        for (size_t k = 0; k < raizes.size(); ++k) {
            alturaFaixa(raizes[k]);
            colocar(raizes[k], topo);
            topo += faixa[static_cast<size_t>(raizes[k])] + gy;
        }
        d.altura = topo - gy;
        d.largura = colX[static_cast<size_t>(colunas - 1)] + larguraColuna[static_cast<size_t>(colunas - 1)] * F;
        return d;
    };

    const double dispW = std::max(1, p.areaLargura - 2 * p.margem);
    const double dispH = std::max(1, p.areaAltura - 2 * p.margem);
    const auto base = dispor(1.0, 0.0, 0.0);
    const double F = std::clamp(std::min(dispW / std::max(1.0, base.largura), dispH / std::max(1.0, base.altura)),
                                p.fatorMin, p.fatorMax);

    // A dimensão que sobra vira espaço entre colunas / linhas (limitado, pra não espalhar demais).
    double extraX = 0.0, extraY = 0.0;
    auto atual = dispor(F, 0.0, 0.0);
    if (colunas > 1 && atual.largura < dispW) {
        double mediaLargura = 0.0;
        for (double c : larguraColuna) mediaLargura += c * F;
        mediaLargura /= colunas;
        extraX = std::min((dispW - atual.largura) / (colunas - 1), 1.2 * mediaLargura);
    }
    if (atual.altura < dispH) {
        double lo = 0.0, hi = std::min(dispH - atual.altura, 1.0 * p.alturaBase * F);
        for (int it = 0; it < 24; ++it) {
            const double meio = (lo + hi) / 2.0;
            if (dispor(F, extraX, meio).altura <= dispH) lo = meio;
            else hi = meio;
        }
        extraY = lo;
    }
    const auto final_ = dispor(F, extraX, extraY);

    r.fator = F;
    for (int i = 0; i < n; ++i) {
        const auto& no = nos[static_cast<size_t>(i)];
        CaixaAuto c;
        c.x = p.margem + static_cast<int>(std::lround(final_.x[static_cast<size_t>(i)]));
        c.y = p.margem + static_cast<int>(std::lround(final_.y[static_cast<size_t>(i)]));
        c.w = std::max(8, static_cast<int>(std::lround(w0[static_cast<size_t>(i)] * F)));  // sem piso grande: estouraria os espaços
        c.h = std::max(6, static_cast<int>(std::lround(h0[static_cast<size_t>(i)] * F)));
        r.caixas[no.id] = c;
        r.nivel[no.id] = nivel[static_cast<size_t>(i)];
    }
    return r;
}

// Tamanho de um cartão de `nivel` (0 = raiz) para um dado fator do mapa, sem o multiplicador manual.
inline CaixaAuto tamanhoDoNivel(const ParametrosAuto& p, int nivel, double fator) {
    const double q = std::max(p.pisoNivel, std::pow(p.decaimentoPorNivel, nivel));
    CaixaAuto c;
    c.w = std::max(8, static_cast<int>(std::lround(p.larguraBase * q * fator)));
    c.h = std::max(6, static_cast<int>(std::lround(p.alturaBase * q * fator)));
    return c;
}

// Dois cartões se sobrepõem (ou ficam a menos de `folga` px um do outro).
inline bool caixasSeSobrepoem(const CaixaAuto& a, const CaixaAuto& b, int folga = 0) {
    return a.x < b.direita() + folga && b.x < a.direita() + folga && a.y < b.baixo() + folga && b.y < a.baixo() + folga;
}

// Crescimento pelo slider: cada alvo vai do multiplicador `de` ao `para` (em torno do centro, sobre o
// tamanho calculado autoW/autoH). Devolve a maior fração t em [0,1] desse caminho em que nenhum cartão
// encosta em outro (nos `fixos` ou entre os próprios alvos). 1 = chegou onde foi pedido; o slider "para".
struct AlvoEscala {
    double cx = 0, cy = 0;
    double autoW = 1, autoH = 1;
    double de = 1, para = 1;
};

inline CaixaAuto caixaDoAlvo(const AlvoEscala& a, double t) {
    const double m = a.de + (a.para - a.de) * t;
    const double w = a.autoW * m, h = a.autoH * m;
    CaixaAuto c;
    c.x = static_cast<int>(std::lround(a.cx - w / 2.0));
    c.y = static_cast<int>(std::lround(a.cy - h / 2.0));
    c.w = std::max(1, static_cast<int>(std::lround(w)));
    c.h = std::max(1, static_cast<int>(std::lround(h)));
    return c;
}

inline double fracaoPermitidaDoCrescimento(const std::vector<AlvoEscala>& alvos, const std::vector<CaixaAuto>& fixos,
                                           int folga = 2) {
    auto livre = [&](double t) {
        std::vector<CaixaAuto> caixas;
        caixas.reserve(alvos.size());
        for (const auto& a : alvos) caixas.push_back(caixaDoAlvo(a, t));
        for (size_t i = 0; i < caixas.size(); ++i) {
            for (const auto& f : fixos)
                if (caixasSeSobrepoem(caixas[i], f, folga)) return false;
            for (size_t j = i + 1; j < caixas.size(); ++j)
                if (caixasSeSobrepoem(caixas[i], caixas[j], folga)) return false;
        }
        return true;
    };
    if (livre(1.0)) return 1.0;
    if (!livre(0.0)) return 0.0;  // já encostado: não cresce
    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 24; ++it) {
        const double meio = (lo + hi) / 2.0;
        if (livre(meio)) lo = meio;
        else hi = meio;
    }
    return lo;
}

}  // namespace matriz::ui::layoutarvore
