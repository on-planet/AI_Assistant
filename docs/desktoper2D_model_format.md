# Kilo2D 最小模型格式（JSON）v1

> 目标：用尽量少的字段支持「部件（parts）+ 层级（hierarchy）+ 中心点（pivot）+ 参数（parameters）」与“参数驱动变换”。
>
> 该格式偏向运行时（Runtime）加载；后续做编辑器时可以在 v2 增加网格、遮罩、物理等。

## 1. 顶层对象

文件必须是一个 JSON object。

```json
{
  "format": "kilo2d-model",
  "version": 1,
  "meta": {
    "name": "demo",
    "author": "",
    "copyright": ""
  },
  "parameters": [],
  "parts": []
}
```

- `format`: 固定字符串，用于快速识别。
- `version`: 整数版本号（当前为 1）。
- `meta`: 可选。
- `parameters`: 参数列表。
- `parts`: 部件列表。

## 2. 参数（parameters）

```json
{
  "id": "HeadYaw",
  "name": "头部左右",
  "min": -1.0,
  "max": 1.0,
  "default": 0.0
}
```

字段：
- `id` (string, required): 参数唯一 ID。
- `name` (string, optional): 显示名。
- `min/max/default` (number, required): 参数范围与默认值。

运行时规则：
- 参数值应被 clamp 到 `[min, max]`。

## 3. 部件（parts）

部件是渲染与变形的基本单位。

```json
{
  "id": "Face",
  "name": "脸",
  "parent": "Head",

  "texture": "assets/demo/face.bmp",
  "size": [512, 512],
  "pivot": [256, 320],

  "drawOrder": 100,

  "transform": {
    "pos": [0, 0],
    "rotDeg": 0,
    "scale": [1, 1],
    "opacity": 1.0
  },

  "bindings": [
    {
      "param": "HeadYaw",
      "type": "rotDeg",
      "input": [-1, 1],
      "output": [-15, 15]
    }
  ]
}
```

字段：
- `id` (string, required): 部件唯一 ID。
- `name` (string, optional): 显示名。
- `parent` (string | null, optional): 父部件 ID；缺省或 `null` 表示根。
- `texture` (string, optional): 贴图路径（相对模型 JSON 所在目录）。
  - 运行时可以先只支持 BMP；后续可通过 `stb_image` 扩展 PNG/JPG。
- `size` ([w,h], required): 部件在“模型空间”的尺寸（像素）。
- `pivot` ([x,y], required): 枢轴/中心点（像素），相对部件左上角。
- `drawOrder` (int, optional): 绘制顺序，值越大越靠前。
- `transform` (object, optional): 基础变换。
  - `pos`: 平移（像素）
  - `rotDeg`: Z 轴旋转（角度）
  - `scale`: 缩放
  - `opacity`: 透明度（0..1）
- `bindings` (array, optional): 参数驱动。

## 4. 绑定（bindings）

一个 binding 把一个参数映射到一个“目标属性”。

```json
{
  "param": "HeadYaw",
  "type": "posX",
  "input": [-1, 1],
  "output": [-20, 20]
}
```

字段：
- `param` (string, required): 参数 ID。
- `type` (string, required): 目标属性（v1 支持）
  - `posX`, `posY`
  - `rotDeg`
  - `scaleX`, `scaleY`
  - `opacity`
- `input` ([a,b], required): 参数输入范围。
- `output` ([c,d], required): 输出范围。

运行时规则：
- 线性映射：`t = (value - a) / (b - a)`，再 `lerp(c,d,t)`。
- 允许 `a>b`（反向映射）。
- 建议对输入值先 clamp 到 `[min(a,b), max(a,b)]`，避免参数越界导致跳变。
- 当 `|b-a|` 极小时应退化为常量输出（例如输出 `c`），避免数值不稳定。
- `scaleX/scaleY` 建议在运行时设置下限（如 `>= 0.05`），防止极值下翻转或缩放趋零。
- `opacity` 建议在运行时 clamp 到 `[0,1]`，防止透明度闪烁。
- 建议对 NaN/Inf 做兜底（回退到基础变换或上一帧稳定值）。

### 4.1 极值回归建议（推荐）

为保证“参数映射 + 父子层级”稳定，建议模型中加入专用测试参数（如 `TestExtreme`）：

- 对同一参数同时绑定 `scaleX/scaleY/opacity/posX/rotDeg`。
- `output` 有意覆盖接近边界的值（如负缩放增量、负透明增量）。
- 在父子层级（`parent`）下验证：
  - 父节点缩放极值时，子节点无瞬时跳变；
  - 透明度叠乘后仍保持在 `[0,1]`；
  - 连续往返极值时无明显抖动。

## 5. 解析与容错建议

- 遇到未知字段：忽略（便于扩展）。
- 遇到缺失字段：对可选字段用默认值；对 required 字段返回错误。
- `parts.parent` 解析后应建立拓扑序（父在前），或在 update 时递归求世界矩阵并做循环检测。

## 6. 后续扩展方向（不在 v1）

- 网格与变形器：`mesh`（顶点/索引/uv）、`deformers`（warp、骨骼）。
- 遮罩：`masks` / `clip`。
- 物理：`physics`（弹簧链/二阶系统）。
- 参数曲线：`curve`（Bezier/样条）。
