#include "NSExceptionGuard.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#include <cxxabi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
#include <exception>

// Raw-fd crash log: opened once at install time, kept open for the lifetime
// of the process. write()+fsync() are async-signal-safe — safe to call from
// a terminate handler or signal context.
static int g_crashFd = -1;

static void crashWrite(const char* s) {
    if (g_crashFd < 0) return;
    size_t len = strlen(s);
    // Ignore return: we're crashing, nothing to do on error.
    (void)write(g_crashFd, s, len);
}

static void crashFlush() {
    if (g_crashFd >= 0) fsync(g_crashFd);
}

// Opens (or creates) the crash log at the same location WatchdogLogger uses.
static void abrirCrashLog() {
    // ~/Library/Application Support/Logs/BKR Matriz/crash.log
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    char path[1024];
    snprintf(path, sizeof(path), "%s/Library/Application Support/Logs/BKR Matriz", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/Library/Application Support/Logs/BKR Matriz/crash.log", home);

    g_crashFd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_crashFd >= 0) {
        fprintf(stderr, "[NSExceptionGuard] crash log fd=%d path=%s\n", g_crashFd, path);
    } else {
        // Fallback to /tmp
        snprintf(path, sizeof(path), "/tmp/BKR Matriz");
        mkdir(path, 0755);
        snprintf(path, sizeof(path), "/tmp/BKR Matriz/crash.log");
        g_crashFd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        fprintf(stderr, "[NSExceptionGuard] fallback crash log fd=%d path=%s\n", g_crashFd, path);
    }
}

// Records an NSException to both stderr and the crash fd using only
// async-signal-safe I/O.
static void gravarNSException(NSException* exception, const char* source) {
    NSString* name = [exception name] ?: @"(null)";
    NSString* reason = [exception reason] ?: @"(no reason)";
    NSDictionary* userInfo = [exception userInfo];
    NSArray<NSString*>* symbols = [exception callStackSymbols];

    // stderr
    fprintf(stderr, "\n=== UNCAUGHT NSEXCEPTION [%s] ===\n", source);
    fprintf(stderr, "Name:     %s\n", [name UTF8String]);
    fprintf(stderr, "Reason:   %s\n", [reason UTF8String]);
    if (userInfo)
        fprintf(stderr, "UserInfo: %s\n", [[userInfo description] UTF8String]);
    fprintf(stderr, "Stack:\n");
    for (NSString* frame in symbols)
        fprintf(stderr, "  %s\n", [frame UTF8String]);
    fprintf(stderr, "===========================\n\n");
    fflush(stderr);

    // crash.log via raw fd
    crashWrite("\n=== UNCAUGHT NSEXCEPTION [");
    crashWrite(source);
    crashWrite("] ===\n");
    crashWrite("Name:     ");
    crashWrite([name UTF8String]);
    crashWrite("\n");
    crashWrite("Reason:   ");
    crashWrite([reason UTF8String]);
    crashWrite("\n");
    if (userInfo) {
        crashWrite("UserInfo: ");
        crashWrite([[userInfo description] UTF8String]);
        crashWrite("\n");
    }
    crashWrite("Stack:\n");
    for (NSString* frame in symbols) {
        crashWrite("  ");
        crashWrite([frame UTF8String]);
        crashWrite("\n");
    }
    crashWrite("===========================\n\n");
    crashFlush();
}

// --- Handler 1: NSUncaughtExceptionHandler ---
static void matrizUncaughtExceptionHandler(NSException* exception) {
    gravarNSException(exception, "NSUncaughtExceptionHandler");
    abort();
}

// --- Handler 2: std::set_terminate backup ---
static std::terminate_handler g_previousTerminate = nullptr;

static void matrizTerminateHandler() {
    crashWrite("\n=== std::terminate FIRED ===\n");

    // Try to detect if the current exception is an NSException via
    // __cxa_current_exception_type(). The ObjC runtime wraps NSException
    // in a C++ exception of type objc_object/id.
    const std::type_info* ti = abi::__cxa_current_exception_type();
    if (ti) {
        crashWrite("C++ exception type: ");
        crashWrite(ti->name());
        crashWrite("\n");
        fprintf(stderr, "[terminate] C++ exception type: %s\n", ti->name());

        // Try to rethrow and catch as NSException
        try {
            throw;
        } catch (NSException* e) {
            gravarNSException(e, "std::terminate/rethrow-as-NSException");
        } catch (const std::exception& e) {
            crashWrite("std::exception: ");
            crashWrite(e.what());
            crashWrite("\n");
            fprintf(stderr, "[terminate] std::exception: %s\n", e.what());
        } catch (...) {
            crashWrite("(unknown exception type)\n");
            fprintf(stderr, "[terminate] unknown exception type\n");
        }
    } else {
        crashWrite("(no current exception)\n");
        fprintf(stderr, "[terminate] no current exception\n");
    }

    crashWrite("===========================\n\n");
    crashFlush();

    if (g_previousTerminate)
        g_previousTerminate();
    else
        abort();
}

void matriz::diag::instalarGuardaDeExcecao() {
    abrirCrashLog();

    // Log handler address BEFORE installation
    auto prevHandler = NSGetUncaughtExceptionHandler();
    fprintf(stderr, "[NSExceptionGuard] NSGetUncaughtExceptionHandler BEFORE: %p\n", (void*)prevHandler);
    crashWrite("[NSExceptionGuard] handler BEFORE: ");
    char buf[64];
    snprintf(buf, sizeof(buf), "%p\n", (void*)prevHandler);
    crashWrite(buf);

    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        @"NSApplicationCrashOnExceptions": @YES
    }];

    NSSetUncaughtExceptionHandler(&matrizUncaughtExceptionHandler);

    // Log handler address AFTER installation
    auto newHandler = NSGetUncaughtExceptionHandler();
    fprintf(stderr, "[NSExceptionGuard] NSGetUncaughtExceptionHandler AFTER:  %p (our=%p)\n",
            (void*)newHandler, (void*)&matrizUncaughtExceptionHandler);
    snprintf(buf, sizeof(buf), "%p (ours=%p)\n", (void*)newHandler, (void*)&matrizUncaughtExceptionHandler);
    crashWrite("[NSExceptionGuard] handler AFTER:  ");
    crashWrite(buf);
    crashFlush();

    // Backup: std::set_terminate
    g_previousTerminate = std::set_terminate(matrizTerminateHandler);
    fprintf(stderr, "[NSExceptionGuard] std::set_terminate installed (prev=%p, ours=%p)\n",
            (void*)g_previousTerminate, (void*)&matrizTerminateHandler);
}

void matriz::diag::breadcrumb(const char* msg) {
    if (g_crashFd < 0) return;

    // Timestamp: seconds since epoch (async-signal-safe integer formatting)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    char header[128];
    snprintf(header, sizeof(header), "[BC %ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    crashWrite(header);
    crashWrite(msg);
    crashWrite("\n");
    crashFlush();
}

#else

void matriz::diag::instalarGuardaDeExcecao() {}
void matriz::diag::breadcrumb(const char*) {}

#endif
