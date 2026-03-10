#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "desktoper2D/editor/editor_commands.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_types.h"

namespace desktoper2D {

const char *BindingTypeNameUi(BindingType t);
void UpsertBinding(ModelPart &part, int param_index, BindingType type, float in_min, float in_max, float out_min, float out_max);
std::uint64_t NextUiTimelineStableId();
void EnsureTimelineKeyframeStableIds(AnimationChannel &ch);
std::vector<int> BuildTimelineSelectedIndices(const AnimationChannel &ch,
                                              const std::vector<std::uint64_t> &selected_ids);
void NormalizeTimelineSelection(AppRuntime &runtime, AnimationChannel &ch);
bool IsTimelineKeyframeSelected(const AppRuntime &runtime, const TimelineKeyframe &kf);
void SelectTimelineKeyframeById(AppRuntime &runtime, AnimationChannel &ch, std::uint64_t stable_id, bool additive);
void ClearTimelineSelection(AppRuntime &runtime, AnimationChannel &ch);
EditCommand MakeTimelineEditCommand(const AnimationChannel &channel,
                                    const std::vector<TimelineKeyframe> &before_keyframes,
                                    const std::vector<TimelineKeyframe> &after_keyframes,
                                    const std::vector<std::uint64_t> &before_selected_ids,
                                    const std::vector<std::uint64_t> &after_selected_ids);
void UpsertTimelineKeyframe(AnimationChannel &ch, float time_sec, float value);
float EvalTimelinePreviewValue(const AnimationChannel &channel, float time_sec);
float SnapTimelineTime(const AppRuntime &runtime, float t);
bool TimelineKeyframeNearlyEqual(const TimelineKeyframe &a, const TimelineKeyframe &b);
bool TimelineKeyframeListEqual(const std::vector<TimelineKeyframe> &lhs,
                               const std::vector<TimelineKeyframe> &rhs);
void PushTimelineEditCommand(AppRuntime &runtime,
                             const AnimationChannel &channel,
                             const std::vector<TimelineKeyframe> &before_keyframes,
                             const std::vector<TimelineKeyframe> &after_keyframes,
                             const std::vector<std::uint64_t> &before_selected_ids,
                             const std::vector<std::uint64_t> &after_selected_ids);

}  // namespace desktoper2D
