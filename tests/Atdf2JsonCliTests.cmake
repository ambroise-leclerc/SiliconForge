# The command line of tools/atdf2json: help, usage errors, standard output or -o, --base, and its diagnostics.
# cmake -DTOOL=<atdf2json> -DWORK=<scratch directory> -P Atdf2JsonCliTests.cmake
foreach(variable IN ITEMS TOOL WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is not defined")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/mini.atdf" [=[<avr-tools-device-file><devices><device name="TestAVR" architecture="AVR8">
<address-spaces><address-space>
<memory-segment name="FLASH" start="0" size="0x4000" pagesize="0x80"/>
<memory-segment name="IRAM" start="0x100" size="0x400"/>
<memory-segment name="MAPPED_IO" start="0x20" size="0xe0"/>
<memory-segment name="REGISTERS" start="0" size="32"/>
<memory-segment name="EEPROM" start="0" size="512"/>
</address-space></address-spaces><peripherals/><interrupts><interrupt name="reset" index="0"/></interrupts>
</device></devices><pinouts><pinout name="DIP2"><pin position="1" pad="PD1"/><pin position="2" pad="GND"/></pinout>
</pinouts></avr-tools-device-file>]=])

# Runs the tool; fails unless it exits with `expected`. Leaves its streams in `stdout` and `stderr`.
function(run expected)
    execute_process(COMMAND "${TOOL}" ${ARGN} TIMEOUT 20 RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result STREQUAL "${expected}")
        message(FATAL_ERROR "atdf2json ${ARGN}: exit ${result}, expected ${expected}\n${out}\n${err}")
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

run(0 --help)
expect_contains("${stdout}" "--package PACKAGE" "--help")
run(2)
expect_contains("${stderr}" "the following arguments are required: atdf" "a call without a file")
run(2 "${WORK}/mini.atdf" --unknown)
expect_contains("${stderr}" "unrecognized arguments: --unknown" "an unknown option")

run(0 "${WORK}/mini.atdf")
string(JSON vector GET "${stdout}" vectors RESET)
string(JSON size GET "${stdout}" memories flash pageSize)
if(NOT vector EQUAL 0 OR NOT size EQUAL 128)
    message(FATAL_ERROR "Unexpected conversion:\n${stdout}")
endif()
expect_contains("${stderr}" "Skeleton only" "a conversion without --base")

file(WRITE "${WORK}/base.json" [=[{"timing": {"nop": {"cycles": 9}}, "supplyVoltage": 5.0}]=])
run(0 "${WORK}/mini.atdf" --package=DIP2 --base "${WORK}/base.json" -o "${WORK}/out.json")
if(NOT stdout STREQUAL "" OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "A conversion with --base and -o writes nothing else:\n${stdout}\n${stderr}")
endif()
file(READ "${WORK}/out.json" converted)
expect_contains("${converted}" "\"cycles\": 9" "the kept timing")
expect_contains("${converted}" "\"supplyVoltage\": 5.0" "the kept supply voltage, written as in the base")

run(1 "${WORK}/mini.atdf" --package QFN)
expect_contains("${stderr}" "atdf2json: Unknown package QFN; available: DIP2" "an unknown package")
run(1 "${WORK}/missing.atdf")
expect_contains("${stderr}" "atdf2json: cannot read" "a missing file")
file(WRITE "${WORK}/broken.atdf" "<avr-tools-device-file><devices>")
run(1 "${WORK}/broken.atdf")
expect_contains("${stderr}" "atdf2json: " "a malformed file")

file(REMOVE_RECURSE "${WORK}")
