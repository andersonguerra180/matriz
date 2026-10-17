#import <IOKit/pwr_mgt/IOPMLib.h>

#include "SemSono.h"

namespace matriz::app {

struct SemSono::Impl {
    IOPMAssertionID id = kIOPMNullAssertionID;
};

SemSono::SemSono(const std::string& motivo) : impl_(new Impl) {
    CFStringRef razao = CFStringCreateWithCString(kCFAllocatorDefault, motivo.c_str(), kCFStringEncodingUTF8);
    IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn, razao, &impl_->id);
    if (razao) CFRelease(razao);
}

SemSono::~SemSono() {
    if (impl_ && impl_->id != kIOPMNullAssertionID) IOPMAssertionRelease(impl_->id);
}

}  // namespace matriz::app
