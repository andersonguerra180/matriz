-- MATRIZ — banco de registro (registro.sqlite)
--
-- Este banco vive na pasta do projeto (P5 — projeto portátil). Guarda apenas
-- o que é registro: arquivo, ficha, checksum, marcadores, decisões humanas.
-- Nada de IA escreve aqui (P2). Sugestões de modelo vivem em indice.sqlite
-- e só migram para cá quando o operador confirma (P3) — nesse momento
-- entram como valor com fonte 'humano', e o evento fica no histórico.
--
-- Convenção: todo id é TEXT (uuid v4), gerado pela aplicação.
-- Todo timestamp é TEXT ISO-8601 UTC ("YYYY-MM-DDTHH:MM:SSZ").

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS schema_info (
    chave   TEXT PRIMARY KEY,
    valor   TEXT NOT NULL
);
INSERT OR IGNORE INTO schema_info (chave, valor) VALUES ('versao_schema', '1');

-- ---------------------------------------------------------------------------
-- Projeto (linha única — o arquivo .sqlite inteiro já é o projeto, P5)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS projeto (
    id                              TEXT PRIMARY KEY,
    modo                            TEXT NOT NULL CHECK (modo IN ('preservacao', 'catalogo')),
    nome                            TEXT NOT NULL,
    instituicao_ou_selo             TEXT,
    responsavel                     TEXT,
    prefixo_nomenclatura            TEXT NOT NULL,
    -- Máscara padrão do acervo inteiro (item 10, §11.6) — herdada por toda
    -- pasta do Acervo que não tiver a sua própria. Tokens em
    -- Source/Consolidacao/Mascara.h; atualizado pra bater com eles (o valor
    -- antigo usava tokens de uma versão anterior da especificação, nunca
    -- implementados).
    mascara_nomenclatura            TEXT NOT NULL DEFAULT '{codigo}-{seq:03}-{titulo}',
    -- Hierarquia de pastas do backup (item 5) — CSV de níveis, na ordem em
    -- que o operador montou. NULL = padrão (projeto,ano,tipo_midia,
    -- tipo_arquivo). Valores em Source/Consolidacao/Consolidacao.h.
    hierarquia_backup               TEXT,
    destino_local                   TEXT,
    vocabulario_assuntos_livre      INTEGER NOT NULL DEFAULT 1 CHECK (vocabulario_assuntos_livre IN (0, 1)),
    formato_padrao_captura          TEXT,
    isrc_registrante                TEXT,
    permitir_sobrescrever_master    INTEGER NOT NULL DEFAULT 0 CHECK (permitir_sobrescrever_master IN (0, 1)),
    aviso_sobrescrita_confirmado_em TEXT,
    perfil_conformidade             TEXT,
    criado_em                       TEXT NOT NULL,
    atualizado_em                   TEXT NOT NULL
);

-- Só pode haver um projeto por banco (a pasta inteira é o projeto).
CREATE TRIGGER IF NOT EXISTS projeto_linha_unica
BEFORE INSERT ON projeto
WHEN (SELECT COUNT(*) FROM projeto) >= 1
BEGIN
    SELECT RAISE(ABORT, 'projeto: já existe um projeto neste banco');
END;

-- ---------------------------------------------------------------------------
-- Destinos de entrega (local, NAS/SMB, S3, Google Drive — mesma interface, §12.1)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS destino (
    id              TEXT PRIMARY KEY,
    projeto_id      TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    tipo            TEXT NOT NULL CHECK (tipo IN ('local', 'nas_smb', 's3', 'google_drive')),
    nome            TEXT NOT NULL,
    -- Parâmetros de conexão (caminho, bucket, endpoint...). Nunca guarda segredo
    -- em texto puro: config referencia uma entrada no chaveiro do SO.
    config_json     TEXT NOT NULL DEFAULT '{}',
    criado_em       TEXT NOT NULL
);

-- ---------------------------------------------------------------------------
-- Assunto — vocabulário controlado do projeto (§10.1)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS assunto (
    id                  TEXT PRIMARY KEY,
    projeto_id          TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    termo               TEXT NOT NULL,
    termo_normalizado   TEXT NOT NULL, -- lower(trim(termo)), colapsa "Congado"/"congado"/"congada de Búzios" digitado de novo
    criado_em           TEXT NOT NULL,
    UNIQUE (projeto_id, termo_normalizado)
);

CREATE INDEX IF NOT EXISTS idx_assunto_projeto ON assunto(projeto_id);

-- ---------------------------------------------------------------------------
-- Item — o objeto real: o rolo, o LP, a sessão fotográfica, o release (§5.3)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS item (
    id              TEXT PRIMARY KEY,
    projeto_id      TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    -- NULL até o ingest do arquivo dar certo (§5, critério 6): o sequencial
    -- ALLNO-xxxxx é recurso escasso e permanente, não pode ser gasto por um
    -- item que falhou na leitura ou tinha tamanho zero. UNIQUE continua
    -- valendo porque o SQLite trata cada NULL como distinto.
    codigo_acervo   TEXT,
    titulo          TEXT NOT NULL,
    tipo_midia      TEXT, -- casa com o "tipo:" de uma definição YAML de ficha (§6); NULL = ainda não catalogado
                           -- (Reorientação completa §7.1: conteúdo aparece antes de qualquer classificação —
                           -- material sem ficha nenhuma é estado legítimo, nunca bloqueia a navegação)
    -- 'duplicata' (item 9, Acréscimos §5/§8.2): o conteúdo já existe no
    -- acervo (mesmo SHA-256), reconhecido em vez de reimportado — nunca
    -- copiado de novo, nunca um segundo asset pro mesmo arquivo.
    -- Estados de workflow do §4 ('novo' -> 'arquivado') mais os dois estados
    -- de ingest que não são etapa de curadoria: 'duplicata' (conteúdo já no
    -- acervo) e os legados de bancos anteriores, que continuam válidos pra
    -- projeto reaberto não virar erro de CHECK.
    estado          TEXT NOT NULL DEFAULT 'novo'
                    CHECK (estado IN ('novo', 'em_analise', 'catalogado', 'revisado', 'aprovado',
                                      'publicado', 'arquivado', 'duplicata',
                                      'nao_digitalizado', 'capturado', 'qc_ok', 'alerta')),
    notas_livres    TEXT,
    em_quarentena   INTEGER NOT NULL DEFAULT 0 CHECK (em_quarentena IN (0, 1)),
    criado_em       TEXT NOT NULL,
    atualizado_em   TEXT NOT NULL,
    UNIQUE (projeto_id, codigo_acervo)
);

CREATE INDEX IF NOT EXISTS idx_item_projeto ON item(projeto_id);
CREATE INDEX IF NOT EXISTS idx_item_tipo_midia ON item(tipo_midia);
CREATE INDEX IF NOT EXISTS idx_item_estado ON item(estado);
CREATE INDEX IF NOT EXISTS idx_item_quarentena_criado ON item(em_quarentena, criado_em);

-- ---------------------------------------------------------------------------
-- Tags — cada tag é armazenada separadamente para busca (§ metadata spec).
-- Múltiplas tags por item, cada uma como linha independente.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS item_tag (
    id       TEXT PRIMARY KEY,
    item_id  TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    tag      TEXT NOT NULL,
    UNIQUE (item_id, tag)
);

CREATE INDEX IF NOT EXISTS idx_item_tag_item ON item_tag(item_id);
CREATE INDEX IF NOT EXISTS idx_item_tag_tag ON item_tag(tag);

-- Assunto aplicado ao item inteiro, não só a um trecho (§10.3)
CREATE TABLE IF NOT EXISTS item_assunto (
    item_id     TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    assunto_id  TEXT NOT NULL REFERENCES assunto(id) ON DELETE CASCADE,
    autor       TEXT,
    criado_em   TEXT NOT NULL,
    PRIMARY KEY (item_id, assunto_id)
);

-- Relações entre itens: mesmo evento, mesma sessão, versão alternativa, parte de conjunto (§5.3)
CREATE TABLE IF NOT EXISTS item_relacao (
    id          TEXT PRIMARY KEY,
    item_id_a   TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    item_id_b   TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    tipo        TEXT NOT NULL CHECK (tipo IN ('mesmo_evento', 'mesma_sessao', 'versao_alternativa', 'parte_de_conjunto')),
    criado_em   TEXT NOT NULL,
    CHECK (item_id_a <> item_id_b)
);

CREATE INDEX IF NOT EXISTS idx_item_relacao_a ON item_relacao(item_id_a);
CREATE INDEX IF NOT EXISTS idx_item_relacao_b ON item_relacao(item_id_b);

-- ---------------------------------------------------------------------------
-- Ficha — valores dos campos definidos pelo YAML do tipo de mídia (§6)
-- Uma linha por campo (ou por campo dentro de um nível repetido, ex.: faixa).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS item_campo (
    id                  TEXT PRIMARY KEY,
    item_id             TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    nivel               TEXT NOT NULL DEFAULT 'raiz',      -- 'raiz' ou o nome do nível aninhado (ex.: 'faixa')
    nivel_indice         INTEGER NOT NULL DEFAULT 0,        -- posição dentro do nível repetido (ex.: número da faixa)
    campo_id            TEXT NOT NULL,                      -- casa com o "id:" do campo na definição YAML
    valor               TEXT,                               -- escalar em texto; tabela/lista_pessoas vai como JSON
    fonte               TEXT NOT NULL
                        CHECK (fonte IN ('humano', 'herdado_projeto', 'leitura_tecnica')),
    atualizado_em       TEXT NOT NULL,
    UNIQUE (item_id, nivel, nivel_indice, campo_id)
);

CREATE INDEX IF NOT EXISTS idx_item_campo_item ON item_campo(item_id);

-- ---------------------------------------------------------------------------
-- Histórico de alterações — autor e data (§5.3). Também é onde fica registrado
-- o instante em que uma sugestão de IA vira decisão humana (P3).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS item_historico (
    id                      TEXT PRIMARY KEY,
    item_id                 TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    tipo_evento             TEXT NOT NULL
                            CHECK (tipo_evento IN (
                                'criacao', 'edicao_campo', 'mudanca_estado',
                                'confirmacao_sugestao_ia', 'marcador', 'relacao'
                            )),
    campo_id                TEXT,           -- nulo quando o evento não é de um campo específico
    valor_anterior          TEXT,
    valor_novo              TEXT,
    -- Preenchidos só quando tipo_evento = 'confirmacao_sugestao_ia': o que a IA
    -- sugeriu no índice antes do operador confirmar e o valor migrar pro registro.
    modelo_origem           TEXT,
    modelo_origem_versao    TEXT,
    confianca_origem        REAL,
    autor                   TEXT NOT NULL,
    criado_em                TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_item_historico_item ON item_historico(item_id);

-- ---------------------------------------------------------------------------
-- Marcador — instante ou trecho, com assunto (§10.1, §10.4)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS marcador (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    tempo_inicio    REAL NOT NULL,  -- segundos
    tempo_fim       REAL,           -- nulo = marcador de ponto, preenchido = trecho
    titulo          TEXT,
    observacao      TEXT,
    autor           TEXT NOT NULL,
    criado_em       TEXT NOT NULL,
    CHECK (tempo_fim IS NULL OR tempo_fim >= tempo_inicio)
);

CREATE INDEX IF NOT EXISTS idx_marcador_item ON marcador(item_id);

CREATE TABLE IF NOT EXISTS marcador_assunto (
    marcador_id     TEXT NOT NULL REFERENCES marcador(id) ON DELETE CASCADE,
    assunto_id      TEXT NOT NULL REFERENCES assunto(id) ON DELETE CASCADE,
    PRIMARY KEY (marcador_id, assunto_id)
);

-- ---------------------------------------------------------------------------
-- Observação — campo Observações/Notes da ficha (item 9 dos acréscimos de
-- UI/catalogação). Várias por item, com autor e data. minutagem_ms é
-- inteiro (não texto) de propósito: dado que nasce texto e vira número
-- depois exige migração, e software de preservação evita migração
-- silenciosa de dado. marcador_id fica NULL até a timeline de áudio/vídeo
-- existir (fatia futura) — quando existir, a ligação passa a ser por ali
-- em vez de minutagem digitada à mão.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS item_observacao (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    texto           TEXT NOT NULL,
    autor           TEXT NOT NULL,
    criado_em       TEXT NOT NULL,
    minutagem_ms    INTEGER,        -- posição em milissegundos; NULL quando não se aplica
    marcador_id     TEXT REFERENCES marcador(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_item_observacao_item ON item_observacao(item_id);

-- ---------------------------------------------------------------------------
-- Arquivo — master, derivada, capa, verso, documento, stem (§5.4)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS arquivo (
    id                      TEXT PRIMARY KEY,
    item_id                 TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    vault_id                TEXT REFERENCES vault(id) ON DELETE SET NULL,
    caminho_relativo        TEXT NOT NULL,   -- relativo ao vault ou pasta do projeto (P5)
    -- Caminho absoluto de onde o arquivo foi ingerido (Reorientação completa
    -- §4/§5.1 — árvore ORIGEM): a estrutura de pastas de origem é metadado,
    -- nunca é achatada nem descartada. NULL só em bancos antigos reabertos
    -- antes deste campo existir (schema idempotente, sem migração formal
    -- ainda — ver Project::abrir). O ingest de hoje ainda COPIA o arquivo
    -- pra dentro do projeto (caminho_relativo) em vez de só referenciar no
    -- lugar como o §4.3 pede — esse é um gap conhecido, não resolvido nesta
    -- etapa; este campo existe pra a árvore Origem funcionar mesmo assim,
    -- preservando de onde cada arquivo veio.
    caminho_absoluto_origem TEXT,
    papel                   TEXT NOT NULL,   -- preservation_master | access_copy | capa_frente | capa_verso | encarte | documento | stem | foto_suporte ...
    eh_master               INTEGER NOT NULL DEFAULT 0 CHECK (eh_master IN (0, 1)),
    tamanho_bytes           INTEGER,

    checksum_md5            TEXT,
    checksum_sha256         TEXT,
    checksum_gerado_em      TEXT,
    checksum_verificado_em  TEXT,

    caracteristicas_tecnicas_json TEXT NOT NULL DEFAULT '{}', -- duração, sample rate, bit depth, codec, resolução, fps...

    -- Preenchido só quando este arquivo é derivada de outro (P1): o que foi
    -- aplicado, quando, por quem — nunca sobrescreve o master.
    derivada_de_arquivo_id  TEXT REFERENCES arquivo(id) ON DELETE SET NULL,
    receita_processamento_json TEXT,

    estado_sincronizacao    TEXT NOT NULL DEFAULT 'nao_sincronizado'
                            CHECK (estado_sincronizacao IN ('nao_sincronizado', 'sincronizando', 'sincronizado', 'erro')),
    estado_verificacao_nuvem TEXT NOT NULL DEFAULT 'nao_verificado'
                            CHECK (estado_verificacao_nuvem IN ('nao_verificado', 'verificado', 'divergente')),
    destino_id              TEXT REFERENCES destino(id) ON DELETE SET NULL,
    sincronizado_em         TEXT,
    verificado_em           TEXT,

    criado_em               TEXT NOT NULL,
    atualizado_em            TEXT NOT NULL,

    UNIQUE (item_id, caminho_relativo)
);

CREATE INDEX IF NOT EXISTS idx_arquivo_item ON arquivo(item_id);
CREATE INDEX IF NOT EXISTS idx_arquivo_checksum_sha256 ON arquivo(checksum_sha256);
CREATE INDEX IF NOT EXISTS idx_arquivo_caminho_absoluto_origem ON arquivo(caminho_absoluto_origem);
CREATE INDEX IF NOT EXISTS idx_arquivo_item_master ON arquivo(item_id, eh_master);
CREATE INDEX IF NOT EXISTS idx_arquivo_estado_sincronizacao ON arquivo(estado_sincronizacao);

-- ---------------------------------------------------------------------------
-- Sinalizador de reconciliação em curso (§8).
--
-- A trava de master abaixo existe pra impedir que um caminho de EDIÇÃO
-- reescreva a identidade de um master. A reconciliação de Vault não é
-- edição: ela não muda o arquivo, ela CONSTATA que o arquivo mudou de lugar
-- ou de conteúdo por fora do software, e o registro tem que refletir isso —
-- senão o catálogo aponta pra um caminho que não existe mais e o §8 fica
-- impossível de cumprir.
--
-- Como o SQLite não desliga trigger, a reconciliação declara o que está
-- fazendo inserindo uma linha aqui, e a trava consulta. É explícito e
-- auditável: qualquer UPDATE de master fora dessa janela continua barrado.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS reconciliacao_em_curso (
    marca INTEGER PRIMARY KEY CHECK (marca = 1)
);

-- ---------------------------------------------------------------------------
-- P1 embutido na estrutura: master é travado por padrão. Qualquer UPDATE que
-- mude caminho ou checksum de um arquivo eh_master=1 é abortado, a menos que
-- o projeto tenha permitir_sobrescrever_master=1 ou uma reconciliação de
-- Vault esteja em curso. Correção sempre vira derivada nova (INSERT com
-- derivada_de_arquivo_id preenchido).
-- ---------------------------------------------------------------------------
CREATE TRIGGER IF NOT EXISTS arquivo_master_travado
BEFORE UPDATE OF caminho_relativo, checksum_md5, checksum_sha256 ON arquivo
WHEN OLD.eh_master = 1
 AND (NEW.caminho_relativo <> OLD.caminho_relativo
      OR IFNULL(NEW.checksum_md5, '') <> IFNULL(OLD.checksum_md5, '')
      OR IFNULL(NEW.checksum_sha256, '') <> IFNULL(OLD.checksum_sha256, ''))
 AND (SELECT permitir_sobrescrever_master FROM projeto WHERE id = (SELECT projeto_id FROM item WHERE id = OLD.item_id)) = 0
 AND (SELECT COUNT(*) FROM reconciliacao_em_curso) = 0
BEGIN
    SELECT RAISE(ABORT, 'arquivo: master travado — ligue "permitir sobrescrever master" no projeto para editar, ou gere uma derivada');
END;

-- Um master nunca pode ser marcado como derivada de outro arquivo.
CREATE TRIGGER IF NOT EXISTS arquivo_master_sem_derivacao
BEFORE INSERT ON arquivo
WHEN NEW.eh_master = 1 AND NEW.derivada_de_arquivo_id IS NOT NULL
BEGIN
    SELECT RAISE(ABORT, 'arquivo: master não pode ter derivada_de_arquivo_id');
END;

-- ---------------------------------------------------------------------------
-- Acervo (Reorientação completa §5) — a estrutura VIRTUAL que o operador
-- monta, independente da estrutura real em disco (Origem, que não tem
-- tabela própria: é derivada de arquivo.caminho_absoluto_origem, §5.1).
-- Puro planejamento — nenhuma linha aqui move, copia ou renomeia nada em
-- disco (§5.3); a consolidação (§6, item 10) é que materializa isso.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS acervo_pasta (
    id              TEXT PRIMARY KEY,
    projeto_id      TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    pasta_pai_id    TEXT REFERENCES acervo_pasta(id) ON DELETE CASCADE, -- NULL = pasta de topo
    nome            TEXT NOT NULL,
    ordem           INTEGER NOT NULL DEFAULT 0,
    -- Máscara de nomenclatura por pasta (§5.6) — herdada da pai quando NULL;
    -- resolvida em tempo de consolidação, não gravada aqui.
    mascara_nomenclatura TEXT,
    criado_em       TEXT NOT NULL,
    atualizado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_acervo_pasta_projeto ON acervo_pasta(projeto_id);
CREATE INDEX IF NOT EXISTS idx_acervo_pasta_pai ON acervo_pasta(pasta_pai_id);

-- Item <-> pasta do acervo. Muitos-para-muitos de propósito: material pode
-- estar em mais de uma pasta (§5.4) — a consolidação avisa da duplicação,
-- nunca proíbe aqui. Item sem nenhuma linha aqui aparece em "Não
-- organizados" (§5.5), calculado por ausência, não guardado.
CREATE TABLE IF NOT EXISTS acervo_item_pasta (
    id          TEXT PRIMARY KEY,
    item_id     TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    pasta_id    TEXT NOT NULL REFERENCES acervo_pasta(id) ON DELETE CASCADE,
    criado_em   TEXT NOT NULL,
    UNIQUE (item_id, pasta_id)
);

CREATE INDEX IF NOT EXISTS idx_acervo_item_pasta_item ON acervo_item_pasta(item_id);
CREATE INDEX IF NOT EXISTS idx_acervo_item_pasta_pasta ON acervo_item_pasta(pasta_id);

-- ---------------------------------------------------------------------------
-- Coleção inteligente (Acréscimos §10.2 — "salvar busca como coleção
-- inteligente, que se atualiza sozinha"): guarda a DEFINIÇÃO da busca
-- (texto + filtros), nunca um resultado. Reexecutada do zero toda vez que é
-- aberta — por isso não há tabela de membros, só os parâmetros.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS colecao_inteligente (
    id                  TEXT PRIMARY KEY,
    projeto_id          TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    nome                TEXT NOT NULL,
    busca_texto         TEXT,
    filtros_tipo_midia  TEXT, -- CSV de tipo_midia — vazio/NULL = todos
    filtros_estado      TEXT, -- CSV de estado — vazio/NULL = todos
    filtros_extensao    TEXT, -- CSV de extensão de arquivo — vazio/NULL = todas
    filtros_origem      TEXT, -- CSV de origem (Digital/Analógico) — vazio/NULL = todas
    ano_de              INTEGER, -- faixa de ano; NULL nos dois = sem faixa
    ano_ate             INTEGER,
    ordem               INTEGER NOT NULL DEFAULT 0,
    criado_em           TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_colecao_inteligente_projeto ON colecao_inteligente(projeto_id);

-- ---------------------------------------------------------------------------
-- Localização conhecida (item 9 — continuous ingestion, Acréscimos §5/§8.2):
-- "um asset, muitas localizações" — quando o mesmo conteúdo (SHA-256 igual)
-- é visto de novo em outro caminho, não vira um segundo item nem uma
-- segunda cópia física; só amplia onde esse arquivo já foi encontrado.
-- Escopo desta etapa: dedup exato por checksum. Near-duplicate (pHash/
-- Chromaprint) e detecção automática de fonte reconectada NÃO estão aqui —
-- dependem do modelo de fingerprint/asset completo (§4/§8), não construído
-- ainda (gap já declarado no relatório do item 7).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS localizacao_conhecida (
    id                  TEXT PRIMARY KEY,
    arquivo_id          TEXT NOT NULL REFERENCES arquivo(id) ON DELETE CASCADE,
    caminho_absoluto    TEXT NOT NULL,
    criado_em           TEXT NOT NULL,
    UNIQUE (arquivo_id, caminho_absoluto)
);

CREATE INDEX IF NOT EXISTS idx_localizacao_conhecida_arquivo ON localizacao_conhecida(arquivo_id);

-- ---------------------------------------------------------------------------
-- Consolidação (item 10, §11.7) — rastro do que já foi copiado pra
-- `consolidado/`, pra a consolidação seguinte ser incremental (só copia o
-- que é novo ou mudou, "não toca no que não mudou"). Uma linha por
-- combinação (item, pasta do acervo, arquivo) — o mesmo item em duas pastas
-- (§5.4) gera duas linhas, duas cópias físicas, de propósito.
-- Escopo desta etapa, declarado: metadado embutido (BWF/bext, iXML, EXIF
-- write, ID3, XMP) NÃO é gravado na cópia — precisa de escritores por
-- formato que não existem ainda. O checksum aqui é da cópia renomeada tal
-- como saiu, sem nenhum metadado embutido além do que já veio na origem.
-- Destino também restrito a pasta local nesta etapa; NAS/FTP/S3/nuvem são
-- os mesmos "destino" já previstos na tabela `destino`, mas a consolidação
-- em si só sabe escrever em disco local por enquanto.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS consolidacao_registro (
    id                      TEXT PRIMARY KEY,
    item_id                 TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    pasta_id                TEXT NOT NULL DEFAULT '',
    arquivo_id              TEXT NOT NULL REFERENCES arquivo(id) ON DELETE CASCADE,
    caminho_relativo_destino TEXT NOT NULL, -- relativo à raiz de destino escolhida, ex.: "consolidado/01 Fitas/ACR-001.wav"
    checksum_sha256         TEXT NOT NULL,   -- da CÓPIA consolidada (sem embedding), verificado depois de copiar
    consolidado_em          TEXT NOT NULL,
    UNIQUE (item_id, pasta_id, arquivo_id)
);

CREATE INDEX IF NOT EXISTS idx_consolidacao_registro_arquivo ON consolidacao_registro(arquivo_id);

-- ---------------------------------------------------------------------------
-- Vault (Cofre de Preservação)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS vault (
    id            TEXT PRIMARY KEY,
    projeto_id    TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    nome          TEXT NOT NULL,
    tipo          TEXT NOT NULL CHECK (tipo IN ('local','optico','lto','rede','nuvem_sync')),
    uuid_volume   TEXT,
    raiz_relativa TEXT,
    localizacao   TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'offline' CHECK (status IN ('online','offline')),
    visto_em      TEXT,
    criado_em     TEXT NOT NULL,
    verificado_em TEXT
);

-- ---------------------------------------------------------------------------
-- Cache de Arquivo (Visualização Offline)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cache_arquivo (
    arquivo_id    TEXT PRIMARY KEY REFERENCES arquivo(id) ON DELETE CASCADE,
    miniatura     BLOB,
    forma_onda    BLOB,
    lufs_i        REAL,
    lra           REAL,
    true_peak     REAL,
    peak_amostra  REAL,
    correlacao_media REAL,
    calculado_em  TEXT NOT NULL,
    versao_analise INTEGER NOT NULL DEFAULT 1
);

-- ---------------------------------------------------------------------------
-- Proveniência Append-Only
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS proveniencia (
    id          TEXT PRIMARY KEY,
    item_id     TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    evento      TEXT NOT NULL,
    autor       TEXT NOT NULL,
    detalhes    TEXT,
    criado_em   TEXT NOT NULL
);

-- ---------------------------------------------------------------------------
-- Publicação
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS publicacao (
    id            TEXT PRIMARY KEY,
    projeto_id    TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    vault_id      TEXT NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    autor         TEXT NOT NULL,
    layout_pasta  TEXT NOT NULL,
    criada_em     TEXT NOT NULL,
    status        TEXT NOT NULL CHECK (status IN ('planejada','concluida','falha')),
    nota          TEXT
);

CREATE TABLE IF NOT EXISTS item_publicacao (
    item_id           TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    publicacao_id     TEXT NOT NULL REFERENCES publicacao(id) ON DELETE CASCADE,
    caminho_relativo  TEXT NOT NULL,
    checksum_validado TEXT NOT NULL,
    verificado_em     TEXT NOT NULL,
    PRIMARY KEY (item_id, publicacao_id)
);

-- ---------------------------------------------------------------------------
-- Tipo de Marcador (Lookup)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS tipo_marcador (
    id     TEXT PRIMARY KEY,
    rotulo TEXT NOT NULL,
    cor    TEXT NOT NULL,
    embutido INTEGER NOT NULL DEFAULT 0
);

-- ---------------------------------------------------------------------------
-- Entidades Ricas
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS entidade (
    id         TEXT PRIMARY KEY,
    projeto_id TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,
    tipo       TEXT NOT NULL CHECK (tipo IN ('pessoa','lugar','evento','organizacao')),
    nome       TEXT NOT NULL,
    nota       TEXT,
    criado_em  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS item_entidade (
    item_id     TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    entidade_id TEXT NOT NULL REFERENCES entidade(id) ON DELETE CASCADE,
    papel       TEXT,
    PRIMARY KEY (item_id, entidade_id, papel)
);

-- ---------------------------------------------------------------------------
-- AI Scan — resultados persistidos de análise contextual com IA (Gemini).
-- Armazenados no registro (não no índice) porque são decisão do operador
-- (ele escolheu executar o scan) e precisam sobreviver a rebuild do índice.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ai_scan_resultado (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    modelo          TEXT NOT NULL,       -- e.g. "gemini-2.0-flash"
    -- 'visual' | 'audio' | 'video' | 'documento'. Sem CHECK: a lista de tipos
    -- analisáveis cresce junto com a API, e um CHECK aqui transforma cada tipo
    -- novo numa exceção em tempo de execução no meio de um scan longo.
    tipo_analise    TEXT NOT NULL,
    contexto_json   TEXT NOT NULL,       -- JSON array of detected concepts/tags
    resumo          TEXT,                -- human-readable summary
    confianca       REAL,               -- 0.0-1.0
    analisado_em    TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_ai_scan_resultado_item ON ai_scan_resultado(item_id);

-- FTS index for AI scan results — enables contextual search ("bicycle", "beach", etc.)
CREATE TRIGGER IF NOT EXISTS trg_ai_scan_busca_insert AFTER INSERT ON ai_scan_resultado FOR EACH ROW BEGIN
    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.resumo);
END;

CREATE TRIGGER IF NOT EXISTS trg_ai_scan_busca_delete AFTER DELETE ON ai_scan_resultado FOR EACH ROW BEGIN
    DELETE FROM busca_fts WHERE item_id = old.item_id AND conteudo = old.resumo;
END;

-- ---------------------------------------------------------------------------
-- Busca Textual Spotlight (FTS5)
-- ---------------------------------------------------------------------------
CREATE VIRTUAL TABLE IF NOT EXISTS busca_fts USING fts5(
    item_id UNINDEXED,
    conteudo,
    tokenize='unicode61'
);

-- Triggers para manter busca_fts em sincronia
-- codigo_acervo é NULL enquanto o ingest não confirma (§5): indexar o NULL
-- encheria a FTS de linhas sem conteúdo, então cada trigger filtra.
CREATE TRIGGER IF NOT EXISTS trg_item_busca_insert AFTER INSERT ON item FOR EACH ROW BEGIN
    INSERT INTO busca_fts(item_id, conteudo) SELECT new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL;
    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.id, new.titulo);
END;

CREATE TRIGGER IF NOT EXISTS trg_item_busca_delete AFTER DELETE ON item FOR EACH ROW BEGIN
    DELETE FROM busca_fts WHERE item_id = old.id;
END;

CREATE TRIGGER IF NOT EXISTS trg_item_busca_update AFTER UPDATE ON item FOR EACH ROW BEGIN
    DELETE FROM busca_fts WHERE item_id = old.id
      AND (conteudo = IFNULL(old.codigo_acervo, '') OR conteudo = old.titulo);
    INSERT INTO busca_fts(item_id, conteudo) SELECT new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL;
    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.id, new.titulo);
END;

CREATE TRIGGER IF NOT EXISTS trg_item_campo_busca_insert AFTER INSERT ON item_campo FOR EACH ROW BEGIN
    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.valor);
END;

CREATE TRIGGER IF NOT EXISTS trg_item_campo_busca_delete AFTER DELETE ON item_campo FOR EACH ROW BEGIN
    DELETE FROM busca_fts WHERE item_id = old.item_id AND conteudo = old.valor;
END;

CREATE TRIGGER IF NOT EXISTS trg_item_campo_busca_update AFTER UPDATE ON item_campo FOR EACH ROW BEGIN
    DELETE FROM busca_fts WHERE item_id = old.item_id AND conteudo = old.valor;
    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.valor);
END;

CREATE TRIGGER IF NOT EXISTS trg_item_assunto_busca_insert AFTER INSERT ON item_assunto FOR EACH ROW BEGIN
    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, (SELECT termo FROM assunto WHERE id = new.assunto_id));
END;

CREATE TRIGGER IF NOT EXISTS trg_item_assunto_busca_delete AFTER DELETE ON item_assunto FOR EACH ROW BEGIN
    DELETE FROM busca_fts WHERE item_id = old.item_id AND conteudo = (SELECT termo FROM assunto WHERE id = old.assunto_id);
END;

-- ---------------------------------------------------------------------------
-- Coleções Inteligentes embutidas (§10) — VIEWS, não tabelas.
--
-- Uma view não guarda resultado: cada SELECT reexecuta a pergunta sobre o
-- estado atual do catálogo. É o que faz "Ausentes" ficar certo no instante em
-- que a reconciliação de Vault (§8) marca um arquivo como ausente, sem
-- ninguém precisar reindexar nada.
--
-- Todas expõem a MESMA forma — (item_id, colecao) — pra a árvore da esquerda
-- listar qualquer uma sem saber o que cada uma pergunta.
-- ---------------------------------------------------------------------------

-- Clipping: True Peak acima de -0.1 dBTP. Vem do cache de análise, calculado
-- na ingestão (I2) — nunca medido na hora de abrir a coleção.
CREATE VIEW IF NOT EXISTS colecao_clipping AS
SELECT DISTINCT a.item_id AS item_id, 'clipping' AS colecao
FROM cache_arquivo c
JOIN arquivo a ON a.id = c.arquivo_id
WHERE c.true_peak IS NOT NULL AND c.true_peak > -0.1;

-- Ausentes: o arquivo não está mais onde foi catalogado. A ficha continua
-- inteira (§8) — é o item que precisa ser reencontrado, não recadastrado.
CREATE VIEW IF NOT EXISTS colecao_ausentes AS
SELECT DISTINCT a.item_id AS item_id, 'ausentes' AS colecao
FROM arquivo a
WHERE a.estado_presenca IN ('ausente', 'corrompido');

-- Não baixados: placeholder de iCloud/Dropbox registrado sem forçar o
-- download dos bytes (critério 12).
CREATE VIEW IF NOT EXISTS colecao_nao_baixados AS
SELECT DISTINCT a.item_id AS item_id, 'nao_baixados' AS colecao
FROM arquivo a
WHERE a.estado_presenca = 'nao_baixado';

-- Incompletos: sem título, sem artista ou sem ISRC. O título vive em `item`;
-- artista e ISRC são campos de ficha, então a checagem é por ausência de
-- linha em item_campo (ou linha vazia).
CREATE VIEW IF NOT EXISTS colecao_incompletos AS
SELECT i.id AS item_id, 'incompletos' AS colecao
FROM item i
WHERE i.titulo IS NULL OR TRIM(i.titulo) = ''
   OR NOT EXISTS (SELECT 1 FROM item_campo c WHERE c.item_id = i.id
                   AND c.campo_id IN ('artista_principal', 'artista') AND TRIM(IFNULL(c.valor, '')) <> '')
   OR NOT EXISTS (SELECT 1 FROM item_campo c WHERE c.item_id = i.id
                   AND c.campo_id = 'isrc' AND TRIM(IFNULL(c.valor, '')) <> '');

-- Vulneráveis: existe um único lugar no mundo com esse conteúdo. Item que
-- nunca entrou numa publicação concluída não tem cópia de preservação
-- nenhuma — é o que a coleção existe pra tornar visível antes de ser tarde.
CREATE VIEW IF NOT EXISTS colecao_vulneraveis AS
SELECT i.id AS item_id, 'vulneraveis' AS colecao
FROM item i
WHERE NOT EXISTS (
    SELECT 1 FROM item_publicacao ip
    JOIN publicacao p ON p.id = ip.publicacao_id
    WHERE ip.item_id = i.id AND p.status = 'concluida');

-- Revisão: tem marcador aberto do tipo "revisar" ou "dropout".
CREATE VIEW IF NOT EXISTS colecao_revisao AS
SELECT DISTINCT m.item_id AS item_id, 'revisao' AS colecao
FROM marcador m
WHERE IFNULL(m.status, 'aberto') = 'aberto'
  AND (m.tipo_id IN ('revisar', 'dropout'));

-- União de todas, pra a UI consultar um lugar só.
CREATE VIEW IF NOT EXISTS colecao_embutida AS
SELECT item_id, colecao FROM colecao_clipping
UNION ALL SELECT item_id, colecao FROM colecao_ausentes
UNION ALL SELECT item_id, colecao FROM colecao_nao_baixados
UNION ALL SELECT item_id, colecao FROM colecao_incompletos
UNION ALL SELECT item_id, colecao FROM colecao_vulneraveis
UNION ALL SELECT item_id, colecao FROM colecao_revisao;

-- ---------------------------------------------------------------------------
-- Camada de Preservação Digital (OAIS / PREMIS / FAIR)
-- ---------------------------------------------------------------------------
-- persistent_id é adicionado à tabela item via garantirColuna em Project.cpp
-- (ALTER TABLE item ADD COLUMN persistent_id TEXT) — não aqui, pois a tabela
-- item já existe em bancos anteriores e CREATE TABLE IF NOT EXISTS não altera
-- colunas existentes. A coluna é o identificador apresentável/interoperável
-- (ex.: "BKR:ASSET:8F72A91C") que coexiste com item.id (UUID técnico interno).

-- Agentes de preservação — quem ou o que executou cada ação
CREATE TABLE IF NOT EXISTS preservation_agent (
    id            TEXT PRIMARY KEY,
    nome          TEXT NOT NULL,
    tipo          TEXT NOT NULL CHECK (tipo IN ('software','hardware','pessoa','organizacao')),
    versao        TEXT,
    identificador TEXT,
    criado_em     TEXT NOT NULL
);

-- Unique por nome+versão para evitar duplicação de agentes built-in
CREATE UNIQUE INDEX IF NOT EXISTS idx_preservation_agent_nome_versao
    ON preservation_agent(nome, IFNULL(versao, ''));

-- Eventos PREMIS — append-only, jamais apagado ou alterado
CREATE TABLE IF NOT EXISTS preservation_event (
    id                   TEXT PRIMARY KEY,
    arquivo_id           TEXT REFERENCES arquivo(id) ON DELETE CASCADE,
    item_id              TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    event_type           TEXT NOT NULL,
    event_date_time      TEXT NOT NULL,
    event_detail         TEXT,
    event_outcome        TEXT NOT NULL CHECK (event_outcome IN ('SUCCESS','FAILURE','WARNING')),
    event_outcome_detail TEXT,
    agent_id             TEXT REFERENCES preservation_agent(id) ON DELETE SET NULL,
    criado_em            TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_preservation_event_item
    ON preservation_event(item_id);
CREATE INDEX IF NOT EXISTS idx_preservation_event_type
    ON preservation_event(event_type);
CREATE INDEX IF NOT EXISTS idx_preservation_event_outcome
    ON preservation_event(item_id, event_type, event_outcome);

-- Direitos — uma linha por item (upsert via salvarDireitos)
CREATE TABLE IF NOT EXISTS preservation_right (
    id            TEXT PRIMARY KEY,
    item_id       TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,
    rights_status TEXT NOT NULL DEFAULT 'UNKNOWN'
                  CHECK (rights_status IN ('PUBLIC_DOMAIN','COPYRIGHT','LICENSED','UNKNOWN','RESTRICTED')),
    rights_holder TEXT,
    license       TEXT,
    usage_notes   TEXT,
    restrictions  TEXT,
    criado_em     TEXT NOT NULL,
    atualizado_em TEXT NOT NULL,
    UNIQUE (item_id)
);

CREATE INDEX IF NOT EXISTS idx_preservation_right_item
    ON preservation_right(item_id);

-- View de Preservation Status calculado — nunca digitado manualmente.
--
-- backup_status = OK somente quando existe preservation_event BACKUP_VERIFIED
-- com outcome = SUCCESS. Presença em consolidacao_registro sem evento PREMIS
-- gera WARNING (backup antigo existente mas não formalmente atestado).
--
-- fixity_status = OK somente quando existe FIXITY_CHECK com SUCCESS.
-- Ter apenas checksum_sha256 calculado no ingest (sem verificação posterior)
-- gera WARNING — estado honesto: "temos hash, mas nunca reconfirmamos".
CREATE VIEW IF NOT EXISTS asset_preservation_status AS
SELECT
    i.id            AS item_id,
    i.persistent_id,
    CASE WHEN i.persistent_id IS NOT NULL THEN 'OK' ELSE 'UNKNOWN' END
        AS identity_status,
    CASE
        WHEN EXISTS (
            SELECT 1 FROM arquivo a
            WHERE a.item_id = i.id
              AND a.checksum_sha256 IS NOT NULL
              AND a.estado_presenca = 'corrompido')
            THEN 'CRITICAL'
        WHEN EXISTS (
            SELECT 1 FROM preservation_event pe
            WHERE pe.item_id = i.id
              AND pe.event_type = 'FIXITY_CHECK'
              AND pe.event_outcome = 'SUCCESS')
            THEN 'OK'
        WHEN EXISTS (
            SELECT 1 FROM arquivo a
            WHERE a.item_id = i.id AND a.checksum_sha256 IS NOT NULL)
            THEN 'WARNING'
        ELSE 'UNKNOWN'
    END AS fixity_status,
    CASE
        WHEN EXISTS (
            SELECT 1 FROM arquivo a
            WHERE a.item_id = i.id
              AND a.caracteristicas_tecnicas_json IS NOT NULL
              AND a.caracteristicas_tecnicas_json <> '{}')
            THEN 'OK'
        ELSE 'UNKNOWN'
    END AS format_status,
    CASE
        WHEN EXISTS (
            SELECT 1 FROM preservation_event pe
            WHERE pe.item_id = i.id
              AND pe.event_type = 'BACKUP_VERIFIED'
              AND pe.event_outcome = 'SUCCESS')
            THEN 'OK'
        WHEN EXISTS (
            SELECT 1 FROM consolidacao_registro cr WHERE cr.item_id = i.id)
            THEN 'WARNING'
        ELSE 'WARNING'
    END AS backup_status,
    CASE
        WHEN EXISTS (
            SELECT 1 FROM preservation_right pr
            WHERE pr.item_id = i.id AND pr.rights_status <> 'UNKNOWN')
            THEN 'OK'
        WHEN EXISTS (
            SELECT 1 FROM preservation_right pr WHERE pr.item_id = i.id)
            THEN 'WARNING'
        ELSE 'UNKNOWN'
    END AS rights_status,
    CASE
        WHEN EXISTS (
            SELECT 1 FROM preservation_event pe
            WHERE pe.item_id = i.id AND pe.event_type = 'INGEST')
            THEN 'OK'
        ELSE 'UNKNOWN'
    END AS provenance_status
FROM item i;

-- ---------------------------------------------------------------------------
-- Asset Geolocation — Dado Estruturado de Localização (§14)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS asset_geolocation (
    asset_id            TEXT PRIMARY KEY REFERENCES item(id) ON DELETE CASCADE,
    latitude            REAL,
    longitude           REAL,
    altitude            REAL,
    continent           TEXT,
    country             TEXT,
    country_code        TEXT,
    state_province      TEXT,
    state_code          TEXT,
    city                TEXT,
    municipality        TEXT,
    neighborhood        TEXT,
    district            TEXT,
    postal_code         TEXT,
    street              TEXT,
    street_number       TEXT,
    locality            TEXT,
    formatted_address   TEXT,
    source              TEXT NOT NULL DEFAULT 'NONE',
    precision_accuracy  REAL,
    confidence          REAL,
    created_at          TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_geo_lat_lng ON asset_geolocation(latitude, longitude);
CREATE INDEX IF NOT EXISTS idx_geo_country_state_city ON asset_geolocation(country, state_province, city);

