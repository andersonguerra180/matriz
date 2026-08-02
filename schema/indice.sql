-- MATRIZ — banco de índice (indice.sqlite)
--
-- Camada paralela e descartável (P2). Tudo aqui pode ser apagado e
-- reconstruído a qualquer momento sem perda de informação catalográfica —
-- nenhuma tabela deste arquivo é referenciada por FOREIGN KEY a partir de
-- registro.sqlite, e o inverso (item_id, arquivo_id) é só uma referência
-- solta em TEXT, resolvida em tempo de consulta pela aplicação.
--
-- Em todo registro gerado por modelo: modelo, versão e data de processamento
-- (P3). Nada aqui é "decisão" — é sugestão até o operador confirmar, e nesse
-- momento a aplicação copia o valor para item_campo em registro.sqlite.

PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS schema_info (
    chave   TEXT PRIMARY KEY,
    valor   TEXT NOT NULL
);
INSERT OR IGNORE INTO schema_info (chave, valor) VALUES ('versao_schema', '1');

-- ---------------------------------------------------------------------------
-- Fila de processamento — retomável, com progresso por item (§7.2 estágio 3)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS fila_processamento (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT,
    tipo_tarefa     TEXT NOT NULL, -- 'embedding_clip' | 'transcricao' | 'ocr' | 'rosto' | 'fingerprint' | 'phash' | 'bpm_tonalidade' | 'qualidade_imagem' | 'corte_cena' | 'diarizacao'
    status          TEXT NOT NULL DEFAULT 'pendente' CHECK (status IN ('pendente', 'em_progresso', 'concluido', 'erro')),
    progresso       REAL NOT NULL DEFAULT 0 CHECK (progresso >= 0 AND progresso <= 1),
    erro_mensagem   TEXT,
    iniciado_em     TEXT,
    concluido_em    TEXT,
    criado_em       TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_fila_status ON fila_processamento(status);
CREATE INDEX IF NOT EXISTS idx_fila_item ON fila_processamento(item_id);

-- ---------------------------------------------------------------------------
-- Miniatura e keyframe — miniatura de imagem/vídeo e faixa de keyframes do
-- vídeo na tira de diagnóstico (§7.2 estágio 1, §11.3). caminho_relativo é
-- relativo à pasta do projeto, dentro de .indice-cache/ — puro cache visual,
-- reconstruível a qualquer momento a partir do arquivo original.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS miniatura (
    id                  TEXT PRIMARY KEY,
    item_id             TEXT NOT NULL,
    arquivo_id          TEXT NOT NULL,
    tipo                TEXT NOT NULL CHECK (tipo IN ('miniatura', 'keyframe')),
    tempo_ref           REAL,           -- instante do frame; só para tipo='keyframe'
    caminho_relativo    TEXT NOT NULL,
    largura             INTEGER,
    altura              INTEGER,
    gerado_em           TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_miniatura_arquivo ON miniatura(arquivo_id);

-- ---------------------------------------------------------------------------
-- Forma de onda — peaks pré-calculados pra faixa "Nível" da tira de
-- diagnóstico (§11.3). Downmix mono (a fase L/R é QC de captura, Etapa 5 —
-- escopo diferente). peaks é BLOB de pares float32 [min,max] por bucket.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS forma_onda (
    id                  TEXT PRIMARY KEY,
    item_id             TEXT NOT NULL,
    arquivo_id          TEXT NOT NULL UNIQUE,
    duracao_segundos    REAL NOT NULL,
    buckets_por_segundo REAL NOT NULL,
    peaks               BLOB NOT NULL,
    gerado_em           TEXT NOT NULL
);

-- ---------------------------------------------------------------------------
-- Embeddings — CLIP de imagem/keyframe e de texto, espaço único de busca (§9.1)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS embedding (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT,
    tipo            TEXT NOT NULL CHECK (tipo IN ('imagem', 'keyframe', 'texto')),
    tempo_ref       REAL,           -- instante do keyframe, quando aplicável
    vetor           BLOB NOT NULL,  -- float32 empacotado
    dimensao        INTEGER NOT NULL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_embedding_item ON embedding(item_id);
CREATE INDEX IF NOT EXISTS idx_embedding_tipo ON embedding(tipo);

-- ---------------------------------------------------------------------------
-- Rostos e vozes — reconhecimento opt-in por projeto (§9.3, LGPD)
-- "pessoa" é o agrupamento (cluster) que liga rosto recorrente a voz recorrente (§9.2)
-- Vem antes de transcrição porque transcricao_palavra.falante_id referencia voz.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS pessoa (
    id              TEXT PRIMARY KEY,
    projeto_id      TEXT NOT NULL,
    nome            TEXT,           -- preenchido pelo operador ao identificar o cluster; nulo até lá
    criado_em       TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS rosto (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    tempo_ref       REAL,           -- instante do frame, em vídeo
    regiao_json     TEXT NOT NULL,  -- bbox
    vetor           BLOB NOT NULL,
    dimensao        INTEGER NOT NULL,
    pessoa_id       TEXT REFERENCES pessoa(id) ON DELETE SET NULL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_rosto_item ON rosto(item_id);
CREATE INDEX IF NOT EXISTS idx_rosto_pessoa ON rosto(pessoa_id);

CREATE TABLE IF NOT EXISTS voz (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    tempo_inicio    REAL NOT NULL,
    tempo_fim       REAL NOT NULL,
    vetor           BLOB NOT NULL,
    dimensao        INTEGER NOT NULL,
    pessoa_id       TEXT REFERENCES pessoa(id) ON DELETE SET NULL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_voz_item ON voz(item_id);
CREATE INDEX IF NOT EXISTS idx_voz_pessoa ON voz(pessoa_id);

-- ---------------------------------------------------------------------------
-- Transcrição — timestamp por palavra, marca de verificado (§9.3)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS transcricao (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    idioma          TEXT,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL,
    verificado      INTEGER NOT NULL DEFAULT 0 CHECK (verificado IN (0, 1)),
    verificado_por  TEXT,
    verificado_em   TEXT
);

CREATE INDEX IF NOT EXISTS idx_transcricao_item ON transcricao(item_id);

CREATE TABLE IF NOT EXISTS transcricao_palavra (
    id              TEXT PRIMARY KEY,
    transcricao_id  TEXT NOT NULL REFERENCES transcricao(id) ON DELETE CASCADE,
    texto           TEXT NOT NULL,
    tempo_inicio    REAL NOT NULL,
    tempo_fim       REAL NOT NULL,
    falante_id      TEXT REFERENCES voz(id) ON DELETE SET NULL, -- diarização (pyannote)
    confianca       REAL
);

CREATE INDEX IF NOT EXISTS idx_transcricao_palavra_transcricao ON transcricao_palavra(transcricao_id);

-- ---------------------------------------------------------------------------
-- OCR (§9)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ocr (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    texto           TEXT NOT NULL,
    regiao_json     TEXT, -- bbox na imagem/página, quando aplicável
    confianca       REAL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_ocr_item ON ocr(item_id);

-- ---------------------------------------------------------------------------
-- Fingerprint de áudio e pHash de imagem — duplicata (§7.4, §9)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS fingerprint_audio (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    algoritmo       TEXT NOT NULL,
    hash            TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_fingerprint_hash ON fingerprint_audio(hash);

CREATE TABLE IF NOT EXISTS phash_imagem (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    hash            TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_phash_hash ON phash_imagem(hash);

CREATE TABLE IF NOT EXISTS duplicata (
    id              TEXT PRIMARY KEY,
    tipo            TEXT NOT NULL CHECK (tipo IN ('phash', 'fingerprint', 'lossy_transcodificado')),
    item_id_a       TEXT NOT NULL,
    arquivo_id_a    TEXT NOT NULL,
    item_id_b       TEXT NOT NULL,
    arquivo_id_b    TEXT NOT NULL,
    similaridade    REAL NOT NULL,
    detectado_em    TEXT NOT NULL
);

-- ---------------------------------------------------------------------------
-- Qualidade de imagem e cortes de cena (§9)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS qualidade_imagem (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    blur_score      REAL,
    exposicao_score REAL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS corte_cena (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    arquivo_id      TEXT NOT NULL,
    tempo           REAL NOT NULL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_corte_cena_arquivo ON corte_cena(arquivo_id);

-- ---------------------------------------------------------------------------
-- Sugestão de campo — ponte genérica entre modelo e ficha (P3).
-- Cobre natureza do conteúdo, categoria de sample, bpm, tonalidade, tipo
-- temporal e qualquer outro campo YAML com "sugerido_por" ou "preenchido_por"
-- um modelo. Ao confirmar, a aplicação grava em registro.item_campo
-- (fonte='humano') e registro.item_historico (tipo_evento='confirmacao_sugestao_ia'),
-- e marca confirmado=1 aqui — o registro índice não é apagado, fica como
-- rastro de auditoria descartável.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS sugestao_campo (
    id              TEXT PRIMARY KEY,
    item_id         TEXT NOT NULL,
    nivel           TEXT NOT NULL DEFAULT 'raiz',
    nivel_indice     INTEGER NOT NULL DEFAULT 0,
    campo_id        TEXT NOT NULL,
    valor           TEXT NOT NULL,
    confianca       REAL,
    modelo          TEXT NOT NULL,
    modelo_versao   TEXT NOT NULL,
    processado_em   TEXT NOT NULL,
    confirmado      INTEGER NOT NULL DEFAULT 0 CHECK (confirmado IN (0, 1)),
    confirmado_por  TEXT,
    confirmado_em   TEXT
);

CREATE INDEX IF NOT EXISTS idx_sugestao_campo_item ON sugestao_campo(item_id);
CREATE INDEX IF NOT EXISTS idx_sugestao_campo_pendente ON sugestao_campo(item_id, confirmado);
