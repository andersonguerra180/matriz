# MATRIZ
## Especificação de produto — software de digitalização, acervo e catalogação
Versão 1.0 do documento · para implementação com Claude Code
---
## 0. Instruções para o agente de implementação
Leia este documento inteiro antes de escrever qualquer linha.
**O produto é único e completo.** Não existe versão reduzida, não existe edição básica, não existe recurso capado. Tudo descrito aqui faz parte do software que será lançado. As etapas da seção 16 são **ordem de construção**, não etapas de lançamento — nada é entregue ao usuário antes de o conjunto estar pronto.
**Regras de trabalho:**
1. Nunca deixe função vazia, `TODO`, `pass`, ou stub "para implementar depois". Se um bloco não pode ser terminado agora, pare e pergunte antes de criar o arquivo.
2. Não invente campo, formato ou comportamento que não esteja aqui. Se faltar definição, pergunte.
3. Não altere as decisões marcadas como **não-negociáveis** (seção 3) sem consultar.
4. Prefira arquivo de configuração a código hard-coded sempre que a especificação disser "dirigido por definição".
5. Ao terminar cada etapa da seção 16, produza um walkthrough do que foi construído, o que ficou de fora, e o que a próxima etapa vai precisar.
6. O idioma da interface é português do Brasil, com inglês como segundo idioma previsto na arquitetura desde o início (strings externalizadas).
**O que você pode decidir sozinho:** nomes internos de funções e classes, organização de pastas do código, escolha de biblioteca auxiliar não especificada, estratégia de teste.
**O que você não pode decidir sozinho:** esquema do banco, formato do arquivo de definição de ficha, comportamento de imutabilidade, o que a IA escreve e onde, formato dos exports.
---
## 1. O que é
Aplicativo desktop (macOS e Windows) que resolve o ciclo completo de **digitalizar, catalogar, indexar, navegar e entregar** coleções de mídia — sonora, visual e documental.
Atende dois usos no mesmo software, sem separação de produto:
- **Acervo e preservação** — arquivista, casa de transferência, museu, rádio, espólio, pesquisador, produtor com material histórico.
- **Catálogo comercial** — selo fonográfico, produtor com biblioteca própria, distribuidora, biblioteca de samples.
O diferencial não é DSP nem IA isoladamente. É a combinação de:
1. **Item composto** — áudio, capas, documentos e fotos do mesmo objeto vivem num registro só, não em quatro pastas soltas.
2. **IA local que nunca contamina o material** — indexa e organiza numa camada paralela, descartável e reconstruível.
3. **Ergonomia de operador** — quem usa isso passa oito horas por dia na tela. O software é desenhado para lote, teclado e velocidade, não para formulário.
---
## 2. Modos de projeto
O modo é uma **configuração escolhida ao criar o projeto**, não uma versão do software. Todo usuário tem os dois disponíveis, e pode ter projetos de tipos diferentes lado a lado.
| | Preservação | Catálogo |
|---|---|---|
| Objetivo | Guardar fiel, provar integridade | Organizar, corrigir, exportar |
| Master | Travado por padrão (destravável) | Editável |
| Checksum | Obrigatório | Opcional, ligado por padrão |
| Metadado | Documento histórico, assinado | Vivo, corrigido continuamente |
| Derivada | Separada, com receita logada | Múltiplas versões normais |
| IA | Só sugere, nunca escreve | Só sugere, nunca escreve |
| Exports típicos | Relatório de proveniência, BWF, manifesto | DDEX, CSV de distribuidora, planilha de ISRC |
A regra "IA nunca escreve" vale nos dois, sem exceção e sem opção de desligar.
---
## 3. Princípios não-negociáveis
**P1 — Dois arquivos por padrão.**
Em projeto de preservação, o master entra flat, sem processamento, com checksum no ato, e o software impede sobrescrita. Qualquer correção gera uma derivada separada, com a receita gravada no metadado.
Isso é **padrão, não camisa de força**: existe uma opção por projeto chamada "permitir sobrescrever master", desligada por padrão. Ao ligar, o software mostra um aviso uma única vez explicando a consequência. Depois disso, não incomoda mais. O usuário é adulto e decide.
**P2 — A IA nunca escreve no material nem na ficha.**
Modelos escrevem apenas no índice. O software tem duas camadas que nunca se misturam:
- **Registro** — arquivo, ficha, checksum, marcadores, decisões humanas.
- **Índice** — embeddings, transcrições, tags detectadas, rostos, OCR, fingerprints.
Modelo melhor amanhã = apagar o índice e reprocessar. O registro não muda uma vírgula. Erro de IA nunca vira erro de catálogo — vira ruído de busca, que é reversível.
**P3 — IA propõe, humano assina.**
Todo dado vindo de modelo carrega modelo, versão e data, e aparece marcado visualmente como sugestão. Só migra do índice para o registro quando o operador confirma, e aí consta como decisão humana com autor e data.
**P4 — Tudo roda local.**
Material de acervo é sigiloso, tem direito autoral, tem imagem de pessoa. Nenhum byte sai da máquina sem comando explícito do usuário. Isso também elimina custo por uso e permite operar offline.
**P5 — Projeto é portátil.**
Uma pasta no disco contendo os arquivos e o banco. Copiou pro HD externo, abriu em outra máquina, funciona. Nada de banco central escondido no sistema.
---
## 4. Escopo
### 4.1 Dentro
**Captura:** somente áudio, analógico e digital, via interface.
**Ingest:** qualquer formato — áudio, vídeo, imagem, documento, PDF.
**Mídias catalogáveis:** fita de rolo, cassete, vinil, DAT, MiniDisc, CD, SD, pendrive, HD, filme escaneado (16 mm, Super 8, 8 mm), vídeo analógico já digitalizado (VHS, U-matic, Betacam), foto, negativo, slide, documento, release fonográfico, banco de samples.
**Catalogação:** projeto → item composto → arquivo, com ficha dirigida por definição.
**Índice:** busca semântica, transcrição, OCR, duplicatas, agrupamento, rostos, fingerprint.
**Navegação:** mosaico, tira de diagnóstico, transporte com jog e shuttle, player de vídeo, mapa de embeddings, linha do tempo, transcrição navegável.
**Entrega:** relatório PDF, CSV/XLSX, metadado embutido, sync com destino local, NAS, S3 e Drive, com verificação de integridade.
### 4.2 Fora, e declarado
- Captura de vídeo analógico (placa, TBC, dropout compensator, FFV1 — é outro produto).
- Captura de imagem e scanner (driver, calibração IT8 — é outro produto).
- Edição de áudio destrutiva. Não é DAW.
- Restauração agressiva (declick pesado, separação de fonte).
- Servidor multiusuário simultâneo. É monoposto ou pasta compartilhada.
Para vídeo e imagem, o software **verifica conformidade** do que entrou — perfil, resolução, bit depth, codec, presença de alvo de cor — em vez de tentar produzir.
---
## 5. Modelo de dados
### 5.1 Hierarquia
```
Projeto   (o acervo, o fundo, o selo)
  └─ Item      (o objeto real: o rolo, o LP, a sessão fotográfica, o release)
       └─ Arquivo   (master, derivada, capa, verso, documento, stem)
```
### 5.2 Projeto
Carrega o que é comum e é herdado pelos itens:
- modo (preservação | catálogo)
- nome, instituição ou selo, responsável
- prefixo e máscara de nomenclatura: `{prefixo}-{ano}-{item}-{seq}`
- destino local e destino de nuvem
- vocabulário de assuntos do projeto
- formato padrão de captura
- código de registrante ISRC (uso catálogo)
- flag "permitir sobrescrever master"
- perfil de conformidade adotado
### 5.3 Item
- id, código de acervo, título
- tipo de mídia — determina qual ficha carregar
- estado: não digitalizado, capturado, QC ok, alerta, publicado
- ficha (campos conforme a definição do tipo)
- assuntos vinculados
- marcadores
- notas livres
- relações com outros itens: mesmo evento, mesma sessão, versão alternativa, parte de conjunto
- histórico de alterações com autor e data
### 5.4 Arquivo
- caminho relativo à pasta do projeto
- papel: preservation master, access copy, capa frente, capa verso, encarte, documento, stem, foto de suporte
- checksum MD5 e SHA-256, data de geração, data da última verificação
- características técnicas lidas na entrada
- receita de processamento, se derivada
- estado de sincronização e de verificação em nuvem
### 5.5 Índice — banco separado
- embeddings CLIP de imagem e de keyframe
- embeddings de texto
- transcrições com timestamp por palavra, com marca de verificado ou não verificado
- OCR
- vetores de rosto
- fingerprint de áudio
- pHash de imagem
- classificações sugeridas, com confiança
- para todo registro: modelo, versão, data de processamento
Apagável e reconstruível a qualquer momento, sem perda de informação catalográfica.
---
## 6. Ficha dirigida por definição
### 6.1 Por que
São mais de doze esquemas de campos diferentes. Se cada um for código próprio, a manutenção multiplica e o usuário nunca pode criar tipo próprio — que instituição vai pedir no primeiro mês.
**Cada tipo de mídia é um arquivo YAML de definição.** Uma única tela renderiza qualquer esquema. Adicionar tipo novo é escrever um arquivo, não recompilar.
### 6.2 Recursos que a definição precisa suportar
- grupos de campos
- tipos: texto, número, inteiro, data, booleano, opção fechada, opção livre (autocompletar + criar), lista de pessoas, tabela
- obrigatoriedade
- validação nomeada (`isrc`, `ean13`, `soma_100`)
- visibilidade condicional (`visivel_se`)
- valor herdado do projeto
- valor preenchido por leitura técnica
- valor sugerido por modelo, com confiança
- efeito colateral (`afeta: [habilitar_transcricao]`)
- alerta condicional
- níveis aninhados (release → faixa)
- arquivos esperados, com papel, obrigatoriedade e requisito mínimo
### 6.3 Exemplo — rolo de fita
```yaml
tipo: fita_rolo
rotulo: Fita de rolo
grupos:
  - rotulo: Suporte
    campos:
      - id: largura
        rotulo: Largura
        tipo: opcao
        opcoes: ["1/4 pol", "1/2 pol", "1 pol", "2 pol"]
        obrigatorio: true
      - id: marca
        rotulo: Marca
        tipo: opcao_livre
        opcoes: [Ampex, Scotch/3M, BASF, Agfa, Maxell, TDK, Sony, Quantegy, RMGI, ATR]
      - id: formulacao
        rotulo: Formulação
        tipo: opcao_livre
        opcoes: ["456", "406", "911", "900", "LPR35", "GP9", "ATR Master", "206", "207"]
      - id: espessura
        rotulo: Espessura
        tipo: opcao
        opcoes: ["1.0 mil", "1.5 mil", "0.5 mil"]
  - rotulo: Reprodução
    campos:
      - id: velocidade
        rotulo: Velocidade
        tipo: opcao
        opcoes: ["4,75 cm/s", "9,5 cm/s", "19 cm/s", "38 cm/s", "76 cm/s"]
        obrigatorio: true
      - id: curva
        rotulo: Curva de equalização
        tipo: opcao
        opcoes: [NAB, IEC1, IEC2, AES, CCIR]
        obrigatorio: true
      - id: pistas
        rotulo: Configuração de pistas
        tipo: opcao
        opcoes: [full-track, half-track, quarter-track, 8, 16, 24]
      - id: direcao
        rotulo: Sentido
        tipo: opcao
        opcoes: [unidirecional, bidirecional, invertida]
      - id: maquina
        rotulo: Máquina de reprodução
        tipo: texto
      - id: alinhamento
        rotulo: Fita de alinhamento usada
        tipo: texto
  - rotulo: Estado físico
    campos:
      - id: sticky_shed
        rotulo: Sinais de sticky shed
        tipo: booleano
        alerta_se_true: "Requer tratamento térmico antes da reprodução"
      - id: emendas
        rotulo: Emendas visíveis
        tipo: inteiro
      - id: print_through
        rotulo: Print-through audível
        tipo: booleano
      - id: estado_geral
        rotulo: Estado geral
        tipo: opcao
        opcoes: [bom, regular, frágil, crítico]
  - rotulo: Conteúdo
    campos:
      - id: natureza
        rotulo: Natureza do conteúdo
        tipo: opcao
        opcoes: [fala, música, ambiente, ritual, misto, tons de teste]
        sugerido_por: classificador_fala_musica
        afeta: [habilitar_transcricao]
      - id: idioma
        rotulo: Idioma
        tipo: opcao_livre
        visivel_se: "natureza in [fala, misto]"
```
### 6.4 Exemplo — release fonográfico
```yaml
tipo: release
rotulo: Release fonográfico
niveis: [release, faixa]
release:
  campos:
    - id: titulo
      obrigatorio: true
    - id: artista_principal
      obrigatorio: true
    - id: selo
      herda_do_projeto: true
    - id: numero_catalogo
    - id: upc_ean
      validacao: ean13
    - id: ano
    - id: data_lancamento
      tipo: data
    - id: formato
      tipo: opcao
      opcoes: [álbum, EP, single, compilação, trilha]
    - id: genero
      tipo: opcao_livre
faixa:
  campos:
    - id: numero
      tipo: inteiro
      obrigatorio: true
    - id: titulo
      obrigatorio: true
    - id: isrc
      validacao: isrc
      gerar_em_sequencia: true
    - id: duracao
      preenchido_por: leitura_tecnica
    - id: compositores
      tipo: lista_pessoas
    - id: editora
    - id: participacao
      tipo: lista_pessoas
    - id: explicito
      tipo: booleano
    - id: idioma
    - id: splits
      tipo: tabela
      colunas: [pessoa, papel, percentual]
      validacao: soma_100
arquivos_esperados:
  - papel: master
    obrigatorio: true
    por: faixa
  - papel: capa_frente
    obrigatorio: true
    minimo: 3000x3000
  - papel: capa_verso
  - papel: encarte
```
Sobre ISRC: o código tem estrutura fixa (país, registrante, ano, designação). O software valida, detecta duplicata e gera em sequência a partir do registrante configurado no projeto. O software **não** emite o código de registrante — isso vem da agência nacional, e o texto de ajuda deve dizer isso claramente.
### 6.5 Exemplo — sample
```yaml
tipo: sample
rotulo: Sample
campos:
  - id: categoria
    tipo: opcao
    opcoes: [kick, snare, hat, perc, bass, lead, pad, vocal, fx, loop, one-shot]
    sugerido_por: clip_audio
  - id: bpm
    tipo: numero
    preenchido_por: detector_bpm
  - id: tonalidade
    tipo: opcao
    preenchido_por: detector_tonalidade
  - id: tipo_temporal
    tipo: opcao
    opcoes: [one-shot, loop]
    preenchido_por: leitura_tecnica
  - id: licenca
    tipo: opcao
    opcoes: [royalty-free, exclusiva, sample clearance pendente, uso interno]
  - id: origem
    tipo: texto
```
### 6.6 Tipos a escrever
`fita_rolo`, `cassete`, `vinil`, `dat`, `minidisc`, `cd`, `filme`, `video`, `foto`, `negativo`, `slide`, `documento`, `release`, `sample`.
---
## 7. Ingestão
### 7.1 O erro a evitar
Exigir ficha completa antes de qualquer processamento trava o operador respondendo formulário no chute, com a máquina ociosa, sem ter visto o material.
### 7.2 Três estágios
**Estágio 1 — leitura técnica.** Automático, imediato, sem perguntar nada. Não é indexação de conteúdo, é leitura do que já está no arquivo:
- checksum MD5 e SHA-256
- EXIF, BWF/bext, iXML, ID3, Vorbis, MediaInfo
- miniatura, forma de onda, keyframe
- duração, resolução, sample rate, bit depth, codec, frame rate
- duplicata por pHash e por fingerprint
- lossy transcodificado (corte de banda denunciando origem MP3 ou ATRAC)
- classificação fala × música
- em pasta de disco: inferência de estrutura
**Estágio 2 — ficha.** Aparece **já pré-preenchida** com o resultado do estágio 1. O software não pergunta "tem voz?" — afirma "detectei fala em 94% do material, transcrever?". O operador é revisor, não digitador.
A ficha é sempre respondida **por grupo**, nunca por arquivo. Pasta com 3.000 fotos vira ~40 grupos por similaridade visual e salto de timestamp. Quarenta respostas, não três mil.
**Estágio 3 — indexação pesada.** Fila em background, retomável, com progresso por item. Só entra depois da ficha confirmada, porque a ficha define o que rodar.
### 7.3 Drag de pasta de disco
| Sinal | Inferência |
|---|---|
| Pasta `Artista - Álbum (Ano)` | release, artista, ano |
| `01 - Faixa.wav` | ordem e título |
| ID3 / Vorbis / BWF embutido | título, ISRC, compositor |
| Imagem quadrada grande | capa frente |
| PDF na pasta | encarte |
| Duração + fingerprint | mesma gravação em pastas diferentes |
Conjunto de pastas = catálogo inteiro importado numa passada.
### 7.4 Painel de inconsistências
Após ingest, lista o que está incompleto ou errado: faixa sem ISRC, release sem capa, master em lossy, sample rate divergente dentro do mesmo disco, capa abaixo do mínimo, splits que não somam 100%, ISRC duplicado, arquivo órfão, checksum divergente.
---
## 8. Captura de áudio
- calibração por loopback amarrando dBFS ↔ dBu, com ganho travado depois de calibrar
- gravação longa sem glitch, com monitoramento
- marcadores em tempo real por tecla única
- formato de master definido no projeto
- checksum gerado ao encerrar o arquivo
**QC automático não-destrutivo,** durante e após: clipping, DC offset, dropout, hum e sua frequência exata, desvio de azimute (delay entre canais em frações de sample), diferença de nível L/R, correlação, print-through, trechos de silêncio.
O QC **reporta e sugere**, nunca corrige sozinho. Correções vão para a derivada, com receita logada.
Fontes por caminho:
| Mídia | Caminho | Atenção |
|---|---|---|
| Fita, cassete, vinil | Interface analógica | Curva de reprodução, calibração |
| DAT | S/PDIF | Subcode só sai em decks específicos; senão o operador anota |
| MiniDisc | Analógico (NetMD só em Hi-MD) | ATRAC é lossy; a ficha registra isso |
| CD | Ripagem bit-exata, fora da interface | Fluxo separado |
| SD, pendrive, HD | Ingest | Já é arquivo |
---
## 9. Camada de IA
Local. CPU-only viável, GPU opcional.
| Função | Modelo | Aplica a |
|---|---|---|
| Agrupar por assunto | CLIP + clustering | imagem, keyframe |
| Busca semântica | CLIP + embeddings de texto | tudo |
| Duplicata | pHash, fingerprint de áudio | imagem, áudio |
| Rostos recorrentes | InsightFace | imagem, vídeo |
| OCR | PaddleOCR / Tesseract | imagem, PDF |
| Qualidade de imagem | detector de blur e exposição | imagem |
| Transcrição | Whisper + Silero VAD | áudio falado, vídeo |
| Diarização | pyannote | áudio falado |
| BPM e tonalidade | detector próprio | sample, música |
| Cortes de cena | detector | vídeo |
| Classificação fala × música | classificador leve | áudio |
### 9.1 Espaço único de busca
CLIP e texto vivem no mesmo espaço vetorial. Uma busca cobre tudo: `carnaval 1978` retorna foto, trecho de fita onde alguém fala isso, e o minuto exato do vídeo. Não é busca por nome de arquivo — é busca pelo que existe dentro do material.
### 9.2 Navegação que o índice destrava
- **Mapa do acervo** — projeção 2D dos embeddings; itens parecidos ficam juntos; a forma da coleção fica visível
- **Linha do tempo real** — data extraída do conteúdo (EXIF, OCR do verso, menção na transcrição), não a data do arquivo
- **Pessoas** — rosto recorrente na foto ligado a voz recorrente na fita
- **Transcrição navegável** — clica na palavra, o áudio pula pro segundo exato
### 9.3 Riscos técnicos declarados
**Whisper alucina em silêncio e em ruído.** Fita velha com chiado gera frase inventada. Defesas obrigatórias: VAD antes, para não mandar silêncio ao modelo; transcrição sempre marcada como não-verificada até alguém ler. Em acervo histórico, frase inventada é pior que nenhuma transcrição.
**Áudio não-falado.** Rodar Whisper em fita só de tambor é desperdício de horas e fonte garantida de alucinação. Por isso o campo "natureza do conteúdo" existe, sugerido pelo classificador e confirmado pelo operador.
**Tempo de processamento.** 400 horas de áudio em CPU é questão de semana. Fila retomável, progresso por item, aviso honesto de que indexação não é tempo real.
**Reconhecimento facial e LGPD.** Recurso opt-in por projeto, tudo local, com texto explicando a responsabilidade do operador.
---
## 10. Marcadores e vocabulário
### 10.1 Duas coisas distintas
**Marcador de ponto** — instante ou trecho, com tempo de entrada e saída, título, assunto e observação livre. Exemplo: `6:20–14:05, congado, Búzios`.
**Assunto** — vocabulário controlado do projeto. "Congado" existe uma vez só, apontado por 30 marcadores em 12 fitas. Sem isso vira "congado", "Congado", "congada de Búzios", "cong." e a busca morre.
O operador não sente a diferença: digita, o campo autocompleta com o que já existe no projeto, e só cria termo novo ao confirmar. Sem tela de gerenciamento de tesauro.
### 10.2 Por que o marcador manual é insubstituível
Whisper não entende "congado" — é tambor e canto, não fala. CLIP não sabe que aquilo é a Festa da Santa. O operador sabe. O marcador manual é o único metadado de conteúdo que nasce assinado, e entra direto no registro com autor e data.
Hierarquia de confiança, visível na tela:
```
marcador humano  >  transcrição verificada  >  transcrição bruta  >  sugestão de modelo
```
### 10.3 Herança
Trecho de transcrição que cai dentro de um marcador **herda o assunto**, mesmo estando errado. Busca por "congado" retorna aquele trecho ainda que a palavra nunca apareça escrita. É o operador consertando o que o modelo não alcança, sem corrigir palavra por palavra.
Assunto também se aplica ao item inteiro, não só ao trecho.
### 10.4 Ergonomia — decide se o recurso é usado
- Tecla única abre o marcador com o tempo já cravado. Digita o assunto autocompletando. Enter fecha. Sem modal, sem mouse, sem sair do áudio.
- **Marcador retroativo** — a pessoa percebe 40 segundos depois; a tecla marca "40s atrás", ajustável no arraste.
- **Marcação por arraste na tira.**
- Assunto recém-usado vira chip clicável pelos próximos minutos.
Se o operador precisar parar a fita para catalogar, ele deixa pra depois. E depois nunca chega.
---
## 11. Interface
### 11.1 Layout
Sem abas, sem janela flutuante, sem recurso escondido em submenu.
```
┌──────────────┬──────────────────────────────┬──────────────┐
│              │                              │              │
│  Mosaico do  │      Visualizador            │    Ficha     │
│    acervo    │   (vídeo / imagem / vazio)   │   (sempre    │
│              │                              │    aberta)   │
│  blocos com  ├──────────────────────────────┤              │
│  mini-tira   │      Transporte + jog        │  campos do   │
│  e cor de    ├──────────────────────────────┤  tipo de     │
│  estado      │      Tira de diagnóstico     │  mídia       │
│              │      (largura total)         │              │
└──────────────┴──────────────────────────────┴──────────────┘
```
A ficha é dirigida pela mídia — fita mostra 12 campos, foto mostra 8. O que não se aplica não aparece. É assim que metadado fica sempre visível sem virar formulário de imposto de renda.
### 11.2 Mosaico
Grade de blocos, um por item, cada um com sua mini-tira. Cor indica estado: cinza (não digitalizado), azul (capturado), verde (QC ok e checksum), âmbar (alerta pendente), halo (sincronizado). Duzentos itens numa tela e o operador sabe onde está o trabalho.
### 11.3 Tira de diagnóstico
Substitui a waveform esticada. O item inteiro sempre cabe na tela, em faixas empilhadas de largura fixa, sem rolagem.
**Áudio:**
| Faixa | Mostra | Revela |
|---|---|---|
| Espectro comprimido | banda por tempo | queda de agudo por azimute, hum, corte em 16 kHz denunciando origem lossy, dropout, troca de máquina ou de formulação |
| Fase L/R | correlação | cabo invertido no meio da sessão original |
| Nível | envelope | dinâmica, silêncios, clipping |
| Marcadores | manuais | navegação |
**Vídeo:** troca a faixa de espectro por faixa de keyframes.
A tira é **régua de navegação**: clique posiciona o cursor, arraste cria marcador.
### 11.4 Transporte e jog
- **Jog** — scrub com áudio, resampling em tempo real, sensação de fita na cabeça. Para quando o giro para.
- **Shuttle** — velocidade contínua ±16×, com detent no zero.
- Alternados por tecla, atrelados ao scroll ring do mouse quando disponível.
- Teclado sempre ativo: espaço, J/K/L, setas por frame, `[` e `]` para in e out.
- Transporte remoto opcional via telefone ao lado do deck, reaproveitando a arquitetura de comunicação já existente na linha de produtos.
### 11.5 Vídeo — frame rate
Problema real de filme escaneado: o arquivo vem em 24 ou 25 fps já normalizado, mas o original era 16, 18 ou 24. Ou veio de telecine com pulldown 3:2 e tem quadros duplicados.
Três campos separados, não um:
- **fps de captura** — o que está no arquivo
- **fps original declarado** — 18 fps de Super 8, por exemplo
- **pulldown detectado** — se veio de NTSC
O player reproduz no fps original. Super 8 de 18 fps tocado a 24 deixa as pessoas andando rápido demais, e o arquivista desconfia do software inteiro.
Detecção automática de quadro duplicado e de campo entrelaçado na entrada, registrada na ficha como proveniência técnica.
### 11.6 Temas
**BKR Dark** — padrão, referência Logic Pro dark. Reconhecível, não cansa em turno longo.
**System CRT** — preto e verde fósforo, acessível pela barra de menus.
Ressalva de legibilidade a resolver no design: verde sobre preto tem contraste ruim para texto pequeno e denso. Ficha com 12 campos em fósforo a 11px fica ilegível depois de duas horas. Duas saídas possíveis: o CRT ser tema de painéis (tira, transporte, medidores) mantendo a ficha em legibilidade normal, ou assumir que é modo de exibição e não de trabalho. Decidir no design, não na implementação.
Design system por tokens desde a primeira linha de UI.
---
## 12. Nuvem, integridade e entrega
### 12.1 Destino abstrato
Pasta local, NAS/SMB, S3 compatível e Google Drive — mesma interface interna. Instituição pública frequentemente proíbe Drive por política; suportar S3 e NAS custa pouco e abre o comprador que paga melhor.
### 12.2 O valor não é o upload, é a verificação
Após enviar, o software **rebaixa o checksum e confirma bit a bit**. Sem isso é um botão de sync que qualquer um tem. Com isso vira registro de fixity, que é o que arquivo precisa documentar.
Verificação periódica agendada, com relatório de integridade ao longo do tempo.
### 12.3 Volume — aviso obrigatório na tela
24/96 estéreo ≈ 1 GB por hora. Acervo de 200 rolos ≈ 150 GB. Drive gratuito de 15 GB acaba no terceiro item. O software mostra o cálculo **antes** do upload.
### 12.4 Exports
**Preservação:** relatório PDF de proveniência (antes e depois, receita aplicada, operador, data, equipamento, lote de fita), CSV/XLSX de inventário, BWF com `bext` e iXML embutidos, manifesto de checksums, pacote estruturado para entrega institucional.
**Catálogo:** planilha de ISRC, CSV nos formatos aceitos por distribuidoras, ficha técnica por release, pacote de entrega com master, arte e metadado, relatório de inconsistências.
---
## 13. Stack técnica
| Camada | Escolha | Nota |
|---|---|---|
| App | C++/JUCE ou Tauri | decisão em aberto — ver seção 17 |
| Áudio | JUCE / RtAudio | captura, transporte, scrub |
| Mídia | ffmpeg | decodificação de vídeo, keyframes, leitura técnica |
| Leitura técnica | MediaInfo, ExifTool | metadado embutido |
| Banco | SQLite na pasta do projeto | portabilidade (P5) |
| Vetores | sqlite-vec ou FAISS local | índice separado do registro |
| IA | sidecar Python via IPC | mesmo padrão já usado na linha |
| Empacotamento | PyInstaller + instalador assinado | ver custo abaixo |
**Custo declarado:** empacotar Python e modelos dentro de app desktop leva o instalador a 1,5–2,5 GB e traz GPU opcional, Gatekeeper no macOS, e semanas de trabalho que não aparecem na tela.

**Regra de dependência (adicionada durante a implementação):** nenhuma dependência C/C++ entra por Homebrew, apt, vcpkg ou qualquer gerenciador de pacotes do sistema. Toda dependência entra por `FetchContent` no CMake, compilada junto com o projeto. Motivo duplo: (1) dependência de sistema não reproduz em máquina limpa nem em CI, e quebra silenciosamente no Windows, que não tem Homebrew; (2) na prática, em macOS mais antigo sem bottle disponível, o Homebrew tenta recompilar toda a cadeia de dependências (inclusive o próprio CMake) do zero antes de instalar o pacote pedido — um custo de build completamente desproporcional a uma dependência de biblioteca. `ffmpeg`/`ffprobe` são a única exceção, e mesmo assim não por gerenciador de pacote: os binários são embarcados no instalador (Etapa 10) e o código resolve o caminho relativo ao executável do app, nunca via PATH em build de produção (ver `Source/Ingest/ProcessoExterno.h`).

**"Leitura técnica" na prática (adicionado durante a implementação):** a linha da tabela dizia MediaInfo + ExifTool. Nenhum dos dois tem caminho limpo de FetchContent pra essa regra de dependência. O que entrou: `ffprobe` (dimensão, formato, codec, sample rate, bit depth, fps — mesma ferramenta já usada pra áudio/vídeo, cobre imagem também) e `Exiv2` (EXIF completo: câmera, lente, orientação, GPS, data original — linkado direto via FetchContent). PDF ficou só com tamanho de arquivo por enquanto — PDFium e MuPDF (as duas bibliotecas de contagem de páginas avaliadas) não têm build FetchContent+CMake viável pros dois sistemas operacionais (ver nota longa em `Source/Ingest/LeituraTecnica.cpp`); contagem de página fica ausente de forma explícita em vez de usar um parser artesanal que erra em silêncio.
---
## 14. Concorrência e posicionamento
| Ferramenta | O que faz | Por que não resolve |
|---|---|---|
| CollectiveAccess, CollectionSpace, Omeka | DAM institucional completo, grátis | Servidor, PHP/MySQL, curva absurda |
| Tropy | Organiza foto de pesquisa | Só imagem, sem áudio, sem captura |
| MediaInfo, MediaConch, BWF MetaEdit, Siegfried | Ferramentas técnicas pontuais | Linha de comando, sem catálogo |
| Discogs, MusicBrainz Picard | Metadado de release | Público, não privado; não gerencia arquivo |
| Airtable, Notion, planilha | O que a maioria realmente usa | Não vê dentro do arquivo, não valida nada |
**Posicionamento:** não competir em profundidade institucional com o software livre maduro. Competir em quem eles ignoram — o operador sozinho com 400 rolos e 3.000 fotos, e o selo com 80 lançamentos espalhados em HD externo, que não vai subir servidor.
---
## 15. Riscos do projeto
| Risco | Gravidade | Mitigação |
|---|---|---|
| Tamanho — é um DAM, um player, um indexador e um capturador | **Alta** | Ordem de construção da seção 16, com fundação antes de tudo |
| Whisper alucinando em acervo histórico | Alta | VAD, marca de não-verificado, campo de natureza do conteúdo |
| Instalador de 2 GB, Gatekeeper, GPU opcional | Média | Padrão já dominado em produto anterior da linha |
| Doze esquemas de ficha virando doze bases de código | Média | Definição em YAML, resolvido estruturalmente |
| Jog com scrub de qualidade é difícil | Média | É diferencial; vale o investimento |
| LGPD e reconhecimento facial | Média | Opt-in por projeto, local, texto explicativo |
| Ciclo de venda institucional longo | Média | O público de catálogo compra rápido e sustenta enquanto isso |
---
## 16. Ordem de construção
**Isto é ordem interna de implementação, não etapas de lançamento.** Nada vai ao usuário antes do conjunto estar completo.
Ao final de cada etapa, o agente produz um walkthrough: o que foi construído, decisões tomadas, o que ficou pendente, e o que a etapa seguinte precisa encontrar pronto.
---
**Etapa 1 — Fundação**
Esquema do banco (projeto, item, arquivo, marcador, assunto, índice separado). Parser e renderizador do formato de definição de ficha. As catorze definições YAML. Regras de imutabilidade embutidas na estrutura, com a flag de destravamento. Criação e abertura de projeto portátil. Nenhuma UI além do mínimo para testar.
*Nada funciona sem isso, e tudo depende do formato ficar certo. Se a definição de ficha precisar mudar depois, o retrabalho é grande.*
**Etapa 2 — Ingest e leitura técnica**
Entrada de arquivo por drag. Checksum. Leitura de EXIF, BWF, ID3, MediaInfo, ExifTool. Miniaturas e formas de onda. Detecção de duplicata, de lossy transcodificado e de fala × música. Inferência de estrutura de pasta de disco. Agrupamento por timestamp. Fluxo de ficha por lote. Painel de inconsistências.
**Etapa 3 — Interface principal**
Mosaico, visualizador, ficha lateral, tira de diagnóstico (áudio), transporte com jog e shuttle, atalhos de teclado, marcadores manuais e vocabulário controlado. Temas BKR Dark e System CRT com design system por tokens.
**Etapa 4 — Índice e IA leve**
Sidecar Python. CLIP para imagem e keyframe, busca semântica, agrupamento, pHash, OCR, detector de qualidade de imagem, BPM e tonalidade. Fila de processamento retomável. Marcação visual de sugestão versus decisão humana.
**Etapa 5 — Captura de áudio**
Calibração por loopback, gravação longa, monitoramento, marcadores em tempo real, QC não-destrutivo completo, geração de derivada com receita logada.
**Etapa 6 — Vídeo e imagem**
Player com fps original e pulldown, keyframes na tira, detecção de quadro duplicado e entrelaçamento, verificação de conformidade.
**Etapa 7 — Transcrição e áudio falado**
Whisper com VAD, transcrição navegável com timestamp por palavra, herança de assunto por marcador, busca dentro do áudio, diarização.
**Etapa 8 — Rostos, mapa e linha do tempo**
InsightFace opt-in, projeção 2D de embeddings, linha do tempo por data de conteúdo, vínculo entre rosto e voz.
**Etapa 9 — Nuvem, integridade e exports**
Destino abstrato (local, NAS, S3, Drive), upload com verificação bit a bit, verificação periódica agendada, relatório PDF de proveniência, todos os CSV e XLSX, BWF com metadado embutido, manifesto, pacotes de entrega.
**Etapa 10 — Acabamento**
Internacionalização, instalador assinado nas duas plataformas, manual, primeira execução, migração de projeto entre versões.
---
## 17. Decisões em aberto
1. **Stack de UI** — JUCE (força na captura, jog e scrub, já dominado na casa) ou Tauri (muito mais rápido para construir uma interface deste tamanho, com o áudio em módulo nativo). Precisa ser decidida antes da Etapa 3.
2. **Modelo de licença** — perpétuo com upgrades pagos, ou assinatura. Instituição prefere perpétuo; selo aceita assinatura.
3. **Preço.**
4. **Nome definitivo** — ver seção 18.
5. **Tratamento do tema CRT na ficha** — tema completo ou tema de painéis.
---
## 18. Nome
Critérios: funcionar para acervo e para catálogo, pronunciável em português e inglês, curto, sem trocadilho, e soar como ferramenta séria.
**MATRIZ** — primeira escolha. Em áudio brasileiro, matriz é a matriz de prensagem do vinil; em uso geral, é a origem que gera as cópias. Cobre exatamente as duas pontas: o master do acervo e o catálogo do selo. Raro um nome carregar significado técnico correto para um público e significado geral correto para o outro.
**FOLIO** — alternativa segura. O volume encadernado onde se registra. Curto, sério, funciona nos dois idiomas, baixo risco de conflito.
Outras consideradas: ACCESSION (termo técnico exato de entrada em acervo, forte no institucional e fraco no comercial), PROVENANCE (prestigioso, longo), STACKS (moderno, mais fraco no acervo), REGISTRY (direto, sem personalidade), CANON (excelente conceitualmente, inviável por conflito de marca).
Com prefixo de linha — BKR MATRIZ — ajuda quem já conhece a marca e atrapalha na venda institucional. Recomendação: nome autônomo, com assinatura discreta da casa.
Verificar domínio e registro de marca nas classes de software antes de fechar.
---
## 19. Antes de escrever código
Duas coisas precisam estar de pé:
1. **Esquema do banco** — as cinco entidades, as relações, e as regras de imutabilidade embutidas na estrutura e não na camada de aplicação.
2. **Formato de definição de ficha** — a especificação do YAML e as três primeiras definições completas (`release`, `foto`, `fita_rolo`) para validar que o formato aguenta a variedade real antes de escrever as outras onze.
Com esses dois resolvidos, o resto é execução.
