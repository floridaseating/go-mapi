#include <windows.h>
#include <atomic>
#include <chrono>
#include <string>
#include "diagnostic_trace.h"
#include "mapi_impl.h"
#include "mapi_types.h"

namespace {

std::string CurrentProcessName() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0 || length >= ARRAYSIZE(path)) return "unknown.exe";

    const wchar_t* filename = path;
    if (const wchar_t* slash = wcsrchr(path, L'\\')) filename = slash + 1;

    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return "unknown.exe";

    std::string result(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, 0, filename, -1, result.data(), bytes, nullptr, nullptr) <= 0) {
        return "unknown.exe";
    }
    result.pop_back();
    return result;
}

class TraceMapiCall {
public:
    TraceMapiCall(const char* api, ULONG_PTR uiParent, FLAGS flags) noexcept {
        try {
            started_ = std::chrono::steady_clock::now();
            trace_.timestamp = go_mapi::DiagnosticTrace::UtcTimestampNow();
            trace_.phase = "enter";
            trace_.processId = GetCurrentProcessId();
            trace_.threadId = GetCurrentThreadId();
            const auto sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed) + 1;
            trace_.callId = std::to_string(trace_.processId) + '-' +
                std::to_string(trace_.threadId) + '-' + std::to_string(sequence);
            trace_.api = api;
            trace_.originApp = CurrentProcessName();
            trace_.processArchitecture = sizeof(void*) == 8 ? "x64" : "x86";
            trace_.flags = static_cast<uint32_t>(flags);
            trace_.hasUiParent = uiParent != 0;
            entryRecorded_ = go_mapi::DiagnosticTrace::Append(trace_);
        } catch (...) {
            enabled_ = false;
        }
    }

    ULONG Finish(ULONG result) noexcept {
        if (!enabled_ || !entryRecorded_) return result;
        try {
            trace_.timestamp = go_mapi::DiagnosticTrace::UtcTimestampNow();
            trace_.phase = "exit";
            trace_.hasResult = true;
            trace_.result = static_cast<uint32_t>(result);
            trace_.hasDuration = true;
            trace_.durationMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_).count());
            (void)go_mapi::DiagnosticTrace::Append(trace_);
        } catch (...) {
            // Diagnostics are observational only. Preserve the MAPI result.
        }
        return result;
    }

private:
    static std::atomic<uint64_t> nextSequence_;
    std::chrono::steady_clock::time_point started_{};
    go_mapi::MapiCallTrace trace_{};
    bool enabled_ = true;
    bool entryRecorded_ = false;
};

std::atomic<uint64_t> TraceMapiCall::nextSequence_{0};

} // namespace

// Forward exports - these will be called through the .def file
extern "C" {

ULONG STDAPICALLTYPE MAPISendMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    TraceMapiCall trace("MAPISendMail", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPISendMailA(
        lhSession, ulUIParam, lpMessage, flFlags, ulReserved));
}

ULONG STDAPICALLTYPE MAPISendMailW(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessageW lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    TraceMapiCall trace("MAPISendMailW", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPISendMailW(
        lhSession, ulUIParam, lpMessage, flFlags, ulReserved));
}

ULONG STDAPICALLTYPE MAPILogon(
    ULONG_PTR ulUIParam,
    LPSTR lpszProfileName,
    LPSTR lpszPassword,
    FLAGS flFlags,
    ULONG ulReserved,
    LPLHANDLE lphSession
) {
    TraceMapiCall trace("MAPILogon", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPILogon(
        ulUIParam, lpszProfileName, lpszPassword, flFlags, ulReserved, lphSession));
}

ULONG STDAPICALLTYPE MAPILogoff(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    FLAGS flFlags,
    ULONG ulReserved
) {
    TraceMapiCall trace("MAPILogoff", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPILogoff(
        lhSession, ulUIParam, flFlags, ulReserved));
}

ULONG STDAPICALLTYPE MAPIFreeBuffer(LPVOID pv) {
    TraceMapiCall trace("MAPIFreeBuffer", 0, 0);
    return trace.Finish(go_mapi::MapiImpl::MAPIFreeBuffer(pv));
}

ULONG STDAPICALLTYPE MAPIFindNext(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageType,
    LPSTR lpszSeedMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
) {
    TraceMapiCall trace("MAPIFindNext", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPIFindNext(
        lhSession, ulUIParam, lpszMessageType, lpszSeedMessageID,
        flFlags, ulReserved, lpszMessageID));
}

ULONG STDAPICALLTYPE MAPIReadMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPMapiMessage* lppMessage
) {
    TraceMapiCall trace("MAPIReadMail", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPIReadMail(
        lhSession, ulUIParam, lpszMessageID, flFlags, ulReserved, lppMessage));
}

ULONG STDAPICALLTYPE MAPISaveMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
) {
    TraceMapiCall trace("MAPISaveMail", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPISaveMail(
        lhSession, ulUIParam, lpMessage, flFlags, ulReserved, lpszMessageID));
}

ULONG STDAPICALLTYPE MAPIDeleteMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved
) {
    TraceMapiCall trace("MAPIDeleteMail", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPIDeleteMail(
        lhSession, ulUIParam, lpszMessageID, flFlags, ulReserved));
}

ULONG STDAPICALLTYPE MAPIAddress(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszCaption,
    ULONG nEditFields,
    LPSTR lpszLabels,
    ULONG nRecips,
    LPMapiRecipDesc lpRecips,
    FLAGS flFlags,
    ULONG ulReserved,
    ULONG* lpnNewRecips,
    LPMapiRecipDesc* lppNewRecips
) {
    TraceMapiCall trace("MAPIAddress", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPIAddress(
        lhSession, ulUIParam, lpszCaption, nEditFields, lpszLabels,
        nRecips, lpRecips, flFlags, ulReserved, lpnNewRecips, lppNewRecips));
}

ULONG STDAPICALLTYPE MAPIDetails(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiRecipDesc lpRecip,
    FLAGS flFlags,
    ULONG ulReserved
) {
    TraceMapiCall trace("MAPIDetails", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPIDetails(
        lhSession, ulUIParam, lpRecip, flFlags, ulReserved));
}

ULONG STDAPICALLTYPE MAPIResolveName(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszName,
    FLAGS flFlags,
    ULONG ulReserved,
    LPMapiRecipDesc* lppRecip
) {
    TraceMapiCall trace("MAPIResolveName", ulUIParam, flFlags);
    return trace.Finish(go_mapi::MapiImpl::MAPIResolveName(
        lhSession, ulUIParam, lpszName, flFlags, ulReserved, lppRecip));
}

ULONG STDAPICALLTYPE MAPISendDocuments(
    ULONG_PTR ulUIParam,
    LPSTR lpszDelimChar,
    LPSTR lpszFilePaths,
    LPSTR lpszFileNames,
    ULONG ulReserved
) {
    TraceMapiCall trace("MAPISendDocuments", ulUIParam, 0);
    return trace.Finish(go_mapi::MapiImpl::MAPISendDocuments(
        ulUIParam, lpszDelimChar, lpszFilePaths, lpszFileNames, ulReserved));
}

}  // extern "C"

// DllMain runs under the Windows loader lock. Keep it inert; all filesystem
// and runtime initialization is performed lazily by exported MAPI calls.
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}
