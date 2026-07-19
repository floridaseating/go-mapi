#pragma once

#include <cstdint>
#include <string>

namespace go_mapi {

// MapiCallTrace deliberately contains no message, recipient, subject, body,
// filename, or attachment-path fields. It is safe to collect during the FSIT
// compatibility pilot without copying business content into diagnostics.
struct MapiCallTrace {
    std::string timestamp;
    std::string phase;
    std::string callId;
    uint32_t processId = 0;
    uint32_t threadId = 0;
    std::string api;
    std::string originApp;
    std::string processArchitecture;
    uint32_t flags = 0;
    bool hasUiParent = false;
    uint32_t attachmentCount = 0;
    uint64_t attachmentBytes = 0;
    bool hasResult = false;
    uint32_t result = 0;
    bool hasDuration = false;
    uint64_t durationMs = 0;
};

class DiagnosticTrace {
public:
    static std::string UtcTimestampNow();
    static std::string ToJson(const MapiCallTrace& trace);

    // Appends one JSONL record to a bounded local diagnostic file. Failures
    // are intentionally non-fatal: observing a MAPI call must never change
    // the result returned to the calling application.
    static bool Append(const MapiCallTrace& trace) noexcept;

private:
    static std::string EscapeJsonString(const std::string& value);
    static std::wstring GetTracePath();
};

} // namespace go_mapi
