if(NOT DEFINED IMPL_FILE OR NOT DEFINED TYPES_FILE)
    message(FATAL_ERROR "IMPL_FILE and TYPES_FILE are required")
endif()

file(READ "${IMPL_FILE}" impl_text)
file(READ "${TYPES_FILE}" types_text)

set(required_impl_tokens
    "CoTaskMemAlloc"
    "CoTaskMemFree"
    "SMTP:"
)
foreach(token IN LISTS required_impl_tokens)
    string(FIND "${impl_text}" "${token}" token_index)
    if(token_index EQUAL -1)
        message(FATAL_ERROR "ResolveName ownership contract is missing ${token}")
    endif()
endforeach()

string(FIND "${types_text}" "MAPI_E_UNKNOWN_RECIPIENT" unknown_recipient_index)
if(unknown_recipient_index EQUAL -1)
    message(FATAL_ERROR "ResolveName must expose MAPI_E_UNKNOWN_RECIPIENT")
endif()
