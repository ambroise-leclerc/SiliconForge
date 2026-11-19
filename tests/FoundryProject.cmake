# Makes the project of the exercise of docs/FOUNDRY.md (#61): the firmware sources and their Makefile, the committed
# binaries, the demonstration circuit and the setpoint of the exercise (tests/foundry/pwm-setpoint.json: D6 off while
# the button is released), committed in a Git repository of their own so that the change of the agent shows as a diff.
# Run before each execution of the tests that use it (a fixture), since they change these files.
#   cmake -DSOURCE=<repository> -DPROJECT=<directory> -P FoundryProject.cmake
foreach(variable IN ITEMS SOURCE PROJECT)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is not defined")
    endif()
endforeach()
find_program(GIT git)
if(NOT GIT)
    message(FATAL_ERROR "git is needed for the project of the exercise")
endif()

file(REMOVE_RECURSE ${PROJECT})
file(COPY ${SOURCE}/firmware/Makefile ${SOURCE}/firmware/src ${SOURCE}/firmware/prebuilt DESTINATION ${PROJECT}/firmware)
file(COPY ${SOURCE}/circuits/arduino-demo.json DESTINATION ${PROJECT}/circuits)
file(COPY ${SOURCE}/tests/foundry/pwm-setpoint.json DESTINATION ${PROJECT}/scenarios)
file(CONFIGURE OUTPUT ${PROJECT}/.gitignore CONTENT ".siliconforge/\nfirmware/build/\n" @ONLY NEWLINE_STYLE UNIX)
set(identity -c user.name=foundry -c user.email=foundry@example.invalid -c commit.gpgsign=false)
foreach(step "init;-q" "add;-A" "${identity};commit;-q;-m;The setpoint of the exercise: D6 off while the button is released")
    execute_process(COMMAND ${GIT} ${step} WORKING_DIRECTORY ${PROJECT} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "git ${step} failed in ${PROJECT}")
    endif()
endforeach()
