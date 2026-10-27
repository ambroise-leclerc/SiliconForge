# Warnings applied to SiliconForge's own targets only (never propagated to consumers' code).
function(siliconforge_set_warnings target)
    if(MSVC)
        target_compile_options(${target} INTERFACE /W4 /permissive-)
        if(SILICONFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} INTERFACE /WX)
        endif()
    else()
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wshadow=local
            -Wpedantic
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wimplicit-fallthrough)
        if(SILICONFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} INTERFACE -Werror)
        endif()
    endif()
endfunction()
