# Warnings applied to SiliconForge's own targets only (never propagated to consumers' code).
function(siliconforge_set_warnings target)
    if(MSVC)
        # getenv and friends are standard C++: do not let the CRT's C4996 deprecations break /WX.
        target_compile_definitions(${target} INTERFACE _CRT_SECURE_NO_WARNINGS)
        # C4714 (a __forceinline function was not inlined) only fires with the optimiser, on stdexec's headers.
        target_compile_options(${target} INTERFACE /W4 /permissive- /wd4714)
        if(SILICONFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} INTERFACE /WX)
        endif()
    else()
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            $<IF:$<CXX_COMPILER_ID:GNU>,-Wshadow=local,-Wshadow>
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
