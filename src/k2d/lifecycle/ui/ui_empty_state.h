#pragma once

struct ImVec4;

namespace k2d {

void RenderUnifiedEmptyState(const char *child_id,
                             const char *title,
                             const char *detail,
                             const ImVec4 &accent);

}  // namespace k2d
