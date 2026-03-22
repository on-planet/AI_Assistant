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
    src/desktoper2D/core/json.cpp
    src/desktoper2D/core/png_texture.cpp
    src/desktoper2D/core/async_logger.cpp
    external/nativefiledialog-extended-master/src/nfd_win.cpp
    src/desktoper2D/lifecycle/app.cpp
    src/desktoper2D/lifecycle/app_lifecycle.cpp
    src/desktoper2D/lifecycle/bridge_assembler.cpp
    src/desktoper2D/lifecycle/plugin_lifecycle.cpp
    src/desktoper2D/lifecycle/plugin_default_behavior.cpp
    src/desktoper2D/lifecycle/plugin_config_validator.cpp
    src/desktoper2D/lifecycle/plugin_postprocess.cpp
    src/desktoper2D/lifecycle/plugin_route_decision.cpp
    src/desktoper2D/lifecycle/plugins/onnx_behavior_plugin.cpp
    src/desktoper2D/lifecycle/inference_adapter.cpp
    src/desktoper2D/lifecycle/resource_locator.cpp
    src/desktoper2D/lifecycle/behavior_applier.cpp
    src/desktoper2D/lifecycle/model_reload_service.cpp
    src/desktoper2D/lifecycle/reminder_service.cpp
    src/desktoper2D/lifecycle/scene_classifier.cpp
    src/desktoper2D/lifecycle/ocr_service.cpp
    src/desktoper2D/lifecycle/system_context_service.cpp
    src/desktoper2D/lifecycle/perception_pipeline.cpp
    src/desktoper2D/lifecycle/runtime_config_applier.cpp
    src/desktoper2D/lifecycle/legacy_editor_ui_tools.cpp
    src/desktoper2D/lifecycle/chat/offline_chat_provider.cpp
    src/desktoper2D/lifecycle/chat/cloud_chat_provider.cpp
    src/desktoper2D/lifecycle/chat/hybrid_chat_provider.cpp
    external/sqlite/sqlite3.c
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_sdl3.cpp
    external/imgui/backends/imgui_impl_sdlrenderer3.cpp
    src/desktoper2D/core/mesh_generator.cpp
    src/desktoper2D/core/params.cpp
    src/desktoper2D/core/deform.cpp
    src/desktoper2D/core/model.cpp
    src/desktoper2D/core/model_update_pipeline.cpp
    src/desktoper2D/editor/editor_commands.cpp
    src/desktoper2D/editor/editor_gizmo.cpp
    src/desktoper2D/editor/editor_input.cpp
    src/desktoper2D/editor/editor_controller.cpp
    src/desktoper2D/editor/editor_input_binding.cpp
    src/desktoper2D/rendering/app_renderer.cpp
    src/desktoper2D/controllers/app_bootstrap.cpp
    src/desktoper2D/controllers/window_controller.cpp
    src/desktoper2D/controllers/param_controller.cpp
    src/desktoper2D/controllers/app_loop.cpp
    src/desktoper2D/controllers/app_input_controller.cpp
    src/desktoper2D/controllers/interaction_controller.cpp
    src/desktoper2D/capture/screen_capture_dxgi.cpp
    src/desktoper2D/lifecycle/camera_facemesh_service.cpp
    src/desktoper2D/lifecycle/utils/app_text_utils.cpp
    src/desktoper2D/lifecycle/ui/ui_empty_state.cpp
    src/desktoper2D/lifecycle/services/task_category_features.cpp
    src/desktoper2D/lifecycle/services/task_category_scoring.cpp
    src/desktoper2D/lifecycle/services/task_category_decision.cpp
    src/desktoper2D/lifecycle/services/ocr_postprocess_service.cpp
    src/desktoper2D/lifecycle/services/plugin_runtime_service.cpp
    src/desktoper2D/lifecycle/services/plugin_runtime_service_internal.cpp
    src/desktoper2D/lifecycle/services/plugin_runtime_config_repository.cpp
    src/desktoper2D/lifecycle/services/plugin_runtime_catalog_service.cpp
    src/desktoper2D/lifecycle/services/plugin_runtime_switch_service.cpp
    src/desktoper2D/lifecycle/services/plugin_runtime_override_service.cpp
    src/desktoper2D/lifecycle/asr/offline_asr_provider.cpp
    src/desktoper2D/lifecycle/asr/cloud_asr_provider.cpp
    src/desktoper2D/lifecycle/asr/hybrid_asr_provider.cpp
    src/desktoper2D/lifecycle/asr/vad_segmenter.cpp
    src/desktoper2D/lifecycle/asr/asr_session_service.cpp
    src/desktoper2D/lifecycle/services/decision_service.cpp
    src/desktoper2D/lifecycle/systems/app_systems.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_widgets.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_presenter.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_editor_service.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_runtime_actions.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_tables.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_param_groups.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_panel_state.cpp
    src/desktoper2D/lifecycle/ui/commands/ui_command_bridge.cpp
    src/desktoper2D/lifecycle/ui/commands/workspace_commands.cpp
    src/desktoper2D/lifecycle/ui/commands/ops_commands.cpp
    src/desktoper2D/lifecycle/ui/commands/plugin_commands.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_actions.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_workspace.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_overview.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_editor.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_timeline.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_perception.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_mapping.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_asr_chat.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_error.cpp
    src/desktoper2D/lifecycle/ui/app_debug_ui_ops.cpp
    src/desktoper2D/lifecycle/ui/reminder_panel.cpp
    src/desktoper2D/lifecycle/orchestrator/app_orchestrator.cpp
    src/desktoper2D/lifecycle/bootstrap/app_bootstrap_steps.cpp
    src/desktoper2D/lifecycle/events/app_event_handler.cpp
    src/desktoper2D/lifecycle/systems/runtime_tick_entry.cpp
    src/desktoper2D/lifecycle/ui/runtime_render_entry.cpp
    src/desktoper2D/lifecycle/ui/runtime_imgui_backend.cpp
    src/desktoper2D/lifecycle/systems/runtime_command_executor.cpp
    src/desktoper2D/lifecycle/systems/workspace_reducer.cpp
    src/desktoper2D/lifecycle/editor/editor_interaction_facade.cpp
    src/desktoper2D/lifecycle/editor/editor_session_service.cpp
    src/desktoper2D/lifecycle/bridge_factory.cpp
    src/desktoper2D/lifecycle/state/runtime_state_ops.cpp
    src/desktoper2D/lifecycle/state/runtime_audio_state_ops.cpp
)

target_link_libraries(AI_assistant PRIVATE SDL3::SDL3 "${ONNXRUNTIME_LIB}" ${OpenCV_LIBS} d3d11 dxgi windowsapp runtimeobject)

# Allow includes like "desktoper2D/core/png_texture.h" and "stb_image.h".
target_include_directories(AI_assistant PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog-extended-master/src/include
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
    src/desktoper2D/core/async_logger.cpp
    src/desktoper2D/lifecycle/plugin_lifecycle.cpp
    src/desktoper2D/lifecycle/plugins/onnx_behavior_plugin.cpp
    src/desktoper2D/lifecycle/inference_adapter.cpp
    src/desktoper2D/lifecycle/resource_locator.cpp
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
    src/desktoper2D/lifecycle/asr/asr_session_service.cpp
    src/desktoper2D/lifecycle/services/decision_service.cpp
    src/desktoper2D/lifecycle/services/task_category_features.cpp
    src/desktoper2D/lifecycle/services/task_category_scoring.cpp
    src/desktoper2D/lifecycle/services/task_category_decision.cpp
)

target_include_directories(lifecycle_decoupling_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

add_test(NAME lifecycle_decoupling_tests COMMAND lifecycle_decoupling_tests)


