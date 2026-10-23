/*
 * Extensão do BKR Matriz para a libmtp 1.1.23 (mesma licença: GNU LGPL 2.1 ou posterior, ver ../COPYING).
 */
#include "config.h"
#include "matriz_mtp.h"
#include "ptp.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* "YYYYMMDDThhmmss[.s][Z|+hhmm]" -> time_t (hora local, como ptp_unpack_PTPTIME). */
static time_t hora_mtp(const char *s)
{
  struct tm tm;
  if (s == NULL || strlen(s) < 15)
    return 0;
  memset(&tm, 0, sizeof(tm));
  char buf[5];
  memcpy(buf, s, 4); buf[4] = 0; tm.tm_year = atoi(buf) - 1900;
  memcpy(buf, s + 4, 2); buf[2] = 0; tm.tm_mon = atoi(buf) - 1;
  memcpy(buf, s + 6, 2); tm.tm_mday = atoi(buf);
  memcpy(buf, s + 9, 2); tm.tm_hour = atoi(buf);
  memcpy(buf, s + 11, 2); tm.tm_min = atoi(buf);
  memcpy(buf, s + 13, 2); tm.tm_sec = atoi(buf);
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static int por_handle(const void *a, const void *b)
{
  const MTPProperties *x = (const MTPProperties *) a;
  const MTPProperties *y = (const MTPProperties *) b;
  return x->ObjectHandle < y->ObjectHandle ? -1 : (x->ObjectHandle > y->ObjectHandle ? 1 : 0);
}

LIBMTP_file_t *MATRIZ_MTP_Listar_Pasta(LIBMTP_mtpdevice_t *device, uint32_t storage, uint32_t parent, int *suportado)
{
  PTPParams *params = (PTPParams *) device->params;
  MTPProperties *props = NULL;
  int nprops = 0;
  LIBMTP_file_t *primeiro = NULL, *ultimo = NULL;
  int i = 0;

  *suportado = 0;
  if (parent == LIBMTP_FILES_AND_FOLDERS_ROOT || parent == 0 ||
      !ptp_operation_issupported(params, PTP_OC_MTP_GetObjPropList))
    return NULL;

  if (ptp_mtp_getobjectproplist_level(params, parent, 1, &props, &nprops) != PTP_RC_OK)
    return NULL;
  *suportado = 1;
  if (props == NULL || nprops <= 0)
    return NULL;

  qsort(props, (size_t) nprops, sizeof(MTPProperties), por_handle);

  while (i < nprops) {
    const uint32_t handle = props[i].ObjectHandle;
    LIBMTP_file_t *f = LIBMTP_new_file_t();
    uint32_t pai = parent;
    uint16_t formato = 0;
    if (f == NULL)
      break;
    f->item_id = handle;
    f->storage_id = storage;
    for (; i < nprops && props[i].ObjectHandle == handle; i++) {
      MTPProperties *p = &props[i];
      switch (p->property) {
      case PTP_OPC_ObjectFileName:
        if (p->datatype == PTP_DTC_STR && p->propval.str != NULL && f->filename == NULL)
          f->filename = strdup(p->propval.str);
        break;
      case PTP_OPC_ObjectSize:
        f->filesize = p->datatype == PTP_DTC_UINT64 ? p->propval.u64 : p->propval.u32;
        break;
      case PTP_OPC_ObjectFormat:
        formato = p->propval.u16;
        break;
      case PTP_OPC_ParentObject:
        pai = p->propval.u32;
        break;
      case PTP_OPC_StorageID:
        f->storage_id = p->propval.u32;
        break;
      case PTP_OPC_DateModified:
        if (p->datatype == PTP_DTC_STR)
          f->modificationdate = hora_mtp(p->propval.str);
        break;
      default:
        break;
      }
    }
    f->parent_id = pai;
    f->filetype = formato == PTP_OFC_Association ? LIBMTP_FILETYPE_FOLDER : LIBMTP_FILETYPE_UNKNOWN;
    /* Só filhos diretos da pasta pedida (nível 1 não deveria trazer outros, mas não custa conferir). */
    if (handle == parent || pai != parent || f->filename == NULL || (storage != 0 && f->storage_id != storage)) {
      LIBMTP_destroy_file_t(f);
      continue;
    }
    if (ultimo == NULL)
      primeiro = f;
    else
      ultimo->next = f;
    ultimo = f;
  }

  ptp_destroy_object_prop_list(props, nprops);
  return primeiro;
}
