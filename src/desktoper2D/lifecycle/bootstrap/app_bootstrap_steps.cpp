#include "desktoper2D/lifecycle/bootstrap/app_bootstrap_steps.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_tray.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

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
#include "desktoper2D/lifecycle/runtime_config_applier.h"
#include "desktoper2D/lifecycle/chat/cloud_chat_provider.h"
#include "desktoper2D/lifecycle/chat/hybrid_chat_provider.h"
#include "desktoper2D/lifecycle/chat/offline_chat_provider.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace desktoper2D {

namespace {

void SyncAnimationChannelState() {
    if (!g_runtime.model_loaded) {
        return;
    }
    g_runtime.model.animation_channels_enabled = !g_runtime.manual_param_mode;
}

void SetClickThrough(bool enabled) {
    g_runtime.click_through = enabled;
    if (g_runtime.entry_click_through) {
        SDL_SetTrayEntryChecked(g_runtime.entry_click_through, enabled);
    }
    ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, enabled);
}

void ToggleWindowVisibility() {
    desktoper2D::ToggleWindowVisibility(g_runtime.window, &g_runtime.window_visible);
    desktoper2D::UpdateWindowVisibilityLabel(g_runtime.entry_show_hide, g_runtime.window_visible);
}

void SDLCALL TrayToggleClickThrough(void *, SDL_TrayEntry *) { SetClickThrough(!g_runtime.click_through); }
void SDLCALL TrayToggleVisibility(void *, SDL_TrayEntry *) { ToggleWindowVisibility(); }
void SDLCALL TrayQuit(void *, SDL_TrayEntry *) { g_runtime.running = false; }

SDL_HitTestResult SDLCALL WindowHitTest(SDL_Window *, const SDL_Point *area, void *) {
    return desktoper2D::WindowHitTest(g_runtime.click_through, g_runtime.edit_mode, g_runtime.interactive_rect, area);
}

void SDLCALL MicAudioInputCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int) {
    auto *runtime = static_cast<AppRuntime *>(userdata);
    if (!runtime || additional_amount <= 0) return;

    std::vector<float> tmp(static_cast<std::size_t>(additional_amount / static_cast<int>(sizeof(float))));
    const int got = SDL_GetAudioStreamData(stream, tmp.data(), additional_amount);
    if (got <= 0) return;

    const int n = got / static_cast<int>(sizeof(float));
    std::lock_guard<std::mutex> lk(runtime->mic_mutex);
    for (int i = 0; i < n; ++i) runtime->mic_pcm_queue.push_back(tmp[static_cast<std::size_t>(i)]);

    constexpr std::size_t kMaxQueueSamples = 16000 * 20;
    while (runtime->mic_pcm_queue.size() > kMaxQueueSamples) runtime->mic_pcm_queue.pop_front();
}

}  // namespace

bool AppLifecycleInitImpl(AppLifecycleContext &ctx) {
    (void)ctx.argc;
    (void)ctx.argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        ctx.exit_code = 1;
        return false;
    }

    const AppRuntimeConfig runtime_cfg = LoadRuntimeConfig();
    g_runtime.window = SDL_CreateWindow("Overlay", runtime_cfg.window_width, runtime_cfg.window_height, SDL_WINDOW_RESIZABLE);
    if (!g_runtime.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        ctx.exit_code = 1;
        return false;
    }

    g_runtime.renderer = SDL_CreateRenderer(g_runtime.window, nullptr);
    if (!g_runtime.renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_runtime.window);
        g_runtime.window = nullptr;
        SDL_Quit();
        ctx.exit_code = 1;
        return false;
    }

    SDL_SetWindowOpacity(g_runtime.window, 1.0f);

    ApplyRuntimeConfig(g_runtime, runtime_cfg);
    const bool has_saved_manual_layout = !g_runtime.workspace_manual_docking_ini.empty();
    g_runtime.workspace_manual_layout_pending_load =
        g_runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual && has_saved_manual_layout;
    g_runtime.workspace_preset_apply_requested = g_runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset;
    g_runtime.workspace_dock_rebuild_requested =
        g_runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset ||
        (g_runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual && !has_saved_manual_layout);
    g_runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);

    SDL_GetWindowSize(g_runtime.window, &g_runtime.window_w, &g_runtime.window_h);
    g_runtime.interactive_rect = ComputeInteractiveRect(g_runtime.window_w, g_runtime.window_h);
    ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, g_runtime.click_through);
    SDL_SetWindowHitTest(g_runtime.window, WindowHitTest, nullptr);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

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
        "C:/Windows/Fonts/msyh.ttc",   // 微软雅黑（优先，含中英文）
        "C:/Windows/Fonts/simhei.ttf", // 黑体
        "C:/Windows/Fonts/simsun.ttc", // 宋体
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
    style.DisabledAlpha = 0.72f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.70f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.96f);

    ImGui_ImplSDL3_InitForSDLRenderer(g_runtime.window, g_runtime.renderer);
    ImGui_ImplSDLRenderer3_Init(g_runtime.renderer);

    SDL_AudioSpec desired{};
    desired.format = SDL_AUDIO_F32;
    desired.channels = 1;
    desired.freq = 16000;
    g_runtime.mic_device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired);
    if (g_runtime.mic_device_id != 0) {
        SDL_AudioSpec obtained{};
        int sample_frames = 0;
        if (SDL_GetAudioDeviceFormat(g_runtime.mic_device_id, &obtained, &sample_frames)) g_runtime.mic_obtained_spec = obtained;

        SDL_AudioStream *mic_stream = SDL_CreateAudioStream(&desired, &desired);
        if (mic_stream && SDL_BindAudioStream(g_runtime.mic_device_id, mic_stream)) {
            SDL_SetAudioStreamGetCallback(mic_stream, MicAudioInputCallback, &g_runtime);
            SDL_ResumeAudioDevice(g_runtime.mic_device_id);
        } else {
            if (mic_stream) SDL_DestroyAudioStream(mic_stream);
            SDL_CloseAudioDevice(g_runtime.mic_device_id);
            g_runtime.mic_device_id = 0;
        }
    }

    ctx.exit_code = 0;
    return true;
}

bool AppLifecycleBootstrapImpl(AppLifecycleContext &ctx) {
    AppBootstrapResult bootstrap = BootstrapModelAndResources(g_runtime.renderer);
    g_runtime.model_loaded = bootstrap.model_loaded;

    if (g_runtime.model_loaded) {
        g_runtime.model = bootstrap.model;
        g_runtime.selected_param_index = 0;
        SyncAnimationChannelState();

        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(g_runtime.model.model_path, ec);
        g_runtime.model_last_write_time_valid = !ec;
        if (!ec) g_runtime.model_last_write_time = write_time;
        CommitStableModelBackup(g_runtime.model);
    }

    g_runtime.inference_adapter.reset();
    g_runtime.plugin_ready = false;
    g_runtime.plugin_last_error.clear();
    ClearRuntimeError(g_runtime.plugin_error_info);

    {
        std::string reminder_err;
        for (const auto &db_path : {std::string("assets/reminders.db"), std::string("../assets/reminders.db"), std::string("../../assets/reminders.db")}) {
            std::string try_err;
            if (g_runtime.reminder_service.Init(db_path, &try_err)) {
                g_runtime.reminder_ready = true;
                g_runtime.reminder_last_error.clear();
                break;
            }
            reminder_err = try_err;
        }
        if (!g_runtime.reminder_ready) {
            g_runtime.reminder_last_error = reminder_err;
            UpdateRuntimeError(g_runtime.reminder_error_info,
                               RuntimeErrorDomain::Reminder,
                               RuntimeErrorCode::InitFailed,
                               reminder_err.empty() ? std::string("reminder init failed") : reminder_err);
        } else {
            g_runtime.reminder_upcoming = g_runtime.reminder_service.ListActive(32, nullptr);
            ClearRuntimeError(g_runtime.reminder_error_info);
        }
    }

    {
        std::string perception_err;
        const auto scene_model_candidates = ResourceLocator::BuildCandidatePairs(
            "assets/mobileclip_image.onnx",
            "assets/mobileclip_labels.json");
        auto ocr_candidates = ResourceLocator::BuildCandidateTriples(
            "assets/PP-OCRv5_server_det_infer.onnx",
            "assets/PP-OCRv5_server_rec_infer.onnx",
            "assets/ocr/ppocr_keys.txt");
        const auto ocr_candidates_compat = ResourceLocator::BuildCandidateTriples(
            "assets/PP-OCRv5_server_det_infer.onnx",
            "assets/PP-OCRv5_server_rec_infer.onnx",
            "assets/ppocr_keys.txt");
        ocr_candidates.insert(ocr_candidates.end(),
                              ocr_candidates_compat.begin(),
                              ocr_candidates_compat.end());
        const auto facemesh_candidates = ResourceLocator::BuildCandidatePairs(
            "assets/facemesh.onnx",
            "assets/facemesh.labels.json");
        const bool ok = g_runtime.perception_pipeline.Init(g_runtime.perception_state,
                                            scene_model_candidates,
                                            ocr_candidates,
                                            facemesh_candidates,
                                            &perception_err);
        if (!ok) {
            const std::string msg = perception_err.empty() ? "perception init failed" : perception_err;
            const int code = static_cast<int>(ClassifyRuntimeErrorCodeFromDetail(msg, RuntimeErrorCode::InitFailed));
            AppendPluginLog(g_runtime, "scene:builtin", PluginLogLevel::Error, msg, code);
            AppendPluginLog(g_runtime, "facemesh:builtin", PluginLogLevel::Error, msg, code);
            AppendPluginLog(g_runtime, "ocr:bootstrap", PluginLogLevel::Error, msg, code);
        }
    }

    {
        std::unique_ptr<IAsrProvider> offline = std::make_unique<OfflineAsrProvider>("assets/sense-voice-encoder-int8.onnx");
        std::unique_ptr<IAsrProvider> cloud = std::make_unique<CloudAsrProvider>("https://api.openai.com/v1/audio/transcriptions", "YOUR_API_KEY");
        HybridAsrConfig asr_cfg{};
        asr_cfg.cloud_fallback_enabled = false;
        g_runtime.asr_provider = std::make_unique<HybridAsrProvider>(std::move(offline), std::move(cloud), asr_cfg);

        std::string asr_err;
        g_runtime.asr_ready = g_runtime.asr_provider->Init(&asr_err);
        if (!g_runtime.asr_ready) {
            g_runtime.asr_last_error = asr_err;
            const std::string msg = asr_err.empty() ? std::string("asr init failed") : asr_err;
            UpdateRuntimeError(g_runtime.asr_error_info,
                               RuntimeErrorDomain::Asr,
                               RuntimeErrorCode::InitFailed,
                               msg);
            const int code = static_cast<int>(ClassifyRuntimeErrorCodeFromDetail(msg, RuntimeErrorCode::InitFailed));
            AppendPluginLog(g_runtime, "asr:bootstrap", PluginLogLevel::Error, msg, code);
        } else {
            g_runtime.asr_last_error.clear();
            ClearRuntimeError(g_runtime.asr_error_info);
        }
    }

    {
        std::unique_ptr<IChatProvider> offline = std::make_unique<OfflineChatProvider>("assets/llm/qwen2.5-1.5b-instruct.gguf");
        std::unique_ptr<IChatProvider> cloud = std::make_unique<CloudChatProvider>(
            "https://api.openai.com/v1/chat/completions",
            "YOUR_API_KEY",
            "gpt-4o-mini");
        HybridChatConfig chat_cfg{};
        chat_cfg.prefer_cloud = g_runtime.prefer_cloud_chat;
        chat_cfg.cloud_fallback_enabled = true;
        g_runtime.chat_provider = std::make_unique<HybridChatProvider>(std::move(offline), std::move(cloud), chat_cfg);

        std::string chat_err;
        g_runtime.chat_ready = g_runtime.chat_provider->Init(&chat_err);
        if (!g_runtime.chat_ready) {
            g_runtime.chat_last_error = chat_err;
            UpdateRuntimeError(g_runtime.chat_error_info,
                               RuntimeErrorDomain::Chat,
                               RuntimeErrorCode::InitFailed,
                               chat_err.empty() ? std::string("chat init failed") : chat_err);
        } else {
            g_runtime.chat_last_error.clear();
            ClearRuntimeError(g_runtime.chat_error_info);
        }
    }

    g_runtime.demo_texture = bootstrap.demo_texture;
    g_runtime.demo_texture_w = bootstrap.demo_texture_w;
    g_runtime.demo_texture_h = bootstrap.demo_texture_h;

    SDL_Surface *tray_icon = CreateTrayIconSurface();
    g_runtime.tray = SDL_CreateTray(tray_icon, "SDL Overlay");
    if (tray_icon) SDL_DestroySurface(tray_icon);

    if (g_runtime.tray) {
        SDL_TrayMenu *menu = SDL_CreateTrayMenu(g_runtime.tray);
        if (menu) {
            g_runtime.entry_click_through = SDL_InsertTrayEntryAt(menu, -1, "Click-Through", SDL_TRAYENTRY_CHECKBOX);
            if (g_runtime.entry_click_through) {
                SDL_SetTrayEntryChecked(g_runtime.entry_click_through, g_runtime.click_through);
                SDL_SetTrayEntryCallback(g_runtime.entry_click_through, TrayToggleClickThrough, nullptr);
            }

            g_runtime.entry_show_hide = SDL_InsertTrayEntryAt(menu, -1, g_runtime.window_visible ? "Hide Window" : "Show Window", SDL_TRAYENTRY_BUTTON);
            if (g_runtime.entry_show_hide) SDL_SetTrayEntryCallback(g_runtime.entry_show_hide, TrayToggleVisibility, nullptr);

            SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);
            SDL_TrayEntry *entry_quit = SDL_InsertTrayEntryAt(menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);
            if (entry_quit) SDL_SetTrayEntryCallback(entry_quit, TrayQuit, nullptr);
        }
    }

    if (!g_runtime.window_visible && g_runtime.window) SDL_HideWindow(g_runtime.window);

    ctx.exit_code = 0;
    return true;
}

void AppLifecycleTeardownImpl(AppLifecycleContext &ctx) {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_runtime.reminder_service.Shutdown();
    g_runtime.reminder_ready = false;

    if (g_runtime.inference_adapter) {
        g_runtime.inference_adapter->Shutdown();
        g_runtime.inference_adapter.reset();
    }
    g_runtime.plugin_ready = false;

    g_runtime.perception_pipeline.Shutdown(g_runtime.perception_state);

    if (g_runtime.mic_device_id != 0) {
        SDL_CloseAudioDevice(g_runtime.mic_device_id);
        g_runtime.mic_device_id = 0;
    }

    if (g_runtime.asr_provider) {
        g_runtime.asr_provider->Shutdown();
        g_runtime.asr_provider.reset();
    }
    g_runtime.asr_ready = false;

    if (g_runtime.chat_provider) {
        g_runtime.chat_provider->Shutdown();
        g_runtime.chat_provider.reset();
    }
    g_runtime.chat_ready = false;

    if (g_runtime.tray) {
        SDL_DestroyTray(g_runtime.tray);
        g_runtime.tray = nullptr;
    }

    DestroyModelRuntime(&g_runtime.model);

    if (g_runtime.demo_texture) {
        SDL_DestroyTexture(g_runtime.demo_texture);
        g_runtime.demo_texture = nullptr;
    }

    if (g_runtime.renderer) {
        SDL_DestroyRenderer(g_runtime.renderer);
        g_runtime.renderer = nullptr;
    }
    if (g_runtime.window) {
        SDL_DestroyWindow(g_runtime.window);
        g_runtime.window = nullptr;
    }
    SDL_Quit();
    ctx.exit_code = 0;
}

bool AppLifecycleInit(AppLifecycleContext &ctx) { return AppLifecycleInitImpl(ctx); }
bool AppLifecycleBootstrap(AppLifecycleContext &ctx) { return AppLifecycleBootstrapImpl(ctx); }
void AppLifecycleTeardown(AppLifecycleContext &ctx) { AppLifecycleTeardownImpl(ctx); }

}  // namespace desktoper2D
