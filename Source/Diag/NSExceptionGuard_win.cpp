#include "NSExceptionGuard.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

#pragma comment(lib, "dbghelp.lib")

namespace matriz::diag {

namespace {

HANDLE g_crashLogFile = INVALID_HANDLE_VALUE;
wchar_t g_crashLogDir[MAX_PATH] = {0};

void ensureLogDir() {
    if (g_crashLogDir[0] != L'\0') return;
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        wsprintfW(g_crashLogDir, L"%s\\BKR\\Matriz\\Logs", appData);
        CoTaskMemFree(appData);
        CreateDirectoryW(g_crashLogDir, nullptr);
    }
}

void writeCrashText(const char* str) {
    if (!str) return;
    if (g_crashLogFile == INVALID_HANDLE_VALUE) {
        ensureLogDir();
        wchar_t path[MAX_PATH];
        wsprintfW(path, L"%s\\crash.log", g_crashLogDir);
        g_crashLogFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (g_crashLogFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_crashLogFile, str, static_cast<DWORD>(strlen(str)), &written, nullptr);
        FlushFileBuffers(g_crashLogFile);
    }
}

LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* ep) {
    ensureLogDir();
    
    // 1. Grava texto descritivo no crash.log
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "\n================ CRASH OCCURRED " 
       << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
       << " ================\n";
    ss << "Exception Code: 0x" << std::hex << (ep ? ep->ExceptionRecord->ExceptionCode : 0) << "\n";
    ss << "Exception Flags: 0x" << std::hex << (ep ? ep->ExceptionRecord->ExceptionFlags : 0) << "\n";
    ss << "Exception Address: 0x" << std::hex << (ep ? reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) : 0) << "\n";
    
    writeCrashText(ss.str().c_str());

    // 2. Grava MiniDump se DbgHelp disponível
    wchar_t dmpPath[MAX_PATH];
    wsprintfW(dmpPath, L"%s\\crash.dmp", g_crashLogDir);
    HANDLE hDump = CreateFileW(dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hDump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDump,
                          MiniDumpWithIndirectlyReferencedMemory,
                          ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(hDump);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void instalarGuardaDeExcecao() {
    SetUnhandledExceptionFilter(UnhandledCrashFilter);
    std::cerr << "[NSExceptionGuard] SetUnhandledExceptionFilter installed for Windows\n";
}

void breadcrumb(const char* msg) {
    if (!msg) return;
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&in_time_t), "%H:%M:%S") << "] " << msg << "\n";
    writeCrashText(ss.str().c_str());
}

void desativarAppNapParaSelfTest() {
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
}

} // namespace matriz::diag
#endif
