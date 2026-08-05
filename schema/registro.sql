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
    codigo_acervo   TEXT NOT NULL,
    titulo          TEXT NOT NULL,
    tipo_midia      TEXT, -- casa com o "tipo:" de uma definição YAML de ficha (§6); NULL = ainda não catalogado
                           -- (Reorientação completa §7.1: conteúdo aparece antes de qualquer classificação —
                           -- material sem ficha nenhuma é estado legítimo, nunca bloqueia a navegação)
    -- 'duplicata' (item 9, Acréscimos §5/§8.2): o conteúdo já existe no
    -- acervo (mesmo SHA-256), reconhecido em vez de reimportado — nunca
    -- copiado de novo, nunca um segundo asset pro mesmo arquivo.
    estado          TEXT NOT NULL DEFAULT 'nao_digitalizado'
                    CHECK (estado IN ('nao_digitalizado', 'capturado', 'qc_ok', 'alerta', 'publicado', 'duplicata')),
    notas_livres    TEXT,
    criado_em       TEXT NOT NULL,
    atualizado_em   TEXT NOT NULL,
    UNIQUE (projeto_id, codigo_acervo)
);

CREATE INDEX IF NOT EXISTS idx_item_projeto ON item(projeto_id);
CREATE INDEX IF NOT EXISTS idx_item_tipo_midia ON item(tipo_midia);
CREATE INDEX IF NOT EXISTS idx_item_estado ON item(estado);

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
    caminho_relativo        TEXT NOT NULL,   -- relativo à pasta do projeto (P5)
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

-- ---------------------------------------------------------------------------
-- P1 embutido na estrutura: master é travado por padrão. Qualquer UPDATE que
-- mude caminho ou checksum de um arquivo eh_master=1 é abortado, a menos que
-- o projeto tenha permitir_sobrescrever_master=1. Correção sempre vira
-- derivada nova (INSERT com derivada_de_arquivo_id preenchido).
-- ---------------------------------------------------------------------------
CREATE TRIGGER IF NOT EXISTS arquivo_master_travado
BEFORE UPDATE OF caminho_relativo, checksum_md5, checksum_sha256 ON arquivo
WHEN OLD.eh_master = 1
 AND (NEW.caminho_relativo <> OLD.caminho_relativo
      OR IFNULL(NEW.checksum_md5, '') <> IFNULL(OLD.checksum_md5, '')
      OR IFNULL(NEW.checksum_sha256, '') <> IFNULL(OLD.checksum_sha256, ''))
 AND (SELECT permitir_sobrescrever_master FROM projeto WHERE id = (SELECT projeto_id FROM item WHERE id = OLD.item_id)) = 0
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
    pasta_id                TEXT NOT NULL REFERENCES acervo_pasta(id) ON DELETE CASCADE,
    arquivo_id              TEXT NOT NULL REFERENCES arquivo(id) ON DELETE CASCADE,
    caminho_relativo_destino TEXT NOT NULL, -- relativo à raiz de destino escolhida, ex.: "consolidado/01 Fitas/ACR-001.wav"
    checksum_sha256         TEXT NOT NULL,   -- da CÓPIA consolidada (sem embedding), verificado depois de copiar
    consolidado_em          TEXT NOT NULL,
    UNIQUE (item_id, pasta_id, arquivo_id)
);

CREATE INDEX IF NOT EXISTS idx_consolidacao_registro_arquivo ON consolidacao_registro(arquivo_id);
