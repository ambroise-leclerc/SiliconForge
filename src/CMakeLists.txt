# JSON is parsed and validated only when a device is loaded.
CPMAddPackage(NAME nlohmann_json VERSION 3.12.0
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")
set_property(TARGET nlohmann_json PROPERTY SYSTEM TRUE)
CPMAddPackage(NAME nlohmann_json_schema_validator VERSION 2.4.0
    URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.4.0.tar.gz
    OPTIONS "JSON_VALIDATOR_BUILD_TESTS OFF" "JSON_VALIDATOR_BUILD_EXAMPLES OFF" "JSON_VALIDATOR_INSTALL OFF")
file(READ "${PROJECT_SOURCE_DIR}/mcus/atmega328p.json" SILICONFORGE_ATMEGA328P_JSON)
file(READ "${PROJECT_SOURCE_DIR}/schemas/mcu.schema.json" SILICONFORGE_MCU_SCHEMA)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/mcus/atmega328p.json" "${PROJECT_SOURCE_DIR}/schemas/mcu.schema.json")
configure_file("${PROJECT_SOURCE_DIR}/cmake/EmbeddedDescriptions.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedDescriptions.hpp" @ONLY)

# Architecture-independent foundation: scheduler, pins, serial ports, debugger, firmware loaders.
add_library(SiliconForge_core STATIC
    core/Loader.cpp)
add_library(SiliconForge::core ALIAS SiliconForge_core)
target_include_directories(SiliconForge_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_compile_features(SiliconForge_core PUBLIC cxx_std_26)
target_link_libraries(SiliconForge_core PRIVATE SiliconForge_options)

# AVR family: core and peripherals shared by the megaAVR devices.
add_library(SiliconForge_avr STATIC
    arch/avr/Device.cpp
    arch/avr/AvrCore.cpp
    arch/avr/Instructions.cpp
    arch/avr/peripherals/Gpio.cpp
    arch/avr/peripherals/Timer8.cpp
    arch/avr/peripherals/Usart.cpp)
add_library(SiliconForge::avr ALIAS SiliconForge_avr)
target_link_libraries(SiliconForge_avr PUBLIC SiliconForge::core nlohmann_json::nlohmann_json PRIVATE SiliconForge_options)

# Concrete microcontrollers and the registry that creates them by name.
add_library(SiliconForge_mcu STATIC
    mcu/Atmega328p.cpp
    mcu/Registry.cpp
    mcu/Description.cpp)
add_library(SiliconForge::mcu ALIAS SiliconForge_mcu)
target_link_libraries(SiliconForge_mcu PUBLIC SiliconForge::avr PRIVATE SiliconForge_options nlohmann_json_schema_validator)
target_include_directories(SiliconForge_mcu PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
