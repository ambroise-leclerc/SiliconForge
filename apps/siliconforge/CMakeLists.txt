add_executable(siliconforge main.cpp)
target_link_libraries(siliconforge PRIVATE SiliconForge::mcu SiliconForge_options WebFront::WebFront)
target_compile_definitions(siliconforge PRIVATE SILICONFORGE_UI_DIR="${PROJECT_SOURCE_DIR}/ui")

get_target_property(_webfront_definitions WebFront::WebFront INTERFACE_COMPILE_DEFINITIONS)
if(_webfront_definitions MATCHES "WEBFRONT_EMBED_(WEBKITGTK|WEBVIEW2|WKWEBVIEW)")
    message(STATUS "siliconforge: native window (${CMAKE_MATCH_1})")
else()
    message(STATUS "siliconforge: no native window engine found, the interface opens in the system browser. "
                   "On Linux, install libwebkitgtk-6.0-dev (Ubuntu/Debian) or webkitgtk6.0-devel (Fedora) and run cmake again.")
endif()
