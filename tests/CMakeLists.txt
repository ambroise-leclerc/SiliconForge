CPMAddPackage("gh:catchorg/Catch2@3.15.2")
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

add_executable(siliconforge_tests
    support/PublicHeaders.cpp
    unit/DescriptionTests.cpp
    unit/LoaderTests.cpp
    unit/SchedulerTests.cpp
    unit/AvrInstructionTests.cpp
    unit/AvrInterruptTests.cpp
    unit/GpioTests.cpp
    unit/Timer0Tests.cpp
    unit/UsartTests.cpp
    unit/EepromAdcTests.cpp
    integration/FirmwareTests.cpp)
target_link_libraries(siliconforge_tests PRIVATE SiliconForge::mcu SiliconForge_options Catch2::Catch2WithMain)
target_compile_definitions(siliconforge_tests PRIVATE SILICONFORGE_MCU_DIR="${PROJECT_SOURCE_DIR}/mcus" SILICONFORGE_FIRMWARE_DIR="${PROJECT_SOURCE_DIR}/firmware/prebuilt")
catch_discover_tests(siliconforge_tests PROPERTIES TIMEOUT 60 LABELS fast)

# Explicit opt-in target for profiling; not part of CTest or the default build.
add_executable(siliconforge_mcu_bench EXCLUDE_FROM_ALL bench/McuBench.cpp)
target_link_libraries(siliconforge_mcu_bench PRIVATE SiliconForge::mcu SiliconForge_options)

if(SILICONFORGE_BUILD_APP)
    add_test(NAME CliHelpRejectsInvalidOptions
        COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:siliconforge> -P ${CMAKE_CURRENT_SOURCE_DIR}/CliHelpTests.cmake)
    set_tests_properties(CliHelpRejectsInvalidOptions PROPERTIES TIMEOUT 15 LABELS fast)
    add_executable(siliconforge_bridge_tests integration/WebBridgeTests.cpp)
    target_link_libraries(siliconforge_bridge_tests PRIVATE SiliconForge::mcu SiliconForge_options WebFront::WebFront Catch2::Catch2WithMain)
    catch_discover_tests(siliconforge_bridge_tests PROPERTIES TIMEOUT 15 LABELS fast)
endif()
