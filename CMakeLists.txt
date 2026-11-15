cmake_minimum_required(VERSION 3.20)

# One place to rename the project. Everything else derives from these.
set(APP_NAME "sideSTEP" CACHE STRING "Internal project / binary name prefix")
set(APP_DISPLAY_NAME "sideSTEP" CACHE STRING "Name shown to users")

project(${APP_NAME} LANGUAGES CXX)
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

add_executable(stepthumb cli/main.cpp)
target_link_libraries(stepthumb PRIVATE stepcore)
set_target_properties(stepthumb PROPERTIES OUTPUT_NAME "sidestep-thumb")

option(BUILD_SAMPLE_TOOL "Build the sample STEP generator" OFF)
if(BUILD_SAMPLE_TOOL)
  add_executable(make_sample tools/make_sample.cpp)
  target_include_directories(make_sample PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(make_sample PRIVATE ${OCCT_LIBS} TKPrim TKBO TKFillet)
endif()

if(WIN32)
  add_library(shellext SHARED shellext/thumbnail_provider.cpp shellext/shellext.def)
  set_target_properties(shellext PROPERTIES OUTPUT_NAME "${APP_NAME}Thumbs")
  target_compile_definitions(shellext PRIVATE UNICODE _UNICODE NOMINMAX
    APP_DISPLAY_NAME_W=L"${APP_DISPLAY_NAME}")
  target_link_libraries(shellext PRIVATE stepcore shlwapi gdi32 ole32 shell32 advapi32)
  if(MSVC)
    target_compile_options(stepcore PRIVATE /MP /wd4996)
    target_compile_options(shellext PRIVATE /MP)
  endif()
endif()
