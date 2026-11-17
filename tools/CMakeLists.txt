# Developer tools, in C++ like the rest of the project: no Python or Node.js is needed (docs/PYTHON-STUDY.md).
# They are written to build/tools/: build/tools/atdf2json, build/tools/captures2gif.

CPMAddPackage(NAME pugixml GITHUB_REPOSITORY zeux/pugixml GIT_TAG v1.15 OPTIONS "PUGIXML_INSTALL OFF")
set_property(TARGET pugixml-static PROPERTY SYSTEM TRUE)
# stb_image decodes the PNG captures; only its header is used.
CPMAddPackage(NAME stb GITHUB_REPOSITORY nothings/stb GIT_TAG 9600522de53973336fa033a289858a4354cbb04c DOWNLOAD_ONLY YES)

# ATDF device files to MCU descriptions (docs/MCU-DESCRIPTIONS.md).
add_library(SiliconForge_atdf STATIC atdf2json/Atdf.cpp)
add_library(SiliconForge::atdf ALIAS SiliconForge_atdf)
target_include_directories(SiliconForge_atdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/atdf2json)
target_link_libraries(SiliconForge_atdf PUBLIC pugixml::static nlohmann_json::nlohmann_json PRIVATE SiliconForge_options)
add_executable(atdf2json atdf2json/main.cpp)
target_link_libraries(atdf2json PRIVATE SiliconForge::atdf SiliconForge_options)

# The animation of the README from the sequence UI-Tour records (#10).
add_library(SiliconForge_gif STATIC captures2gif/Gif.cpp)
add_library(SiliconForge::gif ALIAS SiliconForge_gif)
target_include_directories(SiliconForge_gif PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/captures2gif)
target_link_libraries(SiliconForge_gif PRIVATE SiliconForge_options)
add_executable(captures2gif captures2gif/main.cpp)
target_include_directories(captures2gif SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(captures2gif PRIVATE SiliconForge::gif SiliconForge_options)
