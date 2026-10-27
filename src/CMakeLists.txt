# Architecture-independent foundation: scheduler, pins, serial ports, debugger, firmware loaders.
add_library(SiliconForge_core STATIC
    core/Loader.cpp)
add_library(SiliconForge::core ALIAS SiliconForge_core)
target_include_directories(SiliconForge_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_compile_features(SiliconForge_core PUBLIC cxx_std_26)
target_link_libraries(SiliconForge_core PRIVATE SiliconForge_options)

# AVR family: core and peripherals shared by the megaAVR devices.
add_library(SiliconForge_avr STATIC
    arch/avr/AvrCore.cpp
    arch/avr/Instructions.cpp
    arch/avr/peripherals/Gpio.cpp
    arch/avr/peripherals/Timer8.cpp
    arch/avr/peripherals/Usart.cpp)
add_library(SiliconForge::avr ALIAS SiliconForge_avr)
target_link_libraries(SiliconForge_avr PUBLIC SiliconForge::core PRIVATE SiliconForge_options)

# Concrete microcontrollers and the registry that creates them by name.
add_library(SiliconForge_mcu STATIC
    mcu/Atmega328p.cpp
    mcu/Registry.cpp)
add_library(SiliconForge::mcu ALIAS SiliconForge_mcu)
target_link_libraries(SiliconForge_mcu PUBLIC SiliconForge::avr PRIVATE SiliconForge_options)
