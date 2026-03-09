#include "k2d/lifecycle/ui/app_debug_ui_internal.h"

namespace k2d {

void UpsertBinding(ModelPart &part, int param_index, BindingType type, float in_min, float in_max, float out_min, float out_max) {
    for (auto &b : part.bindings) {
        if (b.param_index == param_index && b.type == type) {
            b.in_min = in_min;
            b.in_max = in_max;
            b.out_min = out_min;
            b.out_max = out_max;
            return;
        }
    }

    ParamBinding b{};
    b.param_index = param_index;
    b.type = type;
    b.in_min = in_min;
    b.in_max = in_max;
    b.out_min = out_min;
    b.out_max = out_max;
    part.bindings.push_back(b);
}

std::uint64_t NextUiTimelineStableId() {
    static std::uint64_t next_id = 1ull << 62;
    return next_id++;
}

void EnsureTimelineKeyframeStableIds(AnimationChannel &ch) {
    for (auto &kf : ch.keyframes) {
        if (kf.stable_id == 0) {
            kf.stable_id = NextUiTimelineStableId();
        }
    }
}

std::vector<int> BuildTimelineSelectedIndices(const AnimationChannel &ch,
                                              const std::vector<std::uint64_t> &selected_ids) {
    std::vector<int> indices;
    indices.reserve(selected_ids.size());
    for (std::size_t i = 0; i < ch.keyframes.size(); ++i) {
        if (std::find(selected_ids.begin(), selected_ids.end(), ch.keyframes[i].stable_id) != selected_ids.end()) {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

void NormalizeTimelineSelection(AppRuntime &runtime, AnimationChannel &ch) {
    EnsureTimelineKeyframeStableIds(ch);

    std::vector<std::uint64_t> normalized_ids;
    normalized_ids.reserve(runtime.timeline_selected_keyframe_ids.size() + runtime.timeline_selected_keyframe_indices.size());

    auto append_id = [&](std::uint64_t stable_id) {
        if (stable_id == 0) {
            return;
        }
        if (std::find(normalized_ids.begin(), normalized_ids.end(), stable_id) == normalized_ids.end()) {
            normalized_ids.push_back(stable_id);
        }
    };

    for (std::uint64_t stable_id : runtime.timeline_selected_keyframe_ids) {
        append_id(stable_id);
    }
    for (int idx : runtime.timeline_selected_keyframe_indices) {
        if (idx >= 0 && idx < static_cast<int>(ch.keyframes.size())) {
            append_id(ch.keyframes[static_cast<std::size_t>(idx)].stable_id);
        }
    }

    runtime.timeline_selected_keyframe_ids.clear();
    for (std::uint64_t stable_id : normalized_ids) {
        for (const auto &kf : ch.keyframes) {
            if (kf.stable_id == stable_id) {
                runtime.timeline_selected_keyframe_ids.push_back(stable_id);
                break;
            }
        }
    }
    runtime.timeline_selected_keyframe_indices = BuildTimelineSelectedIndices(ch, runtime.timeline_selected_keyframe_ids);
}

bool IsTimelineKeyframeSelected(const AppRuntime &runtime, const TimelineKeyframe &kf) {
    return std::find(runtime.timeline_selected_keyframe_ids.begin(),
                     runtime.timeline_selected_keyframe_ids.end(),
                     kf.stable_id) != runtime.timeline_selected_keyframe_ids.end();
}

void SelectTimelineKeyframeById(AppRuntime &runtime, AnimationChannel &ch, std::uint64_t stable_id, bool additive) {
    EnsureTimelineKeyframeStableIds(ch);
    if (!additive) {
        runtime.timeline_selected_keyframe_ids.clear();
    }
    if (stable_id != 0 && std::find(runtime.timeline_selected_keyframe_ids.begin(),
                                    runtime.timeline_selected_keyframe_ids.end(),
                                    stable_id) == runtime.timeline_selected_keyframe_ids.end()) {
        runtime.timeline_selected_keyframe_ids.push_back(stable_id);
    }
    NormalizeTimelineSelection(runtime, ch);
}

void ClearTimelineSelection(AppRuntime &runtime, AnimationChannel &ch) {
    runtime.timeline_selected_keyframe_ids.clear();
    runtime.timeline_selected_keyframe_indices.clear();
    NormalizeTimelineSelection(runtime, ch);
}

EditCommand MakeTimelineEditCommand(const AnimationChannel &channel,
                                    const std::vector<TimelineKeyframe> &before_keyframes,
                                    const std::vector<TimelineKeyframe> &after_keyframes,
                                    const std::vector<std::uint64_t> &before_selected_ids,
                                    const std::vector<std::uint64_t> &after_selected_ids) {
    EditCommand cmd{};
    cmd.type = EditCommand::Type::Timeline;
    cmd.channel_id = channel.id;
    cmd.before_keyframes = before_keyframes;
    cmd.after_keyframes = after_keyframes;
    cmd.before_selected_keyframe_ids = before_selected_ids;
    cmd.after_selected_keyframe_ids = after_selected_ids;
    return cmd;
}

void UpsertTimelineKeyframe(AnimationChannel &ch, float time_sec, float value) {
    EnsureTimelineKeyframeStableIds(ch);
    for (auto &kf : ch.keyframes) {
        if (std::abs(kf.time_sec - time_sec) < 1e-4f) {
            kf.value = value;
            return;
        }
    }
    ch.keyframes.push_back(TimelineKeyframe{.stable_id = NextUiTimelineStableId(), .time_sec = time_sec, .value = value});
    std::sort(ch.keyframes.begin(), ch.keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
        return a.time_sec < b.time_sec;
    });
}

float EvalTimelinePreviewValue(const AnimationChannel &channel, float time_sec) {
    if (channel.keyframes.empty()) {
        return 0.0f;
    }
    if (channel.keyframes.size() == 1) {
        return channel.keyframes.front().value;
    }

    const auto &kfs = channel.keyframes;
    const float start_t = kfs.front().time_sec;
    const float end_t = kfs.back().time_sec;
    const float duration = std::max(1e-6f, end_t - start_t);

    float eval_t = time_sec;
    if (channel.timeline_wrap == TimelineWrapMode::Loop) {
        const float x = std::fmod(time_sec - start_t, duration);
        eval_t = start_t + (x < 0.0f ? (x + duration) : x);
    } else if (channel.timeline_wrap == TimelineWrapMode::PingPong) {
        const float period = duration * 2.0f;
        float x = std::fmod(time_sec - start_t, period);
        if (x < 0.0f) x += period;
        eval_t = (x <= duration) ? (start_t + x) : (end_t - (x - duration));
    }

    if (eval_t <= start_t) {
        return kfs.front().value;
    }
    if (eval_t >= end_t) {
        return kfs.back().value;
    }

    for (std::size_t i = 1; i < kfs.size(); ++i) {
        const TimelineKeyframe &a = kfs[i - 1];
        const TimelineKeyframe &b = kfs[i];
        if (eval_t <= b.time_sec) {
            if (channel.timeline_interp == TimelineInterpolation::Step) {
                return a.value;
            }
            const float span = std::max(1e-6f, b.time_sec - a.time_sec);
            const float t = std::clamp((eval_t - a.time_sec) / span, 0.0f, 1.0f);
            if (channel.timeline_interp == TimelineInterpolation::Linear) {
                return a.value + (b.value - a.value) * t;
            }

            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            const float w0 = std::clamp(a.out_weight, 0.0f, 1.0f);
            const float w1 = std::clamp(b.in_weight, 0.0f, 1.0f);
            const float m0 = a.out_tangent * span * w0;
            const float m1 = b.in_tangent * span * w1;
            return h00 * a.value + h10 * m0 + h01 * b.value + h11 * m1;
        }
    }

    return kfs.back().value;
}

float SnapTimelineTime(const AppRuntime &runtime, float t) {
    t = std::clamp(t, 0.0f, std::max(0.0f, runtime.timeline_duration_sec));
    if (!runtime.timeline_snap_enabled) {
        return t;
    }
    switch (runtime.timeline_snap_mode) {
        case 0: {
            const float fps = std::max(1.0f, runtime.timeline_snap_fps);
            return std::round(t * fps) / fps;
        }
        case 1:
            return std::round(t * 10.0f) / 10.0f;
        case 2:
            return runtime.timeline_cursor_sec;
        default:
            return t;
    }
}

bool TimelineKeyframeNearlyEqual(const TimelineKeyframe &a, const TimelineKeyframe &b) {
    return std::abs(a.time_sec - b.time_sec) < 1e-6f &&
           std::abs(a.value - b.value) < 1e-6f &&
           std::abs(a.in_tangent - b.in_tangent) < 1e-6f &&
           std::abs(a.out_tangent - b.out_tangent) < 1e-6f &&
           std::abs(a.in_weight - b.in_weight) < 1e-6f &&
           std::abs(a.out_weight - b.out_weight) < 1e-6f;
}

bool TimelineKeyframeListEqual(const std::vector<TimelineKeyframe> &lhs,
                               const std::vector<TimelineKeyframe> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!TimelineKeyframeNearlyEqual(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

void PushTimelineEditCommand(AppRuntime &runtime,
                             const AnimationChannel &channel,
                             const std::vector<TimelineKeyframe> &before_keyframes,
                             const std::vector<TimelineKeyframe> &after_keyframes,
                             const std::vector<std::uint64_t> &before_selected_ids,
                             const std::vector<std::uint64_t> &after_selected_ids) {
    if (TimelineKeyframeListEqual(before_keyframes, after_keyframes) && before_selected_ids == after_selected_ids) {
        return;
    }
    PushEditCommand(runtime.undo_stack,
                    runtime.redo_stack,
                    MakeTimelineEditCommand(channel,
                                            before_keyframes,
                                            after_keyframes,
                                            before_selected_ids,
                                            after_selected_ids));
}

}  // namespace k2d
