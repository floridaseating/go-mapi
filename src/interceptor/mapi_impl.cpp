#include "mapi_impl.h"
#include "diagnostic_trace.h"
#include "message_converter.h"
#include "fs_utils.h"
#include "json_writer.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace go_mapi {

namespace {

uint64_t AttachmentBytes(const MailMessage& msg) {
    uint64_t total = 0;
    for (const auto& attachment : msg.attachments) {
        total += attachment.size;
    }
    return total;
}

class SendCallTrace {
public:
    SendCallTrace(const char* api,
                  ULONG_PTR uiParent,
                  FLAGS flags,
                  uint32_t attachmentCount) noexcept {
        try {
            started_ = std::chrono::steady_clock::now();
            trace_.timestamp = DiagnosticTrace::UtcTimestampNow();
            trace_.api = api;
            trace_.processArchitecture = sizeof(void*) == 8 ? "x64" : "x86";
            trace_.flags = static_cast<uint32_t>(flags);
            trace_.uiParent = static_cast<uint64_t>(uiParent);
            trace_.attachmentCount = attachmentCount;
        } catch (...) {
            enabled_ = false;
        }
    }

    void SetOriginApp(const std::string& originApp) noexcept {
        if (!enabled_) return;
        try {
            trace_.originApp = originApp;
        } catch (...) {
            enabled_ = false;
        }
    }

    ULONG Finish(ULONG result, uint64_t attachmentBytes = 0) noexcept {
        if (!enabled_) return result;
        try {
            trace_.attachmentBytes = attachmentBytes;
            trace_.result = static_cast<uint32_t>(result);
            trace_.durationMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_).count());
            (void)DiagnosticTrace::Append(trace_);
        } catch (...) {
            // Diagnostics are observational only. Preserve the MAPI result.
        }
        return result;
    }

private:
    std::chrono::steady_clock::time_point started_;
    MapiCallTrace trace_;
    bool enabled_ = true;
};

} // namespace

// QUICK-260423-tk6: copy attachments into a stable sibling dir keyed off the
// supplied stem. On success, mutates msg.attachments in-place so each entry's
// `path` points at the new copy and `size` reflects the copied byte count.
// On any failure, best-effort removes partial files and writes the reason to
// errors\<stem>.error so the Wails app surfaces it. Returns true iff every
// attachment landed (or there were none).
//
// Rationale: the legacy Spanish MAPI app deletes its own TEMP dir as soon as
// MAPISendMail returns, so if we leave the original paths in the JSON the
// Wails app later hits "attachment not found" on draft creation.
static bool CopyAttachmentsForStem(MailMessage& msg, const std::wstring& stem) {
    if (msg.attachments.empty()) return true;

    std::wstring attachDir = FsUtils::GetAttachmentsDirForStem(stem);
    if (attachDir.empty()) return false;
    if (!FsUtils::EnsureDirExists(attachDir)) {
        FsUtils::WriteErrorForStem(stem,
            "failed to create attachments directory");
        return false;
    }

    std::vector<std::wstring> landed;  // for rollback on partial failure
    for (auto& att : msg.attachments) {
        if (att.path.empty()) {
            // No path means nothing to copy — leave entry as-is (Gmail side
            // will skip empty-path attachments). Still counts as success.
            continue;
        }
        // Basename fallback: prefer explicit filename, else message_converter
        // already derived it from the path via FilenameFromPath.
        std::string basename = !att.filename.empty() ? att.filename : att.path;

        std::wstring newPath;
        uint32_t newSize = 0;
        if (!FsUtils::CopyFileToDir(att.path, attachDir, basename,
                                    newPath, newSize)) {
            // Roll back any files we already copied this message to keep the
            // queue clean — half-a-message would be worse than nothing.
            for (const auto& p : landed) {
                DeleteFileW(p.c_str());
            }
            RemoveDirectoryW(attachDir.c_str());
            FsUtils::WriteErrorForStem(stem,
                "failed to copy attachment to queue");
            return false;
        }
        landed.push_back(newPath);

        // Rewrite attachment path/size so the Gmail client reads from the
        // stable copy instead of the caller's about-to-be-deleted TEMP.
        int n = WideCharToMultiByte(CP_UTF8, 0, newPath.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string newPathUtf8(n - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, newPath.c_str(), -1,
                                &newPathUtf8[0], n, nullptr, nullptr);
            att.path = newPathUtf8;
        }
        att.size = newSize;
    }
    return true;
}

std::string MapiImpl::GetOriginApplicationName() {
    wchar_t processPath[MAX_PATH];
    HANDLE hProcess = GetCurrentProcess();

    if (GetModuleFileNameExW(hProcess, nullptr, processPath, MAX_PATH)) {
        // Get just the filename from the full path
        wchar_t* filename = wcsrchr(processPath, L'\\');
        if (filename) {
            filename++;  // Move past the backslash
        } else {
            filename = processPath;
        }

        // Convert to UTF-8
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, filename, -1, NULL, 0, NULL, NULL);
        std::string result(size_needed - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size_needed, NULL, NULL);
        return result;
    }

    return "unknown.exe";
}

ULONG MapiImpl::MAPISendMailA(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    SendCallTrace trace(
        "MAPISendMailA", ulUIParam, flFlags,
        lpMessage ? lpMessage->nFileCount : 0);
    if (!lpMessage) {
        return trace.Finish(MAPI_E_INVALID_MESSAGE);
    }

    try {
        MailMessage msg = message_converter::ConvertAnsiMessage(*lpMessage);
        // originApp populated here because it requires live process context
        // (out of scope for the pure message_converter module).
        msg.originApp = GetOriginApplicationName();
        trace.SetOriginApp(msg.originApp);

        // QUICK-260423-tk6: copy attachments into a queue-owned sibling dir
        // BEFORE writing the JSON. The legacy Spanish MAPI caller deletes its
        // own TEMP directory as soon as this function returns, so the Wails
        // app would otherwise see "attachment not found" on draft creation.
        std::wstring stem = FsUtils::GenerateUniqueStem();
        if (!CopyAttachmentsForStem(msg, stem)) {
            // Error file already written by CopyAttachmentsForStem; do NOT
            // write the JSON — half-a-message would silently drop attachments.
            return trace.Finish(MAPI_E_FAILURE, AttachmentBytes(msg));
        }
        std::wstring filePath = JsonWriter::WriteMailToFileWithStem(msg, stem);

        if (filePath.empty()) {
            return trace.Finish(MAPI_E_FAILURE, AttachmentBytes(msg));
        }

        return trace.Finish(SUCCESS_SUCCESS, AttachmentBytes(msg));
    } catch (...) {
        return trace.Finish(MAPI_E_FAILURE);
    }
}

ULONG MapiImpl::MAPISendMailW(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessageW lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    SendCallTrace trace(
        "MAPISendMailW", ulUIParam, flFlags,
        lpMessage ? lpMessage->nFileCount : 0);
    if (!lpMessage) {
        return trace.Finish(MAPI_E_INVALID_MESSAGE);
    }

    try {
        MailMessage msg = message_converter::ConvertWideMessage(*lpMessage);
        // originApp populated here because it requires live process context
        // (out of scope for the pure message_converter module).
        msg.originApp = GetOriginApplicationName();
        trace.SetOriginApp(msg.originApp);

        // QUICK-260423-tk6: same lifetime fix as the ANSI path — copy
        // attachments into %LOCALAPPDATA%\go-mapi\queue\<stem>\ before the
        // caller's TEMP dir disappears on return.
        std::wstring stem = FsUtils::GenerateUniqueStem();
        if (!CopyAttachmentsForStem(msg, stem)) {
            return trace.Finish(MAPI_E_FAILURE, AttachmentBytes(msg));
        }
        std::wstring filePath = JsonWriter::WriteMailToFileWithStem(msg, stem);

        if (filePath.empty()) {
            return trace.Finish(MAPI_E_FAILURE, AttachmentBytes(msg));
        }

        return trace.Finish(SUCCESS_SUCCESS, AttachmentBytes(msg));
    } catch (...) {
        return trace.Finish(MAPI_E_FAILURE);
    }
}

ULONG MapiImpl::MAPILogon(
    ULONG_PTR ulUIParam,
    LPSTR lpszProfileName,
    LPSTR lpszPassword,
    FLAGS flFlags,
    ULONG ulReserved,
    LPLHANDLE lphSession
) {
    if (!lphSession) {
        return MAPI_E_FAILURE;
    }
    *lphSession = 1;
    return SUCCESS_SUCCESS;
}

ULONG MapiImpl::MAPILogoff(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    FLAGS flFlags,
    ULONG ulReserved
) {
    // Stub: just return success
    return SUCCESS_SUCCESS;
}

ULONG MapiImpl::MAPIFreeBuffer(LPVOID pv) {
    // Supported calls do not yet return provider-owned buffers. ResolveName
    // remains fail-safe until ownership is proven through Windows MAPI32.
    (void)pv;
    return SUCCESS_SUCCESS;
}

ULONG MapiImpl::MAPIFindNext(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageType,
    LPSTR lpszSeedMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
) {
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPIReadMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPMapiMessage* lppMessage
) {
    if (lppMessage) {
        *lppMessage = nullptr;
    }
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPISaveMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
) {
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPIDeleteMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPIAddress(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszCaption,
    ULONG nEditFields,
    LPSTR lpszLabels,
    ULONG nRecips,
    LPMapiRecipDesc lpRecips,
    FLAGS flFlags,
    ULONG ulReserved,
    LPULONG lpnNewRecips,
    LPMapiRecipDesc* lppNewRecips
) {
    if (lpnNewRecips) {
        *lpnNewRecips = 0;
    }
    if (lppNewRecips) {
        *lppNewRecips = nullptr;
    }
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPIDetails(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiRecipDesc lpRecip,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPIResolveName(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszName,
    FLAGS flFlags,
    ULONG ulReserved,
    LPMapiRecipDesc* lppRecip
) {
    if (lppRecip) {
        *lppRecip = nullptr;
    }
    return MAPI_E_NOT_SUPPORTED;
}

ULONG MapiImpl::MAPISendDocuments(
    ULONG_PTR ulUIParam,
    LPSTR lpszDelimChar,
    LPSTR lpszFilePaths,
    LPSTR lpszFileNames,
    ULONG ulReserved
) {
    // Do not report success while silently discarding requested documents.
    return MAPI_E_NOT_SUPPORTED;
}

} // namespace go_mapi
