cmake_minimum_required(VERSION 3.16)

# CPPTEST-01: doctest-based unit tests for message_converter.
#
# Only built when -DBUILD_TESTS=ON is passed to the parent CMake. Links
# against the message_converter_obj OBJECT library from the parent
# CMakeLists.txt (FOUND-05), so the test binary exercises the exact
# same translation units as the production DLL with zero recompile.

add_executable(message_converter_tests
    message_converter_tests.cpp
    $<TARGET_OBJECTS:message_converter_obj>
)

target_include_directories(message_converter_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# Place the test binary next to the DLL and harness for consistency.
set_target_properties(message_converter_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Match the parent DLL target's compiler flags so the linked object files
# are ABI-compatible. These flags are purely compile-time — we keep them
# identical as a defensive measure.
if(MSVC)
    target_compile_options(message_converter_tests PRIVATE /W4 /WX-)
    target_link_libraries(message_converter_tests PRIVATE kernel32.lib user32.lib)
elseif(MINGW)
    target_compile_options(message_converter_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(message_converter_tests PRIVATE kernel32 user32 -static-libgcc -static-libstdc++)
    target_link_options(message_converter_tests PRIVATE -static)
endif()

# Register the test with CTest so `ctest --output-on-failure` in CI runs it.
add_test(NAME message_converter_tests
         COMMAND message_converter_tests)

# QUICK-260423-tk6: fs_utils unit tests (attachment-copy helpers).
#
# Exercises the new GenerateUniqueStem / GetAttachmentsDirForStem /
# EnsureDirExists / CopyFileToDir / WriteErrorForStem helpers. Links
# fs_utils.cpp directly — unlike message_converter, fs_utils is not
# in an OBJECT library because it's only consumed by the DLL itself.
add_executable(fs_utils_tests
    fs_utils_tests.cpp
    ../fs_utils.cpp
)

target_include_directories(fs_utils_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

set_target_properties(fs_utils_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    target_compile_options(fs_utils_tests PRIVATE /W4 /WX-)
    target_link_libraries(fs_utils_tests PRIVATE kernel32.lib user32.lib shell32.lib)
elseif(MINGW)
    target_compile_options(fs_utils_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(fs_utils_tests PRIVATE kernel32 user32 shell32 -static-libgcc -static-libstdc++)
    target_link_options(fs_utils_tests PRIVATE -static)
endif()

add_test(NAME fs_utils_tests
         COMMAND fs_utils_tests)

add_executable(diagnostic_trace_tests
    diagnostic_trace_tests.cpp
    ../diagnostic_trace.cpp
)

target_include_directories(diagnostic_trace_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

set_target_properties(diagnostic_trace_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    target_compile_options(diagnostic_trace_tests PRIVATE /W4 /WX-)
    target_link_libraries(diagnostic_trace_tests PRIVATE kernel32.lib shell32.lib)
elseif(MINGW)
    target_compile_options(diagnostic_trace_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(diagnostic_trace_tests PRIVATE kernel32 shell32 -static-libgcc -static-libstdc++)
    target_link_options(diagnostic_trace_tests PRIVATE -static)
endif()

add_test(NAME diagnostic_trace_tests
         COMMAND diagnostic_trace_tests)

# DllMain runs under the Windows loader lock. Keep its implementation exactly
# inert so loading the provider cannot touch Shell APIs, files, or C++ runtime
# facilities before a caller enters an exported MAPI function.
add_test(
    NAME dllmain_source_contract
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/../main.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_dllmain_contract.cmake
)

add_test(
    NAME simple_mapi_source_contract
    COMMAND ${CMAKE_COMMAND}
        -DEXPORTS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/../mapi_exports.def
        -DTYPES_FILE=${CMAKE_CURRENT_SOURCE_DIR}/../mapi_types.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_mapi_surface_contract.cmake
)

add_executable(mapi_export_contract_tests
    mapi_export_contract_tests.cpp
)

set_target_properties(mapi_export_contract_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    target_compile_options(mapi_export_contract_tests PRIVATE /W4 /WX-)
    target_link_libraries(mapi_export_contract_tests PRIVATE kernel32.lib)
elseif(MINGW)
    target_compile_options(mapi_export_contract_tests PRIVATE -Wall -Wextra)
    target_link_libraries(mapi_export_contract_tests PRIVATE kernel32 -static-libgcc -static-libstdc++)
    target_link_options(mapi_export_contract_tests PRIVATE -static)
endif()

add_dependencies(mapi_export_contract_tests go-mapi)

add_test(NAME mapi_export_contract_tests
         COMMAND mapi_export_contract_tests $<TARGET_FILE:go-mapi>)
