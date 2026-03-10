#pragma once

#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "desktoper2D/core/json.h"

namespace desktoper2D {

std::vector<std::string> NormalizeBackendPriority(const JsonValue *arr);

bool ApplyBackendPriorityToSessionOptions(const std::vector<std::string> &priority,
                                          Ort::SessionOptions &opts,
                                          std::string *out_backend,
                                          std::string *out_error);

}  // namespace desktoper2D
