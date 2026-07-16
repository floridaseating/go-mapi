#include <iostream>
#include <string>
#include <vector>
#include "../test_utils.h"

// Forward declarations of test functions
extern int test_simple_send();
extern int test_with_attachments();
extern int test_unicode();
extern int test_multiple_recipients();
extern int test_unicode_wide();
extern int test_ansi_encoding();
extern int test_null_filename();

using namespace mapi_test;

int main(int argc, char* argv[]) {
    std::cout << "=================================" << std::endl;
    std::cout << "  go-mapi MAPI Test Harness" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << std::endl;

    // Determine DLL path
    std::string dllPath = "go-mapi.dll";
    if (argc > 1) {
        dllPath = argv[1];
    }

    std::cout << "Using DLL: " << dllPath << std::endl;
    std::cout << std::endl;

    // Get the temp directory
    std::string tempDir = TestUtilities::GetGoMapiTempDir();
    if (tempDir.empty()) {
        std::cerr << "Failed to get temp directory" << std::endl;
        return 1;
    }

    std::cout << "Monitoring: " << tempDir << std::endl;
    std::cout << std::endl;

    // Run tests
    int testsPassed = 0;
    int testsFailed = 0;

    std::vector<std::pair<std::string, int(*)()>> tests = {
        { "Simple Send", test_simple_send },
        { "With Attachments", test_with_attachments },
        { "Unicode (ANSI)", test_unicode },
        { "Unicode (Wide/MAPISendMailW)", test_unicode_wide },
        { "Multiple Recipients", test_multiple_recipients },
        { "ANSI Codepage Encoding", test_ansi_encoding },
        { "Null Filename (path fallback)", test_null_filename },
    };

    for (const auto& test : tests) {
        int result = test.second();
        if (result == 0) {
            testsPassed++;
            TestUtilities::PrintTestResult(test.first, true);
        } else {
            testsFailed++;
            TestUtilities::PrintTestResult(test.first, false);
        }
    }

    std::cout << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << "Results: " << testsPassed << " passed, " << testsFailed << " failed" << std::endl;
    std::cout << "=================================" << std::endl;

    return (testsFailed > 0) ? 1 : 0;
}
