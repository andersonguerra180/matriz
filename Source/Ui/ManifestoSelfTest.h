#pragma once

// Manifesto de checksums do fim do backup (BackupWorkspaceComponent::
// gerarManifestChecksumsBackup / exportarChecksumsPara). Roda como
// `BKR Matriz --selftest-manifesto`: consolida JPEGs reais COM embed de
// metadados (os bytes entregues passam a diferir da origem), depois confere:
//  - o manifesto sai com o hash do destino (registro) e passa em `shasum -c`;
//  - o hash da origem (comportamento antigo) falharia;
//  - o manifesto NUNCA abre arquivo (apagar os arquivos do destino não muda nada); item
//    sem registro de consolidação vira comentário "# not consolidated", sem hash zerado, e
//    grava UMA entrada de resumo no log.md;
//  - progresso (N+1 chamadas, monotônico) e cancelamento (sem .sha256 nem
//    .tmp, o do backup anterior some);
//  - a fase final do backup (CSV, XLS, Dublin Core e manifesto) em thread de fundo
//    enquanto a message thread grava no banco — o caso que o TSan precisa ver.
// Tamanho: MATRIZ_MANIFESTO_N (padrão 24 arquivos) e MATRIZ_MANIFESTO_MB
// (padrão 2 MB cada).

namespace matriz::ui {

int rodarManifestoSelfTest();

} // namespace matriz::ui
