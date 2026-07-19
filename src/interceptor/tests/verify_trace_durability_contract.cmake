if(NOT DEFINED WINDOWS_TRACE_FILE OR NOT DEFINED MAIN_FILE)
    message(FATAL_ERROR "WINDOWS_TRACE_FILE and MAIN_FILE are required")
endif()

file(READ "${WINDOWS_TRACE_FILE}" windows_trace_source)
file(READ "${MAIN_FILE}" main_source)

foreach(required_text IN ITEMS
    "class ScopedMutexOwnership"
    "success = FlushFileBuffers(file.get()) != FALSE;"
    "entryRecorded_ = go_mapi::DiagnosticTrace::Append(trace_);"
    "if (!enabled_ || !entryRecorded_) return result;"
)
    string(FIND "${windows_trace_source}${main_source}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing trace durability contract: ${required_text}")
    endif()
endforeach()

string(FIND "${windows_trace_source}" "ReleaseMutex(mutex.get())" early_release_position)
if(NOT early_release_position EQUAL -1)
    message(FATAL_ERROR "Trace mutex is released before the file handle closes")
endif()
