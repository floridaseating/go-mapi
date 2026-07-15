#include "diagnostic_trace.h"

#include <windows.h>
#include <shlobj.h>

namespace go_mapi {
namespace {

constexpr LONGLONG kMaxTraceBytes = 1024 * 1024;
constexpr DWORD kTraceLockTimeoutMs = 2000;
constexpr wchar_t kTraceMutexName[] =
    L"Local\\FloridaSeating.GoMapi.DiagnosticTrace";

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE value) : value_(value) {}
    ~ScopedHandle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }

    HANDLE get() const { return value_; }

private:
    HANDLE value_;
};

} // namespace

std::wstring DiagnosticTrace::GetTracePath() {
    wchar_t localAppData[MAX_PATH];
    const HRESULT result = SHGetFolderPathW(
        nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData);
    if (FAILED(result)) return L"";

    std::wstring directory(localAppData);
    directory += L"\\go-mapi\\diagnostics";
    const int createResult = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (createResult != ERROR_SUCCESS &&
        createResult != ERROR_ALREADY_EXISTS &&
        createResult != ERROR_FILE_EXISTS) {
        return L"";
    }

    return directory + L"\\mapi-calls.jsonl";
}

bool DiagnosticTrace::Append(const MapiCallTrace& trace) {
    const std::wstring tracePath = GetTracePath();
    if (tracePath.empty()) return false;

    ScopedHandle mutex(CreateMutexW(nullptr, FALSE, kTraceMutexName));
    if (!mutex.get()) return false;

    const DWORD waitResult = WaitForSingleObject(mutex.get(), kTraceLockTimeoutMs);
    if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) return false;

    const HANDLE rawFile = CreateFileW(
        tracePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ScopedHandle file(rawFile);
    if (file.get() == INVALID_HANDLE_VALUE) {
        ReleaseMutex(mutex.get());
        return false;
    }

    std::string line = ToJson(trace);
    line.push_back('\n');

    LARGE_INTEGER size{};
    bool success = GetFileSizeEx(file.get(), &size) != FALSE;
    if (success && size.QuadPart + static_cast<LONGLONG>(line.size()) > kMaxTraceBytes) {
        LARGE_INTEGER start{};
        success = SetFilePointerEx(file.get(), start, nullptr, FILE_BEGIN) != FALSE &&
                  SetEndOfFile(file.get()) != FALSE;
    }

    LARGE_INTEGER end{};
    if (success) {
        success = SetFilePointerEx(file.get(), end, nullptr, FILE_END) != FALSE;
    }

    DWORD written = 0;
    if (success) {
        success = WriteFile(
            file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != FALSE &&
            written == static_cast<DWORD>(line.size());
    }
    if (success) FlushFileBuffers(file.get());

    ReleaseMutex(mutex.get());
    return success;
}

} // namespace go_mapi
