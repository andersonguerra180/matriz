#include "DocumentPreviewBridge.h"
#include <cstdlib>

struct WinDocContext {
    int curPage = 1;
    int totalPages = 1;
};

DocHandle docCreate(void) {
    return new WinDocContext();
}

void docDestroy(DocHandle h) {
    if (h) {
        delete static_cast<WinDocContext*>(h);
    }
}

bool docLoad(DocHandle /*h*/, const char* /*path*/) {
    return false;
}

void* docGetNSView(DocHandle /*h*/) {
    return nullptr;
}

void docResize(DocHandle /*h*/, int /*w*/, int /*h2*/) {}
void docZoomIn(DocHandle /*h*/) {}
void docZoomOut(DocHandle /*h*/) {}
void docZoomReset(DocHandle /*h*/) {}
void docGoToNextPage(DocHandle /*h*/) {}
void docGoToPreviousPage(DocHandle /*h*/) {}
void docGoToFirstPage(DocHandle /*h*/) {}
void docGoToLastPage(DocHandle /*h*/) {}

int docGetCurrentPage(DocHandle h) {
    if (auto* ctx = static_cast<WinDocContext*>(h)) return ctx->curPage;
    return 1;
}

int docGetTotalPages(DocHandle h) {
    if (auto* ctx = static_cast<WinDocContext*>(h)) return ctx->totalPages;
    return 1;
}
