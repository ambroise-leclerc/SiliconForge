# MCU discovery in --help, a description loaded by file and by catalog name, and the JSON path of an invalid one.
# cmake -DAPP=<siliconforge> -DSOURCE=<repository> -DWORK=<scratch directory> -P McuCliTests.cmake
# A CMake script needs nothing beyond the build tools, on every platform (issue #34).
foreach(variable IN ITEMS APP SOURCE WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is not defined")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(chip "${WORK}/atmega168.json")
set(firmware "${SOURCE}/firmware/prebuilt/blink.hex")

# Runs the application; fails unless it exits with `expected`. Leaves its streams in `stdout` and `stderr`.
function(run expected)
    execute_process(COMMAND "${APP}" ${ARGN} TIMEOUT 20
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result STREQUAL "${expected}")
        message(FATAL_ERROR "siliconforge ${ARGN}: exit ${result}, expected ${expected}\n${out}\n${err}")
    endif()
    set(stdout "${out}" PARENT_SCOPE)
    set(stderr "${err}" PARENT_SCOPE)
endfunction()

function(expect_contains text expected what)
    string(FIND "${text}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${what} lacks '${expected}':\n${text}")
    endif()
endfunction()

# The ATmega168 derived from the shipped ATmega328P, as docs/MCU-DESCRIPTIONS.md describes it.
file(READ "${SOURCE}/mcus/atmega328p.json" d)
string(JSON d SET "${d}" name "\"ATmega168\"")
string(JSON d SET "${d}" memories flash size 16384)
string(JSON d SET "${d}" memories sram size 1024)
string(JSON d SET "${d}" memories eeprom size 512)
string(JSON d SET "${d}" registers SPH reset 4)
file(WRITE "${chip}" "${d}")

foreach(order IN ITEMS help-first path-first)
    if(order STREQUAL "help-first")
        run(0 --help --mcu-path "${WORK}")
    else()
        run(0 --mcu-path "${WORK}" --help)
    endif()
    expect_contains("${stdout}" "atmega328p attiny424 atmega2560 atmega168" "--help (${order})")
    expect_contains("${stdout}" "--mcu-file" "--help (${order})")
endforeach()

run(0 --mcu-file "${chip}" --headless --speed max --duration 0.1 "${firmware}")
run(0 --mcu-path "${WORK}" --mcu ATMEGA168 --headless --speed max --duration 0.1 "${firmware}")

string(JSON count LENGTH "${d}" peripherals)
math(EXPR last "${count} - 1")
set(adc -1)
foreach(index RANGE ${last})
    string(JSON type GET "${d}" peripherals ${index} type)
    if(type STREQUAL "avr.adc")
        set(adc ${index})
        break()
    endif()
endforeach()
if(adc EQUAL -1)
    message(FATAL_ERROR "The ATmega328P description has no avr.adc peripheral")
endif()
string(JSON d SET "${d}" peripherals ${adc} vector "\"missing\"")
file(WRITE "${chip}" "${d}")
run(1 --mcu-file "${chip}")
expect_contains("${stderr}" "/peripherals/${adc}/vector" "the diagnostic of an invalid description")

file(REMOVE_RECURSE "${WORK}")
