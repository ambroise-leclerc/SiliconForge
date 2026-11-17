# Requires C++26 on a target. CMake 4.3 has no cxx_std_26 compile feature for MSVC, so cl gets /std:c++latest (the
# newest standard it implements) directly; the other compilers keep the compile feature.
function(siliconforge_require_cxx26 target scope)
    if(MSVC)
        target_compile_options(${target} ${scope} /std:c++latest)
    else()
        target_compile_features(${target} ${scope} cxx_std_26)
    endif()
endfunction()
