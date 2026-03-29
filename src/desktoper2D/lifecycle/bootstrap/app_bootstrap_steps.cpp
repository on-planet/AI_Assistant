#include "desktoper2D/lifecycle/bootstrap/app_bootstrap_steps.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_tray.h>

#include "imgui.h"

#include "desktoper2D/controllers/app_bootstrap.h"
#include "desktoper2D/controllers/window_controller.h"
#include "desktoper2D/lifecycle/app_lifecycle.h"
#include "desktoper2D/lifecycle/asr/cloud_asr_provider.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/asr/hybrid_asr_provider.h"
#include "desktoper2D/lifecycle/asr/offline_asr_provider.h"
#include "desktoper2D/lifecycle/inference_adapter.h"
#include "desktoper2D/lifecycle/model_reload_service.h"
#include "desktoper2D/lifecycle/resource_locator.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/state/runtime_audio_state_ops.h"
#include "desktoper2D/lifecycle/runtime_config_applier.h"
#include "desktoper2D/lifecycle/systems/app_systems.h"
#include "desktoper2D/lifecycle/chat/cloud_chat_provider.h"
#include "desktoper2D/lifecycle/chat/hybrid_chat_provider.h"
#include "desktoper2D/lifecycle/chat/offline_chat_provider.h"
#include "desktoper2D/lifecycle/ui/runtime_imgui_backend.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace desktoper2D {

namespace {

std::vector<std::tuple<std::string, std::string, std::string>> BuildOcrCandidateTriples(
    const std::vector<std::tuple<std::string, std::string, std::string>> &relative_triples) {
    std::vector<std::tuple<std::string, std::string, std::string>> out;
    for (const auto &triple : relative_triples) {
        auto candidates = ResourceLocator::BuildCandidateTriples(
            std::get<0>(triple),
            std::get<1>(triple),
            std::get<2>(triple));
        out.insert(out.end(), candidates.begin(), candidates.end());
    }
    return out;
}

AppRuntime *RuntimeFromUserdata(void *userdata) {
    return static_cast<AppRuntime *>(userdata);
}

void SyncAnimationChannelState(AppRuntime &runtime) {
    if (!runtime.model_loaded) {
        return;
    }
    runtime.model.animation_channels_enabled = !runtime.manual_param_mode;
}

void SDLCALL TrayToggleClickThrough(void *userdata, SDL_TrayEntry *) {
    auto *runtime = RuntimeFromUserdata(userdata);
    if (!runtime) return;
    desktoper2D::SetClickThrough(runtime->window_state, !runtime->window_state.click_through);
}

void SDLCALL TrayToggleVisibility(void *userdata, SDL_TrayEntry *) {
    auto *runtime = RuntimeFromUserdata(userdata);
    if (!runtime) return;
    desktoper2D::ToggleWindowVisibility(runtime->window_state);
}

void SDLCALL TrayQuit(void *userdata, SDL_TrayEntry *) {
    auto *runtime = RuntimeFromUserdata(userdata);
    if (!runtime) return;
    runtime->running = false;
}

SDL_HitTestResult SDLCALL WindowHitTest(SDL_Window *, const SDL_Point *area, void *userdata) {
    auto *runtime = RuntimeFromUserdata(userdata);
    if (!runtime) return SDL_HITTEST_NORMAL;
    return desktoper2D::WindowHitTest(runtime->window_state, runtime->edit_mode, area);
}

void SDLCALL MicAudioInputCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int) {
    auto *audio_state = static_cast<RuntimeAudioState *>(userdata);
    if (!audio_state || additional_amount <= 0) return;

    std::vector<float> tmp(static_cast<std::size_t>(additional_amount / static_cast<int>(sizeof(float))));
    const int got = SDL_GetAudioStreamData(stream, tmp.data(), additional_amount);
    if (got <= 0) return;

    const int n = got / static_cast<int>(sizeof(float));
    AppendMicPcmSamples(*audio_state, tmp.data(), static_cast<std::size_t>(n));
}

}  // namespace

bool AppLifecycleInitImpl(AppLifecycleContext &ctx) {
    const AppLifecycleInitCapabilityContext init_ctx = ctx.InitCapability();
    (void)init_ctx.argc;
    (void)init_ctx.argv;

    AppRuntime *runtime_ptr = init_ctx.RequireRuntime();
    if (!runtime_ptr) {
        SDL_Log("AppLifecycle init missing runtime");
        return false;
    }
    AppRuntime &runtime = *runtime_ptr;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        init_ctx.SetExitCode(1);
        return false;
    }

    const AppRuntimeConfig runtime_cfg = LoadRuntimeConfig();
    if (!CreateWindowAndRenderer(runtime.window_state, "Overlay", runtime_cfg.window_width, runtime_cfg.window_height)) {
        SDL_Log("CreateWindowAndRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        init_ctx.SetExitCode(1);
        return false;
    }
    SetWindowOpacity(runtime.window_state, 1.0f);

    ApplyRuntimeConfig(runtime, runtime_cfg);
    const bool has_saved_manual_layout = !runtime.workspace_ui.panels.manual_docking_ini.empty();
    runtime.workspace_ui.panels.manual_layout_pending_load =
        runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual && has_saved_manual_layout;
    runtime.workspace_ui.panels.preset_apply_requested = runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Preset;
    runtime.workspace_ui.dock_rebuild_requested =
        runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Preset ||
        (runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual && !has_saved_manual_layout);
    runtime.workspace_ui.panels.last_applied_mode = static_cast<WorkspaceMode>(-1);

    SyncWindowSize(runtime.window_state);
    RefreshInteractiveRect(runtime.window_state);
    ApplyWindowShape(runtime.window_state);
    SetWindowHitTestCallback(runtime.window_state, WindowHitTest, &runtime);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsLight();

    ImGuiIO &io = ImGui::GetIO();
#if defined(IMGUI_HAS_DOCK)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    io.IniFilename = nullptr;
    io.Fonts->Clear();

    ImFontConfig font_cfg{};
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.PixelSnapH = true;

    const char *font_candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",   // Microsoft YaHei preferred
        "C:/Windows/Fonts/simhei.ttf", // SimHei
        "C:/Windows/Fonts/simsun.ttc", // SimSun
    };

    bool font_loaded = false;
    for (const char *font_path : font_candidates) {
        if (io.Fonts->AddFontFromFileTTF(font_path,
                                         18.0f,
                                         &font_cfg,
                                         io.Fonts->GetGlyphRangesChineseFull()) != nullptr) {
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) {
        io.Fonts->AddFontDefault();
    }
    io.FontGlobalScale = 1.0f;

    ImGuiStyle &style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.45f;
    style.WindowRounding = 10.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 12.0f;
    style.TabRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    auto to_color = [](float r, float g, float b, float a = 1.0f) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    };

    const ImVec4 text_primary = to_color(28.0f, 34.0f, 40.0f);
    const ImVec4 text_muted = to_color(124.0f, 132.0f, 140.0f);
    const ImVec4 panel_bg = to_color(245.0f, 247.0f, 250.0f);
    const ImVec4 window_bg = to_color(250.0f, 251.0f, 253.0f);
    const ImVec4 border_soft = to_color(205.0f, 212.0f, 220.0f);
    const ImVec4 border_emphasis = to_color(172.0f, 182.0f, 196.0f);
    const ImVec4 accent_soft = to_color(154.0f, 178.0f, 204.0f);
    const ImVec4 accent_strong = to_color(122.0f, 150.0f, 184.0f);
    const ImVec4 accent_hover = to_color(182.0f, 202.0f, 224.0f);
    const ImVec4 accent_active = to_color(108.0f, 136.0f, 172.0f);
    const ImVec4 hover_bg = to_color(233.0f, 239.0f, 247.0f);
    const ImVec4 active_bg = to_color(220.0f, 231.0f, 244.0f);

    style.Colors[ImGuiCol_Text] = text_primary;
    style.Colors[ImGuiCol_TextDisabled] = text_muted;
    style.Colors[ImGuiCol_WindowBg] = window_bg;
    style.Colors[ImGuiCol_ChildBg] = window_bg;
    style.Colors[ImGuiCol_PopupBg] = panel_bg;
    style.Colors[ImGuiCol_Border] = border_soft;
    style.Colors[ImGuiCol_BorderShadow] = to_color(0.0f, 0.0f, 0.0f, 0.0f);

    style.Colors[ImGuiCol_FrameBg] = panel_bg;
    style.Colors[ImGuiCol_FrameBgHovered] = hover_bg;
    style.Colors[ImGuiCol_FrameBgActive] = active_bg;

    style.Colors[ImGuiCol_TitleBg] = panel_bg;
    style.Colors[ImGuiCol_TitleBgActive] = hover_bg;
    style.Colors[ImGuiCol_TitleBgCollapsed] = panel_bg;

    style.Colors[ImGuiCol_MenuBarBg] = window_bg;

    style.Colors[ImGuiCol_ScrollbarBg] = window_bg;
    style.Colors[ImGuiCol_ScrollbarGrab] = accent_soft;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = accent_hover;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = accent_active;

    style.Colors[ImGuiCol_CheckMark] = accent_strong;
    style.Colors[ImGuiCol_SliderGrab] = accent_soft;
    style.Colors[ImGuiCol_SliderGrabActive] = accent_strong;

    style.Colors[ImGuiCol_Button] = panel_bg;
    style.Colors[ImGuiCol_ButtonHovered] = hover_bg;
    style.Colors[ImGuiCol_ButtonActive] = active_bg;

    style.Colors[ImGuiCol_Header] = hover_bg;
    style.Colors[ImGuiCol_HeaderHovered] = hover_bg;
    style.Colors[ImGuiCol_HeaderActive] = active_bg;

    style.Colors[ImGuiCol_Separator] = border_soft;
    style.Colors[ImGuiCol_SeparatorHovered] = accent_soft;
    style.Colors[ImGuiCol_SeparatorActive] = accent_strong;

    style.Colors[ImGuiCol_ResizeGrip] = accent_soft;
    style.Colors[ImGuiCol_ResizeGripHovered] = accent_hover;
    style.Colors[ImGuiCol_ResizeGripActive] = accent_active;

    style.Colors[ImGuiCol_Tab] = panel_bg;
    style.Colors[ImGuiCol_TabHovered] = hover_bg;
    style.Colors[ImGuiCol_TabActive] = active_bg;
    style.Colors[ImGuiCol_TabUnfocused] = panel_bg;
    style.Colors[ImGuiCol_TabUnfocusedActive] = hover_bg;

    style.Colors[ImGuiCol_TableHeaderBg] = panel_bg;
    style.Colors[ImGuiCol_TableBorderStrong] = border_emphasis;
    style.Colors[ImGuiCol_TableBorderLight] = border_soft;
    style.Colors[ImGuiCol_TableRowBg] = to_color(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = panel_bg;

    style.Colors[ImGuiCol_TextSelectedBg] = accent_hover;
    style.Colors[ImGuiCol_DragDropTarget] = accent_strong;
    style.Colors[ImGuiCol_NavHighlight] = accent_soft;

    if (!InitRuntimeImGuiBackends(runtime.window_state)) {
        ImGui::DestroyContext();
        DestroyWindowAndRenderer(runtime.window_state);
        SDL_Quit();
        init_ctx.SetExitCode(1);
        return false;
    }

    SDL_AudioSpec desired{};
    desired.format = SDL_AUDIO_F32;
    desired.channels = 1;
    desired.freq = 16000;
    runtime.audio_state.mic_device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired);
    if (runtime.audio_state.mic_device_id != 0) {
        SDL_AudioSpec obtained{};
        int sample_frames = 0;
        if (SDL_GetAudioDeviceFormat(runtime.audio_state.mic_device_id, &obtained, &sample_frames)) runtime.audio_state.mic_obtained_spec = obtained;

        SDL_AudioStream *mic_stream = SDL_CreateAudioStream(&desired, &desired);
        if (mic_stream && SDL_BindAudioStream(runtime.audio_state.mic_device_id, mic_stream)) {
            SDL_SetAudioStreamGetCallback(mic_stream, MicAudioInputCallback, &runtime.audio_state);
            SDL_ResumeAudioDevice(runtime.audio_state.mic_device_id);
        } else {
            if (mic_stream) SDL_DestroyAudioStream(mic_stream);
            CloseMicAudioDevice(runtime.audio_state);
        }
    }

    init_ctx.SetExitCode(0);
    return true;
}

bool AppLifecycleBootstrapImpl(AppLifecycleContext &ctx) {
    const AppLifecyclePhaseCapabilityContext bootstrap_ctx = ctx.BootstrapCapability();
    AppRuntime *runtime_ptr = bootstrap_ctx.RequireRuntime();
    if (!runtime_ptr) {
        SDL_Log("AppLifecycle bootstrap missing runtime");
        return false;
    }
    AppRuntime &runtime = *runtime_ptr;

    AppBootstrapResult bootstrap = BootstrapModelAndResources(runtime.window_state);
    runtime.model_loaded = bootstrap.model_loaded;

    if (runtime.model_loaded) {
        runtime.model = bootstrap.model;
        runtime.selected_param_index = 0;
        SyncAnimationChannelState(runtime);

        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(runtime.model.model_path, ec);
        runtime.model_last_write_time_valid = !ec;
        if (!ec) runtime.model_last_write_time = write_time;
        CommitStableModelBackup(runtime.model);
    }

    runtime.plugin.inference_adapter.reset();
    runtime.plugin.ready = false;
    runtime.plugin.last_error.clear();
    ClearRuntimeError(runtime.plugin.error_info);

    {
        std::string reminder_err;
        for (const auto &db_path : {std::string("assets/reminders.db"), std::string("../assets/reminders.db"), std::string("../../assets/reminders.db")}) {
            std::string try_err;
            if (runtime.reminder_service.Init(db_path, &try_err)) {
                runtime.reminder_ready = true;
                runtime.reminder_last_error.clear();
                break;
            }
            reminder_err = try_err;
        }
        if (!runtime.reminder_ready) {
            runtime.reminder_last_error = reminder_err;
            UpdateRuntimeError(runtime.reminder_error_info,
                               RuntimeErrorDomain::Reminder,
                               RuntimeErrorCode::InitFailed,
                               reminder_err.empty() ? std::string("reminder init failed") : reminder_err);
        } else {
            runtime.reminder_upcoming = runtime.reminder_service.ListActive(32, nullptr);
            ClearRuntimeError(runtime.reminder_error_info);
        }
    }

    {
        std::string perception_err;
        const auto scene_model_candidates = ResourceLocator::BuildCandidatePairs(
            "assets/default_plugins/mobileclip/resources/mobileclip_image.onnx",
            "assets/default_plugins/mobileclip/resources/mobileclip_labels.json");
        const std::vector<std::tuple<std::string, std::string, std::string>> ocr_relative_paths = {
            {"assets/default_plugins/ocr/resources/PP-OCRv5_server_det_infer.onnx",
             "assets/default_plugins/ocr/resources/PP-OCRv5_server_rec_infer.onnx",
             "assets/default_plugins/ocr/resources/ocr/ppocr_keys.txt"},
            {"assets/default_plugins/ocr/resources/PP-OCRv5_server_det_infer.onnx",
             "assets/default_plugins/ocr/resources/PP-OCRv5_server_rec_infer.onnx",
             "assets/default_plugins/ocr/resources/ppocr_keys.txt"},
        };
        const auto ocr_candidates = BuildOcrCandidateTriples(ocr_relative_paths);
        const auto facemesh_candidates = ResourceLocator::BuildCandidatePairs(
            "assets/default_plugins/facemesh/resources/facemesh.onnx",
            "assets/default_plugins/facemesh/resources/facemesh.labels.json");
        const bool ok = runtime.perception_pipeline.Init(runtime.perception_state,
                                                         scene_model_candidates,
                                                         ocr_candidates,
                                                         facemesh_candidates,
                                                         &perception_err);
        if (!ok) {
            const std::string msg = perception_err.empty() ? "perception init failed" : perception_err;
            const int code = static_cast<int>(ClassifyRuntimeErrorCodeFromDetail(msg, RuntimeErrorCode::InitFailed));
            AppendPluginLog(runtime, "scene:builtin", PluginLogLevel::Error, msg, code);
            AppendPluginLog(runtime, "facemesh:builtin", PluginLogLevel::Error, msg, code);
            AppendPluginLog(runtime, "ocr:bootstrap", PluginLogLevel::Error, msg, code);
        }
    }

    {
        std::unique_ptr<IAsrProvider> offline = std::make_unique<OfflineAsrProvider>("assets/default_plugins/asr/resources/sense-voice-encoder-int8.onnx");
        std::unique_ptr<IAsrProvider> cloud = std::make_unique<CloudAsrProvider>("https://api.openai.com/v1/audio/transcriptions", "YOUR_API_KEY");
        HybridAsrConfig asr_cfg{};
        asr_cfg.cloud_fallback_enabled = false;
        runtime.asr_provider = std::make_unique<HybridAsrProvider>(std::move(offline), std::move(cloud), asr_cfg);

        std::string asr_err;
        runtime.asr_ready = runtime.asr_provider->Init(&asr_err);
        if (!runtime.asr_ready) {
            runtime.asr_last_error = asr_err;
            const std::string msg = asr_err.empty() ? std::string("asr init failed") : asr_err;
            UpdateRuntimeError(runtime.asr_error_info,
                               RuntimeErrorDomain::Asr,
                               RuntimeErrorCode::InitFailed,
                               msg);
            const int code = static_cast<int>(ClassifyRuntimeErrorCodeFromDetail(msg, RuntimeErrorCode::InitFailed));
            AppendPluginLog(runtime, "asr:bootstrap", PluginLogLevel::Error, msg, code);
        } else {
            runtime.asr_provider_generation += 1;
            runtime.asr_last_error.clear();
            ClearRuntimeError(runtime.asr_error_info);
        }
        runtime.RuntimeAsrChatState::panel_state_version += 1;
    }

    {
        std::unique_ptr<IChatProvider> offline = std::make_unique<OfflineChatProvider>("assets/llm/qwen2.5-1.5b-instruct.gguf");
        std::unique_ptr<IChatProvider> cloud = std::make_unique<CloudChatProvider>(
            "https://api.openai.com/v1/chat/completions",
            "YOUR_API_KEY",
            "gpt-4o-mini");
        HybridChatConfig chat_cfg{};
        chat_cfg.prefer_cloud = runtime.prefer_cloud_chat;
        chat_cfg.cloud_fallback_enabled = true;
        runtime.chat_provider = std::make_unique<HybridChatProvider>(std::move(offline), std::move(cloud), chat_cfg);

        std::string chat_err;
        runtime.chat_ready = runtime.chat_provider->Init(&chat_err);
        if (!runtime.chat_ready) {
            runtime.chat_last_error = chat_err;
            UpdateRuntimeError(runtime.chat_error_info,
                               RuntimeErrorDomain::Chat,
                               RuntimeErrorCode::InitFailed,
                               chat_err.empty() ? std::string("chat init failed") : chat_err);
        } else {
            runtime.chat_last_error.clear();
            ClearRuntimeError(runtime.chat_error_info);
        }
        runtime.RuntimeAsrChatState::panel_state_version += 1;
    }

    SetDemoTexture(runtime.window_state,
                   bootstrap.demo_texture,
                   bootstrap.demo_texture_w,
                   bootstrap.demo_texture_h);

    SDL_Surface *tray_icon = CreateTrayIconSurface();
    runtime.window_state.tray = SDL_CreateTray(tray_icon, "SDL Overlay");
    if (tray_icon) SDL_DestroySurface(tray_icon);

    if (runtime.window_state.tray) {
        SDL_TrayMenu *menu = SDL_CreateTrayMenu(runtime.window_state.tray);
        if (menu) {
            runtime.window_state.entry_click_through = SDL_InsertTrayEntryAt(menu, -1, "Click-Through", SDL_TRAYENTRY_CHECKBOX);
            if (runtime.window_state.entry_click_through) {
                SDL_SetTrayEntryChecked(runtime.window_state.entry_click_through, runtime.window_state.click_through);
                SDL_SetTrayEntryCallback(runtime.window_state.entry_click_through, TrayToggleClickThrough, &runtime);
            }

            runtime.window_state.entry_show_hide = SDL_InsertTrayEntryAt(
                menu,
                -1,
                runtime.window_state.window_visible ? "Hide Window" : "Show Window",
                SDL_TRAYENTRY_BUTTON);
            if (runtime.window_state.entry_show_hide) {
                SDL_SetTrayEntryCallback(runtime.window_state.entry_show_hide, TrayToggleVisibility, &runtime);
            }

            SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);
            SDL_TrayEntry *entry_quit = SDL_InsertTrayEntryAt(menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);
            if (entry_quit) SDL_SetTrayEntryCallback(entry_quit, TrayQuit, &runtime);
        }
    }

    ApplyWindowVisibility(runtime.window_state);

    bootstrap_ctx.SetExitCode(0);
    return true;
}

void AppLifecycleTeardownImpl(AppLifecycleContext &ctx) {
    const AppLifecyclePhaseCapabilityContext teardown_ctx = ctx.TeardownCapability();
    AppRuntime *runtime_ptr = teardown_ctx.RequireRuntime();
    if (!runtime_ptr) {
        SDL_Log("AppLifecycle teardown missing runtime");
        return;
    }
    AppRuntime &runtime = *runtime_ptr;

    ShutdownRuntimeImGuiBackends();
    ImGui::DestroyContext();

    runtime.reminder_service.Shutdown();
    runtime.reminder_ready = false;

    if (runtime.plugin.inference_adapter) {
        runtime.plugin.inference_adapter->Shutdown();
        runtime.plugin.inference_adapter.reset();
    }
    runtime.plugin.ready = false;

    runtime.perception_pipeline.Shutdown(runtime.perception_state);

    CloseMicAudioDevice(runtime.audio_state);
    ShutdownAppSystems(BuildOpsStateSlice(runtime));

    if (runtime.asr_provider) {
        runtime.asr_provider->Shutdown();
        runtime.asr_provider.reset();
    }
    runtime.asr_provider_generation = 0;
    runtime.asr_ready = false;
    runtime.RuntimeAsrChatState::panel_state_version += 1;

    if (runtime.chat_provider) {
        runtime.chat_provider->Shutdown();
        runtime.chat_provider.reset();
    }
    runtime.chat_ready = false;
    runtime.RuntimeAsrChatState::panel_state_version += 1;

    if (runtime.window_state.tray) {
        SDL_DestroyTray(runtime.window_state.tray);
        runtime.window_state.tray = nullptr;
    }

    DestroyModelRuntime(&runtime.model);

    DestroyDemoTexture(runtime.window_state);
    DestroyWindowAndRenderer(runtime.window_state);
    SDL_Quit();
    teardown_ctx.SetExitCode(0);
}

bool AppLifecycleInit(AppLifecycleContext &ctx) { return AppLifecycleInitImpl(ctx); }
bool AppLifecycleBootstrap(AppLifecycleContext &ctx) { return AppLifecycleBootstrapImpl(ctx); }
void AppLifecycleTeardown(AppLifecycleContext &ctx) { AppLifecycleTeardownImpl(ctx); }

}  // namespace desktoper2D
