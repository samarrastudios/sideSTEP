cmake_minimum_required(VERSION 3.20)

# One place to rename the project. Everything else derives from these.
set(APP_NAME "sideSTEP" CACHE STRING "Internal project / binary name prefix")
set(APP_DISPLAY_NAME "sideSTEP" CACHE STRING "Name shown to users")

project(${APP_NAME} VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCASCADE CONFIG REQUIRED)

# OCCT 7.8 merged the STEP toolkits into TKDESTEP; older versions split them.
if(TARGET TKDESTEP)
  set(OCCT_STEP_LIBS TKDESTEP)
else()
  set(OCCT_STEP_LIBS TKXDESTEP TKSTEP)
endif()
set(OCCT_LIBS ${OCCT_STEP_LIBS} TKXCAF TKLCAF TKXSBase TKMesh TKTopAlgo TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel)

add_library(stepcore STATIC core/step_loader.cpp core/renderer.cpp)
target_include_directories(stepcore PUBLIC core)
target_include_directories(stepcore PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(stepcore PRIVATE ${OCCT_LIBS})
set_target_properties(stepcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
  target_compile_definitions(stepcore PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# Brand icon + version details for Windows binaries (see assets/brand/BRAND.md).
function(add_app_resource target filename filetype description)
  if(WIN32)
    string(REPLACE "." "," RC_VERSION_COMMAS "${PROJECT_VERSION}")
    set(RC_FILENAME "${filename}")
    set(RC_FILETYPE "${filetype}")
    set(RC_DESCRIPTION "${description}")
    configure_file(res/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
  endif()
endfunction()
if(WIN32)
  enable_language(RC)
endif()

add_executable(stepthumb cli/main.cpp)
add_app_resource(stepthumb "sidestep-thumb.exe" VFT_APP "${APP_DISPLAY_NAME} thumbnail tester")
target_link_libraries(stepthumb PRIVATE stepcore)
set_target_properties(stepthumb PROPERTIES OUTPUT_NAME "sidestep-thumb")

option(BUILD_SAMPLE_TOOL "Build the sample STEP generator" OFF)
if(BUILD_SAMPLE_TOOL)
  add_executable(make_sample tools/make_sample.cpp)
  target_include_directories(make_sample PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(make_sample PRIVATE ${OCCT_LIBS} TKPrim TKBO TKFillet)
endif()

if(WIN32)
  add_library(shellext SHARED shellext/thumbnail_provider.cpp shellext/preview_handler.cpp shellext/shared.h shellext/shellext.def)
  set_target_properties(shellext PROPERTIES OUTPUT_NAME "${APP_NAME}Thumbs")
  add_app_resource(shellext "${APP_NAME}Thumbs.dll" VFT_DLL "${APP_DISPLAY_NAME} thumbnail provider for File Explorer")
  target_compile_definitions(shellext PRIVATE UNICODE _UNICODE NOMINMAX
    APP_DISPLAY_NAME_W=L"${APP_DISPLAY_NAME}")
  target_link_libraries(shellext PRIVATE stepcore shlwapi gdi32 user32 opengl32 ole32 shell32 advapi32)
  if(MSVC)
    target_compile_options(stepcore PRIVATE /MP /wd4996)
    target_compile_options(shellext PRIVATE /MP /we4129)
  endif()
endif()

# ---- viewer: the app that opens when you double-click a STEP file ----------
option(BUILD_VIEWER "Build the Qt 3D viewer" ON)
if(WIN32 AND BUILD_VIEWER)
  find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Concurrent)
  qt_add_executable(viewer WIN32
    viewer/main.cpp viewer/main_window.cpp viewer/main_window.h
    viewer/occ_view.cpp viewer/occ_view.h
    viewer/model.cpp viewer/model.h
    viewer/measure.cpp viewer/measure.h
    viewer/measure_tool.cpp viewer/measure_tool.h
    viewer/section_tool.cpp viewer/section_tool.h
    viewer/icons.cpp viewer/icons.h
    viewer/hud.cpp viewer/hud.h
    viewer/view_cube.cpp viewer/view_cube.h
    viewer/open_with.cpp viewer/open_with.h
    viewer/file_assoc.cpp viewer/file_assoc.h
    viewer/unblock.cpp viewer/unblock.h)
  set_target_properties(viewer PROPERTIES OUTPUT_NAME "${APP_NAME}" AUTOMOC ON)
  add_app_resource(viewer "${APP_NAME}.exe" VFT_APP "${APP_DISPLAY_NAME} STEP viewer")
  qt_add_resources(viewer "brand" PREFIX "/brand" BASE "assets/brand" FILES
    assets/brand/lockup/sidestep-lockup-dark.png
    assets/brand/icon/png/sidestep-16.png assets/brand/icon/png/sidestep-24.png
    assets/brand/icon/png/sidestep-32.png assets/brand/icon/png/sidestep-48.png
    assets/brand/icon/png/sidestep-64.png assets/brand/icon/png/sidestep-256.png)
  target_include_directories(viewer PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_compile_definitions(viewer PRIVATE UNICODE _UNICODE NOMINMAX APP_VERSION="${PROJECT_VERSION}")
  target_link_libraries(viewer PRIVATE Qt6::Widgets Qt6::Concurrent
    TKOpenGl TKV3d TKService ${OCCT_LIBS} TKPrim TKGeomAlgo TKShHealing
    version shlwapi shell32 ole32 advapi32)
  if(MSVC)
    target_compile_options(viewer PRIVATE /MP /wd4996 /we4129)  # C4129: unknown escape, e.g. a lone backslash in a registry path
  endif()
endif()

# ---- test harness for the preview-pane handler (scripts\test.ps1 uses it) ---
if(WIN32)
  add_executable(previewtest tools/preview_test.cpp)
  set_target_properties(previewtest PROPERTIES OUTPUT_NAME "sidestep-previewtest")
  target_compile_definitions(previewtest PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(previewtest PRIVATE stepcore shlwapi ole32 user32 gdi32)
endif()
