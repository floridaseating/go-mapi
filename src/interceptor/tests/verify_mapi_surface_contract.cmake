if(NOT DEFINED EXPORTS_FILE OR NOT DEFINED TYPES_FILE)
    message(FATAL_ERROR "EXPORTS_FILE and TYPES_FILE are required")
endif()

file(READ "${EXPORTS_FILE}" exports_text)
set(required_exports
    MAPILogon
    MAPILogoff
    MAPISendMail
    MAPISendMailW
    MAPISendDocuments
    MAPIFindNext
    MAPIReadMail
    MAPISaveMail
    MAPIDeleteMail
    MAPIAddress
    MAPIDetails
    MAPIResolveName
    MAPIFreeBuffer
)

set(missing_exports "")
foreach(export_name IN LISTS required_exports)
    string(REGEX MATCH "(^|[\r\n])[ \t]*${export_name}([ \t\r\n]|$)"
        export_match "${exports_text}")
    if(NOT export_match)
        list(APPEND missing_exports "${export_name}")
    endif()
endforeach()

if(missing_exports)
    list(JOIN missing_exports ", " missing_text)
    message(FATAL_ERROR "Missing documented Simple-MAPI exports: ${missing_text}")
endif()

file(READ "${TYPES_FILE}" types_text)
string(REGEX MATCH
    "#[ \t]*define[ \t]+MAPI_E_INVALID_MESSAGE[ \t]+17([^0-9]|$)"
    invalid_message_match "${types_text}")
if(NOT invalid_message_match)
    message(FATAL_ERROR "MAPI_E_INVALID_MESSAGE must match the SDK value 17")
endif()

string(REGEX MATCH
    "#[ \t]*define[ \t]+MAPI_E_NOT_SUPPORTED[ \t]+26([^0-9]|$)"
    not_supported_match "${types_text}")
if(NOT not_supported_match)
    message(FATAL_ERROR "MAPI_E_NOT_SUPPORTED must match the SDK value 26")
endif()
