#include <windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include "../test_utils.h"

using namespace mapi_test;

// Test that when lpszFileName is NULL (common with "Send to → Mail recipient"),
// the DLL extracts the filename from lpszPathName.
int test_null_filename() {
    std::cout << "\nTest: Null Filename (extract from path)" << std::endl;

    HMODULE hDll = LoadLibraryA("go-mapi.dll");
    if (!hDll) {
        std::cerr << "Failed to load go-mapi.dll" << std::endl;
        return 1;
    }

    MAPISendMailFunc MAPISendMail = reinterpret_cast<MAPISendMailFunc>(
        GetProcAddress(hDll, "MAPISendMail")
    );
    if (!MAPISendMail) {
        std::cerr << "Failed to get MAPISendMail function" << std::endl;
        FreeLibrary(hDll);
        return 1;
    }

    // Clean up first
    std::string tempDir = TestUtilities::GetGoMapiTempDir();
    TestUtilities::CleanupTestFiles(tempDir);

    char subject[] = "Test with null filename";
    char body[] = "Attachment has path but no filename";
    char toAddress[] = "test@example.com";
    char toName[] = "Test User";

    // Attachment with lpszPathName set but lpszFileName = NULL
    // This is what Windows "Send to → Mail recipient" does
    char filePath[] = "C:\\Users\\marc\\Documents\\PyG_BGBL_GLOBAL_SL.xlsx";

    MapiFileDesc attachment = {};
    attachment.nPosition = static_cast<ULONG>(-1);
    attachment.lpszPathName = filePath;
    attachment.lpszFileName = nullptr;  // NULL — the bug case

    MapiRecipDesc recipient = {};
    recipient.ulRecipClass = MAPI_TO;
    recipient.lpszName = toName;
    recipient.lpszAddress = toAddress;

    MapiMessage message = {};
    message.lpszSubject = subject;
    message.lpszNoteText = body;
    message.nRecipCount = 1;
    message.lpRecips = &recipient;
    message.nFileCount = 1;
    message.lpFiles = &attachment;

    ULONG result = MAPISendMail(0, 0, &message, 0, 0);
    std::cout << "MAPISendMail returned: " << result << std::endl;

    if (result != 0) {
        std::cerr << "MAPISendMail failed" << std::endl;
        FreeLibrary(hDll);
        return 1;
    }

    // Read the JSON and verify the filename was extracted from the path
    std::string json = TestUtilities::ReadNewestJsonContent(tempDir);
    if (json.empty()) {
        std::cerr << "No JSON file found" << std::endl;
        FreeLibrary(hDll);
        return 1;
    }

    bool hasFilename = json.find("\"filename\":\"PyG_BGBL_GLOBAL_SL.xlsx\"") != std::string::npos;
    bool hasPath = json.find("PyG_BGBL_GLOBAL_SL.xlsx") != std::string::npos;

    if (hasFilename) {
        std::cout << "  Filename correctly extracted from path" << std::endl;
    } else {
        std::cerr << "  Filename NOT extracted from path!" << std::endl;
        auto pos = json.find("filename");
        if (pos != std::string::npos) {
            std::cerr << "  JSON filename area: " << json.substr(pos, 80) << std::endl;
        }
    }

    // Also test with MAPISendMailW (wide version)
    TestUtilities::CleanupTestFiles(tempDir);

    typedef ULONG (WINAPI *MAPISendMailWFunc)(
        LHANDLE, ULONG_PTR, void*, ULONG, ULONG
    );
    MAPISendMailWFunc MAPISendMailW = reinterpret_cast<MAPISendMailWFunc>(
        GetProcAddress(hDll, "MAPISendMailW")
    );

    bool hasFilenameW = false;
    if (MAPISendMailW) {
        wchar_t wSubject[] = L"Test wide null filename";
        wchar_t wBody[] = L"Wide attachment test";
        wchar_t wToAddr[] = L"test@example.com";
        wchar_t wToName[] = L"Test User";
        wchar_t wFilePath[] = L"C:\\Users\\marc\\Documents\\Informe_año_2025.pdf";

        MapiRecipDescW recipW = {};
        recipW.ulRecipClass = MAPI_TO;
        recipW.lpszName = wToName;
        recipW.lpszAddress = wToAddr;

        MapiFileDescW fileW = {};
        fileW.nPosition = static_cast<ULONG>(-1);
        fileW.lpszPathName = wFilePath;
        fileW.lpszFileName = nullptr;  // NULL

        MapiMessageW msgW = {};
        msgW.lpszSubject = wSubject;
        msgW.lpszNoteText = wBody;
        msgW.nRecipCount = 1;
        msgW.lpRecips = &recipW;
        msgW.nFileCount = 1;
        msgW.lpFiles = &fileW;

        ULONG resultW = MAPISendMailW(0, 0, &msgW, 0, 0);
        std::cout << "MAPISendMailW returned: " << resultW << std::endl;

        std::string jsonW = TestUtilities::ReadNewestJsonContent(tempDir);
        // In UTF-8, ñ is \xC3\xB1, so "año" is "a\xC3\xB1o"
        hasFilenameW = jsonW.find("Informe_a") != std::string::npos
                    && jsonW.find("o_2025.pdf") != std::string::npos;

        if (hasFilenameW) {
            std::cout << "  Wide filename correctly extracted from path" << std::endl;
        } else {
            std::cerr << "  Wide filename NOT extracted!" << std::endl;
        }
    }

    TestUtilities::CleanupTestFiles(tempDir);
    FreeLibrary(hDll);
    return (hasFilename && hasFilenameW) ? 0 : 1;
}
