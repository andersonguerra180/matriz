#pragma once

#include <JuceHeader.h>
#include <string>

namespace matriz::model {

class ProjectLog {
public:
    explicit ProjectLog(const juce::File& projectRootFolder);

    // Path to the log.md file
    juce::File getLogFile() const;

    // Checks if the log.md file exists on disk
    bool exists() const;

    // Reads the complete text of log.md (creates an initial header if file doesn't exist)
    juce::String readContent();

    // Saves new content to log.md (used when user edits notes)
    bool saveContent(const juce::String& newContent);

    // Appends an automated action entry with an ISO 8601 timestamp
    void appendEntry(const juce::String& actionTitle,
                     const juce::StringArray& details,
                     const juce::String& author = "System");

private:
    juce::File projectFolder_;
    juce::File logFile_;

    void ensureInitialized();
};

} // namespace matriz::model
