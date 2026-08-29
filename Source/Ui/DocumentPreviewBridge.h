#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void* DocHandle;

DocHandle docCreate(void);
void docDestroy(DocHandle h);
bool docLoad(DocHandle h, const char* path);
void* docGetNSView(DocHandle h);
void docResize(DocHandle h, int w, int h2);
void docZoomIn(DocHandle h);
void docZoomOut(DocHandle h);
void docZoomReset(DocHandle h);
void docGoToNextPage(DocHandle h);
void docGoToPreviousPage(DocHandle h);
void docGoToFirstPage(DocHandle h);
void docGoToLastPage(DocHandle h);
int docGetCurrentPage(DocHandle h);
int docGetTotalPages(DocHandle h);

#ifdef __cplusplus
}
#endif
