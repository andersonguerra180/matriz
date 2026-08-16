#pragma once

#include <JuceHeader.h>
#include <fstream>
#include <string>
#include <chrono>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace matriz::app {

inline void registrarLogOperacao(const juce::File& pastaProjeto,
                                 const std::string& operacao,
                                 const std::string& itemId,
                                 const std::string& workerId,
                                 std::chrono::system_clock::time_point inicio,
                                 std::chrono::system_clock::time_point fim,
                                 juce::int64 bytesProcessados,
                                 const std::string& erro = "") {
    juce::File logFile = pastaProjeto.getChildFile("matriz_operacoes.log");
    std::ofstream out(logFile.getFullPathName().toStdString(), std::ios::app);
    if (!out.is_open()) return;

    auto timeToIso8601 = [](std::chrono::system_clock::time_point tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::tm tmVal;
#ifdef _WIN32
        gmtime_s(&tmVal, &time);
#else
        gmtime_r(&time, &tmVal);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmVal);
        return std::string(buf);
    };

    double duracaoMs = std::chrono::duration<double, std::milli>(fim - inicio).count();

    size_t ramBytes = 0;
#ifdef __APPLE__
    struct task_basic_info info;
    mach_msg_type_number_t infoCount = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        ramBytes = info.resident_size;
    }
#endif

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("operacao", juce::String(operacao));
    obj->setProperty("item_id", juce::String(itemId));
    obj->setProperty("worker_id", juce::String(workerId));
    obj->setProperty("inicio", juce::String(timeToIso8601(inicio)));
    obj->setProperty("fim", juce::String(timeToIso8601(fim)));
    obj->setProperty("duracao_ms", duracaoMs);
    obj->setProperty("bytes_processados", static_cast<double>(bytesProcessados));
    obj->setProperty("ram_uso_bytes", static_cast<double>(ramBytes));
    obj->setProperty("erro", juce::String(erro));

    // juce::var constructor accepts DynamicObject* raw pointer
    out << juce::JSON::toString(juce::var(obj.get()), true).toStdString() << "\n";
}

} // namespace matriz::app
