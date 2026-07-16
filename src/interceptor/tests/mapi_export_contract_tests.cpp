#include <windows.h>
#include <mapi.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

static_assert(sizeof(MapiFileDesc) == (sizeof(void*) == 8 ? 40 : 24));
static_assert(sizeof(MapiRecipDesc) == (sizeof(void*) == 8 ? 40 : 24));
static_assert(sizeof(MapiMessage) == (sizeof(void*) == 8 ? 96 : 48));
static_assert(sizeof(MapiFileDescW) == (sizeof(void*) == 8 ? 40 : 24));
static_assert(sizeof(MapiRecipDescW) == (sizeof(void*) == 8 ? 40 : 24));
static_assert(sizeof(MapiMessageW) == (sizeof(void*) == 8 ? 96 : 48));

template <typename T>
T RequiredFunction(HMODULE module, const char* name) {
    auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!function) {
        std::cerr << "missing Simple-MAPI export: " << name << '\n';
    }
    return function;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mapi_export_contract_tests <go-mapi.dll>\n";
        return 2;
    }

    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) {
        std::cerr << "LoadLibraryA failed for " << argv[1]
                  << " (error " << GetLastError() << ")\n";
        return 1;
    }

    constexpr std::array<const char*, 13> requiredExports = {
        "MAPILogon", "MAPILogoff", "MAPISendMail", "MAPISendMailW",
        "MAPISendDocuments", "MAPIFindNext", "MAPIReadMail", "MAPISaveMail",
        "MAPIDeleteMail", "MAPIAddress", "MAPIDetails", "MAPIResolveName",
        "MAPIFreeBuffer",
    };
    for (const char* name : requiredExports) {
        if (!GetProcAddress(module, name)) {
            std::cerr << "missing documented Simple-MAPI export: " << name << '\n';
            FreeLibrary(module);
            return 1;
        }
    }

    auto logon = RequiredFunction<LPMAPILOGON>(module, "MAPILogon");
    auto logoff = RequiredFunction<LPMAPILOGOFF>(module, "MAPILogoff");
    auto sendMail = RequiredFunction<LPMAPISENDMAIL>(module, "MAPISendMail");
    auto sendMailW = RequiredFunction<LPMAPISENDMAILW>(module, "MAPISendMailW");
    auto resolveName = RequiredFunction<LPMAPIRESOLVENAME>(
        module, "MAPIResolveName");
    auto freeBuffer = RequiredFunction<LPMAPIFREEBUFFER>(module, "MAPIFreeBuffer");
    auto sendDocuments = RequiredFunction<LPMAPISENDDOCUMENTS>(
        module, "MAPISendDocuments");
    auto findNext = RequiredFunction<LPMAPIFINDNEXT>(module, "MAPIFindNext");
    auto readMail = RequiredFunction<LPMAPIREADMAIL>(module, "MAPIReadMail");
    auto saveMail = RequiredFunction<LPMAPISAVEMAIL>(module, "MAPISaveMail");
    auto deleteMail = RequiredFunction<LPMAPIDELETEMAIL>(
        module, "MAPIDeleteMail");
    auto address = RequiredFunction<LPMAPIADDRESS>(module, "MAPIAddress");
    auto details = RequiredFunction<LPMAPIDETAILS>(module, "MAPIDetails");
    if (!logon || !logoff || !sendMail || !sendMailW || !resolveName ||
        !freeBuffer || !sendDocuments || !findNext || !readMail || !saveMail ||
        !deleteMail || !address || !details) {
        FreeLibrary(module);
        return 1;
    }

    if (logon(0, nullptr, nullptr, 0, 0, nullptr) != MAPI_E_FAILURE) {
        std::cerr << "MAPILogon accepted a null session output\n";
        FreeLibrary(module);
        return 1;
    }

    LHANDLE session = 0;
    if (logon(0, nullptr, nullptr, 0, 0, &session) != SUCCESS_SUCCESS ||
        session == 0) {
        std::cerr << "MAPILogon did not return a usable session\n";
        FreeLibrary(module);
        return 1;
    }

    if (sendMail(session, 0, nullptr, 0, 0) != MAPI_E_INVALID_MESSAGE ||
        sendMailW(session, 0, nullptr, 0, 0) != MAPI_E_INVALID_MESSAGE) {
        std::cerr << "MAPISendMail null-message contract does not match MAPI.h\n";
        FreeLibrary(module);
        return 1;
    }

    char testAddress[] = "mapi-canary@example.invalid";
    lpMapiRecipDesc recipient = nullptr;
    const ULONG resolveResult = resolveName(
        session, 0, testAddress, 0, 0, &recipient);
    if (resolveResult != SUCCESS_SUCCESS ||
        recipient == nullptr ||
        recipient->lpszName !=
            reinterpret_cast<char*>(recipient) + sizeof(MapiRecipDesc) ||
        recipient->ulReserved != 0 ||
        recipient->ulRecipClass != MAPI_TO ||
        recipient->lpszName == nullptr ||
        std::strcmp(recipient->lpszName, testAddress) != 0 ||
        recipient->lpszAddress == nullptr ||
        recipient->lpszAddress !=
            recipient->lpszName + std::strlen(recipient->lpszName) + 1 ||
        std::strcmp(recipient->lpszAddress,
                    "SMTP:mapi-canary@example.invalid") != 0 ||
        recipient->ulEIDSize != 0 ||
        recipient->lpEntryID != nullptr) {
        std::cerr << "MAPIResolveName did not return a valid recipient\n";
        if (recipient != nullptr) {
            freeBuffer(recipient);
        }
        FreeLibrary(module);
        return 1;
    }
    if (freeBuffer(recipient) != SUCCESS_SUCCESS) {
        std::cerr << "MAPIFreeBuffer rejected the resolved recipient\n";
        FreeLibrary(module);
        return 1;
    }
    recipient = nullptr;

    lpMapiRecipDesc invalidRecipient = reinterpret_cast<lpMapiRecipDesc>(
        static_cast<uintptr_t>(1));
    if (resolveName(session, 0, nullptr, 0, 0, &invalidRecipient) !=
            MAPI_E_UNKNOWN_RECIPIENT ||
        invalidRecipient != nullptr ||
        resolveName(session, 0, testAddress, 0, 0, nullptr) != MAPI_E_FAILURE) {
        std::cerr << "MAPIResolveName invalid-input contract failed\n";
        FreeLibrary(module);
        return 1;
    }

    char emptyAddress[] = "";
    invalidRecipient = reinterpret_cast<lpMapiRecipDesc>(
        static_cast<uintptr_t>(1));
    if (resolveName(session, 0, emptyAddress, 0, 0, &invalidRecipient) !=
            MAPI_E_UNKNOWN_RECIPIENT ||
        invalidRecipient != nullptr) {
        std::cerr << "MAPIResolveName accepted an empty address\n";
        FreeLibrary(module);
        return 1;
    }

    char malformedAddress[] = "one@example@invalid";
    invalidRecipient = reinterpret_cast<lpMapiRecipDesc>(
        static_cast<uintptr_t>(1));
    if (resolveName(session, 0, malformedAddress, 0, 0, &invalidRecipient) !=
            MAPI_E_UNKNOWN_RECIPIENT ||
        invalidRecipient != nullptr) {
        std::cerr << "MAPIResolveName accepted a malformed address\n";
        FreeLibrary(module);
        return 1;
    }
    invalidRecipient = reinterpret_cast<lpMapiRecipDesc>(
        static_cast<uintptr_t>(1));
    if (resolveName(session, 0, testAddress, 0, 1, &invalidRecipient) !=
            MAPI_E_FAILURE ||
        invalidRecipient != nullptr) {
        std::cerr << "MAPIResolveName accepted a nonzero reserved value\n";
        FreeLibrary(module);
        return 1;
    }

    char prefixedAddress[] = "SMTP:prefixed@example.invalid";
    lpMapiRecipDesc prefixedRecipient = nullptr;
    if (resolveName(session, 0, prefixedAddress, 0, 0, &prefixedRecipient) !=
            SUCCESS_SUCCESS ||
        prefixedRecipient == nullptr ||
        prefixedRecipient->lpszName != reinterpret_cast<char*>(
            prefixedRecipient) + sizeof(MapiRecipDesc) ||
        std::strcmp(prefixedRecipient->lpszName,
                    "prefixed@example.invalid") != 0 ||
        prefixedRecipient->lpszAddress != prefixedRecipient->lpszName +
            std::strlen(prefixedRecipient->lpszName) + 1 ||
        std::strcmp(prefixedRecipient->lpszAddress, prefixedAddress) != 0) {
        if (prefixedRecipient != nullptr) {
            freeBuffer(prefixedRecipient);
        }
        std::cerr << "MAPIResolveName SMTP-prefix contract failed\n";
        FreeLibrary(module);
        return 1;
    }
    if (freeBuffer(prefixedRecipient) != SUCCESS_SUCCESS) {
        std::cerr << "MAPIFreeBuffer rejected prefixed recipient\n";
        FreeLibrary(module);
        return 1;
    }

    if (sendDocuments(0, nullptr, nullptr, nullptr, 0) !=
        MAPI_E_NOT_SUPPORTED) {
        std::cerr << "MAPISendDocuments reported false success\n";
        FreeLibrary(module);
        return 1;
    }

    lpMapiMessage readMessage = reinterpret_cast<lpMapiMessage>(
        static_cast<uintptr_t>(1));
    ULONG newRecipientCount = 1;
    lpMapiRecipDesc newRecipients = reinterpret_cast<lpMapiRecipDesc>(
        static_cast<uintptr_t>(1));
    if (findNext(session, 0, nullptr, nullptr, 0, 0, nullptr) !=
            MAPI_E_NOT_SUPPORTED ||
        readMail(session, 0, nullptr, 0, 0, &readMessage) !=
            MAPI_E_NOT_SUPPORTED ||
        readMessage != nullptr ||
        saveMail(session, 0, nullptr, 0, 0, nullptr) !=
            MAPI_E_NOT_SUPPORTED ||
        deleteMail(session, 0, nullptr, 0, 0) != MAPI_E_NOT_SUPPORTED ||
        address(session, 0, nullptr, 0, nullptr, 0, nullptr, 0, 0,
                &newRecipientCount, &newRecipients) != MAPI_E_NOT_SUPPORTED ||
        newRecipientCount != 0 || newRecipients != nullptr ||
        details(session, 0, nullptr, 0, 0) != MAPI_E_NOT_SUPPORTED) {
        std::cerr << "Unsupported Simple-MAPI calls did not fail safely\n";
        FreeLibrary(module);
        return 1;
    }

    if (freeBuffer(nullptr) != SUCCESS_SUCCESS ||
        logoff(session, 0, 0, 0) != SUCCESS_SUCCESS) {
        std::cerr << "Simple-MAPI session teardown failed\n";
        FreeLibrary(module);
        return 1;
    }

    FreeLibrary(module);
    return 0;
}
