cmake_minimum_required(VERSION 3.24)
project(xMenuForgeNative LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "xMenuForge native targets require Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xMenuForgeCommon STATIC
  common/FileTypes.cpp
  common/Config.cpp
)
target_include_directories(xMenuForgeCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_definitions(xMenuForgeCommon PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(xMenuForgeCommon PUBLIC shell32 shlwapi dwmapi uxtheme)

add_library(xMenuForgeShell SHARED
  shell/ShellExtension.cpp
  shell/ShellExtension.def
)
target_link_libraries(xMenuForgeShell PRIVATE xMenuForgeCommon ole32 user32 gdi32)
set_target_properties(xMenuForgeShell PROPERTIES OUTPUT_NAME "xMenuForge.Shell")

add_executable(xMenuForgeHost WIN32
  host/HostMain.cpp
  host/CommandRunner.cpp
  host/Dialogs.cpp
  host/Registration.cpp
  host/host.manifest
)
target_link_libraries(xMenuForgeHost PRIVATE xMenuForgeCommon ole32 user32 gdi32 comctl32 advapi32)
set_target_properties(xMenuForgeHost PROPERTIES OUTPUT_NAME "xMenuForge.Host")
