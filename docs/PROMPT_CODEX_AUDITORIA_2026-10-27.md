# Prompt: correções da auditoria de 2026-10-27 (BKR Matriz)

> Copie tudo abaixo da linha e cole no Codex, aberto na pasta do repositório
> `/Volumes/BUNKER 4TB/Apps/BKR Matriz`.

---

## 0. Quem você é e o que vai fazer

Você vai corrigir, **em fases e nesta ordem**, defeitos de proteção de dados do BKR Matriz
(app desktop C++/JUCE de catalogação e backup de acervo: SOURCE → MAIN → CLONE).
Os defeitos foram apontados por uma auditoria externa e **já foram conferidos no código atual**.
Seu trabalho é corrigi-los com mudanças **mínimas e cirúrgicas**, sem causar nenhuma regressão.

O usuário fala português. Fale com ele em português, simples e direto.

## 1. Leia antes de qualquer edição (obrigatório)

1. `AGENTS.md`: regras permanentes do projeto. Valem acima deste prompt se houver conflito.
2. `HANDOFF.md`: a seção do topo, **"Auditoria externa de 2026-10-27"**, tem o arquivo e a
   linha de cada defeito. Leia também a seção "Baseline dos self-tests".
3. `docs/auditoria_2026-10-27_texto.txt`: texto completo da auditoria. Os achados se chamam F01…F17.
   As linhas citadas pela auditoria são de outra cópia do código; **use as do HANDOFF**.

## 2. Regras que você NÃO pode quebrar

- **Mudança mínima.** Nada de refatoração ampla, renomear funções ou arquivos, "melhorar" código
  vizinho ou mudar layout. Mexa só no que o defeito exige.
- **Nunca remova funcionalidade, botão, card, seta ou controle.** Na aba Backup não pode haver
  rolagem nem seta removida (ver AGENTS.md).
- **Todo texto novo visível ao usuário** precisa de chave em `Source/Ui/Strings.h` (inglês) **e** em
  `Source/Ui/StringsPt.h` (português), usada com `matriz::i18n::t("...")`. (O `Source/I18n/Strings.h`
  é outra coisa; não use.) Nada de inglês fixo novo no código, mesmo onde o código antigo usa
  `isPt ? "..." : "..."`. Modelo de chave com diagnóstico e solução: `export.pv_alterado`,
  `export.pv_alterado_diag`, `export.pv_alterado_solucao`.
- **Nada de I/O de disco/banco pesado na message thread.** Trabalho de cópia ou hash vai para uma
  thread (`juce::Thread::launch` ou o pool já usado no componente) e volta à UI com
  `juce::MessageManager::callAsync` capturando `juce::Component::SafePointer<...>`, **nunca `this`
  bruto**. Modelo: `Source/Ui/SyncDestinationDialog.cpp`, perto da linha 560.
- **EventBus não é thread-safe**: só dispare evento de dentro de `callAsync`.
- **Windows**: nada de API só de macOS fora de `*_mac.mm` / `#if JUCE_MAC`. Caminho gravado no banco
  passa por `CaminhosBanco`, nome gerado pelo app passa por `NomesSeguros`.
- **Nunca apague dado do usuário** para "consertar" estado. Na dúvida, mova para a lixeira do projeto
  ou deixe como está e registre no log.
- **Não altere dados de projetos existentes automaticamente** (nada de migração que mude `estado`
  de item, por exemplo). Se for preciso, descreva no HANDOFF e deixe a decisão para o usuário.
- **Um commit por defeito.** Formato das mensagens recentes (veja `git log -5`):
  `Fix(Área): sintoma -> Arquivo.cpp:função -> causa - correção. Self-test: ...`.
  Faça `git add` **só dos arquivos que você mudou**. A raiz tem muitos arquivos não rastreados
  (`Assets/Icons/...`, `cofre/`, `Reference/`, `*.txt`): **nunca os adicione**. Nada de `git add -A`/`git add .`.
- **Não edite código com um build rodando.** Se interromper um build, mate também os `clang`/`clang++`
  filhos e apague os `.o` gravados depois do início do build; senão o binário sai com código velho.
- **Não faça merge na `main`, não faça push e não abra PR** sem o usuário pedir.
- Se uma correção exigir algo que este prompt não previu (UI nova grande, mudança de schema que
  altera dado, comportamento que o usuário vai notar), **pare e pergunte** antes.

## 3. Preparação (antes da Fase 1)

```bash
cd "/Volumes/BUNKER 4TB/Apps/BKR Matriz"
git status --short | grep -v '^??'      # só pode aparecer HANDOFF.md; qualquer outro arquivo: pare e pergunte
git switch -c fix/auditoria-2026-10-27
# Se HANDOFF.md estiver modificado ou os dois arquivos de docs/ ainda não estiverem rastreados,
# é o plano desta tarefa: commite-os primeiro, sozinhos.
git add HANDOFF.md docs/PROMPT_CODEX_AUDITORIA_2026-10-27.md docs/auditoria_2026-10-27_texto.txt
git commit -m "Docs(HANDOFF): plano de correção da auditoria de 2026-10-27 + prompt de execução"
```

Build Debug (Unix Makefiles; leva vários minutos nesta máquina, um i7 dual-core):

```bash
cmake --build build --target matriz -j4
```

**Baseline dos testes ANTES de mexer em qualquer coisa.** Grave a saída em arquivo, sem pipe direto,
e rode **um teste por vez**, sem nenhum build em paralelo:

```bash
APP="build/matriz_artefacts/Debug/BKR Matriz.app/Contents/MacOS/BKR Matriz"
mkdir -p /tmp/matriz_audit
"$APP" --selftest-lote          > /tmp/matriz_audit/lote_antes.txt 2>&1
"$APP" --selftest-verifysource  > /tmp/matriz_audit/verifysource_antes.txt 2>&1
"$APP" --selftest-rebuild       > /tmp/matriz_audit/rebuild_antes.txt 2>&1
"$APP" --selftest-manifesto     > /tmp/matriz_audit/manifesto_antes.txt 2>&1
grep "  FAIL " /tmp/matriz_audit/lote_antes.txt > /tmp/matriz_audit/lote_fails_antes.txt
```

- O `--selftest-lote` tem hoje **11 falhas pré-existentes** (baseline). Elas **não** são regressão. O que
  vale é a lista em `lote_fails_antes.txt`.
- "H Undo de outra thread" às vezes oscila: se só ela mudar, rode de novo antes de concluir algo.
- **Não** rode `--selftest-ingerir-arquivos` nem `--selftest-uitest`, a não ser que mexa em ingest ou UI
  coberta por eles. O primeiro só pode rodar sozinho (timeout de 600 s); o segundo trava no teardown
  final (mate o processo depois da última linha "ETAPA 3"; 42 FAIL pré-existentes).

**Critério de "sem regressão" depois de cada fase:** o build compila sem erro novo, toda linha `  FAIL `
de antes continua igual (nenhuma nova) e os checks que você criou aparecem como `  OK `.

## 4. Fases (siga a ordem; termine e teste uma antes de começar a outra)

Em cada defeito: (a) leia a função inteira e quem a chama (`grep -rn`); (b) escreva o teste que
falha; (c) corrija; (d) rode o teste; (e) faça o commit. Antes de começar cada fase, diga ao usuário
em 3–5 linhas o que vai mudar.

---

### FASE 1: impedir perda direta de dados (F02, F03)

#### F02: o sync apaga a lixeira antiga sem migrar
- **Onde:** `Source/Sync/SyncEngine.cpp`, `criarPastaLixeira` (≈ linhas 143–160). Se `<raiz>/_lixeira`
  existe, chama `legacyLixeira.deleteRecursively()`.
- **Corrigir:** em vez de apagar, **mover** o conteúdo da `_lixeira` legada para
  `<raiz>/Project/_lixeira/<timestamp>_legado/` (mesmo volume, então `moveFileTo` é um rename),
  preservando a estrutura de subpastas. Só apague a pasta legada se ela ficar **vazia** depois de
  mover tudo. Se algum item não puder ser movido, **deixe a pasta legada inteira como está** (nada
  apagado) e siga. A função tem que continuar devolvendo a pasta `_sync` nova como hoje.
- **Não fazer:** não mudar `moverParaLixeira`, nem o formato do timestamp, nem quem chama.
- **Teste novo** (em `Source/Ui/LoteSelfTest.cpp`, mesmo padrão `checar(ok, "descrição")`): crie uma raiz
  temporária com `_lixeira/sub/velho.txt`, chame o fluxo que usa `criarPastaLixeira` (se ela for
  `static`/anônima, teste pelo caminho público que a chama, ou exponha com a mudança mínima). Aceite:
  `velho.txt` existe em `Project/_lixeira/*_legado/sub/` com o mesmo conteúdo e nada se perdeu.

#### F03: a recuperação apaga o destino antes de copiar e não confere nada
- **Onde:** `Source/Ui/BackupRecoveryDialog.cpp`, `RecoveryBrowserComponent::executarCopiaPara`
  (≈ linhas 115–170): `arqDestino.deleteFile()` e depois `arqOrigem.copyFileTo(arqDestino)`, na message thread.
- **Corrigir:**
  1. **Reutilize a cópia verificada que já existe**: `copiarVerificado` em
     `Source/Consolidacao/Consolidacao.cpp` (≈ linha 1326, hoje num namespace anônimo). Ela copia para
     `<nome>.mtz-tmp` calculando SHA-256, relê, compara e só então renomeia por cima do destino.
     Mude só a visibilidade: tire `CopiaVerificada` e `copiarVerificado` do namespace anônimo e declare
     as duas em `Consolidacao.h`. **Não mude o corpo da função** (o MAIN depende dela).
  2. **Não apague o destino antes.** A confirmação de "substituir?" continua igual; o arquivo antigo
     só é trocado quando a cópia nova já conferiu (o rename do `copiarVerificado` faz isso).
  3. **Bloqueie origem == destino:** se `arqDestino == arqOrigem`, ou se a pasta escolhida está dentro
     da pasta da versão de backup (`versao_.destinoPath`), mostre um aviso (chave i18n nova) e não copie.
  4. **Rode a cópia fora da message thread** (`juce::Thread::launch`), com `SafePointer` e `callAsync`
     para mostrar o resultado. As mensagens de sucesso e erro continuam as mesmas; o erro passa a
     incluir o motivo devolvido em `CopiaVerificada::erro`. Para arquivo grande, use
     `ProgressoGlobal::iniciarTarefa/atualizarFracao/concluirTarefa`, como os outros jobs (opcional se
     complicar; se não fizer, anote no HANDOFF).
  5. O `ProjectLog` "File Recovered from Backup" continua sendo gravado, na message thread, só em sucesso.
- **Teste novo:** chame `copiarVerificado` com (a) um destino já existente e uma origem válida → destino
  com o conteúdo novo e nenhum `.mtz-tmp` sobrando; (b) uma origem que não abre → destino antigo
  **intacto, byte a byte**, e `ok == false`.
- Rode `--selftest-rebuild` e `--selftest-manifesto` também: eles passam pelo `copiarVerificado`.

**Fim da Fase 1:** build, testes da seção 3, dois commits (um por defeito), HANDOFF atualizado.

---

### FASE 2: arquivo diferente nunca pode sair do backup por "parecer duplicata" (F01)

- **Onde:**
  - `Source/Ingest/IngestArquivo.cpp`, `buscarAssetPorMetadados` (≈ linhas 506–758): força casamento
    (`coincidences = 3`) com extensão + tamanho iguais para mp3/mp4/mov/m4a/flac/mkv/ogg (≈ 600–606),
    imagens (≈ 680–682) e documentos (≈ 733). **Sem comparar SHA-256.**
  - `Source/Ui/IngestWizardComponent.cpp` ≈ linhas 640–690: quando o casamento foi **só por metadados**
    (`duplicadoPorChecksum == false`), grava `estado = 'duplicata'` com a nota "Possible duplicate of …
    Flagged as duplicate." (linha ≈ 685).
  - `Source/Consolidacao/Consolidacao.cpp:555` exclui do MAIN todo item `estado = 'duplicata'`.
- **Corrigir (no wizard, não na heurística):**
  - Casamento **por SHA-256** (`duplicadoPorChecksum == true`): fica exatamente como está (conteúdo
    idêntico, já no acervo).
  - Casamento **só por metadados**: o item entra com o **mesmo estado do caminho normal** (`'novo'`,
    veja ≈ linha 721) e vai para o MAIN. Ele continua marcado como *possível* duplicata para revisão
    na aba Duplicates. Troque a nota por algo como "Possible duplicate of X (same name/format/size,
    content not identical). Review in Duplicates." e mantenha o prefixo `Possible duplicate of`.
  - Confira para onde vai `resultado.ehDuplicata`/`res.ehDuplicata` antes (badge, checkbox desmarcado,
    contagem do resumo). Se algo faz o item **não ser importado** por padrão quando o casamento é só por
    metadados, isso também precisa mudar para "importa, mas sinaliza". O visual de aviso pode ficar.
  - **Não mude `buscarAssetPorMetadados`.** A aba Duplicates (`DuplicatesWorkspaceComponent.cpp:2117`)
    usa essa função para *sugerir* pares que o usuário revisa, e isso está correto.
  - **Não mexa em itens já gravados** como `duplicata` em projetos existentes. A consulta para o
    usuário ver esses itens já está no HANDOFF.
- **Teste novo:** num projeto temporário, ingira pelo mesmo caminho do wizard (ou pela função que ele
  chama) dois arquivos **diferentes** com a mesma extensão `.mp3` e o mesmo tamanho exato (gere bytes
  diferentes de mesmo comprimento). Aceite: o segundo **não** fica com `estado = 'duplicata'` e entra
  em `planejarConsolidacao` (o plano inclui os dois). Controle: dois arquivos **idênticos** continuam
  com o segundo virando `duplicata`.
- Se o wizard for difícil de testar headless, teste a decisão extraindo a escolha do estado para uma
  função pequena e pura no mesmo arquivo (mudança mínima) e teste essa função.

**Fim da Fase 2:** build, testes, um commit, HANDOFF.

---

### FASE 3: "verificado" tem que significar hash de verdade (F06, F05, F04)

#### F06: Verify Source diz "já está no MAIN" consultando só o catálogo
- **Onde:** `Source/Vault/VerifySource.cpp:40-41` (`carregarHashes`) e `:75` (`verificar`);
  consumidores em `Source/Ui/VerifySourceDialog.cpp` (≈ 207, 264–269, 340, 449) e
  `Source/Ui/MainComponent.cpp:3740`.
- **Armadilha:** se você só tirar do `noMain` os hashes sem cópia no MAIN, eles vão aparecer como
  **FALTANDO**, e o diálogo oferece **ingerir de novo**. Isso cria duplicatas. **Não faça isso.**
- **Corrigir:**
  - `HashesConhecidos` ganha um terceiro conjunto, `soCatalogo` (hash no catálogo **sem** linha em
    `consolidacao_registro` para aquele `arquivo_id` no MAIN). `noMain` passa a ser só os hashes de
    `arquivo` que **têm** `consolidacao_registro`. Use o SHA de `arquivo.checksum_sha256` (o de
    `consolidacao_registro` é dos bytes finais e pode diferir por embed/marca d'água). Antes, confira se
    exports/pacotes também gravam em `consolidacao_registro`: hoje gravam `Consolidacao.cpp`,
    `MainEdit.cpp`, `Project.cpp` e `BackupSyncDialog.cpp` (`grep -rn "INSERT.*consolidacao_registro" Source`).
    Leia cada um. Se algum grava linha que não é do MAIN, filtre por `destino_path` do MAIN do mesmo
    jeito que o planner (`planejarConsolidacao`) faz.
  - `ResultadoVerificacaoFonte` ganha `totalSoCatalogo`: arquivos do disco que estão catalogados mas
    ainda **sem cópia no MAIN**. Eles **não** entram em `faltantes`, nem na oferta de ingerir.
  - O diálogo mostra essa contagem numa linha nova (chave i18n), algo como "Catalogued, not yet in the
    MAIN: N (run Make Backup)", e o relatório texto (≈ linha 449) também. Nada é removido da tela.
  - Continua só leitura e o carregamento continua na message thread, como hoje (é uma query).
- **Teste:** em `Source/Ui/VerifySourceSelfTest.cpp`, um arquivo catalogado sem `consolidacao_registro`
  conta em `totalSoCatalogo`, não em `totalNoMain` nem em `faltantes`; com a linha de consolidação, conta
  em `totalNoMain`.

#### F05: o CHECK do pacote olha só o tamanho
- **Onde:** `Source/Consolidacao/PacoteCollection.cpp`, `verificarPacote` (≈ 690–730, decisão em 711–712);
  `RegistroArquivo::sha256` (do JSON) existe e não é usado aí. UI em
  `Source/Ui/BackupWorkspaceComponent.cpp` (`verificarPacoteExistente` ≈ 5397, tabela de rótulos ≈ 5485).
- **Corrigir:**
  - Novo parâmetro `bool conferirHash = false` em `verificarPacote`. Com `true`, para cada arquivo que
    passou na checagem de tamanho, calcule `matriz::ingest::sha256DoArquivo(f, continuar, &cancelado)`
    (cancelável pelo mesmo `aoProgredir`) e compare com `e->sha256` (só quando `e->sha256` tem 64
    caracteres). Diferente → novo `TipoProblema::HashDiferente`, que é `completavel()` (o COMPLETE
    recopia do mesmo jeito que recopia `ArquivoAlterado`; confira `completarPacote` e trate igual).
    Crie as chaves `export.pv_hash`, `export.pv_hash_diag` e `export.pv_hash_solucao` (EN + PT) e
    registre-as na tabela de rótulos (≈ linha 5485), ao lado de `export.pv_alterado`.
  - `ResultadoVerificacao` ganha `bool hashConferido` para o relatório dizer **qual** checagem foi feita.
    O relatório da verificação rápida passa a dizer "checked by size (quick)", e o da completa,
    "checked by SHA-256 (full)" (chaves i18n).
  - UI mínima: depois de escolher a pasta do pacote em VERIFICAR PACOTE EXISTENTE..., pergunte com um
    `AlertWindow` de dois botões, "Quick (size)" ou "Full (SHA-256, reads every file)", e passe o
    resultado. Nenhum botão existente muda de lugar.
  - A verificação que roda **depois** do COMPLETE (≈ linha 5579) continua rápida (não releia 268 GB).
- **Teste:** pacote temporário com um arquivo cujo conteúdo foi trocado **sem mudar o tamanho**
  (troque 1 byte). Rápida → `ok`. Completa → `HashDiferente`. COMPLETE → recopiado, e a nova verificação
  completa dá 0 problemas.

#### F04: no sync rápido de clone, "sha256" é o tamanho do arquivo
- **Onde:** `Source/Sync/SyncEngine.cpp`: `escanearEComparar` (≈ 204), modo rápido (≈ 307–311:
  `a.sha256 = String(a.tamanhoBytes)`), pareamento de MOVIDO (≈ 403–420). O clone do MAIN usa sempre o
  modo rápido (≈ 795, 946). `aplicarSync` (≈ 536, 550) já ignora valores com menos de 64 caracteres:
  **mantenha assim.**
- **Corrigir (só o perigoso, sem mudar o custo do sync normal):**
  - No pareamento de MOVIDO, quando o plano estiver em modo rápido, um par novo×removido de **mesmo
    tamanho** só vira MOVIDO depois de calcular o **SHA-256 real** dos dois arquivos (só desses
    candidatos, sob demanda, cancelável) e eles baterem. Se não baterem, o novo fica NOVO e o removido
    fica REMOVIDO (vai para a lixeira, não é apagado). Você vai precisar do `juce::File` físico dos dois
    lados no momento do pareamento (o `ArquivoScan` tem `arquivoFisico`). Leve o mínimo necessário.
  - Grave o SHA real calculado em `sha256Ref`/`sha256Alvo` desses itens, para o `aplicarSync` conferir a cópia.
  - **Não** passe o clone do MAIN para modo completo por padrão (releria o acervo inteiro em todo sync).
  - Se já existir na UI uma ação manual de "comparar/verificar clone", ligue nela o modo completo
    (`escanearEComparar(..., true)`). **Se não existir, não crie botão**: anote no HANDOFF como decisão
    pendente do usuário ("verificação integral do clone por SHA-256").
- **Teste:** referência com `a.bin` (4096 bytes A) e alvo com `b.bin` (4096 bytes B, diferentes) em
  caminhos diferentes. Modo rápido → **não** pareia como MOVIDO (1 novo + 1 removido). Controle: se os
  bytes forem iguais, pareia como MOVIDO.

**Fim da Fase 3:** build, testes (inclusive `--selftest-verifysource`), três commits, HANDOFF.

---

### FASE 4: o painel não pode dizer "OK" sem prova (F07, F08)

Aqui a regra é **não esconder nada**: os cards continuam no lugar. Só o cálculo fica honesto, e onde
falta evidência aparece "not proven" / "não comprovado".

#### F07: selo 3-2-1
- **Onde:** `Source/Preservation/Preservation.cpp`, `avaliarRegra321` (≈ 1316–1366). Hoje soma
  `vault` (são **origens**, não cópias) + 1, e `destino` configurado.
- **Corrigir:** contar só cópias que existem:
  - MAIN = 1 se há linhas em `consolidacao_registro` (senão 0);
  - \+ cada clone do MAIN em `backup_destino` com `papel <> 'ORIGINAL'`, `desatualizado_rebuild = 0` e
    `ultima_revisao_conhecida > 0` (já sincronizado);
  - \+ 1 pela SOURCE (o original), se esse for o entendimento do resto do app. **Confira e documente no
    comentário**; se houver dúvida, não some.
  - Tipos de mídia e off-site: o banco não tem localização física declarada. Então `temOffsite = false`
    sempre, e o status mostra "off-site: not proven". `vault` e `destino` **não** contam mais.
  - O `status` só é "OK" quando os três critérios forem comprovados. Com a fórmula nova, na prática, não
    vai ser, e isso é o comportamento honesto esperado.
- **Teste** (no lote): projeto com 2 vaults cadastrados e zero cópias → status **não** é "OK" (é
  exatamente o T01 da auditoria).

#### F08: sucesso antigo continua valendo e o compliance aprova com 1 item
- **Onde:** view `asset_preservation_status` (`schema/registro.sql` ≈ 925–990, coluna `backup_status`) e
  `Source/Ui/PreservationWorkspaceComponent.cpp` (contador ≈ 91–97, `compliance_score` ≈ 235–249).
- **Corrigir:**
  - `backup_status` = OK só se o evento `BACKUP_VERIFIED` **mais recente** do item
    (`ORDER BY event_date_time DESC, criado_em DESC LIMIT 1`) for `SUCCESS`. O resto da lógica fica igual.
  - **Armadilha:** a view é `CREATE VIEW IF NOT EXISTS`, então projetos existentes **não** recebem a view
    nova. Siga o padrão de `Source/Model/Project.cpp` ≈ 1051 (`DROP VIEW IF EXISTS colecao_revisao` +
    `CREATE VIEW ...` em `aplicarSchemas`). A view é usada por `AnalyticsEngine.cpp:198` e
    `Preservation.cpp:309`: mantenha **os mesmos nomes de coluna**.
  - Contador `backupVerificado_`: mesma regra (último evento por item).
  - `compliance_score`: os critérios com `> 0` (`fixityVerificada_`, `formatoIdentificado_`,
    `backupVerificado_`) passam a exigir `== totalAssets_`. Não remova o card.
- **Teste:** um item com SUCCESS e depois FAILURE (data posterior) → `backup_status` ≠ OK (T02 da
  auditoria). Item só com SUCCESS → OK.

**Fim da Fase 4:** build, testes, dois commits, HANDOFF.

---

### FASE 5: só plano, sem código

**Não implemente nada desta fase.** Escreva no HANDOFF, numa subseção da seção da auditoria, um plano
curto para cada item (arquivo, risco, proposta mínima, como testar, custo), e **pare para o usuário
decidir**:

- F09–F11: sync e promoção de clone sem "tudo ou nada" (journal/staging, validar antes de promover,
  comparar que não altera nada: `escanearEComparar` chama `sanitizarEstruturaDestino` e remove o marcador
  de sync incompleto).
- F13: sidecar da INBOX apagado sem comparar (`Consolidacao.cpp` ≈ 1760–1780). É pequeno: marque como
  "candidato a próximo fix".
- F15: `Database::Trava` segue sem lock depois de 60 s; WAL `synchronous=NORMAL`; `destination.json`
  apagado antes do rename (`Project.cpp` ≈ 33–58).
- F12, F14, F16, F17: com a observação do HANDOFF de que F14/F16/F17 têm prioridade baixa para este app.

## 5. Depois de cada fase

1. Build Debug sem erro novo.
2. `--selftest-lote` (e os outros afetados) gravando em arquivo; compare as linhas `  FAIL ` com o
   baseline: **nenhuma nova**. Se aparecer alguma, **pare, não faça commit, e explique ao usuário**.
3. Commit(s) da fase, um por defeito, só com os arquivos da fase.
4. Atualize a seção "Auditoria externa de 2026-10-27" do `HANDOFF.md`: o que foi corrigido (hash do
   commit), o que foi verificado, o que **não** foi verificado (clique no app, ASan/TSan, Windows) e as
   decisões pendentes. Faça um commit separado `Docs(HANDOFF): ...`, ou junte ao último commit da fase
   se o padrão do repo for esse (veja `git log`).
5. Diga ao usuário, em português, em poucas linhas: o que mudou, o que testar e como.

## 6. Entrega final (depois da Fase 4 e do plano da Fase 5)

Gere o Release e entregue o comando para abrir o app (feche o app aberto antes):

```bash
cmake --build build-release --target matriz -j4
pkill -x "BKR Matriz"; open "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-release/matriz_artefacts/Release/BKR Matriz.app"
```

Antes de entregar, confirme que o binário tem a mudança (por exemplo, com `strings` no binário, procure
uma chave i18n nova). Se o usuário relatar que "nada mudou", confira primeiro com
`ps aux | grep "BKR Matriz"` **qual** `.app` está aberto: há várias worktrees e builds nesta máquina.

Entregue também um roteiro curto de teste manual, um passo por defeito, por exemplo:
- F03: Backup → Versions → recuperar um arquivo para uma pasta que já tem um com o mesmo nome →
  substituir → o arquivo novo está lá e não sobrou `.mtz-tmp`; tentar recuperar para a própria pasta
  da versão → aviso, nada copiado.
- F01: ingerir pelo wizard dois MP3 diferentes de mesmo tamanho → os dois aparecem e entram no Make Backup.
- F06: Tools → Verify Source Disks num disco já catalogado e sem backup → aparece "catalogued, not yet
  in the MAIN", não "already in the MAIN".
- F05: VERIFICAR PACOTE EXISTENTE → Full → arquivo alterado aparece como hash diferente → COMPLETE o corrige.
- F07/F08: aba Preservation → o 3-2-1 não mostra OK só com vaults cadastrados.
