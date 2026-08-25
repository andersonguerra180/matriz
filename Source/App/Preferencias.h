#pragma once

#include <JuceHeader.h>
#include <string>

// Preferências de nível de aplicativo — nada disso mora dentro de uma pasta
// de projeto (P5): idioma da interface e a lista de projetos recentes
// existem independente de qual projeto está aberto, então vivem num
// arquivo próprio em ~/Library/Application Support/MATRIZ (ou equivalente),
// nunca em registro.sqlite/indice.sqlite.

namespace matriz::app {

void inicializarPreferencias();
void fecharPreferencias();

// "en" ou "pt_BR". Default "en" (Parte 2 da correção de fluxo).
juce::String lerLocale();
void gravarLocale(const juce::String& locale);

// Gemini API key (AI Scan feature). Stored in app preferences, never in project DB.
juce::String lerGeminiApiKey();
void gravarGeminiApiKey(const juce::String& key);

// Theme preference: "dark", "light". Default "dark".
juce::String lerTema();
void gravarTema(const juce::String& tema);

// Font size scale factor (0.8 - 1.5). Default 1.0.
float lerEscalaFonte();
void gravarEscalaFonte(float escala);

enum class RecentlyIngestedMode { Time, ClearOnStart };
RecentlyIngestedMode lerRecentlyIngestedMode();
void gravarRecentlyIngestedMode(RecentlyIngestedMode modo);
int lerRecentlyIngestedHoras();
void gravarRecentlyIngestedHoras(int horas);

const juce::Time& inicioDaSessao();

bool ehRecemIngerido(const std::string& criadoEmIso);

struct ProjetoRecente {
    juce::String pasta;
    juce::String nome;
    juce::String modo; // "preservacao" ou "catalogo" — mesmo valor gravado em projeto.modo
};

// Mais recente primeiro. Entradas cuja pasta não existe mais são
// descartadas silenciosamente na leitura (não é erro — só não aparecem).
std::vector<ProjetoRecente> lerRecentes();

// Move `pasta` pro topo da lista (ou insere), grava. Limita a 10 entradas.
void registrarRecente(const juce::String& pasta, const juce::String& nome, const juce::String& modo);

// Lógica pura de "mover/inserir no topo com dedup por pasta + limite de
// tamanho" — separada de qualquer I/O de arquivo pra ser testável em
// isolamento (tools/selftest, sem tocar nas preferências reais do usuário).
std::vector<ProjetoRecente> comRecenteNoTopo(std::vector<ProjetoRecente> lista, const ProjetoRecente& novo,
                                              size_t maximo = 10);

} // namespace matriz::app
