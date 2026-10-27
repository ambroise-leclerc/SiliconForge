add_executable(siliconforge main.cpp)
target_link_libraries(siliconforge PRIVATE SiliconForge::mcu SiliconForge_options WebFront::WebFront)
target_compile_definitions(siliconforge PRIVATE SILICONFORGE_UI_DIR="${PROJECT_SOURCE_DIR}/ui")
