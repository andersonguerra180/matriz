#pragma once

namespace matriz::diag {

// Installs three crash-recording mechanisms BEFORE any UI is created:
//
// 1. NSSetUncaughtExceptionHandler — logs name + reason + userInfo +
//    callStackSymbols via raw open()/write()/fsync() (crash-proof).
// 2. std::set_terminate() backup — uses __cxa_current_exception_type() to
//    detect NSException on the C++ terminate path (objc_exception_rethrow →
//    __cxa_rethrow → terminate, which bypasses the ObjC handler).
// 3. NSApplicationCrashOnExceptions = YES via registerDefaults.
//
// Both handlers log their installation address to stderr so we can verify
// in the log that installation happened and wasn't overwritten.
//
// macOS only. On other platforms this is a no-op.
void instalarGuardaDeExcecao();

// Called from breadcrumb sites — writes a one-line timestamped breadcrumb
// to the crash log via raw fd I/O (safe from signal/terminate context).
void breadcrumb(const char* msg);

} // namespace matriz::diag
