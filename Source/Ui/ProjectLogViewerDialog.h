#pragma once

#include <JuceHeader.h>
#include "../Model/ProjectLog.h"

namespace matriz::ui {

class ProjectLogViewerDialog : public juce::Component {
public:
    explicit ProjectLogViewerDialog(matriz::model::ProjectLog log);
    ~ProjectLogViewerDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(matriz::model::ProjectLog log);

private:
    matriz::model::ProjectLog log_;

    juce::Label lblTitle_;
    juce::Label lblSubtitle_;
    juce::Label lblStatus_;
    juce::TextEditor txtEditor_;
    juce::TextButton btnSave_;
    juce::TextButton btnClose_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLogViewerDialog)
};

} // namespace matriz::ui
