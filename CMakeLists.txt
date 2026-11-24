cmake_minimum_required(VERSION 3.16)
project(xz-embedded VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# These mirror the real configuration knobs xz.h/xz_private.h read at
# compile time (see userspace/Makefile's reference build and
# linux/lib/xz/Kconfig for the canonical option set and defaults).
option(XZ_USE_CRC64 "Support the CRC64 integrity check" ON)
option(XZ_USE_SHA256 "Support the SHA-256 integrity check" ON)
option(XZ_DEC_ANY_CHECK "Accept any integrity check type, not just the enabled ones" ON)
option(XZ_DEC_CONCATENATED "Support decoding concatenated .xz streams" ON)
option(XZ_DEC_MICROLZMA "Support the MicroLZMA header variant" OFF)

option(XZ_DEC_X86 "x86 BCJ filter decoder" ON)
option(XZ_DEC_POWERPC "PowerPC BCJ filter decoder" ON)
option(XZ_DEC_IA64 "IA-64 BCJ filter decoder" ON)
option(XZ_DEC_ARM "ARM BCJ filter decoder" ON)
option(XZ_DEC_ARMTHUMB "ARM-Thumb BCJ filter decoder" ON)
option(XZ_DEC_ARM64 "ARM64 BCJ filter decoder" ON)
option(XZ_DEC_SPARC "SPARC BCJ filter decoder" ON)
option(XZ_DEC_RISCV "RISC-V BCJ filter decoder" ON)

set(XZ_DEC_BCJ OFF)
foreach(arch X86 POWERPC IA64 ARM ARMTHUMB ARM64 SPARC RISCV)
    if(XZ_DEC_${arch})
        set(XZ_DEC_BCJ ON)
    endif()
endforeach()

set(XZ_SOURCES
    linux/lib/xz/xz_crc32.c
    linux/lib/xz/xz_dec_lzma2.c
    linux/lib/xz/xz_dec_stream.c
)
if(XZ_USE_CRC64)
    list(APPEND XZ_SOURCES linux/lib/xz/xz_crc64.c)
endif()
if(XZ_USE_SHA256)
    list(APPEND XZ_SOURCES linux/lib/xz/xz_sha256.c)
endif()
if(XZ_DEC_BCJ)
    list(APPEND XZ_SOURCES linux/lib/xz/xz_dec_bcj.c)
endif()

add_library(xz STATIC ${XZ_SOURCES})

target_include_directories(xz
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/linux/include/linux>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    PRIVATE
        linux/lib/xz
        userspace
)

set(XZ_PUBLIC_DEFINITIONS "")
set(XZ_PRIVATE_DEFINITIONS "")
foreach(opt XZ_USE_CRC64 XZ_USE_SHA256 XZ_DEC_CONCATENATED XZ_DEC_MICROLZMA
            XZ_DEC_X86 XZ_DEC_POWERPC XZ_DEC_IA64 XZ_DEC_ARM XZ_DEC_ARMTHUMB
            XZ_DEC_ARM64 XZ_DEC_SPARC XZ_DEC_RISCV)
    if(${opt})
        list(APPEND XZ_PUBLIC_DEFINITIONS ${opt})
    endif()
endforeach()
if(XZ_DEC_ANY_CHECK)
    list(APPEND XZ_PRIVATE_DEFINITIONS XZ_DEC_ANY_CHECK)
endif()

target_compile_definitions(xz
    PUBLIC ${XZ_PUBLIC_DEFINITIONS}
    PRIVATE ${XZ_PRIVATE_DEFINITIONS}
)

set_target_properties(xz PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${PROJECT_NAME}::xz ALIAS xz)

# xz-embedded implements only a decoder and ships no fixture data or
# automated test of its own (its userspace/*.c and
# linux/lib/xz/xz_dec_test.c tools all expect a .xz file or kernel module
# fed in by hand). Check the decoder against known .xz streams from the
# reference xz-utils compressor instead.
include(CTest)

if(BUILD_TESTING)
    add_subdirectory(cmake/tests)
endif()

install(TARGETS xz
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES linux/include/linux/xz.h
    DESTINATION include/${PROJECT_NAME}
)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)
