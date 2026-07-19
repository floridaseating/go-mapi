#include "mapi_impl.h"
#include "message_converter.h"
#include "fs_utils.h"
#include "json_writer.h"
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ole32.lib")

namespace go_mapi {

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
    if (!lpMessage) {
        return MAPI_E_INVALID_MESSAGE;
    }

    try {
        MailMessage msg = message_converter::ConvertAnsiMessage(*lpMessage);
        // originApp populated here because it requires live process context
        // (out of scope for the pure message_converter module).
        msg.originApp = GetOriginApplicationName();

        // QUICK-260423-tk6: copy attachments into a queue-owned sibling dir
        // BEFORE writing the JSON. The legacy Spanish MAPI caller deletes its
        // own TEMP directory as soon as this function returns, so the Wails
        // app would otherwise see "attachment not found" on draft creation.
        std::wstring stem = FsUtils::GenerateUniqueStem();
        if (!CopyAttachmentsForStem(msg, stem)) {
            // Error file already written by CopyAttachmentsForStem; do NOT
            // write the JSON — half-a-message would silently drop attachments.
            return MAPI_E_FAILURE;
        }
        std::wstring filePath = JsonWriter::WriteMailToFileWithStem(msg, stem);

        if (filePath.empty()) {
            return MAPI_E_FAILURE;
        }

        return SUCCESS_SUCCESS;
    } catch (...) {
        return MAPI_E_FAILURE;
    }
}

ULONG MapiImpl::MAPISendMailW(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessageW lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    if (!lpMessage) {
        return MAPI_E_INVALID_MESSAGE;
    }

    try {
        MailMessage msg = message_converter::ConvertWideMessage(*lpMessage);
        // originApp populated here because it requires live process context
        // (out of scope for the pure message_converter module).
        msg.originApp = GetOriginApplicationName();

        // QUICK-260423-tk6: same lifetime fix as the ANSI path — copy
        // attachments into %LOCALAPPDATA%\go-mapi\queue\<stem>\ before the
        // caller's TEMP dir disappears on return.
        std::wstring stem = FsUtils::GenerateUniqueStem();
        if (!CopyAttachmentsForStem(msg, stem)) {
            return MAPI_E_FAILURE;
        }
        std::wstring filePath = JsonWriter::WriteMailToFileWithStem(msg, stem);

        if (filePath.empty()) {
            return MAPI_E_FAILURE;
        }

        return SUCCESS_SUCCESS;
    } catch (...) {
        return MAPI_E_FAILURE;
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
    CoTaskMemFree(pv);
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
    ULONG* lpnNewRecips,
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
    if (!lppRecip) {
        return MAPI_E_FAILURE;
    }
    *lppRecip = nullptr;
    if (ulReserved != 0) {
        return MAPI_E_FAILURE;
    }
    if (!lpszName || lpszName[0] == '\0') {
        return MAPI_E_UNKNOWN_RECIPIENT;
    }

    const size_t inputLength = std::strlen(lpszName);
    const bool hasSmtpPrefix = inputLength >= 5 &&
        (lpszName[0] == 'S' || lpszName[0] == 's') &&
        (lpszName[1] == 'M' || lpszName[1] == 'm') &&
        (lpszName[2] == 'T' || lpszName[2] == 't') &&
        (lpszName[3] == 'P' || lpszName[3] == 'p') &&
        lpszName[4] == ':';
    const char* displayName = hasSmtpPrefix ? lpszName + 5 : lpszName;
    const size_t displayLength = hasSmtpPrefix ? inputLength - 5 : inputLength;
    if (displayLength < 3) {
        return MAPI_E_UNKNOWN_RECIPIENT;
    }

    size_t atIndex = displayLength;
    for (size_t index = 0; index < displayLength; ++index) {
        const unsigned char character =
            static_cast<unsigned char>(displayName[index]);
        if (character <= 0x20 || character == 0x7f) {
            return MAPI_E_UNKNOWN_RECIPIENT;
        }
        if (character == '@') {
            if (atIndex != displayLength) {
                return MAPI_E_UNKNOWN_RECIPIENT;
            }
            atIndex = index;
        }
    }
    if (atIndex == 0 || atIndex >= displayLength - 1) {
        return MAPI_E_UNKNOWN_RECIPIENT;
    }

    const size_t maxSize = std::numeric_limits<size_t>::max();
    if (!hasSmtpPrefix && inputLength > maxSize - 5) {
        return MAPI_E_INSUFFICIENT_MEMORY;
    }
    const size_t addressLength = hasSmtpPrefix
        ? inputLength
        : inputLength + 5;
    size_t allocationSize = sizeof(MapiRecipDesc);
    const auto addAllocationBytes = [&allocationSize, maxSize](size_t amount) {
        if (amount > maxSize - allocationSize) {
            return false;
        }
        allocationSize += amount;
        return true;
    };
    if (!addAllocationBytes(displayLength) ||
        !addAllocationBytes(1) ||
        !addAllocationBytes(addressLength) ||
        !addAllocationBytes(1)) {
        return MAPI_E_INSUFFICIENT_MEMORY;
    }

    auto* descriptor = static_cast<LPMapiRecipDesc>(
        CoTaskMemAlloc(allocationSize));
    if (!descriptor) {
        return MAPI_E_INSUFFICIENT_MEMORY;
    }
    std::memset(descriptor, 0, allocationSize);

    char* cursor = reinterpret_cast<char*>(descriptor) + sizeof(*descriptor);
    descriptor->lpszName = cursor;
    std::memcpy(cursor, displayName, displayLength);
    cursor[displayLength] = '\0';
    cursor += displayLength + 1;

    descriptor->lpszAddress = cursor;
    if (hasSmtpPrefix) {
        std::memcpy(cursor, lpszName, inputLength);
    } else {
        std::memcpy(cursor, "SMTP:", 5);
        std::memcpy(cursor + 5, lpszName, inputLength);
    }
    cursor[addressLength] = '\0';

    descriptor->ulRecipClass = MAPI_TO;
    *lppRecip = descriptor;
    return SUCCESS_SUCCESS;
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
