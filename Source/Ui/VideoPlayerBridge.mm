#import "VideoPlayerBridge.h"

#if defined(__APPLE__)

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

struct VPState {
    AVPlayer* player;
    AVPlayerLayer* playerLayer;
    NSView* containerView;
    NSTextField* timecodeField;
    double cachedDuration;
    bool timecodeVisible;
};

VPHandle vpCreate(void) {
    VPState* s = new VPState();
    s->player = nil;
    s->playerLayer = nil;
    s->containerView = nil;
    s->timecodeField = nil;
    s->cachedDuration = 0.0;
    s->timecodeVisible = false;
    return static_cast<VPHandle>(s);
}

void vpDestroy(VPHandle h) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->player) {
        [s->player pause];
        s->player = nil;
    }
    s->timecodeField = nil;
    s->containerView = nil;
    s->playerLayer = nil;
    delete s;
}

bool vpLoad(VPHandle h, const char* path) {
    if (!h || !path) return false;
    VPState* s = static_cast<VPState*>(h);

    if (s->player) {
        [s->player pause];
        s->player = nil;
    }
    s->timecodeField = nil;
    s->containerView = nil;
    s->playerLayer = nil;
    s->cachedDuration = 0.0;

    NSString* nsPath = [NSString stringWithUTF8String:path];
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    AVPlayerItem* item = [AVPlayerItem playerItemWithURL:url];
    if (!item) return false;

    s->player = [AVPlayer playerWithPlayerItem:item];
    if (!s->player) return false;
    s->player.volume = 0.0f;
    s->player.actionAtItemEnd = AVPlayerActionAtItemEndPause;

    s->containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 360)];
    s->containerView.wantsLayer = YES;

    s->playerLayer = [AVPlayerLayer playerLayerWithPlayer:s->player];
    s->playerLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    s->playerLayer.backgroundColor = CGColorCreateGenericRGB(0.18, 0.18, 0.20, 1.0);
    [s->containerView.layer addSublayer:s->playerLayer];

    s->timecodeField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 48)];
    [s->timecodeField setEditable:NO];
    [s->timecodeField setSelectable:NO];
    [s->timecodeField setBordered:NO];
    [s->timecodeField setDrawsBackground:YES];
    [s->timecodeField setBackgroundColor:[NSColor colorWithCalibratedRed:0.04 green:0.04 blue:0.04 alpha:0.92]];
    [s->timecodeField setTextColor:[NSColor whiteColor]];
    [s->timecodeField setAlignment:NSTextAlignmentCenter];
    [s->timecodeField setFont:[NSFont monospacedDigitSystemFontOfSize:26 weight:NSFontWeightBold]];
    s->timecodeField.wantsLayer = YES;
    s->timecodeField.layer.cornerRadius = 3.0;
    s->timecodeField.layer.masksToBounds = YES;
    s->timecodeField.hidden = !s->timecodeVisible;
    [s->containerView addSubview:s->timecodeField positioned:NSWindowAbove relativeTo:nil];

    CMTime dur = s->player.currentItem.asset.duration;
    if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur))
        s->cachedDuration = CMTimeGetSeconds(dur);

    return true;
}

void vpPlay(VPHandle h) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->player) [s->player play];
}

void vpPause(VPHandle h) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->player) [s->player pause];
}

void vpStop(VPHandle h) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->player) {
        [s->player pause];
        [s->player seekToTime:kCMTimeZero];
    }
}

void vpSeek(VPHandle h, double seconds) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->player)
        [s->player seekToTime:CMTimeMakeWithSeconds(seconds, 600)
              toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
}

bool vpIsPlaying(VPHandle h) {
    if (!h) return false;
    VPState* s = static_cast<VPState*>(h);
    return s->player && s->player.rate > 0.0f;
}

double vpPosition(VPHandle h) {
    if (!h) return 0.0;
    VPState* s = static_cast<VPState*>(h);
    if (!s->player) return 0.0;
    CMTime t = s->player.currentTime;
    return CMTIME_IS_VALID(t) ? CMTimeGetSeconds(t) : 0.0;
}

double vpDuration(VPHandle h) {
    if (!h) return 0.0;
    VPState* s = static_cast<VPState*>(h);
    if (s->cachedDuration > 0.0) return s->cachedDuration;
    if (!s->player || !s->player.currentItem) return 0.0;
    CMTime dur = s->player.currentItem.asset.duration;
    if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur)) {
        s->cachedDuration = CMTimeGetSeconds(dur);
        return s->cachedDuration;
    }
    return 0.0;
}

void* vpGetNSView(VPHandle h) {
    if (!h) return nullptr;
    VPState* s = static_cast<VPState*>(h);
    return (__bridge void*)s->containerView;
}

void vpResize(VPHandle h, int w, int h2) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->playerLayer)
        s->playerLayer.frame = CGRectMake(0, 0, w, h2);
    if (s->timecodeField) {
        CGFloat tcW = 240;
        CGFloat tcH = 48;
        CGFloat tcX = (w - tcW) * 0.5;
        CGFloat tcY = 24;
        s->timecodeField.frame = NSMakeRect(tcX, tcY, tcW, tcH);
    }
}

void vpSetTimecodeVisible(VPHandle h, bool visible) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    s->timecodeVisible = visible;
    if (s->timecodeField) {
        s->timecodeField.hidden = !visible;
    }
}

void vpSetTimecodeText(VPHandle h, const char* text) {
    if (!h) return;
    VPState* s = static_cast<VPState*>(h);
    if (s->timecodeField && text) {
        s->timecodeField.stringValue = [NSString stringWithUTF8String:text];
    }
}

#else

VPHandle vpCreate(void) { return nullptr; }
void vpDestroy(VPHandle) {}
bool vpLoad(VPHandle, const char*) { return false; }
void vpPlay(VPHandle) {}
void vpPause(VPHandle) {}
void vpStop(VPHandle) {}
void vpSeek(VPHandle, double) {}
bool vpIsPlaying(VPHandle) { return false; }
double vpPosition(VPHandle) { return 0.0; }
double vpDuration(VPHandle) { return 0.0; }
void* vpGetNSView(VPHandle) { return nullptr; }
void vpResize(VPHandle, int, int) {}
void vpSetTimecodeVisible(VPHandle, bool) {}
void vpSetTimecodeText(VPHandle, const char*) {}

#endif
