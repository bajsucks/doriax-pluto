# Pluto - a Lua 5.4 superset with extended syntax and a larger standard
# library (https://github.com/PlutoLang/Pluto).
#
# Upstream ships Makefiles and Visual Studio projects but no CMake build, so
# this file fetches the sources and declares the `pluto` library the engine
# links against, together with the bundled `soup` support library that Pluto's
# standard library extensions depend on.

include(FetchContent)

set(PLUTO_GIT_REPOSITORY "https://github.com/PlutoLang/Pluto.git"
    CACHE STRING "Pluto git repository")
# 0.12.x is the last line tracking Lua 5.4 (0.12.2 is Lua 5.4.8); Pluto 0.13
# rebased on Lua 5.5, which the engine's LuaBridge bindings do not target.
set(PLUTO_GIT_TAG "0.12.2" CACHE STRING "Pluto git tag or commit")
set(PLUTO_SOURCE_DIR "" CACHE PATH "Build an existing Pluto checkout instead of fetching one")

if(PLUTO_SOURCE_DIR)
    FetchContent_Declare(pluto SOURCE_DIR "${PLUTO_SOURCE_DIR}")
else()
    FetchContent_Declare(
        pluto
        GIT_REPOSITORY "${PLUTO_GIT_REPOSITORY}"
        GIT_TAG "${PLUTO_GIT_TAG}"
        GIT_SHALLOW TRUE
    )
endif()
FetchContent_MakeAvailable(pluto)

set(PLUTO_SRC_DIR "${pluto_SOURCE_DIR}/src")
if(NOT EXISTS "${PLUTO_SRC_DIR}/lua.h")
    message(FATAL_ERROR "Pluto sources were not found under ${PLUTO_SRC_DIR}")
endif()

# --- soup: support library used by Pluto's standard library extensions ----
file(GLOB PLUTO_SOUP_SOURCES CONFIGURE_DEPENDS "${PLUTO_SRC_DIR}/vendor/Soup/soup/*.cpp")
add_library(pluto_soup STATIC ${PLUTO_SOUP_SOURCES})
target_include_directories(pluto_soup SYSTEM PUBLIC "${PLUTO_SRC_DIR}/vendor/Soup/soup")
set_target_properties(
    pluto_soup
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
)

# --- pluto: the Lua 5.4 + Pluto runtime -----------------------------------
# lua.cpp is the standalone interpreter and luac.cpp the bytecode compiler;
# neither belongs in the embedded library.
file(GLOB PLUTO_SOURCES CONFIGURE_DEPENDS "${PLUTO_SRC_DIR}/*.cpp")
list(REMOVE_ITEM PLUTO_SOURCES
    "${PLUTO_SRC_DIR}/lua.cpp"
    "${PLUTO_SRC_DIR}/luac.cpp"
)

add_library(pluto STATIC ${PLUTO_SOURCES})
target_include_directories(pluto SYSTEM PUBLIC "${PLUTO_SRC_DIR}")
set_target_properties(
    pluto
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
)

find_package(Threads REQUIRED)
target_link_libraries(pluto_soup PUBLIC Threads::Threads)
target_link_libraries(pluto PUBLIC pluto_soup)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(pluto PRIVATE LUA_USE_WINDOWS)
    target_link_libraries(pluto_soup PUBLIC ws2_32 bcrypt dnsapi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(pluto PRIVATE LUA_USE_MACOSX)
    # Soup resolves host names through the system resolver API.
    target_link_libraries(pluto_soup PUBLIC resolv)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(pluto PRIVATE LUA_USE_LINUX)
    # Soup uses dlopen and ns_initparse(), matching Pluto's own -ldl -lresolv.
    target_link_libraries(pluto_soup PUBLIC ${CMAKE_DL_LIBS})
    find_library(PLUTO_RESOLV_LIBRARY resolv)
    if(PLUTO_RESOLV_LIBRARY)
        target_link_libraries(pluto_soup PUBLIC ${PLUTO_RESOLV_LIBRARY})
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(pluto PRIVATE LUA_USE_IOS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Pluto upstream builds with these; the multichar literals are intentional.
    target_compile_options(pluto PRIVATE -Wno-multichar)
    target_compile_options(pluto_soup PRIVATE -Wno-multichar)
endif()

if(EMSCRIPTEN AND EMSCRIPTEN_THREAD_SUPPORT)
    target_compile_options(pluto PRIVATE -pthread)
    target_compile_options(pluto_soup PRIVATE -pthread)
endif()
