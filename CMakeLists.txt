cmake_minimum_required(VERSION 3.29)
project(AI_assistant)

enable_testing()

set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(K2D_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Ensure MSVC parses source files as UTF-8 to avoid non-ASCII build issues.
if (MSVC)
    add_compile_options(/utf-8)
endif()

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

# OpenCV (prebuilt package under external/opencv)
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED)
set(OPENCV_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/build/x64/vc16/bin")

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
    src/k2d/lifecycle/scene_classifier.cpp
    src/k2d/lifecycle/ocr_service.cpp
    src/k2d/lifecycle/system_context_service.cpp
    src/k2d/lifecycle/perception_pipeline.cpp
    src/k2d/lifecycle/runtime_config_applier.cpp
    src/k2d/lifecycle/legacy_editor_ui_tools.cpp
    src/k2d/lifecycle/chat/offline_chat_provider.cpp
    src/k2d/lifecycle/chat/cloud_chat_provider.cpp
    src/k2d/lifecycle/chat/hybrid_chat_provider.cpp
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
    src/k2d/editor/editor_input_binding.cpp
    src/k2d/rendering/app_renderer.cpp
    src/k2d/controllers/app_bootstrap.cpp
    src/k2d/controllers/window_controller.cpp
    src/k2d/controllers/param_controller.cpp
    src/k2d/controllers/app_loop.cpp
    src/k2d/controllers/app_input_controller.cpp
    src/k2d/controllers/interaction_controller.cpp
    src/k2d/capture/screen_capture_dxgi.cpp
    src/k2d/lifecycle/camera_facemesh_service.cpp
    src/k2d/lifecycle/utils/app_text_utils.cpp
    src/k2d/lifecycle/services/task_category_service.cpp
    src/k2d/lifecycle/asr/offline_asr_provider.cpp
    src/k2d/lifecycle/asr/cloud_asr_provider.cpp
    src/k2d/lifecycle/asr/hybrid_asr_provider.cpp
    src/k2d/lifecycle/asr/vad_segmenter.cpp
    src/k2d/lifecycle/asr/asr_session_service.cpp
    src/k2d/lifecycle/services/decision_service.cpp
    src/k2d/lifecycle/state/app_runtime_state.cpp
    src/k2d/lifecycle/systems/app_systems.cpp
    src/k2d/lifecycle/ui/app_debug_ui.cpp
    src/k2d/lifecycle/ui/reminder_panel.cpp
    src/k2d/lifecycle/orchestrator/app_orchestrator.cpp
    src/k2d/lifecycle/bootstrap/app_bootstrap_steps.cpp
    src/k2d/lifecycle/events/app_event_handler.cpp
    src/k2d/lifecycle/systems/runtime_tick_entry.cpp
    src/k2d/lifecycle/ui/runtime_render_entry.cpp
    src/k2d/lifecycle/editor/editor_interaction_facade.cpp
    src/k2d/lifecycle/editor/editor_session_service.cpp
    src/k2d/lifecycle/bridge_factory.cpp
    src/k2d/lifecycle/state/runtime_state_ops.cpp
)

target_link_libraries(AI_assistant PRIVATE SDL3::SDL3 "${ONNXRUNTIME_LIB}" ${OpenCV_LIBS} d3d11 dxgi windowsapp runtimeobject)

# Allow includes like "k2d/core/png_texture.h" and "stb_image.h".
target_include_directories(AI_assistant PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
    ${ONNXRUNTIME_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

# Copy onnxruntime.dll next to executable for local run/package.
if (EXISTS "${ONNXRUNTIME_DLL}")
    add_custom_command(TARGET AI_assistant POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:AI_assistant>/onnxruntime.dll")
endif()

# Copy OpenCV runtime DLLs next to executable for local run/package.
if (EXISTS "${OPENCV_BIN_DIR}")
    add_custom_command(TARGET AI_assistant POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${OPENCV_BIN_DIR}"
                "$<TARGET_FILE_DIR:AI_assistant>")
endif()

add_executable(plugin_lifecycle_tests
    tests/plugin_lifecycle_tests.cpp
    src/k2d/core/async_logger.cpp
    src/k2d/lifecycle/plugin_lifecycle.cpp
    src/k2d/lifecycle/inference_adapter.cpp
)

target_link_libraries(plugin_lifecycle_tests PRIVATE SDL3::SDL3 "${ONNXRUNTIME_LIB}" ${OpenCV_LIBS} d3d11 dxgi windowsapp runtimeobject)

target_include_directories(plugin_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
    ${ONNXRUNTIME_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

if (EXISTS "${ONNXRUNTIME_DLL}")
    add_custom_command(TARGET plugin_lifecycle_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:plugin_lifecycle_tests>/onnxruntime.dll")
endif()

if (EXISTS "${OPENCV_BIN_DIR}")
    add_custom_command(TARGET plugin_lifecycle_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${OPENCV_BIN_DIR}"
                "$<TARGET_FILE_DIR:plugin_lifecycle_tests>")
endif()

add_test(NAME plugin_lifecycle_tests COMMAND plugin_lifecycle_tests)

add_executable(lifecycle_decoupling_tests
    tests/lifecycle_decoupling_tests.cpp
    src/k2d/lifecycle/asr/asr_session_service.cpp
    src/k2d/lifecycle/services/decision_service.cpp
    src/k2d/lifecycle/services/task_category_service.cpp
)

target_include_directories(lifecycle_decoupling_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

add_test(NAME lifecycle_decoupling_tests COMMAND lifecycle_decoupling_tests)


