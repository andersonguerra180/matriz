# Auditoria BKR Matriz — 2026-11-02

- **Branch auditada:** `fix/auditoria-2026-10-27` @ `e2f38c0` (109 commits; ~180 mil linhas em `Source/`).
- **Modo:** somente leitura/diagnóstico. Nenhum código de produção foi alterado. Este arquivo é a única mudança, no branch `audit/relatorio-2026-11-02`.
- **Legenda de verificação:** 🟢 **EXECUTADO** (rodei e observei) · 🟡 **LIDO** (análise estática, não executado) · ⚪ **NÃO VERIFICADO** (hipótese / de memória).

## 0. Limites desta auditoria

1. **Não foi possível compilar nem rodar o app e os self-tests neste ambiente.** O `cmake` configurou e baixou JUCE/dependências, mas falhou no `juceaide` (faltam headers X11/Xrandr). Mesmo com os headers, o projeto só tem `DiskIdentity` para macOS e Windows (`DiskIdentity_mac.mm` / `DiskIdentity_win.cpp`), então os alvos não linkam em Linux. Por isso **ASan/TSan e os self-tests (`matriz_selftest`, `--selftest-lote`, interop) NÃO foram rodados.** O pedido de rodar sanitizers fica pendente para uma máquina macOS/Windows.
2. O que consegui **executar de verdade** foi o **schema real** (`schema/registro.sql`, 55 tabelas, 102 índices, 17 gatilhos) no SQLite 3.53.1 (Python), com fixtures sintéticas de 100 mil itens. O app usa SQLite 3.45.3 + FTS5, então os números absolutos podem diferir um pouco, mas a semântica é a mesma. Fixtures e scripts ficaram no scratchpad, fora do repo.
3. Não li as ~180 mil linhas uma a uma. A leitura foi dirigida: migrações de abertura, FTS/busca, consolidação/export, servidor do celular/WhatsApp, undo, CI/CMake, logs. Há áreas **não auditadas em profundidade** (listadas na §11).

## 1. Resumo executivo

O código é bem comentado e tem boas defesas (cópia verificada com hash e arquivo temporário no MAIN, saneamento de nomes, token em tempo constante, SHA-256 do SQLite fixado). Os problemas mais sérios são poucos e concentrados:

| # | Achado | Sev. |
|---|---|---|
| 1 | **A reconstrução da FTS (`migrarBuscaFtsIndexada`) falha sempre que existe um `item_campo.valor` NULL** (`busca_fts_map.conteudo` é `NOT NULL`). O erro é engolido, a transação sofre rollback e **tudo se repete a cada abertura do projeto**. Reproduzi a falha. É a causa provável nº 1 da abertura de 8–16 min (a confirmar pelo `perf.log`). | **Alta** |
| 2 | O erro de link do `matriz_ingest_selftest` é real, mas **atinge 4 alvos**: `MergeFichas.cpp` falta em `matriz_selftest`, `matriz_ingest_selftest`, `matriz_interop_selftest` e `matriz_analytics_selftest`. O CI que roda 3 deles quebra. | **Alta** |
| 3 | **O Export (Collection Package/Bounce/EXPORT) deixa arquivo truncado com o nome final** se a cópia falhar ou for cancelada. A rodada seguinte o trata como "já exportado" e pula. O export também não verifica hash. | **Alta** |
| 4 | **Chaves de pareamento WhatsApp hardcoded** (`ServidorCelular.cpp`) aceitas como válidas, com upload de tamanho ilimitado. Qualquer dispositivo da LAN que conheça a chave (ela está no repositório) grava arquivos no disco. | **Alta** |
| 5 | Escritas diretas em `busca_fts` (`ProjetoAberto.cpp:2602, 2696–2706, 2749–2754, 2789`) ignoram o `busca_fts_map` (regra do AGENTS.md) e fazem **varredura completa da FTS** por chamada (medido: 0,3–0,5 s por chamada com 1,3 M linhas; escala linear). | **Alta** |
| 6 | A busca faz FTS **e depois 6 consultas `LIKE '%x%'`** por token, o que anula o ganho do índice FTS. Funciona em 100 mil itens, não escala para milhões. | Média |
| 7 | O Undo **reexecuta SQL guardado dentro do `registro.sqlite`** (tabela, colunas e `WHERE` concatenados). Um projeto adulterado/recebido de terceiros executa DML arbitrário ao clicar em Undo. | Média |
| 8 | `CREATE VIEW asset_preservation_status` sem `IF NOT EXISTS`/`DROP` em `Project.cpp:1095` é um no-op em projetos existentes (o comentário diz que atualiza). | Média |
| 9 | 502 blocos `catch (...) {}` vazios; vários mascaram falhas de migração e de backup. | Média |

**Maior risco de dados:** achado 3 (export truncado reaproveitado).
**Maior risco de segurança:** achado 4 (porta HTTP aberta na LAN com segredo público).
**Maior risco de produto:** achado 1 + 5 (abertura lenta e busca/edição de tag que não escalam).

---

## 2. Tabela de achados

Esforço: **P** ≤ 1 h · **M** ≤ 1 dia · **G** vários dias.

| ID | Sev. | Área | Arquivo:linha | Evidência | Impacto | Correção sugerida | Esf. | Verif. |
|---|---|---|---|---|---|---|---|---|
| **B-01** | Alta | Build/Link | `CMakeLists.txt:165-` (`matriz_selftest`), `:236-282` (`matriz_ingest_selftest`), `:342-347` (`matriz_interop_selftest`), `:821-836` (`matriz_analytics_selftest`); `Source/Model/Project.cpp:753` | `Project.cpp` chama `merge::levarConflitosPresosAoMantido`, definida em `Source/Model/MergeFichas.cpp:384`. Só o alvo `matriz` (`CMakeLists.txt:606`) lista `MergeFichas.cpp`. | `undefined reference` ao linkar 4 alvos; CI (`build.yml` constrói 3 deles) fica vermelho. | Adicionar `Source/Model/MergeFichas.cpp` nos 4 alvos (já têm `NomesCanonicos.cpp` e `Project.cpp`, que são suas únicas dependências). Alternativa: um `add_library(matriz_model OBJECT ...)` único para evitar a próxima divergência. | P | 🟡 |
| **B-02** | Alta | Migração/FTS | `Project.cpp:498-640` (`migrarBuscaFtsIndexada`), `:618`, `:629`; `schema/registro.sql:686-690`, `:731-732` | A reconstrução faz `INSERT INTO busca_fts SELECT item_id, valor FROM item_campo` **sem** `WHERE valor IS NOT NULL` e depois `INSERT INTO busca_fts_map ... SELECT rowid,item_id,conteudo FROM busca_fts`. `busca_fts_map.conteudo` é `NOT NULL`. `item_campo.valor` é nulável (`registro.sql:179`) e o próprio código já trata valor NULL como existente (`Project.cpp` bloco "item_campo→item": `// UPDATE antigo: valor NULL`). O `catch (...)` faz `ROLLBACK` e segue sem log. 🟢 Repro: `NOT NULL constraint failed: busca_fts_map.conteudo` → rollback, mapa fica com 0 linhas. | Se existe **uma única** linha com valor NULL, o projeto antigo refaz a reconstrução inteira (milhões de linhas FTS) **em toda abertura** e nunca termina a migração. Os gatilhos novos (`trg_item_campo_busca_insert/update`, `registro.sql:731-744`) também **abortam o INSERT/UPDATE** de `item_campo` com `valor` NULL. | `WHERE valor IS NOT NULL` (e `TRIM(valor) <> ''` como no backfill) nas 9 inserções; `COALESCE(new.valor,'')` ou `WHEN new.valor IS NOT NULL` nos gatilhos; **logar** a exceção do `catch` com `what()`; gravar `schema_info('fts_map_migrado','1')` para não depender de inspecionar o SQL do gatilho. | P | 🟢 repro / 🟡 sem rodar o app |
| **B-03** | Alta | FTS/Regra | `ProjetoAberto.cpp:2602`, `:2696-2706`, `:2749-2754`, `:2789-2790` | `DELETE FROM busca_fts WHERE item_id=? AND conteudo LIKE '#%'` e `INSERT INTO busca_fts` diretos. AGENTS.md exige manter `busca_fts_map` e **proíbe** `DELETE ... WHERE item_id = ? AND conteudo = ?`. `item_id` é `UNINDEXED`. 🟢 `EXPLAIN`: `SCAN busca_fts VIRTUAL TABLE`. Medido: 0,52 s (1ª) e 0,32 s/chamada com 1,3 M linhas; 20 chamadas = 6,3 s. Os gatilhos `trg_item_tag_busca_*` já indexam/removem as mesmas tags. | (a) Edição de tag em lote vira O(itens × tamanho da FTS): com 5 M itens (≈65 M linhas) ≈ 15 s **por item**. (b) Linhas inseridas na FTS sem entrada no mapa; (c) provável **duplicação** de termos (gatilho + INSERT manual) e resultados de busca obsoletos. | Apagar o código manual e confiar nos gatilhos (os `INSERT/DELETE` em `item_tag` já os disparam). Se precisar reindexar, `DELETE ... rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id=?)`. | M | 🟢 custo / 🟡 duplicação |
| **B-04** | Alta | Export | `Consolidacao.cpp:1195-1207` (existência → pula), `:1206-1207` (`copyFileTo` + só tamanho), `:1272-1274` (`catch` só registra) | Na falha (disco cheio, volume desmontado, cancelamento, queda de energia) o `copyFileTo` deixa o arquivo parcial **com o nome final**; nada o apaga. Na próxima execução `destinoArquivo.existsAsFile()` → `++jaExistiam; continue;`. O aviso `jaExistiamDiferentes` só dispara para cópia simples e só aparece como lista. Sem verificação de hash no export (o MAIN usa `copiarVerificado`, `:1318`). | Entrega ao cliente/arquivo morto com mídia truncada, silenciosamente. Contradiz a promessa de integridade do produto. | Trocar `copyFileTo` por `copiarVerificado(origem, destino, continuar)` (já existe: `.mtz-tmp` + hash + rename); no `catch`, apagar o destino parcial; ao pular "já existe", comparar tamanho **e** hash com o catálogo (`arquivo.checksum_sha256`) para copia simples. Varrer `*.mtz-tmp` no início (`limparTemporariosDeCopia` já existe). | M | 🟡 |
| **B-05** | Média | Migração | `Project.cpp:1095` | Comentário "Atualiza a view em projetos existentes" mas o SQL é `CREATE VIEW asset_preservation_status` **sem** `IF NOT EXISTS` nem `DROP`. A view já existe (`registro.sql:935`), então lança, e o `catch (...) {}` engole. Compare `:1083`, que faz `DROP VIEW IF EXISTS` antes. | A regra "BACKUP_VERIFIED só vale quando o evento mais recente foi SUCCESS" **nunca chega** a projetos criados antes dela. | `DROP VIEW IF EXISTS asset_preservation_status;` antes do `CREATE`. Melhor: versionar a view em `schema_info`. | P | 🟡 |
| **B-06** | Média | Async/Race | `Source/Whatsapp/ServidorWhatsappApp.cpp:32`; destrutor `:54` | `callAsync([this, arquivo, tamanho] { if (aoConversaRecebida) ... })` com `this` cru, chamado da thread HTTP. Sem `SafePointer`/token. O destrutor chama `servidor_.parar()` mas callbacks já enfileirados disparam depois. | Use-after-free ao fechar o app/painel logo após o recebimento de uma conversa. (As 7 ocorrências em `ProjetoAberto.cpp` usam o token `vivo` e foram conferidas em `:880`.) | `std::weak_ptr<bool> vivo_` (padrão de `ProjetoAberto::tokenVida`) ou `Component::SafePointer`. | P | 🟡 |
| **B-07** | Média | Erros engolidos | 502 ocorrências de `catch (...) {}` em `Source/`, 52 só em `Project.cpp` | `grep -rn 'catch (\.\.\.) *{ *}'`. Casos graves: `Project.cpp:753-762` (merge), `:498` (FTS, ver B-02), `:1171-1215` (gatilhos). | Falhas de migração/backup invisíveis; o caso B-02 durou porque ninguém via o erro. | Função `logarErroSilencioso(const char* onde)` que registra `what()` no log do projeto e use-a nos `catch` de migração e de backup. Priorizar os de `Project.cpp`, `Consolidacao.cpp`, `Sync/`. | M | 🟢 contagem |
| **B-08** | Média | Segurança/SQL | `ProjetoAberto.cpp:2060-2116` (leitura de `undo_parte`), `:2084`, `:2097-2098`, `:2103`, `:2116` | O Undo persiste em `registro.sqlite` os campos `tabela`, `onde`, `colunas_json` e depois monta `"UPDATE " + t + " SET ... WHERE " + p.onde`. Os valores vão como parâmetro, mas **o texto do SQL vem do banco**. | Projeto recebido/adulterado (cópia de terceiros, backup comprometido) executa DML arbitrário com `Undo`. Escopo limitado ao próprio banco (um `prepare` aceita só 1 instrução), mas permite apagar/alterar dados. | Validar `tabela` contra uma lista branca e `colunas` com `PRAGMA table_info`; restringir `onde` a `coluna = ?`/`IN (?,…)` gerados por código. | M | 🟡 |
| **B-09** | Baixa | Invariante | `ProjetoAberto.cpp:9115-9120` + AGENTS.md | AGENTS.md lista só `marcadosHtml_/Zip_/Print_/Watermark_` sob `marcacoesMutex_`; `obterConjuntoMarcacao` também devolve `marcadosBounce_` e `marcadosRejeitado_`, que **não estão** nas cópias sob lock de `:558-563` e `:1053-1058`. | Se esses dois conjuntos são lidos em background (snapshot), há data race; se não, a doc está incompleta. | Confirmar com TSan e incluir os dois na cópia sob lock (ou documentar). | P | 🟡 (rodar TSan) |
| **B-10** | Baixa | Dup. de código | `Project.cpp:520-575` vs `schema/registro.sql:693-770` vs `Project.cpp:1171-1215` | Os 14 gatilhos de busca existem em 3 cópias, e o bloco `:1171-1215` recria o formato **antigo** (sem `busca_fts_map`) se o gatilho não existir. | Deriva entre cópias; um gatilho ausente é recriado no formato errado e depois nunca migrado (a detecção só olha um gatilho). | Uma só fonte (`registro.sql`) + migração por versão. Apagar o bloco `:1171-1215`. | M | 🟡 |
| **B-11** | Baixa | Windows | `Consolidacao.cpp:1380-1383` | Fallback de rename: `destinoFinal.deleteFile()` e depois `moveFileTo`. Se o move falhar, o destino anterior já foi apagado. | Perda de uma cópia boa anterior em rename entre volumes que falha. | Mover para `destino.mtz-old`, mover o novo, só então apagar o antigo. | P | 🟡 |
| **S-01** | Alta | Rede/Segredo | `Source/Celular/ServidorCelular.cpp:141` (`set_payload_max_length(SIZE_MAX)`), `:158` (CORS), `:178-183` | `kFallback = "yYW2fkWyxA7ATbpCdvzeSPUuDcqgXEhq"` e `kMini = "LhTbrWU4ENPJvqKVseMRA7tvbfq2fKcY"` são aceitas como chave válida nas rotas `/whatsapp/*` (que fazem `POST` de `.zip`/`.txt` para o disco). `ipPermitido` aceita **qualquer IP privado RFC1918**, não só a sub-rede local. Sem limite de corpo nem checagem de espaço livre. | Qualquer dispositivo na mesma rede (Wi-Fi de convidados, coworking, hotel) grava arquivos de tamanho arbitrário no Mac/PC enquanto o servidor estiver ligado: enche o disco do projeto, planta zips para o importador. As chaves estão no repositório. | Remover `kFallback`/`kMini` (e a migração de "BKR Matriz Mini" se não for mais necessária); exigir chave por sessão; limite de corpo (ex.: 2 GB) e checagem de `getBytesFreeOnVolume`; restringir à sub-rede da interface escolhida; desligar por padrão e mostrar aviso de privacidade. **Rotacionar** as chaves (já vazadas). | P–M | 🟡 |
| **S-02** | Média | Rede | `RecebimentoCelular.cpp:116-117`, `ServidorCelular.cpp:196-203` | Token na query string (`?t=`) e HTTP sem TLS. Tokens aparecem em histórico do navegador do celular/proxy. | Sniffing na LAN reaproveita o token. | Header `X-Token` em vez de query, cabeçalho `Referrer-Policy: no-referrer`; HTTPS só se o servidor sair da LAN (decisão do dono). | M | 🟡 |
| **S-03** | Média | Cripto | `Source/App/Preferencias.cpp:87-93` | A chave de pareamento do WhatsApp (32 chars) é gerada com `juce::Random::getSystemRandom()` (PRNG não criptográfico). `gerarToken()` (`RecebimentoCelular.cpp:120`) usa `std::random_device` (adequado). | Espaço efetivo de chaves menor que o nominal. | Usar `std::random_device`/`juce::Random` com `CryptographicallySecure` ou o mesmo `gerarToken()`. | P | 🟡 |
| **S-04** | Média | ZIP | `Source/Whatsapp/WhatsappIngestao.cpp:384-405` | O nome extraído usa só o último componente (`fromLastOccurrenceOf("/")`), então **zip-slip está bloqueado** 🟡. Mas `writeFromInputStream(*in, -1)` extrai sem limite de tamanho/razão (só o `.txt` tem teto, `:122`). | Zip bomb enche o disco temporário. | Limitar `e->uncompressedSize` total e por entrada; abortar se o disco livre < 2× o zip. | P | 🟡 |
| **S-05** | Média | Privacidade/Segredo | `Source/App/Preferencias.cpp:49-53`, `Source/Ingest/AiScan.cpp:69-115` | Chave Gemini em texto claro no arquivo de preferências. O `AiScan` envia mídia a `generativelanguage.googleapis.com`. **Nenhum chamador** de `executarAiScan`/`lerGeminiApiKey` no código (ver D-01). | Se for reativado: chave em claro e conteúdo do acervo saindo da máquina sem consentimento por arquivo. Hoje é só superfície morta. | Remover o módulo (D-01). Se mantiver: Keychain/DPAPI e consentimento explícito. | P | 🟡 |
| **S-06** | Baixa | Privacidade | `Source/Diag/Watchdog.h:100-117` (`perf.log`), `NSExceptionGuard_win.cpp:65-77`, 9 `writeToLog` com caminhos | `perf.log` em `~/Library/Logs/BKR Matriz` (mac) / `%APPDATA%` sem rotação à vista; traz stack traces (`juce::SystemStats::getStackBacktrace`) e, nas linhas `[Abrir]`, nome do projeto. O minidump usa `MiniDumpWithIndirectlyReferencedMemory` e guarda memória do processo (caminhos com nome de usuário, nomes, GPS, chaves). | Quem anexa log/dump a um pedido de suporte entrega dados pessoais do acervo. | Rotação por tamanho; mascarar `$HOME`/usuário nos caminhos logados; avisar antes de exportar dump; trocar para `MiniDumpNormal`. | M | 🟡 |
| **S-07** | Média | Supply chain | `cmake/Dependencies.cmake` | SQLite **3.45.3** (SHA-256 fixado ✔), Exiv2 **v0.28.3**, zlib v1.3.1, yaml-cpp 0.8.0, cpp-httplib v0.60.1, QR v1.8.0, libmtp 1.1.23 (embutida). Dependências git por **tag**, não por SHA de commit. ⚪ *De memória, conferir:* SQLite < 3.50.2 tem CVE-2025-6965 e < 3.49.1 tem CVE-2025-3277; Exiv2 < 0.28.5 tem CVE-2025-26623 (heap overflow, parser de JP2). | O Exiv2 lê **arquivos não confiáveis** por definição (ingest). | Subir SQLite ≥ 3.50.2 e Exiv2 ≥ 0.28.5; trocar `GIT_TAG` por hash de commit; gerar SBOM; fuzz leve do ingest (EXIF/JP2/ZIP). | M | ⚪ |
| **R-01** | Alta | Backup | ver B-04 | — | — | — | — | — |
| **R-02** | Média | Backup | `Consolidacao.cpp:1318-1385` | `copiarVerificado` está bem desenhado (temp + hash da leitura + releitura + rename), mas a releitura do `tmp` pode ser servida do **cache de páginas** do SO, então não prova o meio físico. | Corrupção silenciosa do disco de destino pode passar na verificação imediata. | `F_NOCACHE` (mac) / `FILE_FLAG_NO_BUFFERING` (Win) na releitura, ou re-verificar depois (já existe fixity em `Preservation.cpp`). Conferir que o `flush()` do JUCE 8.0.14 faz `fsync`/`FlushFileBuffers`. | M | 🟡 / ⚪ fsync |
| **R-03** | Média | Banco | `Source/Db/Database.cpp:193-194` | `journal_mode=WAL` + `synchronous=NORMAL`. | Queda de energia pode perder as últimas transações já "confirmadas" (o banco continua íntegro). Para o registro de consolidação, isso significa arquivo copiado sem linha no registro. | `synchronous=FULL` ao menos durante `executarConsolidacao`/`Undo`; ou `PRAGMA wal_checkpoint(FULL)` no fim do lote. | P | 🟡 |
| **R-04** | Média | Recuperação | busca por `quick_check`/`integrity_check`/`recover` | Só `CompactacaoRegistro.cpp:25,153` roda `integrity_check`. Não há `quick_check` na abertura nem rotina de recuperação de `registro.sqlite` corrompido além do backup MAIN (`Sync/SyncEngine.cpp:88-112`, `BackupRecoveryDialog`). | Banco corrompido só é descoberto quando uma consulta falha. | `PRAGMA quick_check` assíncrono após abrir (e antes de copiar o registro para o MAIN); snapshot rotativo (`VACUUM INTO`) do registro a cada N operações. | M | 🟡 |
| **R-05** | Baixa | Backup | `Consolidacao.cpp:951` | Espaço livre só é checado ao planejar (`getBytesFreeOnVolume`); durante a cópia depende do erro de escrita (tratado: "disco cheio?"). | Volume desmontado no meio gera falha por arquivo, não interrupção limpa do lote. | Checar a presença do volume (`destination_id`) entre arquivos e abortar com estado claro. | M | 🟡 |
| **P-01** | Alta | Perf./Abertura | `Project.cpp:726-1318` (`aplicarSchemas`) | Ver §5. | Abertura de 8–16 min. | Ver §5. | M | 🟢 / 🟡 |
| **P-02** | Média | Perf./Busca | `ProjetoAberto.cpp:7385-7470` | Por token: 1 consulta FTS + 6 `LIKE '%x%'` (item 9 colunas, `item_campo`, `item_tag`, `item_observacao`, `arquivo` ×2). 🟢 plano: laço aninhado por item (`SEARCH i USING idx_item_projeto`, `SEARCH c USING idx_item_campo_item`). 0,15 s em `item_campo` de 600 mil linhas (cache quente). | ≈8 s (quente) por token e por consulta com 5 M itens; HD frio: minutos. A FTS fica redundante porque o LIKE já cobre. | Usar só a FTS (com `tokenize='trigram'` se precisar de substring) e fazer `LIKE` apenas no que a FTS não indexa; cancelar a consulta anterior ao digitar; paginar. | G | 🟢 |
| **P-03** | Média | Perf. | `Project.cpp:1174` | `SELECT COUNT(*) FROM busca_fts` a cada abertura. FTS5 não mantém contador: varre. 🟢 0,13–0,19 s por 1,3 M linhas. | ≈1 s a cada 10 M linhas, só para saber se está vazia. | `SELECT 1 FROM busca_fts LIMIT 1`. | P | 🟢 |
| **P-04** | Média | Perf./Abertura | `Project.cpp:1270-1288` | `INSERT OR IGNORE INTO preservation_event ... SELECT ... FROM item WHERE NOT EXISTS(...)` a cada abertura: 0,67–0,75 s por 100 mil itens (cache quente), 1 sonda por item. | Linear no acervo; a cada abertura. | Guardar `schema_info('migration_event_feito')` e pular. | P | 🟢 |
| **P-05** | Baixa | Perf. | `MergeFichas.cpp:384-390`, `registro.sql:212` | `levarConflitosPresosAoMantido` roda em `BEGIN IMMEDIATE` a cada abertura; filtra `item_historico` por `modelo_origem`/`confianca_origem`, sem índice (só existe `idx_item_historico_item`). `mantidoDe` faz `SELECT id FROM item WHERE codigo_acervo = ?` até 32× por item preso; o índice parcial `idx_item_catalogo_codigo` (`WHERE COALESCE(em_quarentena,0)=0 AND COALESCE(estado,'')!='duplicata'`) **não é usável** para itens duplicata. | Varredura de `item_historico` + `item` por abertura (e lock de escrita). | Índice `item_historico(modelo_origem, confianca_origem)` parcial e índice total em `item(codigo_acervo)`; executar só se `schema_info` indicar pendência. | P | 🟡 |
| **P-06** | Baixa | Perf. | `Consolidacao.cpp:942-945` | `WHERE id NOT IN (SELECT item_id FROM acervo_item_pasta ...)` | Em 5 M itens vira uma sub-varredura grande; `NOT IN` com NULL é frágil. | `NOT EXISTS` ou `LEFT JOIN ... IS NULL`. | P | 🟡 |
| **D-01** | Baixa | Código morto | `Source/Ingest/AiScan.cpp/.h` (compilado em `CMakeLists.txt:614`), `Preferencias.cpp:49-53`, `Project.cpp` `migrarAiScanParaIndice`, `MetadadoEmbutido.cpp:272` | `executarAiScan`, `reindexarAiScanNaBusca`, `resultadosDoItem`, `lerGeminiApiKey`, `gravarGeminiApiKey` têm 2 ocorrências no projeto (declaração + definição): **nenhum chamador**. | Binário maior, superfície de ataque/privacidade (S-05), migração que continua rodando em toda abertura. | Remover o módulo e a migração (ou manter só a leitura do resumo antigo). | M | 🟢 grep |
| **D-02** | Info | Código morto | `MainComponent.cpp:677, 811, 971-1027` | `MainComponent::mosaico_` **não é órfão** nesta branch: é criado, configurado e usado. A suspeita do pedido não se confirma. | — | — | — | 🟢 grep |
| **D-03** | Baixa | Código morto | ver Apêndice A | Heurística: 132 funções declaradas em `.h` com 2 ocorrências no projeto inteiro (incluindo `tools/`). Blocos inteiros: `ProvaData/*` (OpenTimestamps: `exportarProvaPdf`, `registrarItem`, `iniciarAtualizadorEmBackground`…), `Analytics/*` (`exportarCSV/PDF`, `SavedAnalyses`), `Preferencias` (`*IntakeMode/Horas`), vários métodos de `ArvoreBackupComponent` e `MainComponent::mostrar*`. | Candidatos, **não confirmados** (podem ser chamados por macro, `std::function` ou só por testes). | Revisar o apêndice caso a caso antes de apagar. | M | 🟡 heurística |
| **T-01** | Alta | CI | `.github/workflows/build.yml` | (a) Dispara só em `main` e `windows-port`; esta branch (`fix/...`) e seus commits não passam por CI. (b) Constrói 3 self-tests; o `matriz_analytics_selftest` nunca é construído (B-01 também o quebra). (c) **Nenhuma** execução de `--selftest-lote` / UI self-tests (13 falhas pré-existentes segundo `HANDOFF.md`). (d) TSan só macOS e só `matriz_selftest`; ASan só Windows e só `matriz_selftest` (nada para `ingest_selftest`, `interop`, nem para as áreas que o AGENTS.md chama de sensíveis). (e) Sem Linux, sem análise estática (clang-tidy/cppcheck), sem fuzz, sem teste de escala. | Regressões como B-01 chegam à `main` ou ficam sem aviso por dias. | Rodar CI em `push` de qualquer branch; incluir `analytics_selftest`; subir o `--selftest-lote` em modo headless; ASan nos 4 alvos; um job de escala (gerar 200 mil itens sintéticos, medir `aplicarSchemas`, busca e export). | M–G | 🟡 |

---

## 3. Bugs ocultos (detalhe)

### 3.1 B-01 — erro de link (confirmado por leitura)

`grep -rn levarConflitosPresosAoMantido` mostra: definição em `Source/Model/MergeFichas.cpp:384`, declaração em `MergeFichas.h:80`, uso em `Project.cpp:753` e em `LoteSelfTest.cpp`. `MergeFichas.cpp` aparece **uma única vez** no `CMakeLists.txt` (`:606`, alvo `matriz`). Todo alvo que compila `Project.cpp` (`:173`, `:282`, `:347`, `:648`, `:836`) precisa dele, logo 4 alvos estão quebrados, não só o `matriz_ingest_selftest`.

```cmake
# em cada um dos 4 alvos, junto de Source/Model/Project.cpp
    Source/Model/MergeFichas.cpp
```

### 3.2 B-02 — a migração que nunca termina (reprodução)

Script em `…/scratchpad/repro_fts.py` (SQLite 3.53.1, 38 mil itens, 380 mil campos, 1 NULL a cada 500 itens):

```
FALHOU apos 1.0s: NOT NULL constraint failed: busca_fts_map.conteudo
map rows depois do rollback: (0,)
INSERT item_campo valor NULL com gatilho novo -> NOT NULL constraint failed: busca_fts_map.conteudo
```

Sequência no app: `aplicarSchemas` → `registro.sql` cria `busca_fts_map` → `migrarBuscaFtsIndexada` vê que `trg_item_campo_busca_delete` ainda não cita `busca_fts_map` → dropa os 14 gatilhos, recria, `DELETE FROM busca_fts`, repopula 9 fontes, e **na última instrução** estoura o `NOT NULL` → `ROLLBACK` (desfaz tudo, inclusive os gatilhos novos) → `catch (...)` mudo. Próxima abertura: mesma coisa. Todo o trabalho de reindexar o acervo, incluindo o texto de **todos** os `item_campo` (inclusive EXIF/`[OTHER METADATA]` volumosos), é refeito e jogado fora.

Isso explica por que o tempo varia (7,9 → 12–16 min): é I/O de HD frio sobre um registro grande, não CPU (no meu teste a reconstrução de 100 mil itens levou 8,9 s em SSD).

**Como confirmar em 1 minuto (sem código):** no `perf.log` do dono, procurar `[Abrir] migracao=busca_fts_indexada N ms`. Se for o bloco dominante a cada abertura, é este. Contra-prova: `SELECT COUNT(*) FROM item_campo WHERE valor IS NULL;` > 0 e `SELECT COUNT(*) FROM busca_fts_map;` = 0.

```cpp
// Project.cpp:618 e vizinhas — todas as fontes que podem ter NULL
registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, valor FROM item_campo WHERE valor IS NOT NULL");
// ...
} catch (const std::exception& e) {
    juce::Logger::writeToLog("[migracao] busca_fts_indexada FALHOU: " + juce::String(e.what()));
    try { registro.exec("ROLLBACK"); } catch (...) {}
}
```

### 3.3 B-03/B-04 — ver tabela

Ambos têm correção de ≤ 1 dia e trecho em §2.

### 3.4 Itens verificados que NÃO são bug (para poupar tempo)

- Saneamento de nomes: `NomesSeguros.cpp` trata `..` (`:108`), nomes reservados do Windows (`:23-33`), ponto/espaço no fim (`:56`) e limite de 255 bytes UTF-8 (`:77`). O manifesto tem `longPathAware` (`Assets/app.manifest:26`), mas **isso sozinho não basta**: o Windows também exige a chave de registro `LongPathsEnabled` na máquina do usuário. Documentar no instalador.
- Comparação de token em tempo constante (`ServidorCelular.cpp:72`).
- `std::random_device` em `gerarToken` (128 bits).
- SQL dinâmico encontrado: colunas/tabelas vêm de constantes no código (`Project.cpp:280`, `MergeFichas.cpp:39-122`, `NomesCanonicos.cpp:255-544`); valores sempre por `bind`. A exceção é o Undo (B-08).
- Callbacks `callAsync([this, vivo…])` em `ProjetoAberto.cpp` usam `weak_ptr<bool>` e checam antes de usar `this`.
- `MosaicoComponent`/`runDispatchLoopUntil`: os usos restantes estão em self-tests (`IntakeWorkspaceComponent.cpp:4917` é código de teste) — nenhum em produção.

---

## 4. Código morto, restos e redundâncias

Ver D-01…D-03, B-10 e Apêndice A. Resumo:

- **IA/Gemini:** módulo inteiro sem chamador (D-01). Remover reduz a superfície de privacidade e uma migração (`migrarAiScanParaIndice`) que roda em toda abertura.
- **`mosaico_`:** não é órfão (D-02).
- **Gatilhos de busca triplicados** (B-10).
- **Comentários que mentem:** `Project.cpp:1095` (B-05).
- **Fallbacks de chave** hardcoded do "BKR Matriz Mini" (S-01): o app Mini aparentemente foi descontinuado/integrado (`Preferencias.cpp:70-84` ainda lê o arquivo de settings dele).
- Strings sem uso e flags obsoletas: **não** varridas (ver §11).

---

## 5. Performance — abertura do projeto (`aplicarSchemas`)

### Causas prováveis, em ordem

1. **B-02 (reconstrução da FTS em loop).** Custo: reindexar tudo (≈13 linhas FTS por item no meu fixture; 38 mil itens com EXIF e tags ⇒ muitas mais) dentro de uma transação, sob `BEGIN IMMEDIATE`, num HD. Falha no fim → desperdício total, **repetido a cada abertura**. Compatível com tempos de 8–16 min que variam com o cache de disco.
2. **Varreduras completas fixas a cada abertura** (cada uma é barata com cache quente, cara com HD frio e registro grande):
   - `item_campo` ⨝ `item` filtrando campos a migrar (`Project.cpp` ≈ `:998-1015`);
   - `SELECT COUNT(*) FROM busca_fts` (`:1174`, P-03);
   - `INSERT OR IGNORE … FROM item WHERE NOT EXISTS (… preservation_event …)` (`:1270`, P-04);
   - `UPDATE item SET persistent_id … WHERE persistent_id IS NULL` (`:1257`; 0,01 s quando não há nada, ok);
   - `levarConflitosPresosAoMantido` em `BEGIN IMMEDIATE` (P-05);
   - `DROP VIEW`/`CREATE VIEW colecao_revisao` em toda abertura (escrita no arquivo).
3. **Nenhum controle de versão de schema.** Tudo roda "por idempotência" em toda abertura em vez de uma checagem O(1) `schema_info('versao_migracao')`.
4. **Tamanho do registro.** `HANDOFF`/AGENTS citam 965 MB para 11.890 fotos antes da compactação; um acervo de 38 mil itens pode ter vários GB, e as varreduras acima leem páginas de overflow (EXIF, miniaturas legadas em `cache_arquivo.miniatura`). Medir: `SELECT name, SUM(pgsize)/1048576 MB FROM dbstat GROUP BY name ORDER BY 2 DESC LIMIT 15;`.

### Medidas (🟢, SQLite 3.53.1, registro real do schema, SSD, cache quente, 100 mil itens, 13 linhas FTS/item, registro 295 MB)

| Operação | Tempo | Observação |
|---|---|---|
| Ingest de 100 mil itens (com gatilhos FTS) | 47–49 s (≈2,1 mil itens/s) | linear; 2,9 KB/item só no registro |
| Reconstrução completa FTS+mapa | 8,9 s | sem NULL; ≈7,5 min/5 M itens em SSD |
| `COUNT(*)` na `busca_fts` | 0,13–0,19 s | varredura |
| `INSERT OR IGNORE MIGRATION … NOT EXISTS` | 0,67–0,75 s | por abertura |
| `UPDATE persistent_id` (nada a fazer) | 0,01 s | ok |
| `DELETE busca_fts WHERE item_id=? AND conteudo LIKE '#%'` | 0,32–0,52 s **por chamada** | `SCAN busca_fts` |
| mesmas chamadas, lote de 20 | 6,3 s | |
| `DELETE item_tag` via mapa (gatilho novo) ×20 | 0,04 s | caminho correto: 150× mais rápido |
| FTS `MATCH "guitar"*` (100 mil resultados) | 0,30 s | |
| FTS `MATCH` raro | ≈0 s | |
| `LIKE '%777%'` em `item_campo` (600 mil linhas) | 0,15 s | laço por item |
| `LIKE` em `arquivo.caminho_relativo` | 0,05 s | |

### Recomendações (ordem)

1. Corrigir B-02 e **logar** a falha (P).
2. `schema_info` com `versao_migracao`; se igual à atual, pular `aplicarSchemas` inteiro exceto `CREATE … IF NOT EXISTS` baratos (M).
3. Mover migrações pesadas para uma etapa com progresso, fora da message thread, e nunca dentro de `BEGIN IMMEDIATE` da abertura (M).
4. Para saber **qual** bloco dói: o cronômetro do último commit já resolve; pedir ao dono as linhas `[Abrir] migracao=` de uma abertura fria **antes e depois** de aplicar o passo 1.

---

## 6. Escala (500 mil a 5 milhões de itens)

Extrapolação linear dos dados medidos (🟢 base, ⚪ extrapolação). Valores em SSD quente; HD frio pode multiplicar por 5–50×.

| Função | Comportamento | 500 mil | 5 milhões | Risco |
|---|---|---|---|---|
| **Ingest** | gatilhos FTS ≈ 13 linhas/item; 2,1 mil itens/s | ≈4 min só de gatilhos | ≈40 min | Aceitável; usar transação por lote (já há `salvarMetadadoEmLote`). Registro ≈ 1,5 GB / 15 GB. |
| **Abertura** | ver §5 | segundos–minutos | **minutos** (P-03/P-04/P-05 lineares) + risco B-02 | Alto |
| **Busca (digitar)** | FTS + 6 `LIKE` por token | ≈0,8 s quente | ≈8 s quente | Alto: congelamento perceptível; sem cancelamento |
| **Edição de tag** (B-03) | varre a FTS por item | 1,6 s/item | **≈15 s/item**; lote de 100 ≈ 25 min | Crítico para lotes |
| **Filtros do METADATA** | não medi; vários `LIKE` em `item` e `item_campo` (`CatalogWorkspaceComponent`, `AnalyticsEngine.cpp:68`: `LOWER(caminho) LIKE '%.xmp'` por item) | — | — | Médio: ⚪ não verificado |
| **Duplicates** | `NOT LIKE '%[USER_VERIFIED…]%'` sobre `notas_livres` (`IngestArquivo.cpp:526`, `DuplicatesWorkspaceComponent.cpp:2098`) | varredura | varredura | Médio: ⚪ |
| **Backup/Consolidação** | cópia verificada: lê origem + reescreve + relê destino (3× I/O) | I/O-bound | idem | Esperado; a memória é limitada (buffer 1 MiB) ✔ |
| **Export** | sem hash; por arquivo `resolverArquivo` (2 consultas) | N+1 | N+1 | Médio + B-04 |
| **Relink em lote** | nome+tamanho+SHA-256, gravação em 1 transação (HANDOFF) | I/O-bound | idem | OK |
| **Undo** | `undo_parte`/`undo_linha` crescem no registro | cresce sem limite? ⚪ | — | Verificar política de poda |
| **Memória** | snapshots completos (`MatrizSnapshot`) por geração | ⚪ | ⚪ | Medir com `MosaicoStressTest` |

**O que eu não consegui fazer:** rodar ingest/Duplicates/Backup/exports de verdade nas escalas pedidas, pois o app não compila no Linux (§0). Só o SQL (schema real) foi exercitado. Recomendo executar o `MosaicoStressTest` e um gerador de fixture (a partir de `…/scratchpad/bench2.py`) numa máquina macOS/Windows.

---

## 7. Segurança e privacidade

Ver S-01…S-07 e B-08. Pontos adicionais:

- **Path traversal em exports:** `NomesSeguros::sanitizarCaminhoRelativo` remove `..` (`:108`) e é usado em `Miniaturas`, `WhatsappIngestao`, `DispositivoAndroidUsb`, `PainelAndroidUsb`. Não confirmei que **todo** `caminhoRelativoDestino` do planejamento de export passa por ele (o `getChildFile(ip.caminhoRelativoDestino)` em `Consolidacao.cpp:1193` resolve `..` se vier cru). 🟡 Verificar `planejarConsolidacao`.
- **ffmpeg/ffprobe:** só via `ProcessoExterno` (`juce::ChildProcess::start(argv)` com vetor, sem shell) — injeção de shell não é possível; o risco é argumento tipo `-i` com nome iniciando em `-`. Garantir caminhos absolutos ou prefixo `file:`/`--`. 🟡
- **`startAsProcess()`** em `BackupWorkspaceComponent.cpp:94, 2536` e `StorageWorkspaceComponent.cpp:1374` abre arquivos/pastas escolhidos pelo usuário; sem risco evidente.
- **Dados pessoais:** GPS/EXIF ficam no banco por design (`asset_geolocation`) e vão para o export "Original/Embed"; o modo "Clean" remove (`MetadadoEmbutido`). Os logs não gravam coordenadas (varredura por `lat|lon|gps|email|@` só achou mensagens de rede). `matriz_interop.log`: não localizado nesta branch (o interop usa fixtures); ⚪.
- **Segredos hardcoded:** apenas as duas chaves do S-01 (varredura por `api_key|secret|token|password|AIza|sk-|Bearer`).
- **Integridade de backup:** SHA-256 por arquivo no MAIN (✔), fixity em `Preservation.cpp`; export sem hash (B-04).

---

## 8. Confiabilidade do backup

| Cenário | Situação |
|---|---|
| Interrupção no meio da cópia (MAIN) | ✔ `.mtz-tmp` + hash + rename; temporários limpos por `limparTemporariosDeCopia`. |
| Interrupção no meio da cópia (Export) | ✘ arquivo parcial com nome final (B-04). |
| Disco cheio | MAIN: ✔ erro de escrita → apaga `.mtz-tmp`. Export: ✘ idem B-04. Celular: ✘ sem checagem (S-01). |
| Volume desmontado | Parcial (R-05): falha por arquivo, sem interrupção limpa do lote. |
| Verificação por hash | MAIN ✔; releitura pode vir de cache (R-02); Export ✘. |
| Atomicidade | rename ✔; fallback não atômico (B-11). |
| Registro vs arquivos | WAL+NORMAL pode perder o registro de arquivos já copiados após queda de energia (R-03). |
| Recuperação do `registro.sqlite` | cópia no MAIN via `VACUUM INTO`; sem `quick_check` nem reparo automático (R-04). |
| Idempotência | ✔ reexecução pula o que existe — **bom** para o MAIN, **perigoso** para o export enquanto B-04 existir. |

---

## 9. Testes e CI

Ver T-01. Complementos:

- Os self-tests vivem em `tools/*_selftest` e em `Source/Ui/*SelfTest.cpp` (`LoteSelfTest`, `UiSelfTest`, `IngerirArquivosTest`, `MosaicoStressTest`), não há diretório `tests/` nem framework (Catch2/GTest), nem relatório de cobertura.
- **Lacunas de teste sugeridas** (com o defeito que cada uma teria pegado):
  1. Abrir um projeto com `item_campo.valor IS NULL` e checar que `busca_fts_map` fica preenchido e a migração não repete (B-02).
  2. Export com falha injetada na cópia (disco cheio simulado) e reexecução: o arquivo parcial não pode ficar (B-04).
  3. Linkar cada alvo no CI com `-Wl,--no-undefined` equivalente (B-01).
  4. Teste de propriedade para `NomesSeguros` (nomes reservados, `..`, NFC) em macOS/Windows.
  5. Teste do servidor do celular: chave inválida, corpo acima do limite, IP fora da sub-rede (S-01).
  6. Teste de escala: 200 mil itens sintéticos, medir `aplicarSchemas`, busca e edição de tag em lote.
- As 13 falhas pré-existentes do `--selftest-lote` (HANDOFF, 2026-10-31) deveriam virar bloqueio de CI ou ser marcadas como "esperadas" com ID.

---

## 10. Quick wins (≤ 1 h cada)

1. **B-01:** adicionar `MergeFichas.cpp` nos 4 alvos do CMake.
2. **B-02:** `WHERE valor IS NOT NULL` nas inserções da reconstrução + logar a exceção.
3. **B-05:** `DROP VIEW IF EXISTS asset_preservation_status` antes do `CREATE`.
4. **S-01:** apagar `kFallback`/`kMini`, rotacionar chaves, limitar corpo do `POST`.
5. **P-03:** trocar `COUNT(*)` por `SELECT 1 … LIMIT 1`.
6. **P-04:** marcar em `schema_info` a migração `MIGRATION` já feita.
7. **B-06:** `weak_ptr` no `callAsync` de `ServidorWhatsappApp.cpp:32`.
8. **S-03:** gerar a chave do WhatsApp com `std::random_device`.
9. **T-01:** CI em todo `push`/branch e incluir `matriz_analytics_selftest`.
10. Pedir ao dono as linhas `[Abrir] migracao=` do `perf.log` (confirma B-02 em 1 minuto).

## 11. Roadmap priorizado

**Semana 1 (estabilidade e segurança):** B-01, B-02, B-05, B-04 (export verificado), S-01, S-03, B-06, T-01 (CI em todo branch).
**Semana 2 (escala):** B-03 (remover escritas diretas na FTS), P-03/P-04/P-05, `versao_migracao`, `quick_check` assíncrono, R-03.
**Semana 3–4:** P-02 (busca só FTS com trigram, cancelável), B-08 (Undo sem SQL confiado), S-04, S-06, S-07 (SQLite/Exiv2), logging dos `catch(...)` (B-07).
**Depois:** D-01/D-03 (remoção de código morto), fixture de escala + job de CI, fuzz do ingest, ASan/TSan em todos os alvos.

## 12. Avisos e decisões que dependem do dono

1. **Chaves vazadas (S-01):** as duas chaves do WhatsApp já estão no histórico do git. Remover do código **não** as invalida; é preciso decidir se aceita quebrar o pareamento do app "Mini"/página do GitHub Pages ao rotacioná-las.
2. **Export "já existe":** corrigir B-04 muda o comportamento (reexportar arquivos que hoje são pulados). Decidir se o aviso passa a ser erro e se o export passa a gravar hash.
3. **`_SEM_PASTA` físico no MAIN** continua pendente de decisão (AGENTS.md). Não tocado.
4. **Remover o módulo de IA (D-01)** elimina o recurso Gemini de vez; confirmar que não há plano de reativá-lo.
5. **Rede do celular/WhatsApp:** manter HTTP simples na LAN ou exigir TLS (S-02)? Hoje fica ligado enquanto o painel está aberto.
6. **Política de atualização de dependências (S-07):** subir SQLite/Exiv2 exige reexecutar os testes de interoperabilidade mac↔Windows. Conferir os CVEs citados (⚪, de memória).
7. **Dado real afetado:** o `HANDOFF.md` já registra itens do Bunker Analog fora do MAIN (F01) e 15 containers vazios. Esta auditoria não os altera.
8. Nada aqui foi corrigido no código, e nenhum PR foi aberto.

## 13. O que foi / não foi verificado

- 🟢 **Executado:** (a) `git fetch/checkout` da branch auditada; (b) `cmake` no Linux (falha esperada, §0); (c) schema real no SQLite com 100 mil itens: tempos da §5, repro de B-02, plano de B-03/P-02; (d) varreduras `grep` repetíveis (contagens de `catch`, chamadores de funções, segredos).
- 🟡 **Lido:** todo o resto (B-01, B-04…B-11, S-01…S-06, R-*, P-05/P-06, T-01).
- ⚪ **Não verificado:** CVEs de dependências (de memória), `fsync` do JUCE 8.0.14, comportamento no Windows, filtros do METADATA/Duplicates em escala, memória dos snapshots, ASan/TSan, `matriz_interop.log`.
- **Áreas não auditadas em profundidade:** `Ddp/`, `Audio/`, `ProvaData/`, `Sync/`, `Vault/RescanEngine`, `Ui/*Workspace*` (30+ arquivos de UI), `Celular/DispositivoAndroidUsb`, strings/I18n sem uso, flags obsoletas, uso real de `EventBus::dispararItemAlterado` fora da message thread (55 chamadas; não percorri todas).

---

## Apêndice A — candidatos a código morto (heurística: nome com 2 ocorrências em `Source/` + `tools/`)

Gerado por script (regex em declarações de `.h`; `…/scratchpad/mortos.txt`). **Não apagar sem conferir.** Primeiras entradas relevantes:

```
Source/Analytics/AnalyticsEngine.h: executarQuery
Source/Analytics/AnalyticsExport.h: exportarCSV, exportarPDF
Source/Analytics/SavedAnalyses.h: listarAnalises, removerAnalise, salvarAnalise
Source/App/Preferencias.h: gravarGeminiApiKey, lerGeminiApiKey, lerIntakeMode, gravarIntakeMode, lerIntakeHoras, gravarIntakeHoras
Source/Ingest/AiScan.h: reindexarAiScanNaBusca, resultadosDoItem   (+ executarAiScan)
Source/ProvaData/ProvaData.h: exportarProvaPdf, exportarProvaDoItem, registrarItem, registrarProjetoParaAtualizacao,
                              iniciarAtualizadorEmBackground, pararAtualizadorEmBackground, estadosDeTodos, carimboConfere, ...
Source/ProvaData/OpenTimestamps.h: temAtestacao
Source/Ui/ArvoreBackupComponent.h: apagarPastaSelecionada, arranjarSoltosSelecionados, moverItemParaPasta, obterListaPastas,
                                   reunirArquivosSoltos, salvarEsquemaComoPreset, selecionarSolto, soltoAdjacente
Source/Ui/MainComponent.h: mostrarHome, mostrarAnalytics, mostrarIngestWizard, mostrarPreservation,
                           atualizarCacheDeTamanhoTotal, garantirLabelProgressoIngest
Source/Ui/AudioWorkspace.h: reconstruirRodape (1 ocorrência: só a declaração)
Source/Consolidacao/MetadadoEmbutido.h: embutirMarcadoresNoBackup, embutirMetadadosEmItens
Source/Consolidacao/ManifestoBounce.h: escreverManifestoBounce
```

Total da heurística: **132** candidatos.
