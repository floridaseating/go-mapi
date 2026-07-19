#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "diagnostic_trace.h"

#include <string>
#include <unordered_set>
#include <utility>

using namespace go_mapi;

static_assert(
    noexcept(DiagnosticTrace::Append(std::declval<const MapiCallTrace&>())),
    "diagnostic recording must never throw across a MAPI call");

static_assert(
    noexcept(DiagnosticTrace::NewCallId()),
    "diagnostic call-id generation must never throw across a MAPI call");

TEST_CASE("diagnostic call IDs stay unique across independent generations") {
    std::unordered_set<std::string> ids;

    for (int i = 0; i < 1024; ++i) {
        const std::string id = DiagnosticTrace::NewCallId();
        CHECK_FALSE(id.empty());
        CHECK(ids.insert(id).second);
    }
}

TEST_CASE("diagnostic trace serializes the privacy-safe QuickBooks call envelope") {
    MapiCallTrace trace{};
    trace.timestamp = "2026-07-15T19:00:00.000Z";
    trace.phase = "exit";
    trace.callId = "4242-73-1844674407370955161";
    trace.processId = 4242;
    trace.threadId = 73;
    trace.api = "MAPISendMail";
    trace.originApp = "QBW32.EXE";
    trace.processArchitecture = "x86";
    trace.flags = 9;
    trace.hasUiParent = true;
    trace.hasResult = true;
    trace.result = 0;
    trace.hasDuration = true;
    trace.durationMs = 37;

    CHECK(DiagnosticTrace::ToJson(trace) ==
          "{\"version\":2,\"timestamp\":\"2026-07-15T19:00:00.000Z\","
          "\"phase\":\"exit\",\"callId\":\"4242-73-1844674407370955161\","
          "\"processId\":4242,\"threadId\":73,"
          "\"api\":\"MAPISendMail\",\"originApp\":\"QBW32.EXE\","
          "\"processArchitecture\":\"x86\",\"flags\":9,"
          "\"hasUiParent\":true,\"result\":0,\"durationMs\":37}");
}

TEST_CASE("diagnostic trace represents an entered call without inventing a result") {
    MapiCallTrace trace{};
    trace.timestamp = "2026-07-15T19:00:00.000Z";
    trace.phase = "enter";
    trace.callId = "10-11-99";
    trace.processId = 10;
    trace.threadId = 11;
    trace.api = "MAPILogon";
    trace.originApp = "QBMapi32.exe";
    trace.processArchitecture = "x86";
    trace.flags = 1;
    trace.hasUiParent = true;

    const std::string json = DiagnosticTrace::ToJson(trace);

    CHECK(json.find("\"phase\":\"enter\"") != std::string::npos);
    CHECK(json.find("\"callId\":\"10-11-99\"") != std::string::npos);
    CHECK(json.find("\"result\":null") != std::string::npos);
    CHECK(json.find("\"durationMs\":null") != std::string::npos);
    CHECK(json.find("\"uiParent\":") == std::string::npos);
    CHECK(json.find("attachmentCount") == std::string::npos);
    CHECK(json.find("attachmentBytes") == std::string::npos);
    CHECK(json.find("subject") == std::string::npos);
    CHECK(json.find("body") == std::string::npos);
    CHECK(json.find("recipient") == std::string::npos);
}

TEST_CASE("diagnostic trace escapes process metadata without accepting message content") {
    MapiCallTrace trace{};
    trace.timestamp = "2026-07-15T19:00:00.000Z";
    trace.api = "MAPISendMailA";
    trace.originApp = "odd\"app\\name.exe";
    trace.processArchitecture = "x64";

    const std::string json = DiagnosticTrace::ToJson(trace);

    CHECK(json.find("odd\\\"app\\\\name.exe") != std::string::npos);
    CHECK(json.find("subject") == std::string::npos);
    CHECK(json.find("body") == std::string::npos);
    CHECK(json.find("recipient") == std::string::npos);
    CHECK(json.find("filename") == std::string::npos);
    CHECK(json.find("path") == std::string::npos);
}
