# JSON is parsed and validated only when a device is loaded.
CPMAddPackage(NAME nlohmann_json VERSION 3.12.0
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")
set_property(TARGET nlohmann_json PROPERTY SYSTEM TRUE)
CPMAddPackage(NAME nlohmann_json_schema_validator VERSION 2.4.0
    URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.4.0.tar.gz
    OPTIONS "JSON_VALIDATOR_BUILD_TESTS OFF" "JSON_VALIDATOR_BUILD_EXAMPLES OFF" "JSON_VALIDATOR_INSTALL OFF")
function(siliconforge_embed_bytes path variable)
    file(READ "${path}" _hex HEX)
    # Eight bytes per source line; no individual string literal and no oversized source line.
    string(REGEX REPLACE "................" "\\0\n" _lines "${_hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_lines}")
    set(${variable} "${_bytes}" PARENT_SCOPE)
endfunction()
siliconforge_embed_bytes("${PROJECT_SOURCE_DIR}/mcus/atmega328p.json" SILICONFORGE_ATMEGA328P_BYTES)
siliconforge_embed_bytes("${PROJECT_SOURCE_DIR}/schemas/mcu.schema.json" SILICONFORGE_MCU_SCHEMA_BYTES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/mcus/atmega328p.json" "${PROJECT_SOURCE_DIR}/schemas/mcu.schema.json")
configure_file("${PROJECT_SOURCE_DIR}/cmake/EmbeddedDescriptions.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedDescriptions.hpp" @ONLY)

# Components and circuits (ADR 0005): the schemas, every components/<type>/component.json and the demonstration circuit
# are embedded, so that an installed binary does not depend on the source tree.
siliconforge_embed_bytes("${PROJECT_SOURCE_DIR}/schemas/component.schema.json" SILICONFORGE_COMPONENT_SCHEMA_BYTES)
siliconforge_embed_bytes("${PROJECT_SOURCE_DIR}/schemas/circuit.schema.json" SILICONFORGE_CIRCUIT_SCHEMA_BYTES)
siliconforge_embed_bytes("${PROJECT_SOURCE_DIR}/circuits/arduino-demo.json" SILICONFORGE_ARDUINO_DEMO_BYTES)
file(GLOB _component_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/components/*/component.json")
list(SORT _component_files)
set(SILICONFORGE_COMPONENT_ARRAYS "")
set(SILICONFORGE_COMPONENT_VIEWS "")
foreach(_component_file IN LISTS _component_files)
    get_filename_component(_component_dir "${_component_file}" DIRECTORY)
    get_filename_component(_component_type "${_component_dir}" NAME)
    string(MAKE_C_IDENTIFIER "${_component_type}" _component_id)
    siliconforge_embed_bytes("${_component_file}" _component_bytes)
    string(APPEND SILICONFORGE_COMPONENT_ARRAYS
        "inline constexpr unsigned char component_${_component_id}[] = {\n${_component_bytes}\n};\n")
    string(APPEND SILICONFORGE_COMPONENT_VIEWS "    view(component_${_component_id}),\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/schemas/component.schema.json" "${PROJECT_SOURCE_DIR}/schemas/circuit.schema.json"
    "${PROJECT_SOURCE_DIR}/circuits/arduino-demo.json" ${_component_files})
configure_file("${PROJECT_SOURCE_DIR}/cmake/EmbeddedCircuits.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedCircuits.hpp" @ONLY)

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
target_include_directories(SiliconForge_mcu PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}")

# Components and circuits described in JSON, checked against the component library.
add_library(SiliconForge_circuit STATIC
    circuit/Description.cpp)
add_library(SiliconForge::circuit ALIAS SiliconForge_circuit)
target_link_libraries(SiliconForge_circuit PUBLIC SiliconForge::core nlohmann_json::nlohmann_json PRIVATE SiliconForge_options nlohmann_json_schema_validator)
target_include_directories(SiliconForge_circuit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}")
