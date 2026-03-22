#include "desktoper2D/lifecycle/ui/commands/plugin_commands.h"

#include <array>

namespace desktoper2D {

void ApplyPluginAction(const UiCommandBridge &bridge, const PluginAction &action) {
    struct PluginCommandMapping {
        PluginActionType action_type;
        UiCommandType command_type;
    };

    static const std::array<PluginCommandMapping, 11> kPluginCommandMappings = {{
        {PluginActionType::RefreshPluginConfigs, UiCommandType::RefreshPluginConfigs},
        {PluginActionType::SwitchPluginByName, UiCommandType::SwitchPluginByName},
        {PluginActionType::DeletePluginConfig, UiCommandType::DeletePluginConfig},
        {PluginActionType::RefreshAsrProviders, UiCommandType::RefreshAsrProviders},
        {PluginActionType::SwitchAsrProviderByName, UiCommandType::SwitchAsrProviderByName},
        {PluginActionType::RefreshOcrModels, UiCommandType::RefreshOcrModels},
        {PluginActionType::SwitchOcrModelByName, UiCommandType::SwitchOcrModelByName},
        {PluginActionType::RefreshUnifiedPlugins, UiCommandType::RefreshUnifiedPlugins},
        {PluginActionType::ApplyOverrideModels, UiCommandType::ApplyOverrideModels},
        {PluginActionType::ReplaceUnifiedPluginAssets, UiCommandType::ReplaceUnifiedPluginAssets},
        {PluginActionType::CreateUserPlugin, UiCommandType::CreateUserPlugin},
    }};

    for (const auto &mapping : kPluginCommandMappings) {
        if (mapping.action_type != action.type) {
            continue;
        }
        UiCommand cmd{};
        cmd.type = mapping.command_type;
        cmd.int_value = static_cast<int>(action.feedback_slot);
        cmd.bool_value = action.bool_value;
        cmd.text_value = action.text_value;
        cmd.text_value_2 = action.text_value_2;
        cmd.text_value_3 = action.text_value_3;
        cmd.text_value_4 = action.text_value_4;
        cmd.text_list_value = action.text_list_value;
        bridge.Enqueue(cmd);
        return;
    }
}

void ApplyPluginAction(AppRuntime &runtime, const PluginAction &action) {
    ApplyPluginAction(BuildUiCommandBridge(runtime), action);
}

}  // namespace desktoper2D
