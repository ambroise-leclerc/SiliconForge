CPMAddPackage("gh:catchorg/Catch2@3.15.2")
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

# Firmware: compiled from firmware/src when avr-gcc is available, otherwise the committed firmware/prebuilt is used.
find_program(AVR_GCC avr-gcc)
find_program(AVR_OBJCOPY avr-objcopy)
find_program(AVR_OBJDUMP avr-objdump)
find_program(MAKE_PROGRAM NAMES make gmake)
if(AVR_GCC AND AVR_OBJCOPY AND MAKE_PROGRAM)
    set(SILICONFORGE_FIRMWARE_DIR ${PROJECT_BINARY_DIR}/firmware)
    add_custom_target(firmware ALL
        COMMAND ${MAKE_PROGRAM} -C ${PROJECT_SOURCE_DIR}/firmware BUILD=${SILICONFORGE_FIRMWARE_DIR}
        COMMENT "Building the firmware with avr-gcc"
        VERBATIM)
    set(SILICONFORGE_FIRMWARE_FROM_SOURCE 1)
else()
    set(SILICONFORGE_FIRMWARE_DIR ${PROJECT_SOURCE_DIR}/firmware/prebuilt)
    message(STATUS "avr-gcc, avr-objcopy or make not found: the tests use the committed firmware/prebuilt binaries "
                   "(install gcc-avr, avr-libc and binutils-avr to build firmware/src)")
endif()

add_executable(siliconforge_tests
    support/PublicHeaders.cpp
    support/OpcodeCoverageListener.cpp
    unit/DescriptionTests.cpp
    unit/CircuitDescriptionTests.cpp
    unit/CircuitModelTests.cpp
    unit/SignalRecorderTests.cpp
    unit/LoaderTests.cpp
    unit/SchedulerTests.cpp
    unit/AvrInstructionTests.cpp
    unit/OpcodeClassTests.cpp
    unit/AvrInterruptTests.cpp
    unit/GpioTests.cpp
    unit/Timer0Tests.cpp
    unit/UsartTests.cpp
    unit/EepromAdcTests.cpp
    unit/AtdfTests.cpp
    unit/GifTests.cpp
    unit/ScenarioTests.cpp
    integration/FirmwareTests.cpp)
target_link_libraries(siliconforge_tests PRIVATE SiliconForge::mcu SiliconForge::circuit SiliconForge::scenario SiliconForge::atdf
    SiliconForge::gif SiliconForge_options Catch2::Catch2WithMain)
# The unit tests of the scenario engine reach its SHA-256.
target_include_directories(siliconforge_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(siliconforge_tests PRIVATE SILICONFORGE_MCU_DIR="${PROJECT_SOURCE_DIR}/mcus" SILICONFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" SILICONFORGE_FIRMWARE_DIR="${SILICONFORGE_FIRMWARE_DIR}")
if(SILICONFORGE_FIRMWARE_FROM_SOURCE)
    target_compile_definitions(siliconforge_tests PRIVATE SILICONFORGE_FIRMWARE_FROM_SOURCE)
    add_dependencies(siliconforge_tests firmware)
endif()
if(AVR_OBJDUMP)
    # An independent reading of the instruction set for the classification of the opcodes (ADR 0006).
    target_compile_definitions(siliconforge_tests PRIVATE SILICONFORGE_AVR_OBJDUMP="${AVR_OBJDUMP}")
endif()
# The opcodes executed by the tests feed the qualification matrix (ADR 0006): one file per test process.
catch_discover_tests(siliconforge_tests PROPERTIES TIMEOUT 60 LABELS fast
    ENVIRONMENT "SILICONFORGE_OPCODE_COVERAGE=${PROJECT_BINARY_DIR}/qualification/coverage/tests")

add_subdirectory(qualification)

add_test(NAME Atdf2JsonCommandLine
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:atdf2json> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/Atdf2JsonCommandLine
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Atdf2JsonCliTests.cmake)
set_tests_properties(Atdf2JsonCommandLine PROPERTIES TIMEOUT 30 LABELS fast)

add_test(NAME BenchCommandLine
    COMMAND ${CMAKE_COMMAND} -DBENCH=$<TARGET_FILE:siliconforge-bench> -DSOURCE=${PROJECT_SOURCE_DIR}
            -DFIRMWARE=${SILICONFORGE_FIRMWARE_DIR} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/BenchCommandLine
            -P ${CMAKE_CURRENT_SOURCE_DIR}/BenchCliTests.cmake)
set_tests_properties(BenchCommandLine PROPERTIES TIMEOUT 60 LABELS fast)

# Explicit opt-in target for profiling; not part of CTest or the default build.
add_executable(siliconforge_mcu_bench EXCLUDE_FROM_ALL bench/McuBench.cpp)
target_link_libraries(siliconforge_mcu_bench PRIVATE SiliconForge::mcu SiliconForge_options)

# The dispatch comparison of docs/MCU-DESCRIPTIONS.md (Linux: CPU affinity, perf). A short run with the same binary in
# the three roles keeps it working; real measures use three Release builds and the documented options.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(siliconforge_compare_dispatch bench/CompareDispatch.cpp)
    target_link_libraries(siliconforge_compare_dispatch PRIVATE nlohmann_json::nlohmann_json SiliconForge_options)
    if(SILICONFORGE_BUILD_APP)
        add_test(NAME CompareDispatchSmoke COMMAND siliconforge_compare_dispatch
            --baseline $<TARGET_FILE:siliconforge> --large $<TARGET_FILE:siliconforge> --compact $<TARGET_FILE:siliconforge>
            --firmware ${SILICONFORGE_FIRMWARE_DIR}/blink.hex --output ${CMAKE_CURRENT_BINARY_DIR}/compare-dispatch-smoke.json
            --blocks 6 --seconds 0.05 --warmup-seconds 0 --resamples 100)
        set_tests_properties(CompareDispatchSmoke PROPERTIES TIMEOUT 60 LABELS fast)
    endif()
endif()

if(SILICONFORGE_BUILD_APP)
    add_test(NAME CliHelpRejectsInvalidOptions
        COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:siliconforge> -P ${CMAKE_CURRENT_SOURCE_DIR}/CliHelpTests.cmake)
    set_tests_properties(CliHelpRejectsInvalidOptions PROPERTIES TIMEOUT 15 LABELS fast)
    add_test(NAME McuCommandLine
        COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:siliconforge> -DSOURCE=${PROJECT_SOURCE_DIR}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/McuCommandLine -P ${CMAKE_CURRENT_SOURCE_DIR}/McuCliTests.cmake)
    set_tests_properties(McuCommandLine PROPERTIES TIMEOUT 60 LABELS fast)
    add_executable(siliconforge_bridge_tests integration/WebBridgeTests.cpp integration/BenchWindowTests.cpp integration/TerminalTests.cpp integration/ProjectTests.cpp)
    target_link_libraries(siliconforge_bridge_tests PRIVATE SiliconForge::app SiliconForge_options Catch2::Catch2WithMain)
    target_compile_definitions(siliconforge_bridge_tests PRIVATE SILICONFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    catch_discover_tests(siliconforge_bridge_tests PROPERTIES TIMEOUT 15 LABELS fast)

    get_target_property(_ui_webfront_definitions WebFront::WebFront INTERFACE_COMPILE_DEFINITIONS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _ui_webfront_definitions MATCHES "WEBFRONT_EMBED_WEBKITGTK")
        # WebFront's autodetected option is scoped to its subdirectory; expose it to the helper's validation.
        set(WEBFRONT_EMBED_WEBKITGTK ON)
        add_executable(siliconforge_ui_test ../apps/siliconforge/main.cpp)
        if(SILICONFORGE_FIRMWARE_FROM_SOURCE)
            add_dependencies(siliconforge_ui_test firmware)
        endif()
        target_link_libraries(siliconforge_ui_test PRIVATE SiliconForge::app SiliconForge_options)
        webfront_add_jasmine_test(NAME UI-Uart TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/board.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/uart_echo.hex)
        webfront_add_jasmine_test(NAME UI-Blink TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/blink.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/blink.hex)
        webfront_add_jasmine_test(NAME UI-Led TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/led.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/blink.hex)
        webfront_add_jasmine_test(NAME UI-LedRealtime TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/led-realtime.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/blink.hex)
        # Files chosen in the window: the spec reads its firmware and MCU description from a generated script. The
        # committed prebuilt firmware is used so that the fixtures exist at configure time.
        file(READ ${PROJECT_SOURCE_DIR}/mcus/atmega328p.json _ui_mcu_fixture)
        file(READ ${PROJECT_SOURCE_DIR}/firmware/prebuilt/blink.hex _ui_hex_fixture)
        string(REPLACE "\r" "" _ui_hex_fixture "${_ui_hex_fixture}")
        string(REPLACE "\n" "\\n" _ui_hex_fixture "${_ui_hex_fixture}")
        file(READ ${PROJECT_SOURCE_DIR}/circuits/led-buttons.json _ui_circuit_fixture)
        file(READ ${PROJECT_SOURCE_DIR}/circuits/arduino-demo.json _ui_demo_fixture)
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ui-fixtures.js
            "window.sfFixtures = {mcu: ${_ui_mcu_fixture}, hex: \"${_ui_hex_fixture}\", circuit: ${_ui_circuit_fixture}, demo: ${_ui_demo_fixture}};\n")
        webfront_add_jasmine_test(NAME UI-Files TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ${CMAKE_CURRENT_BINARY_DIR}/ui-fixtures.js ui/files.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/blink.hex)
        # The instruments on the demonstration circuit, then the circuits themselves: the 7-segment counter given on the
        # command line, the circuits chosen and loaded from the page. Both use the firmware embedded in the application.
        webfront_add_jasmine_test(NAME UI-Instruments TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/instruments.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/demo.hex)
        webfront_add_jasmine_test(NAME UI-Circuits TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ${CMAKE_CURRENT_BINARY_DIR}/ui-fixtures.js ui/circuits.spec.js
            ARGS --port 0 --circuit seven-segment ${SILICONFORGE_FIRMWARE_DIR}/counter.hex)
        # MCU descriptions selected, loaded and rejected in the window (the former manual Playwright scenario).
        webfront_add_jasmine_test(NAME UI-Mcu TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ${CMAKE_CURRENT_BINARY_DIR}/ui-fixtures.js ui/mcu.spec.js ARGS --port 0 ${SILICONFORGE_FIRMWARE_DIR}/blink.hex)
        # Screenshots of the real window for the documentation, each backed by an assertion.
        webfront_add_jasmine_test(NAME UI-Tour TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/tour.spec.js ARGS --port 0)
        # The bench: a project directory of the build tree (the bench works on the scenarios and runs of the working
        # directory) with the example scenario and a variant whose first measurement cannot hold.
        set(_ui_bench_project ${CMAKE_CURRENT_BINARY_DIR}/ui-bench-project)
        file(REMOVE_RECURSE ${_ui_bench_project})
        file(COPY ${PROJECT_SOURCE_DIR}/scenarios/pwm-setpoint.json DESTINATION ${_ui_bench_project}/scenarios)
        file(COPY ${PROJECT_SOURCE_DIR}/circuits/arduino-demo.json DESTINATION ${_ui_bench_project}/circuits)
        file(COPY ${PROJECT_SOURCE_DIR}/firmware/prebuilt/pwm_setpoint.hex DESTINATION ${_ui_bench_project}/firmware/prebuilt)
        file(READ ${PROJECT_SOURCE_DIR}/scenarios/pwm-setpoint.json _ui_bench_scenario)
        string(JSON _ui_bench_scenario SET "${_ui_bench_scenario}" name "\"PWM setpoint that cannot hold\"")
        string(JSON _ui_bench_scenario SET "${_ui_bench_scenario}" steps 1 measure expect "{\"value\": 50, \"tolerance\": 0.5}")
        file(WRITE ${_ui_bench_project}/scenarios/off-target.json "${_ui_bench_scenario}\n")
        # A run of about four seconds: long enough to check that the window stays served while a scenario runs.
        file(READ ${PROJECT_SOURCE_DIR}/scenarios/pwm-setpoint.json _ui_bench_scenario)
        string(JSON _ui_bench_scenario SET "${_ui_bench_scenario}" name "\"Twenty simulated seconds\"")
        string(JSON _ui_bench_scenario SET "${_ui_bench_scenario}" steps "[{\"wait\": \"20s\"}, {\"measure\": {\"id\": \"duty\", \"pin\": \"D6\", \"quantity\": \"dutyCycle\", \"window\": \"1ms\", \"expect\": {\"value\": 25, \"tolerance\": 0.5}}}]")
        file(WRITE ${_ui_bench_project}/scenarios/slow.json "${_ui_bench_scenario}\n")
        webfront_add_jasmine_test(NAME UI-Bench TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/bench.spec.js ARGS --port 0)
        set_tests_properties(UI-Bench PROPERTIES WORKING_DIRECTORY ${_ui_bench_project})
        # The agent terminal (#55), in the same project directory, with the shell standing for a client.
        webfront_add_jasmine_test(NAME UI-Terminal TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/terminal.spec.js ARGS --port 0)
        # Started as from the terminal of an IDE running an agent: the client must not inherit that session.
        set_tests_properties(UI-Terminal PROPERTIES WORKING_DIRECTORY ${_ui_bench_project}
            ENVIRONMENT "CLAUDE_CODE_SSE_PORT=51234;CLAUDECODE=1;TERM_PROGRAM=vscode;VSCODE_GIT_IPC_HANDLE=/tmp/none;CLAUDE_CODE_USE_BEDROCK=1")
        # Opt-in qualification of the clients installed on the machine (docs/TERMINAL.md): no test of the suite needs one.
        webfront_add_jasmine_test(NAME UI-TerminalClients TARGET siliconforge_ui_test FRONTEND WebKitGTK
            SPECS ui/terminal-clients.spec.js ARGS --port 0 TIMEOUT 400)
        set_tests_properties(UI-TerminalClients PROPERTIES WORKING_DIRECTORY ${_ui_bench_project})
        set_property(TEST UI-Uart UI-Blink UI-Led UI-LedRealtime UI-Files UI-Instruments UI-Circuits UI-Mcu UI-Tour UI-Bench UI-Terminal UI-TerminalClients APPEND PROPERTY LABELS ui)
    else()
        message(STATUS "Native UI tests skipped: they need Linux and WebFront's WebKitGTK frontend")
    endif()
endif()
