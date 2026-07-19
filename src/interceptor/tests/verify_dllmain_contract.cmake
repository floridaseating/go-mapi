if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is required")
endif()

file(READ "${SOURCE_FILE}" source_text)
string(FIND "${source_text}" "BOOL APIENTRY DllMain" dllmain_start)
if(dllmain_start EQUAL -1)
    message(FATAL_ERROR "DllMain was not found in ${SOURCE_FILE}")
endif()

# Everything from DllMain to EOF is allow-listed after whitespace removal.
# This intentionally fails if load-time work, callbacks, or helper calls are
# added later; initialization belongs in an exported MAPI call instead.
string(SUBSTRING "${source_text}" ${dllmain_start} -1 dllmain_text)
string(REGEX REPLACE "[ \t\r\n]" "" dllmain_compact "${dllmain_text}")
set(expected "BOOLAPIENTRYDllMain(HMODULE,DWORD,LPVOID){returnTRUE;}")

if(NOT dllmain_compact STREQUAL expected)
    message(FATAL_ERROR
        "DllMain must remain inert. Expected '${expected}', got '${dllmain_compact}'")
endif()
