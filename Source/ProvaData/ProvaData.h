#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../Model/Project.h"

// Prova de data (OpenTimestamps) dos itens do backup, dos trechos de conversa exportados e dos prints.
//
// O que é carimbado é sempre o SHA-256 de um "manifesto" (registro.json) que lista o(s) arquivo(s) original(is) com o hash de cada um.
// O recibo (.ots) fica ao lado do manifesto e é verificável pelo cliente oficial (`ots verify`) e por opentimestamps.org.
// Tudo é arquivo: o estado (na fila / pendente / confirmada) sai do próprio .ots, então viaja com o backup e com o clone.
//
// Armazenamento de um item: Project/provas/<id do item>.registro.json (+ .ots). Depois do backup, uma cópia fica ao lado
// do arquivo do item (<nome>.registro.json e <nome>.registro.json.ots). Nada aqui bloqueia a message thread: quem chama roda em background.

namespace matriz::provadata {

enum class Estado { Nenhum, NaFila, Pendente, Confirmada };

struct EstadoProva {
    Estado estado = Estado::Nenhum;
    std::uint64_t bloco = 0;
    juce::String dataBloco;   // "AAAA-MM-DD HH:MM:SS UTC" (vazio até a data ser consultada)
    juce::String registradoEm;
};

// Dados do computador que o usuário aceita colocar no registro. Tudo desligado por padrão e lembrado entre usos.
struct OpcoesComputador {
    bool nome = false;  // nome do computador + sistema operacional
    bool ip = false;    // IP público
    bool mac = false;   // endereço MAC
    bool algum() const { return nome || ip || mac; }
};
OpcoesComputador lerOpcoesComputador();
void gravarOpcoesComputador(const OpcoesComputador& o);
bool lerRegistrarProvaPadrao();
void gravarRegistrarProvaPadrao(bool v);
bool jaViuExplicacao();
void gravarJaViuExplicacao();

// Extensões de documento elegíveis (além das conversas do WhatsApp).
bool extensaoElegivel(const juce::String& extensaoSemPonto);
// Conversa do WhatsApp ou documento (.pdf/.doc/.docx/.rtf)? Leitura barata.
bool itemElegivel(matriz::model::Project& projeto, const std::string& itemId);

// O arquivo principal do item no backup: onde está, o nome com que entrou e o SHA-256 (do banco; calculado se faltar).
struct InfoArquivoItem {
    juce::File arquivo;
    juce::String nomeOriginal;
    juce::String sha256;
};
std::optional<InfoArquivoItem> infoArquivoDoItem(matriz::model::Project& projeto, const std::string& itemId);

// ── Carimbo de um arquivo qualquer ────────────────────────────────────────────────────────────────
// Cria <alvo>.ots (ou <alvo>.ots.fila se não houver internet). false se o alvo não existe.
bool carimbarArquivo(const juce::File& alvo, bool usarRede);
// Completa o que estiver na fila/pendente. true se algo mudou.
bool atualizarCarimbo(const juce::File& alvo, bool usarRede);
EstadoProva estadoDoCarimbo(const juce::File& alvo);
// O arquivo continua igual ao que foi carimbado? (compara o SHA-256 de hoje com o do recibo; 1 byte alterado = false)
bool carimboConfere(const juce::File& alvo);
juce::File arquivoOts(const juce::File& alvo);

// ── Itens do backup ───────────────────────────────────────────────────────────────────────────────
juce::File pastaDeProvas(const juce::File& pastaProject);
juce::File manifestoDoItem(const juce::File& pastaProject, const std::string& itemId);
EstadoProva estadoDoItem(const juce::File& pastaProject, const std::string& itemId);
// Estado de todos os itens com prova (id → 0 nenhum, 1 na fila, 2 pendente, 3 confirmada). Uma varredura da pasta; barato.
std::map<std::string, int> estadosDeTodos(const juce::File& pastaProject);

// Registra a prova de data de um item (cria o manifesto, carimba e coloca a cópia ao lado do arquivo). Background.
// false se o item não é elegível / já tem prova / o arquivo não está acessível; `erro` recebe o motivo.
bool registrarItem(matriz::model::Project& projeto, const std::string& itemId, const OpcoesComputador& computador, bool usarRede, juce::String* erro = nullptr);
// Copia o manifesto e o recibo do item para ao lado do arquivo dele no backup (após consolidar / registrar).
void colocarAoLadoDoArquivo(matriz::model::Project& projeto, const std::string& itemId);
// Mesmo, sabendo onde o arquivo está (gravação do backup: o destino sai do plano, sem abrir o projeto do MAIN).
void colocarAoLadoDe(const juce::File& pastaProject, const std::string& itemId, const juce::File& arquivoNoBackup);
// Depois de uma gravação de backup: coloca ao lado de cada arquivo as provas que ainda não estão lá.
void colocarTodasAoLado(matriz::model::Project& projeto);

// Pasta "Prova - <nome>" com o documento, registro.json, .ots e COMO VERIFICAR.txt (Explorer: "Exportar prova"). Devolve a pasta criada.
std::optional<juce::File> exportarProvaDoItem(matriz::model::Project& projeto, const std::string& itemId, const juce::File& pastaPai, juce::String* erro = nullptr);

// Certificado de prova de data em PDF (1 página A4: arquivo, SHA-256, estado do carimbo e como verificar). false se não há prova/arquivo.
bool exportarProvaPdf(matriz::model::Project& projeto, const std::string& itemId, const juce::File& destinoPdf, juce::String* erro = nullptr);

// ── Manifestos genéricos (trecho de conversa, print) ──────────────────────────────────────────────
struct EntradaManifesto {
    juce::String nome;
    juce::String sha256;
};
juce::var montarManifesto(const juce::String& tipo, const std::vector<EntradaManifesto>& arquivos, const OpcoesComputador& computador, bool usarRede,
                          const juce::String& observacao = {});
// Escreve o manifesto e o carimba. false se não deu para gravar.
bool gravarManifestoECarimbar(const juce::File& destinoJson, const juce::var& manifesto, bool usarRede);

// ── Textos ────────────────────────────────────────────────────────────────────────────────────────
juce::String textoComoVerificar();       // arquivo COMO VERIFICAR.txt (idioma atual)
juce::String textoEstado(const EstadoProva& e);  // "na fila" / "pendente" / "confirmada em dd/mm/aaaa, bloco N"
juce::String dataCurtaDoBloco(const juce::String& dataBloco);  // "dd/mm/aaaa"
juce::String hashCurto(const juce::String& sha256);            // 16 primeiros caracteres

// ── Atualização em segundo plano (início do app + a cada hora) ──────────────────────────────────────
void registrarProjetoParaAtualizacao(const juce::File& pastaProject);
// Pasta com recibos fora de um projeto (trecho exportado, print): o atualizador completa os .ots dela também (lembrada entre sessões).
void registrarPastaParaAtualizacaoExtra(const juce::File& pasta);
void iniciarAtualizadorEmBackground();
void pararAtualizadorEmBackground();
// Roda uma rodada agora (em outra thread). `aoTerminar` (opcional) volta na message thread com true se algo mudou.
void atualizarAgora(std::function<void(bool)> aoTerminar = nullptr);

// Dados para o self-test: sha256 em hexa de um arquivo.
juce::String sha256Hexa(const juce::File& arquivo);

} // namespace matriz::provadata
