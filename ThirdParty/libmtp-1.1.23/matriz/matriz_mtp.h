/*
 * Extensão do BKR Matriz para a libmtp 1.1.23 (mesma licença: GNU LGPL 2.1 ou posterior, ver ../COPYING).
 * Compilada dentro da biblioteca compartilhada libmatriz_mtp.dylib, junto do código original da libmtp.
 */
#ifndef MATRIZ_MTP_H
#define MATRIZ_MTP_H

#include "libmtp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lista os filhos diretos de `parent` com UMA transação MTP (GetObjectPropList, nível 1), em vez de uma por
 * objeto como LIBMTP_Get_Files_And_Folders. Mesmo formato de retorno (lista de LIBMTP_file_t, liberar com
 * LIBMTP_destroy_file_t). `*suportado` = 0 quando o aparelho recusou o pedido: quem chama usa o caminho lento.
 * A raiz (parent = LIBMTP_FILES_AND_FOLDERS_ROOT) sempre vai pelo caminho lento (poucos itens, e nem todo
 * aparelho aceita nível 1 na raiz).
 */
LIBMTP_file_t *MATRIZ_MTP_Listar_Pasta(LIBMTP_mtpdevice_t *device, uint32_t storage, uint32_t parent, int *suportado);

#ifdef __cplusplus
}
#endif

#endif
