# The bench without a window (ADR 0010): what an agent runs from the project. It needs neither WebFront nor a display.
add_executable(siliconforge-bench main.cpp)
target_link_libraries(siliconforge-bench PRIVATE SiliconForge::scenario SiliconForge_options)
