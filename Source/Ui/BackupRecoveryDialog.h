#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

#include "BackupSyncDialog.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

class BackupRecoveryDialog {
public:
    static void showRecoveryDialog(ProjetoAberto& projeto,
                                   const std::vector<BackupVersionRef>& versoes);
};

} // namespace matriz::ui
