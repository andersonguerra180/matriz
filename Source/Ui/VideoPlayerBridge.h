#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void* VPHandle;

VPHandle vpCreate(void);
void vpDestroy(VPHandle h);
bool vpLoad(VPHandle h, const char* path);
// true se o player nativo consegue reproduzir o arquivo (AVI/MKV/WMV com codec não suportado dão false).
bool vpArquivoReproduzivel(const char* path);
void vpPlay(VPHandle h);
void vpPause(VPHandle h);
void vpStop(VPHandle h);
void vpSeek(VPHandle h, double seconds);
bool vpIsPlaying(VPHandle h);
double vpPosition(VPHandle h);
double vpDuration(VPHandle h);
void* vpGetNSView(VPHandle h);
void vpResize(VPHandle h, int w, int h2);
void vpSetTimecodeVisible(VPHandle h, bool visible);
void vpSetTimecodeText(VPHandle h, const char* text);
// Clique direito (ou Ctrl+clique) na tela do vídeo: o NSView nativo engole o evento, então avisa por aqui.
// Chamado na message thread. Windows: o player ainda é stub (sem view), então não dispara.
void vpSetRightClickCallback(VPHandle h, void (*callback)(void* contexto), void* contexto);

#ifdef __cplusplus
}
#endif
