# SpatialEQ VST3 plug-in (macOS and Windows).
#
#   cd plugin/ui && npm ci && npm run build        # the Svelte UI -> plugin/ui/dist/index.html
#   cmake -S plugin -B build/plugin -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugin --config Release
#
# The VST3 SDK and choc are fetched by CMake. To use local copies pass
# -DFETCHCONTENT_SOURCE_DIR_VST3SDK=/path/to/vst3sdk and/or -DFETCHCONTENT_SOURCE_DIR_CHOC=/path/to/choc.
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
endif()

project(SpatialEQPlugin VERSION 1.1.1.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.1_build_84
    GIT_SHALLOW TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk)
FetchContent_Declare(choc
    GIT_REPOSITORY https://github.com/Tracktion/choc.git
    GIT_TAG 8af031c2ba4f5723f4b0a5ca14e0b4984af15b0e
    SOURCE_SUBDIR header-only) # choc is header-only: fetch it, don't build it

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "Build the SDK editorhost (for testing)")
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vst3sdk choc)
smtg_enable_vst3_sdk()

# ---- The Svelte UI, embedded as a byte array
set(UI_HTML ${CMAKE_CURRENT_SOURCE_DIR}/ui/dist/index.html)
if(NOT EXISTS ${UI_HTML})
    message(FATAL_ERROR "plugin/ui/dist/index.html is missing. Build the UI first: cd plugin/ui && npm ci && npm run build")
endif()
set(UI_HEADER ${PROJECT_BINARY_DIR}/generated/ui_html.h)
add_custom_command(OUTPUT ${UI_HEADER}
    COMMAND ${CMAKE_COMMAND} -DIN=${UI_HTML} -DOUT=${UI_HEADER} -DNAME=kUiHtml
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${UI_HTML} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding the plug-in UI")

# ---- The plug-in
set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../DSP)
set(SOURCES
    source/params.cpp
    source/params.h
    source/plugin.cpp
    source/plugin.h
    source/presets.cpp
    source/presets.h
    source/version.h
    source/webeditor.cpp
    source/webeditor.h
    source/filedialog.h
    ${DSP_DIR}/src/Engine.cpp
    ${DSP_DIR}/src/Fifo.cpp
    ${DSP_DIR}/src/Spectrum.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    ${UI_HEADER})
if(APPLE)
    list(APPEND SOURCES source/platform_mac.mm)
elseif(WIN32)
    list(APPEND SOURCES source/platform_win.cpp)
endif()

smtg_add_vst3plugin(SpatialEQ ${SOURCES})
target_include_directories(SpatialEQ PRIVATE
    source ${DSP_DIR}/include ${choc_SOURCE_DIR} ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(SpatialEQ PRIVATE sdk)
target_compile_definitions(SpatialEQ PRIVATE $<$<BOOL:${WIN32}>:NOMINMAX>)
smtg_target_configure_version_file(SpatialEQ)

if(APPLE)
    set_source_files_properties(source/platform_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(SpatialEQ PRIVATE "-framework Cocoa" "-framework WebKit" "-framework UniformTypeIdentifiers")
    smtg_target_set_bundle(SpatialEQ
        BUNDLE_IDENTIFIER com.savannahdsp.spatialeq.vst3
        COMPANY_NAME "Savannah DSP"
        INFOPLIST "${CMAKE_CURRENT_SOURCE_DIR}/resource/Info.plist")
elseif(WIN32)
    target_sources(SpatialEQ PRIVATE resource/spatialeq.rc)
    target_link_libraries(SpatialEQ PRIVATE comdlg32 ole32 shlwapi version)
    if(MSVC)
        target_compile_options(SpatialEQ PRIVATE /bigobj /utf-8)
    endif()
endif()
