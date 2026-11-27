# Qualification of the CPU core (ADR 0006): cycles against the manual, compiled programs with a verdict, differential
# tests against simavr, and the matrix that consolidates them. Every tool writes its results in
# ${SILICONFORGE_QUALIFICATION_DIR}; `cmake --build build --target qualification` runs them all and rewrites
# docs/qualification/.
set(SILICONFORGE_QUALIFICATION_DIR ${PROJECT_BINARY_DIR}/qualification)
file(MAKE_DIRECTORY ${SILICONFORGE_QUALIFICATION_DIR}/programs)
set(_qualification_coverage "SILICONFORGE_OPCODE_COVERAGE=${SILICONFORGE_QUALIFICATION_DIR}/coverage/qualification")

add_executable(siliconforge_qualification Main.cpp Qualification.cpp Cycles.cpp Programs.cpp Matrix.cpp)
target_link_libraries(siliconforge_qualification PRIVATE SiliconForge::mcu SiliconForge_options nlohmann_json::nlohmann_json)
target_include_directories(siliconforge_qualification PRIVATE ${PROJECT_SOURCE_DIR}/tests)

# Cycles of every instruction of the description, measured and compared with the manual (#26).
add_test(NAME Qualification-Cycles
    COMMAND siliconforge_qualification cycles --mcu atmega328p --out ${SILICONFORGE_QUALIFICATION_DIR}/cycles.json)
set_tests_properties(Qualification-Cycles PROPERTIES TIMEOUT 60 LABELS "fast;qualification" ENVIRONMENT ${_qualification_coverage})
add_test(NAME Qualification-Cycles-atmega2560
    COMMAND siliconforge_qualification cycles --mcu atmega2560 --out ${SILICONFORGE_QUALIFICATION_DIR}/cycles-atmega2560.json)
set_tests_properties(Qualification-Cycles-atmega2560 PROPERTIES TIMEOUT 60 LABELS "fast;qualification")
add_test(NAME CycleConsistency-attiny424
    COMMAND siliconforge_qualification cycles --mcu attiny424 --out ${SILICONFORGE_QUALIFICATION_DIR}/cycles-attiny424.json)
set_tests_properties(CycleConsistency-attiny424 PROPERTIES TIMEOUT 60 LABELS fast)
add_test(NAME Qualification-MatrixByTarget
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:siliconforge_qualification>
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/MatrixByTarget -P ${CMAKE_CURRENT_SOURCE_DIR}/MatrixTests.cmake)
set_tests_properties(Qualification-MatrixByTarget PROPERTIES TIMEOUT 60 LABELS "fast;qualification")

# Compiled programs (#25): firmware/qualification/*.c at three optimisation levels, when the AVR toolchain is there.
set(_qualification_levels O0 O2 Os)
if(SILICONFORGE_FIRMWARE_FROM_SOURCE)
    file(GLOB _qualification_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/firmware/qualification/*.c)
    list(SORT _qualification_sources)
    add_custom_target(qualification_firmware ALL
        COMMAND ${MAKE_PROGRAM} -C ${PROJECT_SOURCE_DIR}/firmware/qualification BUILD=${PROJECT_BINARY_DIR}/firmware/qualification
        COMMENT "Building the qualification programs with avr-gcc"
        VERBATIM)
    add_dependencies(siliconforge_qualification qualification_firmware)
    foreach(_source IN LISTS _qualification_sources)
        get_filename_component(_program ${_source} NAME_WE)
        foreach(_level IN LISTS _qualification_levels)
            add_test(NAME Qualification-Program-${_program}-${_level}
                COMMAND siliconforge_qualification program ${PROJECT_BINARY_DIR}/firmware/qualification/${_program}-${_level}.hex
                    --out ${SILICONFORGE_QUALIFICATION_DIR}/programs/${_program}-${_level}.json)
            set_tests_properties(Qualification-Program-${_program}-${_level} PROPERTIES TIMEOUT 60 LABELS "fast;qualification"
                ENVIRONMENT ${_qualification_coverage})
        endforeach()
    endforeach()
else()
    message(STATUS "Qualification programs skipped: they are compiled from firmware/qualification with avr-gcc")
endif()

# Differential tests against simavr (#24). simavr is a test dependency only, under the GPL v3: it is linked into
# siliconforge_differential and nothing else, and that executable is never installed. Its device files include the
# register definitions of avr-libc, so the differential tests need the AVR toolchain too.
find_file(AVR_IOM328P_HEADER avr/iom328p.h PATHS /usr/lib/avr/include /usr/avr/include /usr/local/avr/include
    /opt/homebrew/opt/avr-libc/avr/include NO_DEFAULT_PATH)
option(SILICONFORGE_DIFFERENTIAL_TESTS "Build the differential tests against simavr" ON)
if(SILICONFORGE_DIFFERENTIAL_TESTS AND AVR_IOM328P_HEADER AND UNIX)
    enable_language(C)
    CPMAddPackage(NAME simavr GITHUB_REPOSITORY buserror/simavr
        GIT_TAG 30eef2530deda6bc2791864ebcc5f7fd75b3f714 DOWNLOAD_ONLY YES)
    set(_simavr ${simavr_SOURCE_DIR}/simavr)
    set(_simavr_generated ${CMAKE_CURRENT_BINARY_DIR}/simavr)
    # What simavr's Makefile generates: the list of the device files that are compiled in, here the ATmega328P alone.
    file(WRITE ${_simavr_generated}/sim_core_config.h
        "#ifndef __SIM_CORE_CONFIG_H__\n#define __SIM_CORE_CONFIG_H__\n#define CONFIG_SIMAVR_VERSION \"siliconforge\"\n#define CONFIG_MEGA328 1\n#endif\n")
    file(WRITE ${_simavr_generated}/sim_core_decl.h
        "#ifndef __SIM_CORE_DECL_H__\n#define __SIM_CORE_DECL_H__\n#include \"sim_core_config.h\"\nextern avr_kind_t mega328;\n"
        "extern avr_kind_t * avr_kind[];\n#ifdef AVR_KIND_DECL\navr_kind_t * avr_kind[] = {\n\t&mega328,\n\tNULL\n};\n#endif\n#endif\n")
    # Only this header of avr-libc: its directory also holds a stdio.h and a stdlib.h that must not shadow the host's.
    configure_file(${AVR_IOM328P_HEADER} ${_simavr_generated}/avr/iom328p.h COPYONLY)
    file(GLOB _simavr_sources ${_simavr}/sim/*.c)
    list(FILTER _simavr_sources EXCLUDE REGEX "/(run_avr|sim_elf|sim_dwarf)\\.c$")
    add_library(simavr_reference STATIC ${_simavr_sources} ${_simavr}/cores/sim_megax8.c ${_simavr}/cores/sim_mega328.c
        SimavrStubs.c)
    target_include_directories(simavr_reference SYSTEM PUBLIC ${_simavr}/sim ${_simavr_generated} ${_simavr}/sim/avr)
    target_include_directories(simavr_reference PRIVATE ${_simavr}/cores)
    target_compile_definitions(simavr_reference PRIVATE NO_COLOR _DEFAULT_SOURCE)
    target_compile_options(simavr_reference PRIVATE -w)
    set_target_properties(simavr_reference PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

    add_executable(siliconforge_differential Differential.cpp Qualification.cpp)
    target_link_libraries(siliconforge_differential PRIVATE SiliconForge::mcu SiliconForge_options nlohmann_json::nlohmann_json simavr_reference)
    target_include_directories(siliconforge_differential PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(siliconforge_differential PRIVATE SILICONFORGE_SIMAVR_REVISION="30eef25")
    # The short campaign of every build; the long one is `siliconforge_differential --programs 200000` (docs/QUALIFICATION.md).
    add_test(NAME Qualification-Differential
        COMMAND siliconforge_differential --seed 1 --programs 2000 --out ${SILICONFORGE_QUALIFICATION_DIR}/differential.json)
    set_tests_properties(Qualification-Differential PROPERTIES TIMEOUT 300 LABELS "fast;qualification"
        ENVIRONMENT ${_qualification_coverage})
else()
    message(STATUS "Differential tests against simavr skipped: they need avr-libc (avr/iom328p.h) on a Unix host")
endif()

# The whole qualification from a clean slate, then the matrix written into docs/qualification (committed at each
# release). The CI runs the same tests and writes the matrix as an artifact.
add_custom_target(qualification
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SILICONFORGE_QUALIFICATION_DIR}/coverage ${SILICONFORGE_QUALIFICATION_DIR}/programs
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SILICONFORGE_QUALIFICATION_DIR}/programs
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L fast
    COMMAND siliconforge_qualification matrix --results ${SILICONFORGE_QUALIFICATION_DIR} --out ${PROJECT_SOURCE_DIR}/docs/qualification
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    USES_TERMINAL
    COMMENT "Qualification of the CPU core: tests, then docs/qualification")
add_dependencies(qualification siliconforge_qualification siliconforge_tests)
if(TARGET siliconforge_differential)
    add_dependencies(qualification siliconforge_differential)
endif()
