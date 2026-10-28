# Help does not hide invalid options, in either order.
foreach(first IN ITEMS --help --option-invalide)
    if(first STREQUAL "--help")
        set(second --option-invalide)
    else()
        set(second --help)
    endif()
    execute_process(COMMAND "${APP}" "${first}" "${second}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 1 OR NOT output MATCHES "Usage: siliconforge" OR NOT error MATCHES "Unknown argument: --option-invalide")
        message(FATAL_ERROR "Unexpected help/error contract: ${result}\n${output}\n${error}")
    endif()
endforeach()
