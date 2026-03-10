#pragma once

#include <string>

#include "desktoper2D/core/json.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"

namespace desktoper2D {

bool JsonArrayAllString(const JsonValue *v);

std::string ReadTextFile(const std::string &path);
std::string Fnv1a64OfFileHex(const std::string &path);

void ApplyWorkerTuningFromJsonObject(const JsonValue &worker, PluginArtifactSpec::WorkerTuning &cfg);

bool ValidatePluginConfig(const JsonValue &root,
                          const std::string &config_path,
                          PluginArtifactSpec *out_spec,
                          std::string *out_error);

}  // namespace desktoper2D
