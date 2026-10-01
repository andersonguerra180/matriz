# Fixes diagnosticados, aguardando OK para aplicar

Regra combinada: diagnosticar e documentar aqui; só aplicar (edit + build + commit
por defeito) quando o usuário der o OK.

## STATUS (29/09/2026, após OK do usuário)
APLICADOS e commitados (build OK, `--selftest-lote` ALL TESTS PASSED): #1 (bbc981d), #2 (4dc1ced), #3 (62390d4,
hipótese do popup NÃO provada — confirmar no app), #4 (b4ed968), #5 (a363552), #6a/#6b (45da1e6/791d46f),
#7 (0ec1de9, sem "Catalog ready" falso + barra "Thumbnails N of M"), #9 (3861075 + 2ff9ed1).
NÃO aplicado: #8 Undo (fica pra depois, com mais testes; causa do menu cinza já achada: aoMudarUndo nunca ligado).
Não rodado: builds ASan/TSan (AGENTS.md pede antes de dar por "pronto" código com callAsync).
Itens antigos já ingeridos (fotos/vídeos sem data) continuam com data de hoje até re-ingest/backfill.

## Mapa da lista do usuário (29/09/2026) → itens deste arquivo

Pendentes (da lista): Intake 1 (Undo) → #8 · Intake 6 (data de vídeo) → #1 · Metadata 6 (tecla E) → #3 ·
Metadata 8 (pasta do Folder Map) → #4 · Metadata 12 (ordenar lista) → #5 · Folder Map 1 → #6 · Projeto 3 → #7.
Extra (fora da lista): #2 hang no Reject do Intake.
Tudo o mais na lista está marcado OK pelo usuário — NÃO mexer (Intake 2/3/4/8/9, Metadata 1-4/10/14-16,
Ficha 1-3, Folder Map 3-5, Projeto 1, Preview, Duplicates, Backup, Storage, Navegação, Drive, Ingest).
Numeração com buracos na lista original (Intake 5/7/10, Metadata 5/7/9/11/13, Folder Map 2, Projeto 2) = vazios.

## 1. Item entra no Intake com a data de hoje (vídeo, foto, qualquer tipo sem data no metadado) — DIAGNOSTICADO, NÃO APLICADO

**Sintoma:** após ingerir, a coluna DATE do Intake mostra a data de hoje.

**Causa raiz (verificada no registro.sqlite do projeto aberto):**
- O fix `fb2776f` (ler `creation_time` das tags do ffprobe) FUNCIONA: AVI da
  Cybershot e MP4 de celular têm `data_criacao` real (ex.: 2025-12-07 08:06:17).
- Mas vídeos SEM tag de data no container ficam sem `data_criacao`:
  367 MOV (só 18 com data), 707 MP4 (só 458), ex.: `Untitled.mov` (arquivo em
  01/05/2023, sem tags). Nesses, o ingest só grava `item.ano` pelo fallback de
  data do arquivo (IngestArquivo.cpp:~308-323).
- `IntakeWorkspaceComponent.cpp:1965` faz `if (dataCriacao.isEmpty()) dataCriacao = item.criadoEm`
  — `criado_em` é o momento do ingest = hoje.

**Solução proposta (cirúrgica):** em `Source/Ingest/IngestArquivo.cpp`, no ramo
fallback (após o UPDATE de `ano`), gravar também
`gravarCampoNativo("data_criacao", criacao.formatted("%Y-%m-%d %H:%M:%S"))`
(mesma regra "mais antiga entre criação e modificação"). Ajustar o comentário
que dizia "só ano".
- Efeito: só vale para ingests NOVOS; itens já ingeridos precisam de rescan/re-ingest
  (ou um backfill SQL, se quiser).
- Alternativa mais leve (sem tocar em data_criacao): Intake:1965 cair em `ano`
  antes de `criadoEm` — mostra só o ano, menos preciso.

**Teste após aplicar:** ingerir um .mov sem tags (ex.: `.../2023/Cybershot 1/1 de maio 23/Untitled.mov`);
Intake deve mostrar 2023-05-01, não hoje.

**Confirmação com caso real (foto, não vídeo):** `1756414928425.jpg` — Finder: criado
11/06/2026, modificado 28/08/2025; sem EXIF. No banco: `ano=2025`, SEM `data_criacao`,
`criado_em=2026-09-29` → Intake mostra 2026-09-29. Mesma causa raiz, vale para
qualquer mídia sem data embutida: 2.694 fotos, 598 vídeos, todos os áudios sem
`data_criacao` no projeto aberto.

**Nota:** a regra "mais antiga entre criação e modificação" dá 28/08/2025 neste
arquivo (correto). A correção acima cobre todos os tipos, pois está no ramo
genérico do ingest.

## 2. Hang do app ao rejeitar itens do Intake — DIAGNOSTICADO, NÃO APLICADO

**Sintoma:** app trava (beach ball, ~45% CPU) depois de Reject no Intake.

**Evidência:** `sample` do processo em execução (pid 730): a message thread está 100%
em `IntakeWorkspaceComponent::rejeitarItemDoIntake` → `ProjetoAberto::removerItensDoProjeto`
(ProjetoAberto.cpp:2487, `DELETE FROM item WHERE id = ?`) → `sqlite3_step` →
`sqlite3FkActions` / `fkScanChildren` → varredura completa de tabela (`sqlite3BtreeNext`,
leitura de páginas do disco).

**Causa raiz:** o DELETE de item dispara ON DELETE CASCADE nas tabelas filhas. Três FKs
não têm índice na coluna filha, então o SQLite faz FULL SCAN dela **a cada item deletado**:
- `preservation_event.arquivo_id` (77.695 linhas) ← o pior
- `arquivo.derivada_de_arquivo_id` (16.218 linhas)
- `proveniencia.item_id` (vazia hoje, mas mesma falha)
Com 654 itens em quarentena rejeitados em lote = centenas de scans de 77 mil linhas
na message thread (o loop de DELETE está dentro da transação única, mas cada DELETE
paga o scan).

**Solução proposta (mínima):** criar os 3 índices (idempotente, `CREATE INDEX IF NOT EXISTS`)
onde o schema é criado/atualizado ao abrir o projeto:
`preservation_event(arquivo_id)`, `arquivo(derivada_de_arquivo_id)`, `proveniencia(item_id)`.
Sem mudar lógica nem UI. Opcional (2º passo, fora do escopo mínimo): mover o
removerItensDoProjeto para background (ver regra da message thread no AGENTS.md).

**Teste após aplicar:** abrir o projeto (índices são criados), rejeitar ~100 itens no
Intake — deve concluir em segundos; `EXPLAIN QUERY PLAN DELETE FROM item WHERE id=?`
não deve mais listar SCAN nessas 3 tabelas.

## 3. Tecla E não volta a funcionar depois de editar em lote — DIAGNOSTICADO (parcial), NÃO APLICADO

**Sintoma (confirmado pelo usuário, build de 29/09 13:25 que JÁ inclui e3d8e1e/ea4f37e):**
após editar um campo em vários itens (inclusive escolhendo opção em dropdown), E não marca
como revisado; só volta depois de desselecionar e selecionar de novo na grade.

**Por que E depende disso:** `MosaicoComponent::keyPressed` (MosaicoComponent.cpp:1469) só
recebe a tecla se a grade tiver o foco de teclado. Depois de editar, o foco fica no campo da
ficha (ou volta pra ele) e E é digitado/engolido ali. Reselecionar na grade devolve o foco.

**O que o fix anterior (e3d8e1e) cobre — e o que NÃO cobre** (FichaPanelComponent.cpp, modo lote):
- COBRE: Enter nos campos de texto (4373) e GEO (4742); onChange do dropdown (4433).
- NÃO COBRE (nenhum chama `aoPedirFocoGrade`): Original Source Medium (4648, ainda faz
  `relayoutEExibir()`), notas/tags e o commit de :4212, os builders de :4541/:4608
  (remoção/adição de tag etc.), e sair do campo por Tab/clique (deliberado, ver e3d8e1e).
- O pedido de foco é SÍNCRONO (`mosaico_->grabKeyboardFocus()` direto no callback). Hipótese
  principal pro dropdown continuar falhando: o `onChange` do ComboBox roda enquanto o popup
  ainda está aberto; quando o menu fecha, o macOS/JUCE devolve o foco ao último componente
  focado (o próprio ComboBox) e desfaz o grab. NÃO CONFIRMADO — só dá pra provar rodando
  (ver diagnóstico abaixo).

**Solução proposta:**
1. Adiar o pedido de foco pro fim do ciclo de eventos: em
   `CatalogWorkspaceComponent.cpp:332` trocar por
   `juce::MessageManager::callAsync([safe = SafePointer(mosaico_.get())]{ if (safe) safe->grabKeyboardFocus(); })`
   (padrão de SafePointer do AGENTS.md) — assim roda depois do popup fechar.
2. Chamar `aoPedirFocoGrade` também nos demais commits deliberados do modo lote
   (OSM 4648, tags/notes :4212, :4541, :4608) — nunca no onFocusLost.
3. Diagnóstico junto (1 linha, removível): logar
   `Component::getCurrentlyFocusedComponent()->getName()/typeid` logo após o pedido e 100 ms depois,
   pra confirmar que o ComboBox estava roubando o foco de volta.

**Teste após aplicar:** selecionar 3+ itens NÃO contíguos (Cmd+clique) → editar um campo de
texto + Enter, depois um dropdown, depois OSM/tags → apertar E em cada caso: os itens devem
receber a borda/selo de revisado sem reselecionar.

## 4. Pasta do Folder Map enviada à grade perde a seleção ao usar filtros da coluna esquerda — DIAGNOSTICADO, NÃO APLICADO

**Sintoma:** "Show content in grid" traz a pasta selecionada e editável (fix f59e671 ok),
mas clicar num filtro da coluna esquerda (categoria / MEDIA TYPE) perde a pasta/seleção.
Comportamento desejado: o conjunto da pasta fica ativo até o usuário desligar em ALL ASSETS.

**Causa raiz:** `CatalogWorkspaceComponent::selecionarCategoria()`
(CatalogWorkspaceComponent.cpp:1482) faz `filtroHerdadoIds_.reset();` INCONDICIONALMENTE,
com o comentário "outra categoria: sai da pasta vinda do Folder Map". Ou seja, qualquer
clique na coluna esquerda (incluindo os botões de MEDIA TYPE, que só deveriam AFUNILAR
dentro da pasta) descarta o conjunto herdado. Consequências:
- as contagens (`atualizarContagens`, :1200) e o filtro do grid (:1018-1020) voltam ao catálogo inteiro;
- `mosaico_->definirFiltroItens(...)` NÃO é resetado junto (só `limparTodosOsFiltros`, :1461, faz isso),
  então o mosaico e o workspace ficam com escopos diferentes (o mosaico ainda filtra pela
  pasta em MosaicoComponent.cpp:759, o workspace não) — inconsistência que também
  explica a seleção "sumindo".
Já o único lugar que DEVE desligar o conjunto é ALL ASSETS: `limparTodosOsFiltros()` (:1460-1461),
chamado por `chave == "all"` (:1480) — isso já está certo.

**Solução proposta (mínima):** em `selecionarCategoria`, tirar o `filtroHerdadoIds_.reset()`
incondicional de :1482 (o "all" já reseta via `limparTodosOsFiltros`). Filtros de MEDIA TYPE,
ano, collection e subject passam a combinar (E) com a pasta, que é o que
`aplicarFiltrosAdicionais` já faz (:1018-1020). DECIDIDO pelo usuário (SIM): clicar numa categoria
NÃO-media-type (ex.: "folders", "selected"…, índice < indiceInicioMediaType_) também
deve manter a pasta? Sugestão: sim (só ALL ASSETS desliga), como o usuário pediu.
Verificar também que a seleção (`selecionados_`) não é podada quando o filtro esconde
itens — se for, manter a seleção da pasta.

**Teste após aplicar:** Folder Map → Show content in grid → clicar MEDIA TYPE (ex. Images)
e um ano: grade e contagens continuam restritos à pasta, seleção mantida e E/edição
funcionando; clicar ALL ASSETS volta ao catálogo inteiro.

## 5. Ordenar a LISTA (Grid) por DATE CREATED só ordena "dentro da página" — DIAGNOSTICADO, NÃO APLICADO

**Sintoma:** com ~1000 itens em várias páginas no modo lista, clicar em DATE CREATED alterna
entre o primeiro e o último da PÁGINA, não da lista inteira (1 ↔ 1000).

**O que já está certo (fix 101fc03):** a ordenação por coluna já é global — com coluna ativa
vira um grupo só, ordenado de ponta a ponta, e a paginação fatia DEPOIS de ordenar
(MosaicoComponent.cpp:833 e :886-903). Testado só pela coluna NAME (col 2). A estrutura
de páginas não é a causa.

**Causa raiz (data, não paginação):** a coluna DATE CREATED do Grid compara/mostra
`i.dataCriacao.empty() ? i.criadoEm : i.dataCriacao` (MosaicoComponent.cpp:584 e :2047), mas
**`ProjetoAberto::listarItensDeProjeto` (ProjetoAberto.cpp:232-320, a listagem do catálogo/Grid)
NUNCA preenche `ItemResumo::dataCriacao`** — só a consulta de quarentena/Intake faz isso
(ProjetoAberto.cpp:793-794). Logo no Grid TODO item cai em `criadoEm` = momento do
ingest: itens ingeridos no mesmo lote têm datas quase iguais, a coluna mostra só o ANO
(2026 pra todos) e a ordem "por data" é na prática a ordem de ingestão — parece
ordenar só localmente. (`ano` sai do banco em :76-77, mas não a data completa.)
Agravante: o item 1 (fallback de data sem gravar data_criacao) faz muitos itens nem terem
data no banco.

**Solução proposta:**
1. `listarItensDeProjeto`: incluir no SELECT `dc_created` e `data_criacao` (mesmos
   subselects já usados em :725-726 da consulta do Intake) e setar `r.dataCriacao`
   (dc_created, senão data_criacao) — a coluna passa a ordenar/mostrar a data do metadado.
2. Junto com o fix do item 1 (gravar data_criacao do arquivo no ingest), itens sem data
   embutida passam a ter data real; pra itens já ingeridos precisa re-ingest/backfill.
3. (Opcional) mostrar a data completa (AAAA-MM-DD) na coluna em vez de só o ano, para o
   usuário ver a ordem mudando. Comparação por texto funciona porque os formatos são
   ISO-like (dc_created pode vir "2024-09-20T22:30:33.000000Z" ou "2025-12-07 08:06:17" —
   ambos ordenam corretamente por prefixo AAAA-MM-DD; se quiser robustez, comparar só
   os 10 primeiros chars).

**Teste após aplicar:** lista com >1 página, clicar DATE CREATED: página 1 = itens mais
antigos da lista inteira, última página = mais novos; clicar de novo inverte (1000 primeiro).

## 6. Folder Map: (a) mapa aberto por padrão / último usado; (b) mapa ORIGINAL sem os links — DIAGNOSTICADO, NÃO APLICADO

### 6a. Qual mapa abre
**Comportamento desejado:** 1ª vez que o usuário abre o Folder Map de um projeto novo → ORIGINAL;
depois disso → sempre o ÚLTIMO mapa usado.

**Causa raiz** (ProjetoAberto.cpp:1917-1928, `mapaAtivoPadrao()`):
- ignora de propósito o ORIGINAL: `mapaAtivoSelecionado_ != kMapaOriginal` — então o último
  usado nunca é o ORIGINAL, e sem seleção o padrão é o **primeiro mapa NÃO-original** da
  tabela `folder_map` (`ORDER BY ordem, criado_em LIMIT 1`); só se não existir nenhum devolve
  vazio (`""`, e aí o ORIGINAL nem é o ativo);
- `mapaAtivoSelecionado_` (ProjetoAberto.h:467/862) é só memória do processo: ao reabrir o
  projeto/app o "último usado" se perde.

**Solução proposta:**
1. `mapaAtivoPadrao()`: se houver seleção válida (incluindo `kMapaOriginal`, que sempre
   existe), devolvê-la; senão devolver `kMapaOriginal`. Remover o fallback "primeiro mapa"
   (mantê-lo só se o mapa lembrado tiver sido apagado → ORIGINAL). `apagarFolderMap` já
   volta pra `mapaAtivoPadrao()` (ArvoreBackupComponent.cpp:2254) — passa a cair no ORIGINAL.
2. Persistir o último usado por projeto: `definirMapaAtivo()` grava o id (ex.: arquivo
   `mapa_ativo.txt` na pasta do projeto, viaja com o projeto; sem mudança de schema) e
   o construtor/`mapaAtivoPadrao()` lê. Sem arquivo = primeira vez = ORIGINAL.

### 6b. ORIGINAL abre sem as linhas entre as pastas
**Causa raiz:** `construirArvoreOriginalVirtual()` (ProjetoAberto.cpp:105-145) monta os nós
com `id` mas NUNCA preenche `pastaPaiId` (o `NoBuilder::pastaPaiId` fica vazio; só a
`arvoreAcervo` real o preenche, :2073). O canvas desenha cada linha a partir de
`node.pastaPaiId` (ArvoreBackupComponent.cpp:1194-1195) e o vínculo pai/filho de todo o
resto (arrastar bloco, filhos, aba isolada, :653/:761/:809). Sem `pastaPaiId` todo nó do
ORIGINAL parece raiz → sem links.

**Solução proposta (1 linha):** no loop de construção, ao criar o id do nó
(`if (filho->id.empty()) { filho->id = "original:" + ...;` ) também setar
`filho->pastaPaiId = atual->id;` (para filhos diretos da raiz `atual->id` é vazio = sem pai,
correto). O ORIGINAL continua somente leitura, só ganha a hierarquia.

**Teste após aplicar:** projeto novo → abrir Folder Map: abre em ORIGINAL, com linhas
ligando pai→filho. Trocar pra outro mapa, fechar e reabrir o projeto: abre nesse mapa.
Apagar o mapa ativo: volta pro ORIGINAL.

## 7. Abrir projeto: "CATALOG READY" aparece antes de o trabalho terminar; sem progresso informativo — DIAGNOSTICADO, NÃO APLICADO

**Sintoma:** ao abrir o projeto o app mostra "Catalog ready" enquanto o catálogo e as
miniaturas ainda estão carregando → o usuário não sabe se travou/crashou ou se só demora.
Requisito do usuário: pode demorar, mas TEM que comunicar o tempo todo, com barra de
progresso detalhada/informativa.

**Causa raiz (3 tarefas de progresso desconectadas, uma delas mentirosa):**
1. `MainComponent::mostrarGrid()` (MainComponent.cpp:1731 e :1771) abre a tarefa
   `catalog_view` e a CONCLUI na mesma chamada síncrona com "Catalog ready" — logo após
   só criar o workspace/pedir o recarregar. Nada foi carregado ainda: a "conclusão" é
   falsa por construção.
2. O trabalho real é assíncrono e vive noutra tarefa: `catalog_assets` (MosaicoComponent.cpp:151-192,
   snapshot em background; `iniciarTarefa(..., 100, ...)` mas NUNCA chama
   `atualizarFracao/atualizarProgresso` — fica sem % até concluir com "N items loaded"), e as
   contagens da sidebar (`atualizarContagens`, sem tarefa nenhuma).
3. A geração de miniaturas faltantes (`ProjetoAberto::gerarMiniaturasFaltantes`,
   ProjetoAberto.cpp:1644, disparada em CatalogWorkspaceComponent.cpp:600-611 numa thread
   "MatrizMiniGen" com `sleep(500)`) roda TOTALMENTE SEM progresso — nem tarefa, nem log
   pro usuário — e ainda percorre TODAS as linhas de `arquivo` e faz `existsAsFile()` no
   disco pra cada item sem miniatura (é o "conferir um por um no disco" do item 2 original,
   só que agora em background). Ao terminar chama `recarregar()` (novo snapshot, sem aviso).
   Além disso as miniaturas visíveis são decodificadas sob demanda
   (MosaicoComponent.cpp:1632-1690), também sem indicação.

**Solução proposta (só UI de progresso, sem mudar a lógica de carga):**
- Uma tarefa única "Opening project" (somenteBarra=true, barra inferior persistente) com
  ETAPAS e texto de detalhe sempre atualizado:
  1) "Reading catalog index… N items" (snapshot; `atualizarFracao` por etapa do SELECT/
     montagem quando possível, senão barra indeterminada + contador);
  2) "Building catalog view / counts…";
  3) "Generating thumbnails… 1,240 of 8,000 · <nome do arquivo>" com fração real
     (`atualizarProgresso(id, feitos, detalhe)` dentro do loop de `gerarMiniaturasFaltantes`,
     total = itens sem miniatura, calculado ANTES do loop; postar via callAsync/limitar a
     ~10 atualizações/s — ProgressoGlobal não é thread-safe pra UI, ver regra do AGENTS.md);
  4) "Verifying files on disk…" se/quando houver essa etapa.
- `mostrarGrid()` deixa de chamar `concluirTarefa("catalog_view","Catalog ready")` síncrono;
  "Catalog ready" só é emitido quando TODAS as etapas terminaram (contador de etapas
  pendentes; nunca concluir por geração obsoleta sem fechar a tarefa — ver decisão do
  AGENTS.md sobre `catalog_assets`, que continua valendo).
- Mostrar também um resumo final útil ("Catalog ready · 8,000 items · 312 thumbnails created
  · 12 files offline").
- Opcional: célula da grade sem miniatura ainda mostra placeholder animado/“loading”.

**Restrições a respeitar:** thread-safety (atualizações de progresso a partir da MatrizMiniGen
via `MessageManager::callAsync` + SafePointer), não reintroduzir `runDispatchLoopUntil` em
`ProgressoGlobal::notificarListeners` (AGENTS.md), e a message thread não deve travar em disco.

**Teste após aplicar:** abrir um projeto grande com miniaturas faltando: a barra deve mostrar
etapas com N de M e nome do arquivo continuamente, sem nunca dizer "Catalog ready" antes de
terminar; ao fim mostra o resumo.

## 8. Undo (Cmd+Z) não desfaz Reject / Send to Grid — DIAGNÓSTICO PARCIAL (causa NÃO confirmada), NÃO APLICADO

**Sintoma:** Reject e Send to Grid no Intake, seguidos de Cmd+Z, não voltam ao estado anterior
(mesmo com o fix 3bcc5bb, que registrou undo pra os dois e recarrega o Intake).

**O que o código faz (verificado):**
- Reject: `removerItensDoProjeto` (ProjetoAberto.cpp:2458) guarda linhas em tabelas TEMP e
  registra o undo (:2499); Send to Grid: `registrarUndoEnvioAoGrid` (:828). O `desfazer()`
  (:1154) roda as ações reversas e `MainComponent::executarUndo` (MainComponent.cpp:4239)
  recarrega Intake/Grid.
- **Cmd+Z NÃO é um atalho de menu.** O item "Undo (Cmd+Z)" do menu Edit (MainWindow.cpp:212)
  é só rótulo (sem KeyPress/ApplicationCommandManager). O atalho real é
  `MainComponent::keyPressed` (MainComponent.cpp:4276), que só recebe a tecla se algum
  componente DENTRO do MainComponent tiver o foco de teclado (ou a tecla borbulhar até ele).

**Hipóteses (a confirmar rodando):**
1. **(mais provável) Foco de teclado perdido** — mesma classe do bug da tecla E (item 3):
   Reject vem de um menu de contexto (`mostrarMenuContexto`) e Send to Grid de botão/popup;
   depois deles o foco pode ficar em nenhum componente/na janela, e o Cmd+Z nunca chega ao
   `MainComponent::keyPressed`. Clicar no menu Edit > Undo funciona (chama `executarUndo`
   direto), o que distingue: se o MENU desfaz e a TECLA não, é foco.
2. **Undo do Reject falha em silêncio** — o reinsert usa `INSERT OR IGNORE` por tabela
   (ProjetoAberto.cpp:2506) dentro de try/catch que só dá `return` no erro (:2512), sem log:
   qualquer falha (schema, FK, coluna nova) deixa o item sem voltar e sem aviso. Também
   depende de as tabelas TEMP (`undo__*`) ainda existirem na MESMA conexão.
3. **Pilha de undo engolida** — se `grupoAberto_` ficar aberto (iniciarGrupoUndo sem
   finalizar), `registrarUndo` empilha no grupo em vez da pilha e o Undo some. Os 6 pares
   iniciar/finalizar atuais parecem balanceados, então é a menos provável.

**ATUALIZAÇÃO 2 — CAUSA ENCONTRADA (menu): o Undo do menu fica cinza SEMPRE, em qualquer situação.**
`ProjetoAberto::aoMudarUndo` é chamado em ProjetoAberto.cpp:1132/1151/1163 mas **nunca é atribuído
em lugar nenhum do código** (verificado: nenhuma ocorrência fora de ProjetoAberto.*). E no macOS a
barra de menus é NATIVA (`MenuBarModel::setMacMainMenu`, MainWindow.cpp:83/721): o estado
habilitado/desabilitado dos itens é gravado quando `menuItemsChanged()` é chamado (só na abertura
do projeto, troca de tema/idioma, rename — MainWindow.cpp:285/291/329/500/723). Ao abrir o projeto a
pilha está vazia → "Undo" nasce desabilitado e nada avisa a janela quando a pilha ganha itens.
→ O MENU Undo nunca vai habilitar, mesmo que a pilha tenha ações. Isso NÃO prova que o Cmd+Z
(tecla) também falha por isso: `MainComponent::keyPressed` (:4276) chama `executarUndo()` sem
consultar o menu. O Cmd+Z que o usuário relata como quebrado ainda pode ser foco/pilha (hipóteses
abaixo) — são dois defeitos separados: (A) menu estático, confirmado; (B) undo real de Reject/Send
to Grid, não confirmado.
**Fix proposto pra (A):** em MainWindow (onde `conteudo_->projetoAberto()` é conhecido), ligar
`projetoAberto()->aoMudarUndo = [safe]{ if (safe) safe->menuItemsChanged(); }` sempre que um
projeto abre/troca (e limpar ao fechar); `registrarUndo`/`desfazer`/`finalizarGrupoUndo` já disparam
o callback (chamar só na message thread — ver ProjetoAberto.cpp:1245: o undo em lote é registrado
via callAsync; callback deve usar SafePointer, padrão do AGENTS.md). Alternativa robusta: um
`ApplicationCommandTarget` com Cmd+Z real, cujo estado é consultado na hora.

**(primeira análise, antes de achar a causa do menu) o item Edit > Undo aparece DESABILITADO** mesmo logo depois de
Reject/Send to Grid. O menu habilita por `temProjetoAberto() && podeDesfazer()`
(MainWindow.cpp:211; `podeDesfazer()` = `!pilhaUndo_.empty()`, ProjetoAberto.h:277). Logo a pilha
está VAZIA: a hipótese 1 (foco) cai — o problema é que a ação reversa NUNCA chega à pilha (ou é
apagada antes). Restam:
- **Pilha engolida por grupo aberto:** `registrarUndo` (ProjetoAberto.cpp:1124-1134) empilha no
  `grupoAberto_` se ele existir, e o grupo só vai pra pilha em `finalizarGrupoUndo`. Se algum
  `iniciarGrupoUndo` ficar sem `finalizar` (exceção entre os dois — p.ex. Ficha em lote de tags,
  FichaPanelComponent.cpp:4537-4543 e notes :4596-4611, chamam `adicionarTag/removerTag` SEM try/catch
  entre iniciar e finalizar; idem Folder Map "Disconnect", ArvoreBackupComponent.cpp:662-670), TODO
  registrarUndo seguinte (Reject, Send to Grid, tudo) vai pro grupo fantasma e o Undo fica
  desabilitado pra sempre na sessão. Coerente com o sintoma; A VERIFICAR: precisa de log/debugger
  (não deu pra rodar grep no código nesta hora — Bash indisponível — e ainda não li os pontos que
  possam limpar `pilhaUndo_`).
- **Reject/Send to Grid não registram:** Reject só registra se o DELETE passar
  (`tabelasGuardadas` vazio ou `desfazendo_==true` fazem `return`, :2493); Send to Grid só registra
  itens com `em_quarentena = 1` no momento (:832) — se o item já foi promovido antes de chamar
  `registrarUndoEnvioAoGrid`, `antes` fica vazio e nada é registrado.
- **Pilha limpa por reload/troca de projeto/aba:** checar quem zera `pilhaUndo_`
  (A VERIFICAR).
**Próximo passo de diagnóstico:** log em `registrarUndo` (descrição, `grupoAberto_` ativo?, tamanho
da pilha) e em `finalizarGrupoUndo`; reproduzir: abrir projeto → Reject 1 item → ver se Edit > Undo
habilita. Se habilitar, repetir depois de uma edição em lote de TAGS/NOTES pra provar o grupo preso.

**Diagnóstico a fazer antes de corrigir (barato):** (a) repetir Reject e desfazer pelo
MENU Edit > Undo — separa foco de lógica; (b) rodar `--selftest-lote` (já cobre undo de
Reject/Send to Grid, commit 0af2d29) e ver se passa; (c) log em `executarUndo`/`desfazer`
(descrição, tamanho da pilha) e no catch do undo do Reject.

**Solução proposta (depois de confirmar):**
- Se foco: Cmd+Z tratado num nível que não dependa do foco — ligar `kCmdUndo` a um
  `KeyPress('z', cmd)` de verdade (ApplicationCommandManager/keyMappings no MainWindow) ou
  devolver o foco ao workspace depois do menu de contexto/Send to Grid
  (`MessageManager::callAsync` + SafePointer, como no item 3).
- Se falha silenciosa: logar o erro e mostrar aviso ao usuário em vez de `return` mudo.
- Manter Undo consistente pra todos os pontos do Intake (Reject, Send to Grid, lote).

**Teste após aplicar:** no Intake, Reject de 1 e de vários itens, Cmd+Z → itens voltam com
arquivo/miniatura/campos; Send to Grid + Cmd+Z → itens voltam ao Intake; testar com o foco
logo após o menu de contexto (sem clicar em nada antes de apertar Cmd+Z).

## 9. Travamento ao editar campo em LOTE com muitos itens selecionados (autocomplete → aplicar) — DIAGNOSTICADO, NÃO APLICADO

**Evidência:** `sample` do processo travado (pid 730, ~46% CPU, 1h35 de uptime) — a message thread está
100% em: `AutoCompleteTextEditor::PopupRow::mouseUp` → `selecionarValor` → `FichaLoteConteudo::aplicarCampoAgora`
(FichaPanelComponent.cpp, loop por item) → `aoAplicarSucessoItem` → `CatalogWorkspaceComponent` (:321-326)
→ `MosaicoComponent::atualizarItemEmMemoria(itemId)` (MosaicoComponent.cpp:202) →
**`aplicarFiltrosEOrdenacao()` (:~248, refiltro COMPLETO + `std::sort` de todos os itens)**, cuja
comparação da coluna ativa (`compararPorColunaDaLista`, texto natural, :573-590) domina o perfil.
O WAL do registro continua crescendo (edição em andamento): o app não crashou, está processando
item por item na message thread.

**Causa raiz:** `aplicarCampoAgora` chama `aoAplicarSucessoItem(id)` UMA vez POR ITEM do lote
(FichaPanelComponent.cpp:~5109), e cada chamada faz `atualizarItemEmMemoria` = ~6 SELECTs + refiltro +
reordenação de TODA a lista → custo O(N itens editados × N itens do catálogo × log). Com ordenação por
coluna ativa (item 5) e milhares de itens, vira minutos. O mesmo problema já foi resolvido pro GEO em lote
(comentário em FichaPanelComponent.cpp:5212-5219: "em 1200 itens isso sozinho levava ~40 s pra nada"),
mas NÃO pros campos comuns (Dublin Core/autocomplete), que ainda passam por aqui.

**Solução proposta:**
1. Em `aplicarCampoAgora`, NÃO chamar `aoAplicarSucessoItem` por item; acumular os ids e, depois do COMMIT,
   chamar UMA vez um callback de lote (ex.: `aoAplicarSucessoLote(ids)`) que atualiza os N itens em memória
   (um SELECT em lote, `WHERE id IN (...)` ou a versão em lote de `atualizarItemEmMemoria`) e roda
   `aplicarFiltrosEOrdenacao()` **uma única vez**.
2. (Complementar) o `atualizarItemEmMemoria` individual pode pular o refiltro completo quando o campo editado
   não afeta filtro/ordenação da coluna ativa.
3. Mostrar progresso (ProgressoGlobal) durante a aplicação em lote grande — item 7 pede comunicação
   constante; hoje a janela congela sem aviso.
Respeitar: 1 transação (já existe), thread-safety, não alterar o comportamento de "pasta não some ao editar"
(Metadata 10, OK).

**Teste após aplicar:** selecionar ~2.000 itens, escolher uma sugestão do autocomplete de SUBJECT e aplicar:
deve concluir em segundos (1 refiltro), com todos os itens atualizados, ordem/filtro mantidos e a seleção intacta.
