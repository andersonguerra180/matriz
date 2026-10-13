#import "MiniaturaDocumento.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <PDFKit/PDFKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <QuickLook/QuickLook.h>
#include <cmath>
#include <algorithm>

namespace {

static bool salvarCgImageComoJpeg(CGImageRef imagem, const char* destinoPath,
                                  int* larguraOut, int* alturaOut) {
    if (!imagem || !destinoPath || !larguraOut || !alturaOut) return false;

    NSURL* urlDestino = [NSURL fileURLWithPath:[NSString stringWithUTF8String:destinoPath]];
    CGImageDestinationRef destinoIO = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)urlDestino, CFSTR("public.jpeg"), 1, NULL);
    if (!destinoIO) return false;

    NSDictionary* propriedades = @{
        (__bridge NSString*)kCGImageDestinationLossyCompressionQuality: @(0.85)
    };
    CGImageDestinationAddImage(destinoIO, imagem, (__bridge CFDictionaryRef)propriedades);
    bool ok = CGImageDestinationFinalize(destinoIO);
    CFRelease(destinoIO);

    if (ok) {
        *larguraOut = static_cast<int>(CGImageGetWidth(imagem));
        *alturaOut = static_cast<int>(CGImageGetHeight(imagem));
    }
    return ok;
}

static CGImageRef renderizarCapaPdf(CFURLRef url, int ladoMaximoPx) {
    // 1. Tenta via PDFKit (suporta todas as versões de PDF, criptografia e rotações de capa)
    @autoreleasepool {
        PDFDocument* doc = [[PDFDocument alloc] initWithURL:(__bridge NSURL*)url];
        if (doc && [doc pageCount] > 0) {
            PDFPage* page = [doc pageAtIndex:0];
            if (page) {
                NSImage* nsImg = [page thumbnailOfSize:NSMakeSize(ladoMaximoPx, ladoMaximoPx) forBox:kPDFDisplayBoxCropBox];
                if (nsImg) {
                    NSRect rect = NSMakeRect(0, 0, nsImg.size.width, nsImg.size.height);
                    CGImageRef cg = [nsImg CGImageForProposedRect:&rect context:nil hints:nil];
                    if (cg) return CGImageRetain(cg);
                }
            }
        }
    }

    // 2. Fallback via CoreGraphics
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL(url);
    if (!doc) return NULL;
    CGPDFPageRef page = CGPDFDocumentGetPage(doc, 1);
    if (!page) {
        CGPDFDocumentRelease(doc);
        return NULL;
    }

    CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
    CGPDFBox boxType = kCGPDFCropBox;
    if (box.size.width <= 0 || box.size.height <= 0) {
        box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
        boxType = kCGPDFMediaBox;
    }
    if (box.size.width <= 0 || box.size.height <= 0) {
        CGPDFDocumentRelease(doc);
        return NULL;
    }

    int rotation = CGPDFPageGetRotationAngle(page);
    bool swap = (rotation == 90 || rotation == 270);
    CGFloat origW = swap ? box.size.height : box.size.width;
    CGFloat origH = swap ? box.size.width : box.size.height;

    CGFloat scale = (CGFloat)ladoMaximoPx / std::max(origW, origH);
    int w = static_cast<int>(std::ceil(origW * scale));
    int h = static_cast<int>(std::ceil(origH * scale));
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(NULL, static_cast<size_t>(w), static_cast<size_t>(h),
                                            8, static_cast<size_t>(w * 4), cs,
                                            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(cs);
    if (!ctx) {
        CGPDFDocumentRelease(doc);
        return NULL;
    }

    // Fundo branco na página do documento
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, 0, w, h));

    CGAffineTransform transform = CGPDFPageGetDrawingTransform(page, boxType, CGRectMake(0, 0, w, h), rotation, true);
    CGContextConcatCTM(ctx, transform);
    CGContextDrawPDFPage(ctx, page);

    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    CGPDFDocumentRelease(doc);
    return image;
}

static CGImageRef renderizarDocumentoTexto(NSURL* url, int ladoMaximoPx) {
    @autoreleasepool {
        NSDictionary* docAttrs = nil;
        NSError* err = nil;
        NSAttributedString* attrStr = [[NSAttributedString alloc] initWithURL:url options:@{} documentAttributes:&docAttrs error:&err];
        if (!attrStr || [attrStr length] == 0) return NULL;

        CGFloat w = 450.0;
        CGFloat h = 600.0;
        NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
        [img lockFocus];
        [[NSColor whiteColor] setFill];
        NSRectFill(NSMakeRect(0, 0, w, h));
        NSRect textRect = NSMakeRect(24, 24, w - 48, h - 48);
        [attrStr drawInRect:textRect];
        [img unlockFocus];

        NSRect rect = NSMakeRect(0, 0, w, h);
        CGImageRef cg = [img CGImageForProposedRect:&rect context:nil hints:nil];
        if (cg) return CGImageRetain(cg);
    }
    return NULL;
}

} // namespace

bool gerarMiniaturaDocumentoNativa(const char* origemPath, const char* destinoPath,
                                    int ladoMaximoPx, int* larguraOut, int* alturaOut) {
    if (!origemPath || !destinoPath || !larguraOut || !alturaOut) return false;

    @autoreleasepool {
        NSString* nsOrigem = [NSString stringWithUTF8String:origemPath];
        if (!nsOrigem) return false;
        NSURL* urlOrigem = [NSURL fileURLWithPath:nsOrigem];
        NSString* ext = [[nsOrigem pathExtension] lowercaseString];

        CGImageRef imagem = NULL;

        // Se for PDF, renderiza a primeira página diretamente via PDFKit / CoreGraphics
        if ([ext isEqualToString:@"pdf"]) {
            imagem = renderizarCapaPdf((__bridge CFURLRef)urlOrigem, ladoMaximoPx);
        }

        // Se for texto / RTF / HTML, renderiza página com layout tipográfico
        if (!imagem && ([ext isEqualToString:@"rtf"] || [ext isEqualToString:@"rtfd"] ||
                        [ext isEqualToString:@"txt"] || [ext isEqualToString:@"html"] ||
                        [ext isEqualToString:@"htm"])) {
            imagem = renderizarDocumentoTexto(urlOrigem, ladoMaximoPx);
        }

        // Se não for PDF ou se renderização falhou, gera via QuickLook (.doc, .docx, .pages, .keynote, etc.)
        if (!imagem) {
            NSDictionary* opcoes = @{
                (__bridge NSString*)kQLThumbnailOptionIconModeKey: @NO
            };
            imagem = QLThumbnailImageCreate(kCFAllocatorDefault,
                                           (__bridge CFURLRef)urlOrigem,
                                           CGSizeMake(ladoMaximoPx, ladoMaximoPx),
                                           (__bridge CFDictionaryRef)opcoes);
        }

        if (!imagem) return false;

        bool sucesso = salvarCgImageComoJpeg(imagem, destinoPath, larguraOut, alturaOut);
        CGImageRelease(imagem);
        return sucesso;
    }
}

#else

bool gerarMiniaturaDocumentoNativa(const char*, const char*, int, int*, int*) {
    return false;
}

#endif
