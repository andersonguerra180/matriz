#pragma once

#include <JuceHeader.h>
#include <chrono>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace matriz::diag {

class WatchdogLogger;
inline std::unique_ptr<WatchdogLogger> g_watchdogLogger;

class WatchdogLogger : private juce::Thread {
public:
    WatchdogLogger() : juce::Thread("WatchdogLoggerThread") {
        logFile_ = resolverLogFile();
        habilitado_.store(logFile_ != juce::File());
        if (habilitado_.load()) {
            auto path = logFile_.getFullPathName();
            fprintf(stderr, "[WatchdogLogger] log path: %s\n", path.toRawUTF8());
            stream_ = logFile_.createOutputStream();
        } else {
            fprintf(stderr, "[WatchdogLogger] DISABLED - could not create log directory\n");
        }
        startThread();
    }

    ~WatchdogLogger() override {
        signalThreadShouldExit();
        cv_.notify_all();
        stopThread(2000);
        stream_.reset();
    }

    static WatchdogLogger& getInstance() {
        jassert (g_watchdogLogger != nullptr);
        return *g_watchdogLogger;
    }

    void log(const juce::String& text) {
        if (!habilitado_.load()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(text);
        }
        cv_.notify_one();
    }

    juce::File logFilePath() const { return logFile_; }

private:
    void run() override {
        if (habilitado_.load() && stream_) {
            juce::String header = "[WatchdogLogger] started - log path: " + logFile_.getFullPathName() + "\n";
            stream_->write(header.toRawUTF8(), header.getNumBytesAsUTF8());
            stream_->flush();
        }

        while (!threadShouldExit()) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return !queue_.empty() || threadShouldExit(); });

            while (!queue_.empty()) {
                juce::String text = queue_.front();
                queue_.pop();
                lock.unlock();

                if (stream_) {
                    text += "\n";
                    stream_->write(text.toRawUTF8(), text.getNumBytesAsUTF8());
                    stream_->flush();
                }

                lock.lock();
            }
        }
    }

    static juce::File resolverLogFile() {
        auto primary = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("Logs/BKR Matriz");
        if (!primary.exists()) {
            primary.createDirectory();
        }
        if (primary.isDirectory()) {
            return primary.getChildFile("perf.log");
        }
        auto fallback = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("BKR Matriz");
        if (!fallback.exists()) {
            fallback.createDirectory();
        }
        return fallback.getChildFile("perf.log");
    }

    juce::File logFile_;
    std::unique_ptr<juce::FileOutputStream> stream_;
    std::atomic<bool> habilitado_{false};
    std::queue<juce::String> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

inline void inicializarWatchdog() {
    g_watchdogLogger = std::make_unique<WatchdogLogger>();
}

inline void fecharWatchdog() {
    g_watchdogLogger.reset();
}

inline constexpr long long kOrcamentoCallbackMs = 16;

class Watchdog {
public:
    explicit Watchdog(const char* name)
        : name_(name), start_(std::chrono::steady_clock::now()) {}

    ~Watchdog() {
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
        if (ms <= kOrcamentoCallbackMs) return;

        static thread_local bool guard = false;
        if (guard) return;
        struct Guard {
            Guard() { guard = true; }
            ~Guard() { guard = false; }
        } g;

        juce::String timeStr = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S.%s");
        juce::String line = juce::String::formatted("[%s] [Watchdog] %s took %lld ms",
                                                     timeStr.toRawUTF8(), name_, ms);
        line << "\n" << juce::SystemStats::getStackBacktrace();
        WatchdogLogger::getInstance().log(line);
    }

private:
    const char* name_;
    std::chrono::steady_clock::time_point start_;
};

class MessageLoopMonitor : private juce::Timer {
public:
    MessageLoopMonitor() : lastTick_(std::chrono::steady_clock::now()) {
        startTimer(250);
    }

    ~MessageLoopMonitor() override {
        stopTimer();
    }

private:
    void timerCallback() override {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick_).count();
        if (ms > 300) {
            juce::String timeStr = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S.%s");
            juce::String line = juce::String::formatted("[%s] [Watchdog] WARNING: Message loop was blocked for %lld ms!",
                                                        timeStr.toRawUTF8(), ms);
            WatchdogLogger::getInstance().log(line);
        }
        lastTick_ = std::chrono::steady_clock::now();
    }

    std::chrono::steady_clock::time_point lastTick_;
};

} // namespace matriz::diag

#define MATRIZ_TRACE_COLAR_(a, b) a##b
#define MATRIZ_TRACE_COLAR(a, b) MATRIZ_TRACE_COLAR_(a, b)
#define MATRIZ_TRACE(name) \
    ::matriz::diag::Watchdog MATRIZ_TRACE_COLAR(watchdog_trace_, __LINE__)(name)
