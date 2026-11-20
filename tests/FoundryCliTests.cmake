# The PWM correction loop of docs/FOUNDRY.md (#61), from the terminal: the agent changes the firmware for the setpoint,
# builds it with the chain of the project, the bench loads the HEX and finds a deviation on the pin, the agent corrects
# the firmware and the bench gives a passing verdict that a second run reproduces byte for byte.
# The two changes of the agent are tests/foundry/*.patch; the project is made by FoundryProject.cmake.
#   cmake -DBENCH=<siliconforge> -DSOURCE=<repository> -DPROJECT=<scratch project> -DMAKE=<make> -P FoundryCliTests.cmake
foreach(variable IN ITEMS BENCH SOURCE PROJECT MAKE)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is not defined")
    endif()
endforeach()
include(${CMAKE_CURRENT_LIST_DIR}/FoundryProject.cmake)
set(runs ${PROJECT}/.siliconforge/runs/pwm-setpoint)

# Runs a command in the project; fails unless it exits with `expected`. Leaves its output in `stdout`.
function(run expected)
    execute_process(COMMAND ${ARGN} TIMEOUT 120 WORKING_DIRECTORY ${PROJECT} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result STREQUAL "${expected}")
        message(FATAL_ERROR "${ARGN}: exit ${result}, expected ${expected}\n${out}\n${err}")
    endif()
    set(stdout "${out}" PARENT_SCOPE)
endfunction()

# What the agent does after each change: build with the chain of the project (the HEX the scenario loads), then run.
function(build_and_run expected)
    run(0 ${MAKE} -s -C firmware prebuilt)
    run(${expected} ${BENCH} bench run scenarios/pwm-setpoint.json)
    file(READ ${runs}/report.json report)
    set(report "${report}" PARENT_SCOPE)
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

# The value and the verdict of a measurement of the report.
function(measurement id)
    string(JSON count LENGTH "${report}" measurements)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON name GET "${report}" measurements ${index} id)
        if(name STREQUAL id)
            string(JSON verdict GET "${report}" measurements ${index} verdict)
            string(JSON value GET "${report}" measurements ${index} value)
            set(verdict ${verdict} PARENT_SCOPE)
            set(value ${value} PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "No measurement ${id} in the report:\n${report}")
endfunction()

# The bench loaded the HEX the chain just built, not another one.
function(expect_built_firmware)
    file(SHA256 ${PROJECT}/firmware/prebuilt/pwm_setpoint.hex built)
    string(JSON loaded GET "${report}" inputs 2 sha256)
    string(JSON role GET "${report}" inputs 2 role)
    if(NOT role STREQUAL "firmware" OR NOT loaded STREQUAL built)
        message(FATAL_ERROR "The bench loaded firmware ${loaded}, the chain built ${built}")
    endif()
endfunction()

# Before any change: the firmware of the project does not meet the new setpoint (25 % instead of 0 %).
run(1 ${BENCH} bench run scenarios/pwm-setpoint.json)
file(READ ${runs}/report.json report)
measurement(duty-released)
if(NOT verdict STREQUAL "fail" OR value LESS 24.5 OR value GREATER 25.5)
    message(FATAL_ERROR "Before the change, duty-released is ${value} % (${verdict}), expected the former 25 %")
endif()

# First change of the agent: a setpoint of 0 %. Timer0 in Fast PWM still drives D6 high for one count per period at
# OCR0A = 0, so the bench finds a deviation, in both released phases: the pulse of one cycle out of 256 (0.390625 %, as
# on the hardware, #73). The verdict fails, the measurements and the trace are kept.
run(0 git apply ${SOURCE}/tests/foundry/1-setpoint-off.patch)
build_and_run(1)
expect_built_firmware()
string(FIND "${stdout}" "FAIL duty-released dutyCycle on D6" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The deviation is not in the summary:\n${stdout}")
endif()
foreach(id IN ITEMS duty-released duty-released-again)
    measurement(${id})
    if(NOT verdict STREQUAL "fail" OR NOT value EQUAL 0.390625)
        message(FATAL_ERROR "With OCR0A = 0, ${id} is ${value} % (${verdict}): expected one cycle out of 256, 0.390625 %")
    endif()
endforeach()
measurement(duty-held)
if(NOT verdict STREQUAL "pass")
    message(FATAL_ERROR "The held setpoint broke with the first change:\n${report}")
endif()
if(NOT EXISTS ${runs}/trace.vcd)
    message(FATAL_ERROR "The failed run left no trace")
endif()
# The person reviews the change of the agent.
run(0 git diff --name-only)
string(FIND "${stdout}" "firmware/src/pwm_setpoint.c" position)
if(position EQUAL -1)
    message(FATAL_ERROR "git diff does not show the change of the firmware:\n${stdout}")
endif()

# The correction: 0 % disconnects OC0A. The verdict passes, and a second run on the same inputs gives the same evidence.
run(0 git apply ${SOURCE}/tests/foundry/2-disconnect-oc0a.patch)
build_and_run(0)
expect_built_firmware()
measurement(duty-released)
if(NOT value EQUAL 0)
    message(FATAL_ERROR "After the correction, duty-released is ${value} %")
endif()
file(SHA256 ${runs}/report.json first_report)
file(SHA256 ${runs}/trace.vcd first_trace)
run(0 ${BENCH} bench run scenarios/pwm-setpoint.json)
file(SHA256 ${runs}/report.json second_report)
file(SHA256 ${runs}/trace.vcd second_trace)
if(NOT first_report STREQUAL second_report OR NOT first_trace STREQUAL second_trace)
    message(FATAL_ERROR "A second run on the same inputs gave other evidence")
endif()
