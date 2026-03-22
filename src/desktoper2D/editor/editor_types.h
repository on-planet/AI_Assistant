#pragma once

namespace desktoper2D {

enum class AxisConstraint {
    None,
    XOnly,
    YOnly,
};

enum class GizmoHandle {
    None,
    MoveX,
    MoveY,
    Rotate,
    Scale,
};

}  // namespace desktoper2D
