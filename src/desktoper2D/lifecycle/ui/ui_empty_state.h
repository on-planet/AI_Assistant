#pragma once

struct ImVec4;

namespace desktoper2D {

void RenderUnifiedEmptyState(const char *child_id,
                             const char *title,
                             const char *detail,
                             const ImVec4 &accent);

}  // namespace desktoper2D
