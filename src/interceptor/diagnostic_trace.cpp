#include "diagnostic_trace.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace go_mapi {

std::string DiagnosticTrace::EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);

    for (unsigned char c : value) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                escaped += buffer;
            } else {
                escaped += static_cast<char>(c);
            }
        }
    }

    return escaped;
}

std::string DiagnosticTrace::UtcTimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << milliseconds.count() << 'Z';
    return out.str();
}

std::string DiagnosticTrace::ToJson(const MapiCallTrace& trace) {
    std::ostringstream out;
    out << '{'
        << "\"version\":2,"
        << "\"timestamp\":\"" << EscapeJsonString(trace.timestamp) << "\","
        << "\"phase\":\"" << EscapeJsonString(trace.phase) << "\","
        << "\"callId\":\"" << EscapeJsonString(trace.callId) << "\","
        << "\"processId\":" << trace.processId << ','
        << "\"threadId\":" << trace.threadId << ','
        << "\"api\":\"" << EscapeJsonString(trace.api) << "\","
        << "\"originApp\":\"" << EscapeJsonString(trace.originApp) << "\","
        << "\"processArchitecture\":\""
        << EscapeJsonString(trace.processArchitecture) << "\","
        << "\"flags\":" << trace.flags << ','
        << "\"hasUiParent\":" << (trace.hasUiParent ? "true" : "false") << ','
        << "\"result\":";
    if (trace.hasResult) {
        out << trace.result;
    } else {
        out << "null";
    }
    out << ',' << "\"durationMs\":";
    if (trace.hasDuration) {
        out << trace.durationMs;
    } else {
        out << "null";
    }
    out << '}';
    return out.str();
}

} // namespace go_mapi
