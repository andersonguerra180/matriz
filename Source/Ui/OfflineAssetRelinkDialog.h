#pragma once

#include <JuceHeader.h>
#include <functional>
#include <string>
#include "../Vault/AssetRelinkEngine.h"

namespace matriz::ui {

class OfflineAssetRelinkDialog : public juce::Component {
public:
    OfflineAssetRelinkDialog(matriz::db::Database& db,
                             const std::string& itemId,
                             const juce::String& itemTitle,
                             const juce::String& expectedPath,
                             const juce::String& storageName,
                             std::function<void(const juce::File& fileSelected)> onRelinkSuccess,
                             std::function<void()> onCancel);
    ~OfflineAssetRelinkDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(matriz::db::Database& db,
                          const std::string& itemId,
                          const juce::String& itemTitle,
                          const juce::String& expectedPath,
                          const juce::String& storageName,
                          std::function<void(const juce::File& fileSelected)> onRelinkSuccess,
                          std::function<void()> onCancel = nullptr);

private:
    matriz::db::Database& db_;
    std::string itemId_;
    juce::String itemTitle_;
    juce::String expectedPath_;
    juce::String storageName_;
    std::function<void(const juce::File&)> onRelinkSuccess_;
    std::function<void()> onCancel_;

    juce::Label lblHeader_;
    juce::Label lblTitleHeader_;
    juce::Label lblTitleValue_;
    juce::Label lblExpectedHeader_;
    juce::TextEditor txtExpectedPath_;
    juce::Label lblStorageHeader_;
    juce::Label lblStorageValue_;
    juce::Label lblStatusWarning_;
    juce::Label lblError_;

    juce::TextButton btnLocate_{"LOCATE FILE"};
    juce::TextButton btnCancel_{"CANCEL"};

    std::unique_ptr<juce::FileChooser> fileChooser_;
};

} // namespace matriz::ui
