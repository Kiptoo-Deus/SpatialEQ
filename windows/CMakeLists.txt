# SpatialEQ for Windows: WASAPI routing + the shared DSP core + the web interface shared with the VST3
# editor (plugin/ui, the same design as the Mac app) in a WebView2 window through choc.
#
#   cd plugin/ui && npm ci && npm run build        # the interface -> plugin/ui/dist/index.html
#   cmake -S windows -B build/windows
#   cmake --build build/windows --config Release
#   ctest --test-dir build/windows -C Release
cmake_minimum_required(VERSION 3.21)
project(SpatialEQ VERSION 1.2.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "The Windows app builds on Windows only; use DSP/ for the portable core.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: SpatialEQ.exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(choc
  GIT_REPOSITORY https://github.com/Tracktion/choc.git
  GIT_TAG 8af031c2ba4f5723f4b0a5ca14e0b4984af15b0e
  SOURCE_SUBDIR header-only) # header-only: fetch it, don't build it
FetchContent_MakeAvailable(choc)

# The interface, embedded as a byte array (same as the plug-in).
set(PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../plugin)
set(UI_HTML ${PLUGIN_DIR}/ui/dist/index.html)
if(NOT EXISTS ${UI_HTML})
  message(FATAL_ERROR "plugin/ui/dist/index.html is missing. Build the UI first: cd plugin/ui && npm ci && npm run build")
endif()
set(UI_HEADER ${PROJECT_BINARY_DIR}/generated/ui_html.h)
add_custom_command(OUTPUT ${UI_HEADER}
  COMMAND ${CMAKE_COMMAND} -DIN=${UI_HTML} -DOUT=${UI_HEADER} -DNAME=kUiHtml -P ${PLUGIN_DIR}/cmake/embed.cmake
  DEPENDS ${UI_HTML} ${PLUGIN_DIR}/cmake/embed.cmake
  COMMENT "Embedding the interface")

set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../DSP)

add_library(spatialeq_core STATIC
  ${DSP_DIR}/src/Engine.cpp
  ${DSP_DIR}/src/Fifo.cpp
  ${DSP_DIR}/src/Spectrum.cpp
  src/Settings.cpp
  src/AudioRouter.cpp
  src/Ducker.cpp
  src/Devices.cpp
  src/App.cpp)
target_include_directories(spatialeq_core PUBLIC ${DSP_DIR}/include src)
target_compile_features(spatialeq_core PUBLIC cxx_std_17)
target_compile_definitions(spatialeq_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(spatialeq_core PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(spatialeq_core PUBLIC ole32 avrt mmdevapi shell32 advapi32)

add_executable(SpatialEQ WIN32
  src/main.cpp
  src/WebUi.cpp
  ${PLUGIN_DIR}/source/platform_win.cpp # file dialogs and child-window embedding, shared with the VST3
  ${UI_HEADER}
  resources/SpatialEQ.rc
  resources/SpatialEQ.manifest)
target_include_directories(SpatialEQ PRIVATE ${PLUGIN_DIR}/source ${choc_SOURCE_DIR} ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(SpatialEQ PRIVATE spatialeq_core comdlg32 dwmapi ole32 shlwapi version)
target_compile_definitions(SpatialEQ PRIVATE SQ_VERSION_STRING="${PROJECT_VERSION}")
# choc's WebView is header-only and large; it also trips /W4 warnings that aren't ours.
target_compile_options(SpatialEQ PRIVATE /W3 /permissive- /utf-8 /bigobj)
set_source_files_properties(resources/SpatialEQ.rc PROPERTIES COMPILE_DEFINITIONS
  "SQ_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};SQ_VERSION_MINOR=${PROJECT_VERSION_MINOR};SQ_VERSION_PATCH=${PROJECT_VERSION_PATCH};SQ_VERSION_STRING=\"${PROJECT_VERSION}\"")

enable_testing()
add_executable(windows_tests tests/windows_tests.cpp)
target_link_libraries(windows_tests PRIVATE spatialeq_core)
target_compile_options(windows_tests PRIVATE /W4 /permissive- /utf-8)
add_test(NAME windows_tests COMMAND windows_tests)
