#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void* VPHandle;

VPHandle vpCreate(void);
void vpDestroy(VPHandle h);
bool vpLoad(VPHandle h, const char* path);
void vpPlay(VPHandle h);
void vpPause(VPHandle h);
void vpStop(VPHandle h);
void vpSeek(VPHandle h, double seconds);
bool vpIsPlaying(VPHandle h);
double vpPosition(VPHandle h);
double vpDuration(VPHandle h);
void* vpGetNSView(VPHandle h);
void vpResize(VPHandle h, int w, int h2);

#ifdef __cplusplus
}
#endif
