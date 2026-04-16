cmake_minimum_required(VERSION 3.20)
project(RuneC C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# rc-core: game backend library (no render deps)
file(GLOB RC_CORE_SOURCES rc-core/*.c)
add_library(rc-core STATIC ${RC_CORE_SOURCES})
target_include_directories(rc-core PUBLIC rc-core)

# rc-viewer: raylib frontend
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/raylib/lib/libraylib.a")
    file(GLOB RC_VIEWER_SOURCES rc-viewer/*.c)
    add_executable(rc-viewer ${RC_VIEWER_SOURCES})
    target_link_libraries(rc-viewer rc-core)
    target_include_directories(rc-viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/include
    )
    target_link_directories(rc-viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/lib
    )
    target_link_libraries(rc-viewer raylib m dl pthread)

    # Platform-specific libs
    if(UNIX AND NOT APPLE)
        find_library(GL_LIB NAMES GL PATHS /usr/lib/x86_64-linux-gnu)
        if(GL_LIB)
            target_link_libraries(rc-viewer ${GL_LIB})
        else()
            target_link_libraries(rc-viewer /usr/lib/x86_64-linux-gnu/libGL.so.1)
        endif()
    endif()
else()
    message(WARNING "Raylib not found at lib/raylib/ — skipping rc-viewer build. "
                    "Place prebuilt raylib in lib/raylib/{include,lib}/")
endif()

# Tests
file(GLOB TEST_SOURCES tests/*.c)
foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} rc-core m)
endforeach()
