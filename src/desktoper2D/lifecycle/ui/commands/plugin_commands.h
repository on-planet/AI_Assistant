#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

namespace desktoper2D {

enum class PluginActionFeedbackSlot {
    None = 0,
    Switch,
    AsrSwitch,
    OcrSwitch,
    OverrideApply,
    UnifiedSwitch,
    UnifiedCreate,
};

enum class PluginActionType {
    RefreshPluginConfigs,
    SwitchPluginByName,
    DeletePluginConfig,
    RefreshAsrProviders,
    SwitchAsrProviderByName,
    RefreshOcrModels,
    SwitchOcrModelByName,
    RefreshUnifiedPlugins,
    ApplyOverrideModels,
    ReplaceUnifiedPluginAssets,
    CreateUserPlugin,
};

struct PluginAction {
    PluginActionType type = PluginActionType::RefreshPluginConfigs;
    PluginActionFeedbackSlot feedback_slot = PluginActionFeedbackSlot::None;
    bool bool_value = false;
    std::string text_value;
    std::string text_value_2;
    std::string text_value_3;
    std::string text_value_4;
    std::vector<std::string> text_list_value;
};

void ApplyPluginAction(AppRuntime &runtime, const PluginAction &action);
void ApplyPluginAction(const UiCommandBridge &bridge, const PluginAction &action);

}  // namespace desktoper2D
