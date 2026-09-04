#include "ProjectLog.h"
#include "Project.h"

namespace matriz::model {

ProjectLog::ProjectLog(const juce::File& projectRootFolder)
    : projectFolder_(projectRootFolder),
      logFile_(projectRootFolder.getChildFile("log.md")) {
}

juce::File ProjectLog::getLogFile() const {
    return logFile_;
}

bool ProjectLog::exists() const {
    return logFile_.existsAsFile();
}

void ProjectLog::ensureInitialized() {
    if (!projectFolder_.isDirectory()) return;

    if (!logFile_.existsAsFile()) {
        juce::String header;
        header << "# BKR Matriz — Project Log\n\n"
               << "> Project root: `" << projectFolder_.getFullPathName() << "`\n"
               << "> Created: `" << juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") << "`\n\n"
               << "---\n\n"
               << "## Operational Log & Notes\n\n";
        logFile_.replaceWithText(header);
    }
}

juce::String ProjectLog::readContent() {
    ensureInitialized();
    return logFile_.loadFileAsString();
}

bool ProjectLog::saveContent(const juce::String& newContent) {
    if (!projectFolder_.isDirectory()) return false;
    return logFile_.replaceWithText(newContent);
}

void ProjectLog::appendEntry(const juce::String& actionTitle,
                             const juce::StringArray& details,
                             const juce::String& author) {
    ensureInitialized();

    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
    juce::String entry;
    entry << "### [" << timestamp << "] " << actionTitle << "\n"
          << "- **Author / Agent:** " << (author.isEmpty() ? "Operator" : author) << "\n";

    for (const auto& line : details) {
        entry << "- " << line << "\n";
    }
    entry << "\n";

    logFile_.appendText(entry);
}

} // namespace matriz::model
