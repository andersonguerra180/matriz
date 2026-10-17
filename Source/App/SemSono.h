#pragma once

#include <memory>
#include <string>

namespace matriz::app {

// RAII: enquanto existir, o sistema não entra em repouso por inatividade (cópia longa de
// backup/rebuild). macOS: IOPMAssertion; Windows: SetThreadExecutionState. Pode ser criado e
// destruído em qualquer thread (a asserção é do processo, não da thread).
class SemSono {
public:
    explicit SemSono(const std::string& motivo);
    ~SemSono();
    SemSono(const SemSono&) = delete;
    SemSono& operator=(const SemSono&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace matriz::app
