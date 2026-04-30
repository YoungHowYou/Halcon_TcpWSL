# CMakeLists.txt for HALCON extension package 'halconuser' example
# Copyright (c) 2019-2023 by MVTec Software GmbH

# Option for using HALCON XL.
option(HALCON_XL "Use HALCON XL" OFF)
# Option for enabling .NET support. This will require a more recent version
# of CMake to work. .NET is Windows-only here, so default to OFF on other
# platforms.
if(WIN32)
  option(HALCON_DOTNET "Build .NET support" ON)
else()
  option(HALCON_DOTNET "Build .NET support" OFF)
endif()
# If the HALCON_DOTNET option is selected, check if this version of CMake
# has the required CSharp support. Note that the Visual Studio 2010 or later
# generator has to be used.
if(HALCON_DOTNET)
  cmake_minimum_required(VERSION 4.1.1)
  set(_CSHARP CSharp)
else()
  cmake_minimum_required(VERSION 3.20)
endif()
# When using CMake 3.9 or newer, do not let RPATH affect the install_name on
# macOS.
if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)
endif()
# If policy CMP0091 is available, set it to NEW to enable use of the
# CMAKE_MSVC_RUNTIME_LIBRARY variable.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
# Start the project here





project(Halcon_TcpWSL C CXX )
set(ProjectName Halcon_TcpWSL)
set(CMAKE_BUILD_TYPE Debug)
# 禁止生成 Debug/Release 子目录，直接输出到 bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
# Append HALCON's cmake directory to the module path.
# 优先从 HALCONEXAMPLES 取，但要兼容 shell 里写成字面量未展开（"$HALCONROOT/..."）
# 的情况——这种情况退化到 HALCONROOT/examples 自己拼。
file(TO_CMAKE_PATH "$ENV{HALCONEXAMPLES}" HALCONEXAMPLES)
if(NOT HALCONEXAMPLES OR HALCONEXAMPLES MATCHES "\\$" OR NOT EXISTS "${HALCONEXAMPLES}/cmake/UseHALCON.cmake")
  file(TO_CMAKE_PATH "$ENV{HALCONROOT}" _HALCONROOT_NORM)
  if(_HALCONROOT_NORM AND EXISTS "${_HALCONROOT_NORM}/examples/cmake/UseHALCON.cmake")
    set(HALCONEXAMPLES "${_HALCONROOT_NORM}/examples")
    message(STATUS "HALCONEXAMPLES not usable from environment, falling back to ${HALCONEXAMPLES}")
  else()
    message(FATAL_ERROR
      "Cannot locate HALCON cmake modules. Set HALCONROOT to your HALCON install "
      "(currently HALCONROOT='$ENV{HALCONROOT}', HALCONEXAMPLES='$ENV{HALCONEXAMPLES}').")
  endif()
endif()
list(APPEND CMAKE_MODULE_PATH "${HALCONEXAMPLES}/cmake")
# Include common settings for HALCON projects
include(UseHALCON)

# Support Linux builds with HALCON and thread libraries
find_package(Threads REQUIRED)

# HALCON library directories differ between Windows and Linux
set(HALCON_ROOT $ENV{HALCONROOT})
if(WIN32)
  set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-win64)
else()
  set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-linux)
endif()
# .def 文件被 hcomp 用于生成算子元数据/帮助，Windows/Linux 通用
set(EXT_DEF_FILES def/Halcon_TcpWSL.def)

find_library(HALCON_LIB halconcpp PATHS ${HALCON_LIB_DIR} NO_DEFAULT_PATH)
if(NOT HALCON_LIB)
  find_library(HALCON_LIB halconcpp)
endif()
if(NOT HALCON_LIB)
  message(FATAL_ERROR "Could not find halconcpp library. Please set HALCONROOT correctly.")
endif()

HALCON_AddExtensionPackage(Halcon_TcpWSL
  DEF_FILES
    ${EXT_DEF_FILES}
  SOURCES
    source/network.cpp
    source/Halcon_TcpWSL.cpp
    source/Halcon_TcpWSL.c
    source/encoder_impl.cpp
    source/encoder_api.cpp
    source/decoder_impl.cpp
    source/decoder_api.cpp

  CHAPTERS
    userextensions
  CLASSES
    TcpWSL
  OPERATORS

)

## 三方库包含
# Halcon 变量由前面定义的 HALCONROOT 环境变量提供
set(HALCON_INCLUDE_DIR ${HALCON_ROOT}/include/halconcpp)
if(WIN32)
    set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-win64)
else()
    set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-linux)
endif()

file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})

# Windows 上的 .def 文件不参与编译，只用于导出
if(WIN32)
    file(GLOB DEF_FILES "${CMAKE_CURRENT_SOURCE_DIR}/def/*.def")
    target_sources(${ProjectName} PRIVATE ${DEF_FILES})
    set_source_files_properties(${DEF_FILES} PROPERTIES
        HEADER_FILE_ONLY TRUE
    )
endif()

target_include_directories(${ProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/DjiH264Encoder/include>
)

# DjiH264Encoder 编译宏定义
target_compile_definitions(${ProjectName} PRIVATE
    _CRT_SECURE_NO_WARNINGS
)
if(WIN32)
    target_compile_definitions(${ProjectName} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    # oneVPL 头里 MFXInit/MFXInitEx 被标记 deprecated（已被 dispatcher API 取代），
    # 但代码通过 MFXVideoSession 封装类间接调用，行为正常。GCC 上抑制告警。
    target_compile_options(${ProjectName} PRIVATE -Wno-deprecated-declarations)
endif()

# 链接 HALCON 库和平台相关库
# 注意：HALCON_AddExtensionPackage 内部使用了 plain 风格的 target_link_libraries，
# 这里也必须用 plain 风格（不带 PRIVATE/PUBLIC 关键字），不然 CMake 会报混用错误。
if(WIN32)
    target_link_libraries(${ProjectName}
        ${HALCON_LIB_DIR}/halconcpp.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd/DjiH264Encoder/lib/vpl.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd/DjiH264Encoder/lib/yuv.lib
        d3d11
        dxgi
    )
else()
    find_library(HALCON_LIB halconcpp PATHS ${HALCON_LIB_DIR} NO_DEFAULT_PATH)
    if(NOT HALCON_LIB)
        find_library(HALCON_LIB halconcpp)
    endif()
    if(NOT HALCON_LIB)
        message(FATAL_ERROR "Could not find halconcpp library. Please install HALCON or adjust HALCONROOT.")
    endif()

    # Linux: 优先使用 3rd/ 里 bundled 的 oneVPL/libyuv，
    # 没有再回退到系统包 (sudo apt install libvpl-dev libyuv-dev)。
    set(_BUNDLED_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd/DjiH264Encoder/lib/x64-linux")
    find_library(VPL_LIB NAMES vpl
        PATHS ${_BUNDLED_LIB_DIR}
        NO_DEFAULT_PATH)
    if(NOT VPL_LIB)
        find_library(VPL_LIB NAMES vpl)
    endif()
    if(NOT VPL_LIB)
        message(FATAL_ERROR
            "Could not find libvpl (oneVPL). "
            "Bundled at ${_BUNDLED_LIB_DIR}/libvpl.so or install: sudo apt install libvpl-dev")
    endif()

    find_library(YUV_LIB NAMES yuv
        PATHS ${_BUNDLED_LIB_DIR}
        NO_DEFAULT_PATH)
    if(NOT YUV_LIB)
        find_library(YUV_LIB NAMES yuv)
    endif()
    if(NOT YUV_LIB)
        message(FATAL_ERROR
            "Could not find libyuv. "
            "Bundled at ${_BUNDLED_LIB_DIR}/libyuv.so or install: sudo apt install libyuv-dev")
    endif()

    message(STATUS "Using oneVPL: ${VPL_LIB}")
    message(STATUS "Using libyuv: ${YUV_LIB}")

    target_link_libraries(${ProjectName}
        ${HALCON_LIB}
        ${VPL_LIB}
        ${YUV_LIB}
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    # 把 bundled .so 目录烧进 RPATH，运行时即使不设 LD_LIBRARY_PATH 也能加载。
    # $ORIGIN          → 用户把 .so 拷到 bin/ 同级时生效
    # $ORIGIN/../3rd/… → 库放在工程原 3rd 目录里时生效（HALCON 加载 bin/lib*.so 时）
    # 直接用 target_link_options 注入，避免 CMake 把 RPATH 自动剥掉
    target_link_options(${ProjectName} PRIVATE
        "LINKER:-rpath,\$ORIGIN"
        "LINKER:-rpath,\$ORIGIN/../3rd/DjiH264Encoder/lib/x64-linux"
        "LINKER:--disable-new-dtags"
    )
endif()

