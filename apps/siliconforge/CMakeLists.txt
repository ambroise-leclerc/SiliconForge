# The example firmware (firmware/prebuilt/*.hex) is embedded, so that the interface offers ready-made demonstrations
# without a toolchain or a file to pick.
file(GLOB _firmware_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/firmware/prebuilt/*.hex")
list(SORT _firmware_files)
set(SILICONFORGE_FIRMWARE_ARRAYS "")
set(SILICONFORGE_FIRMWARE_VIEWS "")
foreach(_firmware_file IN LISTS _firmware_files)
    get_filename_component(_firmware_name "${_firmware_file}" NAME)
    string(MAKE_C_IDENTIFIER "${_firmware_name}" _firmware_id)
    siliconforge_embed_bytes("${_firmware_file}" _firmware_bytes)
    string(APPEND SILICONFORGE_FIRMWARE_ARRAYS
        "inline constexpr unsigned char firmware_${_firmware_id}[] = {\n${_firmware_bytes}\n};\n")
    string(APPEND SILICONFORGE_FIRMWARE_VIEWS "    {\"${_firmware_name}\", view(firmware_${_firmware_id})},\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_firmware_files})
# Their C sources (firmware/src/*.c) too: loading an example copies its source into a folder of the working directory, for the
# editor and the agent to work on.
file(GLOB _source_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/firmware/src/*.c")
list(SORT _source_files)
set(SILICONFORGE_SOURCE_ARRAYS "")
set(SILICONFORGE_SOURCE_VIEWS "")
foreach(_source_file IN LISTS _source_files)
    get_filename_component(_source_name "${_source_file}" NAME)
    string(MAKE_C_IDENTIFIER "${_source_name}" _source_id)
    siliconforge_embed_bytes("${_source_file}" _source_bytes)
    string(APPEND SILICONFORGE_SOURCE_ARRAYS
        "inline constexpr unsigned char source_${_source_id}[] = {\n${_source_bytes}\n};\n")
    string(APPEND SILICONFORGE_SOURCE_VIEWS "    {\"${_source_name}\", view(source_${_source_id})},\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_source_files})
configure_file("${PROJECT_SOURCE_DIR}/cmake/EmbeddedFirmware.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFirmware.hpp" @ONLY)

# What the application and the tests of its bridge are built from.
add_library(SiliconForge_app INTERFACE)
add_library(SiliconForge::app ALIAS SiliconForge_app)
target_include_directories(SiliconForge_app INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(SiliconForge_app INTERFACE SiliconForge::mcu SiliconForge::circuit SiliconForge::scenario WebFront::WebFront)
target_compile_definitions(SiliconForge_app INTERFACE SILICONFORGE_UI_DIR="${PROJECT_SOURCE_DIR}/ui")
if(WIN32)
    # `siliconforge bench` reaches the window of its terminal over the loopback interface (LocalHttp.hpp).
    target_link_libraries(SiliconForge_app INTERFACE ws2_32)
endif()

add_executable(siliconforge main.cpp)
target_link_libraries(siliconforge PRIVATE SiliconForge::app SiliconForge_options)

get_target_property(_webfront_definitions WebFront::WebFront INTERFACE_COMPILE_DEFINITIONS)
if(_webfront_definitions MATCHES "WEBFRONT_EMBED_(WEBKITGTK|WEBVIEW2|WKWEBVIEW|CEF)")
    message(STATUS "siliconforge: native window (${CMAKE_MATCH_1})")
else()
    message(STATUS "siliconforge: no native window engine found, the interface opens in the system browser. "
                   "On Linux, install libwebkitgtk-6.0-dev (Ubuntu/Debian) or webkitgtk6.0-devel (Fedora) and run cmake again.")
endif()
