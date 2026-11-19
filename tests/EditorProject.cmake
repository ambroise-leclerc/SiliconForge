# Makes the project directory of UI-Editor (tests/ui/editor.spec.js): three files, committed in a Git repository of their
# own when `git` is installed. The build tree is ignored by the repository of the sources, which would show no change.
# The files have LF line ends on every platform (file(WRITE) would write CRLF on Windows). Run before each execution of the test (a fixture), since the test edits these files.
#   cmake -DPROJECT=<directory> -P EditorProject.cmake
file(REMOVE_RECURSE ${PROJECT})
file(CONFIGURE OUTPUT ${PROJECT}/main.cpp CONTENT "int main() {\n  return 0;\n}\n" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT ${PROJECT}/src/pwm.cpp CONTENT "// PWM at 25 %\n" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT ${PROJECT}/notes.txt CONTENT "Notes for the agent\n" @ONLY NEWLINE_STYLE UNIX)
find_program(GIT git)
if(GIT)
    set(identity -c user.name=ui -c user.email=ui@example.invalid -c commit.gpgsign=false)
    foreach(step "init;-q" "add;-A" "${identity};commit;-q;-m;initial")
        execute_process(COMMAND ${GIT} ${step} WORKING_DIRECTORY ${PROJECT} RESULT_VARIABLE result)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "git ${step} failed in ${PROJECT}")
        endif()
    endforeach()
endif()
