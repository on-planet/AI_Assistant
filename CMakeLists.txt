cmake_minimum_required(VERSION 3.31)
project(AI_assistant)

enable_testing()

set(CMAKE_CXX_STANDARD 20)

# Put runtime artifacts next to the executable for local runs.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

add_subdirectory(external/SDL-release-3.4.0 EXCLUDE_FROM_ALL)

# ONNX Runtime (prebuilt Windows x64 package under external/)
set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime-win-x64-1.24.2")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/lib")
set(ONNXRUNTIME_LIB "${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib")
set(ONNXRUNTIME_DLL "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll")

add_executable(AI_assistant WIN32
    main.cpp
    src/k2d/core/json.cpp
    src/k2d/core/png_texture.cpp
    src/k2d/core/async_logger.cpp
    src/k2d/lifecycle/app.cpp
    src/k2d/lifecycle/app_lifecycle.cpp
    src/k2d/lifecycle/plugin_lifecycle.cpp
    src/k2d/lifecycle/inference_adapter.cpp
    src/k2d/lifecycle/behavior_applier.cpp
    src/k2d/lifecycle/model_reload_service.cpp
    src/k2d/lifecycle/reminder_service.cpp
    external/sqlite/sqlite3.c
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_sdl3.cpp
    external/imgui/backends/imgui_impl_sdlrenderer3.cpp
    src/k2d/core/mesh_generator.cpp
    src/k2d/core/params.cpp
    src/k2d/core/deform.cpp
    src/k2d/core/model.cpp
    src/k2d/core/model_update_pipeline.cpp
    src/k2d/editor/editor_commands.cpp
    src/k2d/editor/editor_gizmo.cpp
    src/k2d/editor/editor_input.cpp
    src/k2d/editor/editor_controller.cpp
    src/k2d/rendering/app_renderer.cpp
    src/k2d/controllers/app_bootstrap.cpp
    src/k2d/controllers/window_controller.cpp
    src/k2d/controllers/param_controller.cpp
    src/k2d/controllers/app_loop.cpp
    src/k2d/controllers/app_input_controller.cpp
    src/k2d/controllers/interaction_controller.cpp
)

target_link_libraries(AI_assistant PRIVATE SDL3::SDL3 "${ONNXRUNTIME_LIB}")

# Allow includes like "k2d/core/png_texture.h" and "stb_image.h".
target_include_directories(AI_assistant PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
    ${ONNXRUNTIME_INCLUDE_DIR}
)

# Copy onnxruntime.dll next to executable for local run/package.
if (EXISTS "${ONNXRUNTIME_DLL}")
    add_custom_command(TARGET AI_assistant POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:AI_assistant>/onnxruntime.dll")
endif()

add_executable(plugin_lifecycle_tests
    tests/plugin_lifecycle_tests.cpp
    src/k2d/core/async_logger.cpp
    src/k2d/lifecycle/plugin_lifecycle.cpp
    src/k2d/lifecycle/inference_adapter.cpp
)

target_link_libraries(plugin_lifecycle_tests PRIVATE SDL3::SDL3 "${ONNXRUNTIME_LIB}")

target_include_directories(plugin_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
    ${ONNXRUNTIME_INCLUDE_DIR}
)

if (EXISTS "${ONNXRUNTIME_DLL}")
    add_custom_command(TARGET plugin_lifecycle_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:plugin_lifecycle_tests>/onnxruntime.dll")
endif()

add_test(NAME plugin_lifecycle_tests COMMAND plugin_lifecycle_tests)


