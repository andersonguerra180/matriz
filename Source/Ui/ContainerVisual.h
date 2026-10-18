#pragma once

#include <JuceHeader.h>

#include <string>

// Visual e comportamento comuns dos CONTAINERS de importação (nest com origem): o INTAKE e o GRID desenham e abrem o
// container do mesmo jeito. Container é o conjunto que entra junto e realiza uma ação (DDP, produção em DAW, vinil,
// fita, YouTube, conversa de WhatsApp...); NEST é só um agrupamento temático, sem origem.
namespace matriz::ui {

class ProjetoAberto;

// Logo da origem do container (galeria do celular, Drive, Bridge, Lightroom, WhatsApp, DDP, produção, vinil, fita, YouTube).
const juce::Image& logoDaOrigem(const juce::String& origem);

// Containers desenhados como container (ícone grande do tipo ou capa), não como o primeiro arquivo com um selo.
bool ehContainerGrafico(const juce::String& origem);

// Ícone do container: o escolhido na ficha (ex.: a DAW usada na produção) quando existe; senão o da origem.
// `nomeDoRecurso` é o nome do recurso embutido (ex.: "ci_daw_pro_tools_png"); vazio = ícone da origem.
const juce::Image& iconeDoContainer(const juce::String& origem, const juce::String& nomeDoRecurso);

// Capa em arquivo (vinil, fita, YouTube), decodificada uma vez e guardada. Só na message thread. Vazio se o arquivo não abre.
const juce::Image& imagemDaCapaDoContainer(const juce::String& caminho);

// Desenha o container dentro de `area`: a capa quando há, senão o ícone.
void desenharContainerGrafico(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& origem,
                              const juce::String& nomeDoRecurso, const juce::String& caminhoDaCapa);

// Master DDP: abre o player de CD com os arquivos do container (ou avisa que a pasta do DDP não foi achada).
void abrirContainerDdp(ProjetoAberto& projeto, const std::string& nestId);

} // namespace matriz::ui
