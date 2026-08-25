# Documentação Técnica e Status do BKR Matriz

Este documento apresenta uma visão completa do funcionamento do **BKR Matriz**, detalhando a arquitetura do sistema, o comportamento de cada funcionalidade e o histórico de alterações implementadas.

---

## 1. Visão Geral do Sistema

O **BKR Matriz** é um aplicativo desktop profissional desenvolvido em C++ com o framework **JUCE** para arquivamento, catalogação e controle de qualidade de acervos multimídia. 

O aplicativo opera sob dois modos principais:
- **Modo Catálogo (Catalog Mode)**: Focado em lançamentos musicais comerciais. Restringe as fichas de metadados a tipos de mídias relevantes (ex: Álbum/Release, Faixa, Vinil, Cassete, CD, Fita de Rolo, Sample) e agrupa itens por Artista e Lançamento.
- **Modo Arquivo (Archive Mode)**: Focado em preservação histórica geral. Oferece todas as 26 fichas de metadados disponíveis (incluindo fotos, documentos, negativos, rolos de filme, gravações de campo, etc.).

---

## 2. Componentes e Fluxos de Funcionamento

### 2.1. Arquitetura do Banco de Dados (SQLite3)
O estado do projeto é salvo em um arquivo SQLite estruturado em dois bancos de dados:
1. **Registro (`registro.db`)**: Contém as decisões humanas, metadados editáveis, marcadores e a tabela de arquivos importados.
   - `item`: Entidade principal. Armazena o código de acervo (ex: `PREFIX-00001`), o estado (ex: `novo`, `catalogado`, `revisado`), e o tipo de mídia (`tipo_midia`).
   - `item_campo`: Armazena metadados estruturados chave-valor em múltiplos níveis (ex: raiz, faixa, etc.) vinculados à definição YAML correspondente.
   - `item_tag` & `busca_fts`: Armazena tags de catalogação em formato textual e seu índice de pesquisa rápida.
   - `marcador`: Armazena pontos ou intervalos de tempo (timecodes) criados pelo operador na timeline.
   - `item_observacao`: Armazena anotações manuais atreladas a timecodes.
2. **Índice (`indice.db`)**: Cache de performance que armazena a forma de onda de áudio (20 baldes/segundo) e as imagens em miniatura. Isso permite desenhar a tela instantaneamente mesmo se o disco/Vault original estiver desconectado.

### 2.2. Pipeline de Ingestão (Ingest Pipeline)
Quando arquivos ou pastas são arrastados para o aplicativo:
1. **Fila de Ingestão**: Um thread de background processa cada arquivo.
2. **Cálculo de Checksum**: Calcula o hash SHA-256 do arquivo.
3. **Detecção de Duplicados**: Se o hash já existe no projeto, o arquivo é importado com o estado `'duplicata'` sem copiar os dados de novo, preservando espaço.
4. **Leitura Técnica (`LeituraTecnica.cpp`)**: Invoca o `ffprobe` para extrair metadados técnicos (duração, codec, sample rate, canais, largura, altura, etc.) salvos como JSON.
5. **Geração de Miniaturas**:
   - **Imagens**: Lidas com correção de orientação baseada em metadados EXIF.
   - **Vídeos**: O `ffmpeg` é invocado em 3 tentativas com busca inteligente de frame (`-ss`) para extrair uma imagem real do vídeo como thumbnail.
   - **Sessões de Áudio**: Identifica arquivos de projetos (.RPP, .PTX, .ALS, .LOGIC, etc.) e carrega o logotipo do software correspondente a partir da pasta `Assets/`.
6. **Inserção com `tipo_midia = NULL`**: Por padrão (design de qualidade), todo arquivo entra sem classificação, com estado `'novo'`, obrigando o operador a classificá-lo manualmente.

---

## 3. Detalhamento das Telas e Funcionalidades

### 3.1. Grade Principal (Mosaico)
Exibe as miniaturas (thumbnails) de todos os itens do projeto de forma virtualizada:
- **Performance**: Apenas os itens visíveis na tela são desenhados (`paint` instantâneo mesmo com 100 mil itens).
- **Tratamento de Imagens Verticais**: Imagens verticais são rotacionadas com base no EXIF e desenhadas com barras pretas laterais (pillarbox) para evitar distorção.
- **Logos de Softwares**: DAW Sessions exibem as logos reais dos softwares (Reaper, Pro Tools, Logic Pro, Ableton, iZotope RX, Pure Data).
- **Menu de Ações (Botão Direito)**: Permite renomear, mover para pastas, excluir ou categorizar rapidamente através de atalhos de teclado (`1` a `9`).

### 3.2. Árvore de Pastas (Tree View)
Exibe a estrutura física ou lógica de diretórios do acervo:
- **Resolução de IDs**: Ao criar pastas, mover arquivos ou recarregar a árvore, a seleção atual e o foco de navegação são preservados via ID único, evitando que a visualização volte para o topo.
- **Renomeação Direta**: Clicar em renomear abre uma janela de texto assíncrona focando automaticamente o campo de texto para digitação imediata.

### 3.3. Painel de Metadados (Ficha Lateral)
Exibe e permite a edição de metadados em tempo real:
- **Campos Dinâmicos**: Montados em runtime com base no arquivo YAML da ficha correspondente (ex: `digital_audio.yaml`, `sessao.yaml`).
- **Validação Visual**: Campos obrigatórios ou formatos específicos (como ISRC e código de barras EAN) mostram indicadores de erro caso o padrão seja violado, mas nunca impedem o salvamento do texto digitado.
- **Edit Mode Global**: O modo de edição fica ativo o tempo todo (`editMode = true`).
- **Atualização Reativa**: Qualquer alteração em campos de texto, dropdowns ou tags recarrega instantaneamente os componentes do aplicativo (grade, contadores de arquivos e painel de inconsistências).

### 3.4. Estação de Escuta de Áudio (`AudioWorkspace`)
O workspace dedicado aberto ao dar duplo clique em qualquer item de áudio:
- **Visualizador de Forma de Onda**: Mostra o canal esquerdo/direito renderizado a partir do cache do índice.
- **Medidores ao Vivo**: VU/Peak, Vectorscópio (Lissajous) e Espectro de frequências em tempo real (atualizados a 30Hz).
- **Transporte na Parte Inferior**: Play, Stop, Loop In, Loop Out e botão Close posicionados na parte inferior da tela.
- **Função de Marcadores**:
  - Botão **Add Marker** e atalho de teclado `'M'` inserem marcadores no instante atual da reprodução.
  - Marcadores podem ser arrastados pelas bordas na régua de tempo.
  - Todos os marcadores ativos são sincronizados automaticamente na ficha de metadados do item dentro do campo **NOTES** no formato `(MINUTAGEM: TEXTO)`.

### 3.5. Player de Vídeo e Preview
Para vídeos e outras mídias, o duplo clique abre a janela de preview geral:
- **Vídeo Player Integrado**: Reproduz o vídeo de forma nativa e sincroniza a posição com o timeline jog-wheel profissional JUCE posicionado na parte inferior.
- **Scrubbing Silencioso**: A Timeline realiza o scrubbing/arraste de forma fluida sem eco de áudio, tocando o som limpo pelo motor JUCE.

### 3.6. Coleção Inteligente "Needs Review"
Painel fixado na barra lateral que lista automaticamente todos os itens incompletos:
- Itens unclassificados (`tipo_midia IS NULL`).
- Itens classificados que violam regras obrigatórias de sua respectiva ficha YAML (ex: álbuns sem imagem de capa associada, faixas sem artista, etc.).

---

## 4. O Que Já Foi Implementado nas Últimas Sessões

1. **Renomeação de Pastas na Árvore**: Corrigido o bug em que o menu de contexto de árvore síncrono causava travamento ou falha de foco. O editor de texto agora foca de imediato e aceita a digitação.
2. **Preservação de Foco na Árvore**: `recarregar()` agora preserva a pasta selecionada através do ID estrutural.
3. **Transporte de Áudio na Parte Inferior**: Tanto no preview genérico de áudio quanto na **Estação de Escuta principal (`AudioWorkspace.cpp`)**, o painel de botões de transporte está fixado na borda inferior.
4. **Marcadores na Janela de Áudio**: Adicionado o botão de criação de marcador e o atalho de teclado `'M'` no `AudioWorkspace`.
5. **Sincronização Automática com Notas**: Toda criação, movimentação ou deleção de marcador no `AudioWorkspace` ou no `TimelineComponent` gera/atualiza as notas do item no formato `(MINUTAGEM: TEXTO)`.
6. **Classificação de Sessões (DAWs)**: Extensões `.rpp`, `.ptx`, `.ptf`, `.als`, `.logic`, `.logicx`, `.rxdoc` e `.pd` são classificadas como `Sessao`. Elas utilizam a logo original dos softwares extraídas de `Assets/` na grade e no preview.
7. **Remoção do Edit Mode**: O checkbox foi removido e os campos são editáveis diretamente por padrão.
8. **Correção de Busca do ffmpeg/ffprobe**: Adicionada pesquisa em `/opt/homebrew/bin` e outros caminhos no macOS para garantir a extração de miniaturas ao abrir o app via Finder.
9. **Atualização Reativa de Metadados**: Alterações de metadados e tags disparam recarregamento imediato dos filtros da barra lateral e contagens do projeto.
