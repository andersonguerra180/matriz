#import "MiniaturaPsd.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

bool gerarMiniaturaPsdNativa(const char* origemPath, const char* destinoPath,
                              int ladoMaximoPx, int* larguraOut, int* alturaOut) {
    if (!origemPath || !destinoPath || !larguraOut || !alturaOut) return false;

    @autoreleasepool {
        NSURL* urlOrigem = [NSURL fileURLWithPath:[NSString stringWithUTF8String:origemPath]];
        CGImageSourceRef fonte = CGImageSourceCreateWithURL((__bridge CFURLRef)urlOrigem, NULL);
        if (!fonte) return false;

        NSDictionary* opcoesThumb = @{
            (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
            (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize: @(ladoMaximoPx),
            (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform: @YES
        };
        CGImageRef imagem = CGImageSourceCreateThumbnailAtIndex(fonte, 0, (__bridge CFDictionaryRef)opcoesThumb);
        CFRelease(fonte);
        if (!imagem) return false;

        NSURL* urlDestino = [NSURL fileURLWithPath:[NSString stringWithUTF8String:destinoPath]];
        CGImageDestinationRef destinoIO = CGImageDestinationCreateWithURL((__bridge CFURLRef)urlDestino, CFSTR("public.jpeg"), 1, NULL);
        if (!destinoIO) {
            CGImageRelease(imagem);
            return false;
        }

        NSDictionary* propriedades = @{ (__bridge NSString*)kCGImageDestinationLossyCompressionQuality: @(0.85) };
        CGImageDestinationAddImage(destinoIO, imagem, (__bridge CFDictionaryRef)propriedades);
        bool ok = CGImageDestinationFinalize(destinoIO);
        CFRelease(destinoIO);

        if (ok) {
            *larguraOut = static_cast<int>(CGImageGetWidth(imagem));
            *alturaOut = static_cast<int>(CGImageGetHeight(imagem));
        }
        CGImageRelease(imagem);
        return ok;
    }
}

#else

bool gerarMiniaturaPsdNativa(const char*, const char*, int, int*, int*) { return false; }

#endif
