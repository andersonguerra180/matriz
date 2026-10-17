#import <AppKit/AppKit.h>

#include "AreaTransferenciaImagem.h"

namespace matriz::app {

bool copiarImagemParaAreaDeTransferencia(const juce::Image& imagem) {
    if (!imagem.isValid()) return false;
    juce::MemoryOutputStream saida;
    juce::PNGImageFormat png;
    if (!png.writeImageToStream(imagem, saida)) return false;
    @autoreleasepool {
        NSData* dados = [NSData dataWithBytes:saida.getData() length:saida.getDataSize()];
        NSImage* nsImagem = [[NSImage alloc] initWithData:dados];
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        BOOL ok = nsImagem != nil && [pb writeObjects:@[ nsImagem ]];
        if (ok) [pb setData:dados forType:NSPasteboardTypePNG];
        return ok == YES;
    }
}

} // namespace matriz::app
