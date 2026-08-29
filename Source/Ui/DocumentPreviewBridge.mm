#import "DocumentPreviewBridge.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <PDFKit/PDFKit.h>
#import <Quartz/Quartz.h>
#import <WebKit/WebKit.h>

struct DocState {
    NSView* containerView;
    PDFView* pdfView;
    PDFDocument* pdfDoc;
    QLPreviewView* qlView;
    WKWebView* webView;
    BOOL isPdf;
    BOOL isQl;
    BOOL isWeb;
    int totalPages;
    int currentPage;
    CGFloat currentZoom;
};

DocHandle docCreate(void) {
    DocState* s = new DocState();
    s->containerView = nil;
    s->pdfView = nil;
    s->pdfDoc = nil;
    s->qlView = nil;
    s->webView = nil;
    s->isPdf = NO;
    s->isQl = NO;
    s->isWeb = NO;
    s->totalPages = 1;
    s->currentPage = 1;
    s->currentZoom = 1.0;
    return static_cast<DocHandle>(s);
}

void docDestroy(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->pdfView) {
        [s->pdfView removeFromSuperview];
        s->pdfView = nil;
    }
    if (s->qlView) {
        [s->qlView removeFromSuperview];
        s->qlView = nil;
    }
    if (s->webView) {
        [s->webView removeFromSuperview];
        s->webView = nil;
    }
    s->pdfDoc = nil;
    s->containerView = nil;
    delete s;
}

bool docLoad(DocHandle h, const char* path) {
    if (!h || !path) return false;
    DocState* s = static_cast<DocState*>(h);

    if (s->pdfView) { [s->pdfView removeFromSuperview]; s->pdfView = nil; }
    if (s->qlView) { [s->qlView removeFromSuperview]; s->qlView = nil; }
    if (s->webView) { [s->webView removeFromSuperview]; s->webView = nil; }
    s->pdfDoc = nil;
    s->isPdf = NO;
    s->isQl = NO;
    s->isWeb = NO;
    s->currentZoom = 1.0;

    NSString* nsPath = [NSString stringWithUTF8String:path];
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    NSString* ext = [[nsPath pathExtension] lowercaseString];

    if (!s->containerView) {
        s->containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
        s->containerView.wantsLayer = YES;
        s->containerView.layer.backgroundColor = CGColorCreateGenericRGB(0.12, 0.12, 0.14, 1.0);
    }

    if ([ext isEqualToString:@"pdf"]) {
        s->pdfDoc = [[PDFDocument alloc] initWithURL:url];
        if (s->pdfDoc) {
            s->isPdf = YES;
            s->totalPages = (int)[s->pdfDoc pageCount];
            s->currentPage = 1;

            s->pdfView = [[PDFView alloc] initWithFrame:s->containerView.bounds];
            s->pdfView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            [s->pdfView setDocument:s->pdfDoc];
            [s->pdfView setAutoScales:YES];
            [s->pdfView setDisplaysPageBreaks:YES];
            [s->pdfView setDisplayMode:kPDFDisplaySinglePageContinuous];
            [s->containerView addSubview:s->pdfView];
            return true;
        }
    }

    // Try QLPreviewView for docx, doc, xls, xlsx, rtf, txt, md, etc.
    @try {
        s->qlView = [[QLPreviewView alloc] initWithFrame:s->containerView.bounds style:QLPreviewViewStyleNormal];
        if (s->qlView) {
            s->isQl = YES;
            s->qlView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            [s->qlView setPreviewItem:url];
            [s->qlView refreshPreviewItem];
            [s->containerView addSubview:s->qlView];
            s->totalPages = 1;
            s->currentPage = 1;
            return true;
        }
    } @catch (NSException* e) {
        s->isQl = NO;
        s->qlView = nil;
    }

    // Fallback to WKWebView
    @try {
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        s->webView = [[WKWebView alloc] initWithFrame:s->containerView.bounds configuration:config];
        s->webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [s->webView loadFileURL:url allowingReadAccessToURL:[url URLByDeletingLastPathComponent]];
        [s->containerView addSubview:s->webView];
        s->isWeb = YES;
        s->totalPages = 1;
        s->currentPage = 1;
        return true;
    } @catch (NSException* e) {
        return false;
    }
}

void* docGetNSView(DocHandle h) {
    if (!h) return nullptr;
    DocState* s = static_cast<DocState*>(h);
    return (__bridge void*)s->containerView;
}

void docResize(DocHandle h, int w, int h2) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->containerView) {
        NSRect r = NSMakeRect(0, 0, w, h2);
        s->containerView.frame = r;
        if (s->pdfView) s->pdfView.frame = r;
        if (s->qlView) s->qlView.frame = r;
        if (s->webView) s->webView.frame = r;
    }
}

void docZoomIn(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView zoomIn:nil];
    } else if (s->isWeb && s->webView) {
        s->currentZoom += 0.2;
        s->webView.pageZoom = s->currentZoom;
    }
}

void docZoomOut(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView zoomOut:nil];
    } else if (s->isWeb && s->webView) {
        s->currentZoom = MAX(0.2, s->currentZoom - 0.2);
        s->webView.pageZoom = s->currentZoom;
    }
}

void docZoomReset(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView setAutoScales:YES];
    } else if (s->isWeb && s->webView) {
        s->currentZoom = 1.0;
        s->webView.pageZoom = 1.0;
    }
}

void docGoToNextPage(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView goToNextPage:nil];
    } else if (s->isWeb && s->webView) {
        [s->webView evaluateJavaScript:@"window.scrollBy({top: window.innerHeight * 0.85, behavior: 'smooth'});" completionHandler:nil];
    }
}

void docGoToPreviousPage(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView goToPreviousPage:nil];
    } else if (s->isWeb && s->webView) {
        [s->webView evaluateJavaScript:@"window.scrollBy({top: -window.innerHeight * 0.85, behavior: 'smooth'});" completionHandler:nil];
    }
}

void docGoToFirstPage(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView goToFirstPage:nil];
    } else if (s->isWeb && s->webView) {
        [s->webView evaluateJavaScript:@"window.scrollTo({top: 0, behavior: 'smooth'});" completionHandler:nil];
    }
}

void docGoToLastPage(DocHandle h) {
    if (!h) return;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView) {
        [s->pdfView goToLastPage:nil];
    } else if (s->isWeb && s->webView) {
        [s->webView evaluateJavaScript:@"window.scrollTo({top: document.body.scrollHeight, behavior: 'smooth'});" completionHandler:nil];
    }
}

int docGetCurrentPage(DocHandle h) {
    if (!h) return 1;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfView && s->pdfDoc) {
        PDFPage* p = [s->pdfView currentPage];
        if (p) {
            NSUInteger idx = [s->pdfDoc indexForPage:p];
            if (idx != NSNotFound) return (int)(idx + 1);
        }
    }
    return s->currentPage;
}

int docGetTotalPages(DocHandle h) {
    if (!h) return 1;
    DocState* s = static_cast<DocState*>(h);
    if (s->isPdf && s->pdfDoc) {
        return (int)[s->pdfDoc pageCount];
    }
    return s->totalPages;
}

#else

DocHandle docCreate(void) { return nullptr; }
void docDestroy(DocHandle) {}
bool docLoad(DocHandle, const char*) { return false; }
void* docGetNSView(DocHandle) { return nullptr; }
void docResize(DocHandle, int, int) {}
void docZoomIn(DocHandle) {}
void docZoomOut(DocHandle) {}
void docZoomReset(DocHandle) {}
void docGoToNextPage(DocHandle) {}
void docGoToPreviousPage(DocHandle) {}
void docGoToFirstPage(DocHandle) {}
void docGoToLastPage(DocHandle) {}
int docGetCurrentPage(DocHandle) { return 1; }
int docGetTotalPages(DocHandle) { return 1; }

#endif
