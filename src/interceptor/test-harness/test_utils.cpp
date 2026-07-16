#include "test_utils.h"
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <type_traits>

namespace fs = std::filesystem;

namespace mapi_test {

namespace {

const char* kHarnessOrigin = "\"originApp\":\"go-mapi-test-harness.exe\"";

bool IsHarnessJson(const fs::path& path) {
    if (!fs::is_regular_file(path) || path.extension() != ".json") {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str().find(kHarnessOrigin) != std::string::npos;
}

fs::path HarnessFixtureDir() {
    wchar_t tempPath[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH) return {};
    return fs::path(tempPath) /
        (L"go-mapi-test-harness-" + std::to_wstring(GetCurrentProcessId()));
}

template <typename String>
String CreateFixture(const fs::path& relativePath) {
    try {
        fs::path fixtureDir = HarnessFixtureDir();
        if (fixtureDir.empty()) return {};
        fs::path fullPath = fixtureDir / relativePath;
        fs::create_directories(fullPath.parent_path());
        std::ofstream file(fullPath, std::ios::binary | std::ios::trunc);
        if (!file) return {};
        file << "go-mapi test attachment\n";
        file.close();
        if (!file) return {};
        if constexpr (std::is_same_v<String, std::wstring>) {
            return fullPath.wstring();
        } else {
            return fullPath.string();
        }
    } catch (...) {
        return {};
    }
}

} // namespace

MAPISendMailFunc TestUtilities::LoadMAPISendMail(const std::string& dllPath) {
    HMODULE hDll = LoadLibraryA(dllPath.c_str());
    if (!hDll) {
        std::cerr << "Failed to load DLL: " << dllPath << std::endl;
        return nullptr;
    }

    MAPISendMailFunc func = reinterpret_cast<MAPISendMailFunc>(
        GetProcAddress(hDll, "MAPISendMail")
    );

    if (!func) {
        std::cerr << "Failed to get MAPISendMail function pointer" << std::endl;
        FreeLibrary(hDll);
        return nullptr;
    }

    return func;
}

bool TestUtilities::VerifyJsonFileCreated(const std::string& tempDir) {
    try {
        for (const auto& entry : fs::directory_iterator(tempDir)) {
            if (IsHarnessJson(entry.path())) {
                std::cout << "Found JSON file: " << entry.path().filename().string() << std::endl;
                return true;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error checking directory: " << e.what() << std::endl;
    }

    return false;
}

bool TestUtilities::ValidateJsonFile(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Check for required fields
        std::vector<std::string> requiredFields = {
            "\"version\"",
            "\"timestamp\"",
            "\"subject\"",
            "\"body\"",
            "\"bodyFormat\"",
            "\"recipients\"",
            "\"attachments\"",
            "\"originApp\""
        };

        for (const auto& field : requiredFields) {
            if (content.find(field) == std::string::npos) {
                std::cerr << "Missing required field: " << field << std::endl;
                return false;
            }
        }

        // Check for valid JSON structure
        if (content.front() != '{' || content.back() != '}') {
            std::cerr << "Invalid JSON structure" << std::endl;
            return false;
        }

        std::cout << "JSON file valid: " << filePath << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error validating JSON: " << e.what() << std::endl;
        return false;
    }
}

void TestUtilities::CleanupTestFiles(const std::string& tempDir) {
    try {
        if (!fs::exists(tempDir)) return;
        std::vector<fs::path> harnessJsonFiles;
        for (const auto& entry : fs::directory_iterator(tempDir)) {
            if (IsHarnessJson(entry.path())) {
                harnessJsonFiles.push_back(entry.path());
            }
        }
        for (const auto& jsonPath : harnessJsonFiles) {
            fs::path attachmentDir = jsonPath.parent_path() / jsonPath.stem();
            fs::remove(jsonPath);
            if (fs::is_directory(attachmentDir)) {
                fs::remove_all(attachmentDir);
            }
            std::cout << "Deleted: " << jsonPath.filename().string() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error cleaning up files: " << e.what() << std::endl;
    }
}

std::string TestUtilities::GetGoMapiTempDir() {
    char localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathA(
            nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT,
            localAppData))) {
        return "";
    }
    return (fs::path(localAppData) / "go-mapi" / "queue").string();
}

std::string TestUtilities::CreateAttachmentFixture(
    const std::string& relativePath) {
    return CreateFixture<std::string>(fs::path(relativePath));
}

std::wstring TestUtilities::CreateWideAttachmentFixture(
    const std::wstring& relativePath) {
    return CreateFixture<std::wstring>(fs::path(relativePath));
}

void TestUtilities::CleanupAttachmentFixtures() {
    try {
        fs::path fixtureDir = HarnessFixtureDir();
        if (!fixtureDir.empty()) fs::remove_all(fixtureDir);
    } catch (...) {
    }
}

void TestUtilities::PrintTestResult(const std::string& testName, bool passed) {
    if (passed) {
        std::cout << "\n✓ [PASS] " << testName << std::endl;
    } else {
        std::cerr << "\n✗ [FAIL] " << testName << std::endl;
    }
}

int TestUtilities::GetJsonFileCount(const std::string& tempDir) {
    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(tempDir)) {
            if (IsHarnessJson(entry.path())) {
                count++;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error counting files: " << e.what() << std::endl;
    }
    return count;
}

std::string TestUtilities::ReadNewestJsonContent(const std::string& tempDir) {
    std::string newestPath;
    std::filesystem::file_time_type newestTime{};

    try {
        for (const auto& entry : fs::directory_iterator(tempDir)) {
            if (IsHarnessJson(entry.path())) {
                auto wt = entry.last_write_time();
                if (newestPath.empty() || wt > newestTime) {
                    newestTime = wt;
                    newestPath = entry.path().string();
                }
            }
        }
    } catch (...) {}

    if (newestPath.empty()) return "";

    std::ifstream file(newestPath);
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

}  // namespace mapi_test
