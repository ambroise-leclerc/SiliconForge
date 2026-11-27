# siliconforge bench (#59, #80): the documented exit codes, and the evidence kept even when an assertion fails.
# cmake -DBENCH=<siliconforge> -DSOURCE=<repository> -DFIRMWARE=<firmware directory> -DWORK=<scratch> -P BenchCliTests.cmake
foreach(variable IN ITEMS BENCH SOURCE FIRMWARE WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is not defined")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(example "${SOURCE}/scenarios/pwm-setpoint.json")

# Runs the command; fails unless it exits with `expected`. Leaves its streams in `stdout` and `stderr`.
function(run expected)
    # Out of the window this test may be started from: a run here is what is tested (see the case of a window below).
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=SILICONFORGE_MCP_URL --unset=SILICONFORGE_MCP_TOKEN --unset=SILICONFORGE_PROJECT
                            ${window} "${BENCH}" bench ${ARGN} TIMEOUT 30 WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result STREQUAL "${expected}")
        message(FATAL_ERROR "siliconforge bench ${ARGN}: exit ${result}, expected ${expected}\n${out}\n${err}")
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

# The verdict of a report.json, and whether the trace beside it exists.
function(expect_report directory verdict)
    if(NOT EXISTS "${directory}/report.json")
        message(FATAL_ERROR "No report in ${directory}")
    endif()
    file(READ "${directory}/report.json" report)
    string(JSON actual GET "${report}" verdict)
    if(NOT actual STREQUAL verdict)
        message(FATAL_ERROR "${directory}/report.json: verdict ${actual}, expected ${verdict}")
    endif()
    string(JSON version GET "${report}" engine version)
    string(JSON inputs LENGTH "${report}" inputs)
    if(version STREQUAL "" OR inputs LESS 1)
        message(FATAL_ERROR "${directory}/report.json lacks the engine version or the inputs:\n${report}")
    endif()
    set(report "${report}" PARENT_SCOPE)
endfunction()

# The committed example with absolute paths, a firmware of the build and one change.
function(variant name firmware)
    file(READ "${example}" d)
    string(JSON d SET "${d}" circuit "\"${SOURCE}/circuits/arduino-demo.json\"")
    string(JSON d SET "${d}" firmware "\"${FIRMWARE}/${firmware}\"")
    if(ARGC GREATER 2)
        string(JSON d SET "${d}" ${ARGN})
    endif()
    file(WRITE "${WORK}/${name}.json" "${d}")
endfunction()

# Usage.
run(0 --help)
expect_contains("${stdout}" "Exit codes:" "--help")
run(3)
run(3 run)
run(3 frobnicate "${example}")
run(3 run --unknown "${example}")

# Pass: summary, report and trace in the default directory.
run(0 check "${example}")
expect_contains("${stdout}" "ready to run" "check")
run(0 run "${example}")
expect_contains("${stdout}" "PWM setpoint on D6: pass" "run")
expect_report("${WORK}/.siliconforge/runs/pwm-setpoint" pass)
if(NOT EXISTS "${WORK}/.siliconforge/runs/pwm-setpoint/trace.vcd")
    message(FATAL_ERROR "No trace beside the report")
endif()
run(0 run --json --out "${WORK}/json" "${example}")

string(JSON verdict GET "${stdout}" verdict)
if(NOT verdict STREQUAL "pass")
    message(FATAL_ERROR "--json printed verdict ${verdict}")
endif()

# A project with no explicit chip keeps the ATmega328P and its Arduino pin labels.
file(READ "${SOURCE}/circuits/arduino-demo.json" legacyCircuit)
string(JSON legacyCircuit REMOVE "${legacyCircuit}" chip mcu)
file(WRITE "${WORK}/legacy-circuit.json" "${legacyCircuit}")
variant(legacy pwm_setpoint.hex circuit "\"legacy-circuit.json\"")
run(0 run --out "${WORK}/legacy" "${WORK}/legacy.json")
expect_report("${WORK}/legacy" pass)
string(JSON legacyMcu GET "${report}" mcu name)
if(NOT legacyMcu STREQUAL "ATmega328P")
    message(FATAL_ERROR "A circuit without chip.mcu selected ${legacyMcu}")
endif()

# The bench checks the selected ATtiny424's Flash, including an odd final address.
file(WRITE "${WORK}/tiny-circuit.json" [=[
{"formatVersion":1,"name":"ATtiny424","board":{"type":"breadboard","columns":30},"chip":{"mcu":"attiny424","column":1},"components":[],"wires":[]}
]=])
file(WRITE "${WORK}/tiny.json" [=[
{"formatVersion":1,"name":"ATtiny424 Flash","circuit":"tiny-circuit.json","firmware":"tiny.hex","steps":[{"wait":"1us"}]}
]=])
file(WRITE "${WORK}/tiny.hex" ":010FFF00AA47\n:00000001FF\n")
run(0 check "${WORK}/tiny.json")
file(WRITE "${WORK}/tiny.hex" ":01100000AA45\n:00000001FF\n")
run(2 check "${WORK}/tiny.json")
expect_contains("${stderr}" "Flash address 0x1000 is outside ATtiny424 memory" "ATtiny424 Flash diagnostic")

# Started in the terminal of a window that no longer answers: the run is kept here, as without a window.
set(window "SILICONFORGE_MCP_URL=http://127.0.0.1:1/mcp/" "SILICONFORGE_MCP_TOKEN=0123456789abcdef" "SILICONFORGE_PROJECT=${SOURCE}")
file(REMOVE_RECURSE "${WORK}/.siliconforge/runs/pwm-setpoint")
run(0 run "${example}")
expect_contains("${stderr}" "The window does not answer" "a run whose window is gone")
expect_contains("${stdout}" "PWM setpoint on D6: pass" "a run whose window is gone")
expect_report("${WORK}/.siliconforge/runs/pwm-setpoint" pass)
set(window "")

# Fail: a firmware that never acknowledges; the measurements made so far and the trace are kept.
variant(timeout pwm.hex)
run(1 run --out "${WORK}/timeout" "${WORK}/timeout.json")
expect_contains("${stdout}" "D13 high not reached within" "a timeout")
expect_report("${WORK}/timeout" fail)
string(JSON measured LENGTH "${report}" measurements)
if(NOT measured EQUAL 2 OR NOT EXISTS "${WORK}/timeout/trace.vcd")
    message(FATAL_ERROR "The failed run lost its measurements or its trace:\n${report}")
endif()

# Fail: a missed expectation.
variant(missed pwm_setpoint.hex steps 5 measure expect value 60)
run(1 run --out "${WORK}/missed" "${WORK}/missed.json")
expect_contains("${stdout}" "FAIL duty-held" "a missed expectation")
expect_report("${WORK}/missed" fail)

# Incompatible: before anything runs, with the cause on stderr and in the report.
variant(version pwm_setpoint.hex formatVersion 2)
run(2 check "${WORK}/version.json")
expect_contains("${stderr}" "/formatVersion: incompatible (version)" "check of an unknown version")
run(2 run --out "${WORK}/version" "${WORK}/version.json")
expect_report("${WORK}/version" incompatible)
string(JSON cause GET "${report}" problem cause)
if(NOT cause STREQUAL "version" OR EXISTS "${WORK}/version/trace.vcd")
    message(FATAL_ERROR "An incompatible run reports cause ${cause} or left a trace")
endif()
variant(resource pwm_setpoint.hex steps 1 measure pin "\"D42\"")
run(2 run --out "${WORK}/resource" "${WORK}/resource.json")
expect_contains("${stderr}" "/steps/1/measure/pin: incompatible (resource): ATmega328P has no pin D42" "a missing pin")
expect_report("${WORK}/resource" incompatible)
string(JSON inputs LENGTH "${report}" inputs)
if(NOT inputs EQUAL 3)
    message(FATAL_ERROR "The incompatible report lost the inputs read before the refusal:\n${report}")
endif()

# A valid HEX beyond the flash of the MCU: refused by check and by run, with a report, instead of an abort.
file(WRITE "${WORK}/too-large.hex" ":020000040001F9\n:01000000AA55\n:00000001FF\n")
file(READ "${example}" d)
string(JSON d SET "${d}" circuit "\"${SOURCE}/circuits/arduino-demo.json\"")
string(JSON d SET "${d}" firmware "\"too-large.hex\"")
file(WRITE "${WORK}/too-large.json" "${d}")
run(2 check "${WORK}/too-large.json")
expect_contains("${stderr}" "incompatible (capacity): ATmega328P cannot load this firmware" "check of a firmware too large")
run(2 run --out "${WORK}/too-large" "${WORK}/too-large.json")
expect_report("${WORK}/too-large" incompatible)

# A run that becomes incompatible in a directory that holds the trace of an earlier run: the stale trace is removed.
run(0 run --out "${WORK}/reused" "${example}")
if(NOT EXISTS "${WORK}/reused/trace.vcd")
    message(FATAL_ERROR "The passing run left no trace")
endif()
run(2 run --out "${WORK}/reused" "${WORK}/version.json")
expect_report("${WORK}/reused" incompatible)
if(EXISTS "${WORK}/reused/trace.vcd")
    message(FATAL_ERROR "The trace of the earlier run survived beside an incompatible report")
endif()

# A firmware that transmits bytes which are not UTF-8 (0xFF, then "ok" and "é"): the verdict and the evidence are kept,
# the bytes in base64 and as text with U+FFFD for what is not UTF-8.
file(WRITE "${WORK}/binary.hex" ":1000000008E00093C1001091C00015FFFCCF2FEF56
:100010002093C6001091C00015FFFCCF2FE620935F
:10002000C6001091C00015FFFCCF2BE62093C60040
:100030001091C00015FFFCCF23EC2093C600109157
:0E004000C00015FFFCCF29EA2093C600FFCFB9
:00000001FF
")
file(READ "${example}" d)
string(JSON d SET "${d}" circuit "\"${SOURCE}/circuits/arduino-demo.json\"")
string(JSON d SET "${d}" firmware "\"binary.hex\"")
string(JSON d SET "${d}" initial "{}")
string(JSON d SET "${d}" steps "[{\"waitFor\": {\"serial\": \"ok\"}, \"timeout\": \"1ms\"}, {\"wait\": \"100us\"}]")
file(WRITE "${WORK}/binary.json" "${d}")
run(0 run --out "${WORK}/binary" "${WORK}/binary.json")
expect_report("${WORK}/binary" pass)
string(JSON encoded GET "${report}" serial 0 base64)
if(NOT encoded STREQUAL "/29rw6k=" OR NOT EXISTS "${WORK}/binary/trace.vcd")
    message(FATAL_ERROR "The serial bytes or the trace were not kept:\n${report}")
endif()
