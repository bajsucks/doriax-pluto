# (c) Eduardo Doria and contributors
# SPDX-License-Identifier: MIT
#
# Headless checks for the script runtime. Enabled by DORIAX_BUILD_TESTS in the
# root CMakeLists.txt; they need no editor, no window, and no generated headers,
# only the `pluto` target the build already fetched.

add_executable(doriax-pluto-check check.cpp)
target_link_libraries(doriax-pluto-check PRIVATE pluto)
set_target_properties(
    doriax-pluto-check
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
)

# Compares the editor's hand-maintained keyword list with Pluto's lexer, using
# the fetched revision's own llex.cpp and the editor file it must match.
add_executable(doriax-keyword-check keyword_check.cpp)
target_link_libraries(doriax-keyword-check PRIVATE pluto)
if(NOT DORIAX_PLUTO_SRC_DIR)
    message(FATAL_ERROR "DORIAX_PLUTO_SRC_DIR is not set; the keyword check needs Pluto's llex.cpp")
endif()
target_compile_definitions(
    doriax-keyword-check
    PRIVATE
        DORIAX_PLUTO_LLEX_PATH="${DORIAX_PLUTO_SRC_DIR}/llex.cpp"
        DORIAX_EDITOR_KEYWORDS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../editor/window/widget/CustomTextEditor.cpp"
)
set_target_properties(
    doriax-keyword-check
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
)

add_test(NAME pluto-runtime COMMAND doriax-pluto-check)
add_test(NAME pluto-editor-keywords COMMAND doriax-keyword-check)
