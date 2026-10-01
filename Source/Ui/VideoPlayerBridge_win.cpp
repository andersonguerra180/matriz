#include "VideoPlayerBridge.h"
#include <cstdlib>

struct WinVPContext {
    bool playing = false;
    double pos = 0.0;
    double dur = 0.0;
};

VPHandle vpCreate(void) {
    return new WinVPContext();
}

void vpDestroy(VPHandle h) {
    if (h) {
        delete static_cast<WinVPContext*>(h);
    }
}

bool vpLoad(VPHandle /*h*/, const char* /*path*/) {
    return false;
}

void vpPlay(VPHandle h) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) ctx->playing = true;
}

void vpPause(VPHandle h) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) ctx->playing = false;
}

void vpStop(VPHandle h) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) {
        ctx->playing = false;
        ctx->pos = 0.0;
    }
}

void vpSeek(VPHandle h, double seconds) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) ctx->pos = seconds;
}

bool vpIsPlaying(VPHandle h) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) return ctx->playing;
    return false;
}

double vpPosition(VPHandle h) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) return ctx->pos;
    return 0.0;
}

double vpDuration(VPHandle h) {
    if (auto* ctx = static_cast<WinVPContext*>(h)) return ctx->dur;
    return 0.0;
}

void* vpGetNSView(VPHandle /*h*/) {
    return nullptr;
}

void vpResize(VPHandle /*h*/, int /*w*/, int /*h2*/) {}
void vpSetTimecodeVisible(VPHandle /*h*/, bool /*visible*/) {}
void vpSetTimecodeText(VPHandle /*h*/, const char* /*text*/) {}
