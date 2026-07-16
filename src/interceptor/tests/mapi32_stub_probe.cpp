#include <windows.h>
#include <mapi.h>
#include <tlhelp32.h>

#include <cstring>
#include <iostream>
#include <iterator>
#include <string>

template <typename T>
T RequiredFunction(HMODULE module, const char* name) {
    auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!function) {
        std::cerr << "missing system MAPI32 export: " << name
                  << " (error " << GetLastError() << ")\n";
    }
    return function;
}

std::wstring SystemMapiPath() {
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::wstring path(systemDirectory, length);
    path += L"\\MAPI32.dll";
    return path;
}

bool ExpectedProviderLoaded(const char* expectedPath) {
    char fullExpectedPath[MAX_PATH] = {};
    const DWORD expectedLength = GetFullPathNameA(
        expectedPath, static_cast<DWORD>(std::size(fullExpectedPath)),
        fullExpectedPath, nullptr);
    if (expectedLength == 0 || expectedLength >= std::size(fullExpectedPath)) {
        std::cerr << "GetFullPathNameA failed for expected provider (error "
                  << GetLastError() << ")\n";
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "CreateToolhelp32Snapshot failed (error "
                  << GetLastError() << ")\n";
        return false;
    }

    MODULEENTRY32A entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstA(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExePath, fullExpectedPath) == 0) {
                found = true;
                std::cout << "loaded provider: " << entry.szExePath << "\n";
                break;
            }
        } while (Module32NextA(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!found) {
        std::cerr << "expected provider was not loaded: "
                  << fullExpectedPath << "\n";
    }
    return found;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mapi32_stub_probe <expected-provider-dll>\n";
        return 2;
    }

    const std::wstring mapiPath = SystemMapiPath();
    if (mapiPath.empty()) {
        std::cerr << "GetSystemDirectoryW failed (error " << GetLastError()
                  << ")\n";
        return 1;
    }

    HMODULE module = LoadLibraryExW(
        mapiPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        std::wcerr << L"LoadLibraryExW failed for " << mapiPath
                   << L" (error " << GetLastError() << L")\n";
        return 1;
    }

    wchar_t loadedPath[MAX_PATH] = {};
    const DWORD loadedLength = GetModuleFileNameW(
        module, loadedPath, static_cast<DWORD>(std::size(loadedPath)));
    if (loadedLength == 0 || loadedLength >= std::size(loadedPath)) {
        std::cerr << "GetModuleFileNameW failed (error " << GetLastError()
                  << ")\n";
        FreeLibrary(module);
        return 1;
    }
    std::wcout << L"system MAPI32: " << loadedPath << L"\n";
    std::cout << "probe pointer width: " << (sizeof(void*) * 8) << "\n";

    auto logon = RequiredFunction<LPMAPILOGON>(module, "MAPILogon");
    auto resolveName = RequiredFunction<LPMAPIRESOLVENAME>(
        module, "MAPIResolveName");
    auto freeBuffer = RequiredFunction<LPMAPIFREEBUFFER>(
        module, "MAPIFreeBuffer");
    auto logoff = RequiredFunction<LPMAPILOGOFF>(module, "MAPILogoff");
    if (!logon || !resolveName || !freeBuffer || !logoff) {
        FreeLibrary(module);
        return 1;
    }

    LHANDLE session = 0;
    ULONG result = logon(0, nullptr, nullptr, 0, 0, &session);
    std::cout << "MAPILogon returned: " << result << "\n";
    if (result != SUCCESS_SUCCESS || session == 0) {
        FreeLibrary(module);
        return 1;
    }

    char sentinel[] = "sentinel@example.invalid";
    lpMapiRecipDesc recipient = nullptr;
    result = resolveName(session, 0, sentinel, 0, 0, &recipient);
    std::cout << "MAPIResolveName returned: " << result << "\n";

    int exitCode = 1;
    if (result == SUCCESS_SUCCESS &&
        recipient != nullptr &&
        recipient->lpszName ==
            reinterpret_cast<char*>(recipient) + sizeof(MapiRecipDesc) &&
        recipient->ulReserved == 0 &&
        recipient->ulRecipClass == MAPI_TO &&
        recipient->lpszName != nullptr &&
        std::strcmp(recipient->lpszName, sentinel) == 0 &&
        recipient->lpszAddress != nullptr &&
        recipient->lpszAddress ==
            recipient->lpszName + std::strlen(recipient->lpszName) + 1 &&
        std::strcmp(recipient->lpszAddress,
                    "SMTP:sentinel@example.invalid") == 0 &&
        recipient->ulEIDSize == 0 &&
        recipient->lpEntryID == nullptr &&
        ExpectedProviderLoaded(argv[1])) {
        result = freeBuffer(recipient);
        recipient = nullptr;
        std::cout << "MAPIFreeBuffer returned: " << result << "\n";
        if (result == SUCCESS_SUCCESS) {
            result = logoff(session, 0, 0, 0);
            session = 0;
            std::cout << "MAPILogoff returned: " << result << "\n";
            if (result == SUCCESS_SUCCESS) {
                exitCode = 0;
            }
        }
    } else {
        std::cerr << "resolved recipient did not match the ownership contract\n";
    }

    if (recipient != nullptr) {
        const ULONG cleanupResult = freeBuffer(recipient);
        std::cout << "cleanup MAPIFreeBuffer returned: " << cleanupResult
                  << "\n";
    }
    if (session != 0) {
        const ULONG cleanupResult = logoff(session, 0, 0, 0);
        std::cout << "cleanup MAPILogoff returned: " << cleanupResult << "\n";
    }
    FreeLibrary(module);
    return exitCode;
}
