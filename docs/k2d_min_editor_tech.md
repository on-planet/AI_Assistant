# K2D 最小可视化编辑技术说明（当前实现）

## 1. 目标与范围

本阶段实现的能力：

- 部件选择（鼠标命中）
- 拖拽部件（修改部件基础位置）
- pivot 编辑与可视化（十字线 + 包围框）
- 导出 `model.json`（保存回当前模型路径）

定位：这是一个运行时内嵌的最小编辑器，而不是独立编辑器应用。

## 2. 入口与主流程

编辑逻辑集成在 `src/k2d/app.cpp` 的主事件循环和渲染流程中：

1. 事件输入：
   - 鼠标按下/抬起/移动 -> 选择与拖拽
   - 键盘 -> 编辑模式切换、部件切换、保存
2. 每帧更新：
   - 原有模型更新（参数、层级、变形）
   - 编辑状态 TTL 更新（状态提示自动消失）
3. 每帧渲染：
   - 原有模型渲染
   - 编辑叠加层（选中框、pivot 十字线、调试文本）

## 3. 交互设计

### 3.1 键位

- `E`：开关编辑模式
- `Tab` / `Shift + Tab`：下一个/上一个部件
- `Ctrl + S`：工程级保存到 `project.json`（同时落盘当前模型 `model.json`）
- `Ctrl + O`：加载 `project.json`（恢复模型路径与编辑会话状态）

### 3.2 鼠标

- 左键按下并拖拽：拖动部件（移动 base pos）
- `Shift + 左键` 拖拽：拖动 pivot
- 右键拖拽：拖动 pivot
- 松开鼠标：结束拖拽

## 4. 核心实现点

### 4.1 选择（Pick）

- 使用当前 draw order 从前到后反向遍历（可视上层优先）
- 使用 `mesh.indices + deformed_positions` 做三角形级命中（点在三角形测试）
- 只在可见（opacity 足够）部件上命中

实现参考：
- `PointInTriangle` + `PartContainsPointPrecise` + `PickTopPartAt`
- 代码位置：`src/k2d/lifecycle/editor/editor_interaction_facade.cpp`


### 4.2 拖拽部件

- 记录上一次鼠标位置，计算世界空间 delta
- 将世界 delta 转换到父节点局部空间
- 写回 `base_pos_x/base_pos_y`

这样可以在父子层级、旋转、缩放存在时保持拖拽方向正确。

### 4.3 拖拽 pivot

- 将世界 delta 转换到部件局部空间
- 同步修改：
  - `pivot_x/pivot_y`
  - `base_pos_x/base_pos_y`
  - `mesh.positions`
  - `deformed_positions`（若尺寸匹配）

该策略保证“视觉位置稳定 + 局部网格相对 pivot 正确”。

### 4.4 工程级保存/加载（`project.json`）

工程级保存使用 `SaveEditorProjectJsonToDisk`，输出 schema 为 `k2d.editor.project.v1`：

- 保存前先调用 `SaveModelRuntimeJson` 落盘当前模型 JSON
- `project.json` 记录：
  - `model.path`（模型路径）
  - `editor.selectedPartIndex / selectedParamIndex`
  - `editor.manualParamMode / editMode`
  - `editor.viewPanX / viewPanY / viewZoom`
  - `feature.*`（场景分类、OCR、人脸、ASR、Chat 等开关）

加载使用 `LoadEditorProjectJsonFromDisk`：

- 校验 `schema`
- 读取 `model.path` 并调用 `LoadModelRuntime`
- 回填编辑器会话状态与功能开关
- 同步 `animation_channels_enabled = !manual_param_mode`

## 5. 可视化叠加层

编辑模式中额外绘制：

- 选中部件 AABB（矩形边框）
- 选中部件 pivot 十字线
- 文本信息（part 序号、id、pos、pivot）
- 状态提示（保存成功/失败、编辑模式开关）

## 6. 已知限制

- 命中检测已升级为三角形精确命中（基于 `mesh.indices` + 点在三角形测试），复杂遮挡场景较 AABB 误选显著减少
- gizmo 仍在持续增强（轴向拖拽、旋转环、缩放柄交互可进一步细化）
- 吸附、约束（Shift 锁轴等）仍未完善
- 独立编辑器 UI 面板（层级树、属性表）已具备骨架，仍需与完整工作流深度联动


## 7. 下一阶段建议（短期）

1. 命中升级到三角形级别（基于 `mesh.indices` + 点在三角形测试）
2. 增加 Undo/Redo 命令栈（至少覆盖 move/pivot/save 前状态）
3. 增加轴向约束拖拽（X/Y）和网格吸附
4. 增加部件列表与属性面板（最小 ImGui 或 SDL debug 文本菜单）
5. 保存策略升级：另存为 + 自动备份（`model.json.bak`）

---

## 8. 时间轴 v1（关键帧 / 插值 / 通道管理）

已接入最小可用时间轴能力（Runtime Debug 面板中的 `Timeline v1`）：

- 通道管理：新增/删除通道，启用/禁用通道，映射目标参数
- 关键帧：在当前游标时间插入或覆盖关键帧
- 插值：`Step` / `Linear`
- 运行时采样：每帧按时间采样关键帧，回写参数 target

### 8.1 数据结构

- `AnimationChannel` 新增：
  - `timeline_interp`
  - `keyframes`（`time_sec`, `value`）

### 8.2 采样规则

- `Step`：返回左关键帧值
- `Linear`：在相邻关键帧间做线性插值
- 越界：小于首帧取首帧，大于末帧取末帧
- 无关键帧：回退原程序化通道（breath/blink/headsway）

### 8.3 切线曲线（Hermite）

时间轴插值新增 `Hermite`：

- 每个 keyframe 支持：
  - `inTangent`
  - `outTangent`
- 切线单位为 `dv/dt`（每秒变化率）
- 运行时会按段时长做尺度变换后进入 Hermite 基函数

### 8.4 当前限制

- 暂未实现 Bezier 权重手柄
- 暂未实现片段循环/乒乓
- 暂未实现拖拽式轨道编辑（当前为按钮式最小交互）

维护说明：本文档描述当前“最小可视化编辑”版本，后续功能增加时请按章节增量更新。