cmake_minimum_required(VERSION 3.20)
project(HndEarCamera VERSION 1.2.0 LANGUAGES CXX RC)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT: the binaries must run on a PC without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive-> $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- camera protocol, image processing, logging ---------------------------------------------
add_library(hndcore STATIC
    src/core/Log.cpp
    src/core/ImageOps.cpp
    src/core/CameraClient.cpp
    src/core/WifiConnector.cpp)
target_include_directories(hndcore PUBLIC src/core src/common)
target_link_libraries(hndcore PUBLIC ws2_32 iphlpapi wlanapi windowscodecs d2d1 dwrite ole32 shell32)

# --- console test tool ------------------------------------------------------------------------
add_executable(HndCamProbe tools/probe/main.cpp)
target_link_libraries(HndCamProbe PRIVATE hndcore)

# --- standalone browser viewer (no installation) -------------------------------------------------
set(HNDCAM_VIEWER_HTML ${CMAKE_SOURCE_DIR}/tools/viewer/viewer.html)
file(TO_NATIVE_PATH ${HNDCAM_VIEWER_HTML} HNDCAM_VIEWER_HTML_NATIVE)
string(REPLACE "\\" "\\\\" HNDCAM_VIEWER_HTML_RC ${HNDCAM_VIEWER_HTML_NATIVE})
configure_file(src/viewer/viewer_page.rc.in ${CMAKE_BINARY_DIR}/viewer_page.rc @ONLY)
set_source_files_properties(${CMAKE_BINARY_DIR}/viewer_page.rc PROPERTIES OBJECT_DEPENDS ${HNDCAM_VIEWER_HTML})
add_executable(HndViewer WIN32
    src/viewer/main.cpp
    src/viewer/WifiAdapters.cpp
    src/viewer/Installer.cpp
    src/viewer/HndViewer.rc
    src/viewer/HndViewer.manifest
    ${CMAKE_BINARY_DIR}/viewer_page.rc)
set_target_properties(HndViewer PROPERTIES OUTPUT_NAME "CAM-Oreille")
set_source_files_properties(src/viewer/HndViewer.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/src/viewer/CAM-Oreille.ico)
target_include_directories(HndViewer PRIVATE src/viewer)
target_link_libraries(HndViewer PRIVATE hndcore shell32 comctl32 setupapi cfgmgr32 version)

# --- Media Foundation media source (loaded by the Windows Frame Server) ------------------------
if(EXISTS ${CMAKE_SOURCE_DIR}/src/source/dllmain.cpp)
    add_library(HndCamSource SHARED
        src/source/dllmain.cpp
        src/source/Activator.cpp
        src/source/MediaSource.cpp
        src/source/MediaStream.cpp
        src/source/HndCamSource.def
        src/source/HndCamSource.rc)
    target_link_libraries(HndCamSource PRIVATE hndcore mfplat mfuuid mfsensorgroup advapi32 propsys)
endif()

# --- self-contained installer embedding the media source --------------------------------------
if(EXISTS ${CMAKE_SOURCE_DIR}/src/setup/main.cpp)
    set(HNDCAM_SOURCE_DLL ${CMAKE_BINARY_DIR}/bin/HndCamSource.dll)
    file(TO_NATIVE_PATH ${HNDCAM_SOURCE_DLL} HNDCAM_SOURCE_DLL_NATIVE)
    string(REPLACE "\\" "\\\\" HNDCAM_SOURCE_DLL_RC ${HNDCAM_SOURCE_DLL_NATIVE})
    configure_file(src/setup/payload.rc.in ${CMAKE_BINARY_DIR}/payload.rc @ONLY)
    set_source_files_properties(${CMAKE_BINARY_DIR}/payload.rc PROPERTIES OBJECT_DEPENDS ${HNDCAM_SOURCE_DLL})

    add_executable(HndCamSetup WIN32
        src/setup/main.cpp
        src/setup/VirtualCamera.cpp
        src/setup/HndCamSetup.rc
        src/setup/HndCamSetup.manifest
        ${CMAKE_BINARY_DIR}/payload.rc)
    target_include_directories(HndCamSetup PRIVATE src/setup)
    target_link_libraries(HndCamSetup PRIVATE hndcore mf mfplat mfuuid mfsensorgroup advapi32 comctl32 shlwapi cfgmgr32)
    target_link_options(HndCamSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    add_dependencies(HndCamSetup HndCamSource)
endif()
