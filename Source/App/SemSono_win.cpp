#include <windows.h>

#include "SemSono.h"

namespace matriz::app {

struct SemSono::Impl {};

SemSono::SemSono(const std::string&) : impl_(new Impl) {
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
}

SemSono::~SemSono() { SetThreadExecutionState(ES_CONTINUOUS); }

}  // namespace matriz::app
