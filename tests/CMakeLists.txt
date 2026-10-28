CPMAddPackage("gh:catchorg/Catch2@3.15.2")
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

add_executable(siliconforge_tests
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
