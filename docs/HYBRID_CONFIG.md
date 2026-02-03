# Hybrid 模式配置指南

本文说明 DRAW_ENGINE_MODE=3（CPU + GPU 混合）下的常用配置方式与安全建议。

## 适用场景
- 需要 GPU 提升 2D/3D 绘制效率
- 同时对敏感内容（文字/线条/圆形等）要求 CPU 路径避免 GPU 侧信道

## 常用环境变量
```
DRAW_ENGINE_MODE=3
DRAW_ENGINE_HYBRID_PROFILE=secure_text|secure_ui|balanced|gpu_first
DRAW_ENGINE_HYBRID_SECURE=1
DRAW_ENGINE_HYBRID_SENSITIVE_ONLY=1
DRAW_ENGINE_HYBRID_CPU_MASK=1
```

### CPU 掩码含义
- 1  (bit0): 文本（TEXT）
- 2  (bit1): 线条（LINE）
- 4  (bit2): 圆形（CIRCLE）
- 8  (bit3): 矩形（RECT）
- 16 (bit4): 图片（IMAGE）

## 推荐配置案例

### 案例 A：敏感文字走 CPU，其余走 GPU
```
DRAW_ENGINE_MODE=3
DRAW_ENGINE_HYBRID_PROFILE=secure_text
DRAW_ENGINE_HYBRID_SECURE=1
DRAW_ENGINE_HYBRID_SENSITIVE_ONLY=1
DRAW_ENGINE_HYBRID_CPU_MASK=1
```

代码：
```
draw_engine_set_sensitive(1);
draw_text("SECRET", x, y, x2, y2, color);
draw_engine_set_sensitive(0);
```

### 案例 B：UI 文本 + 线条 + 圆形走 CPU，其余走 GPU
```
DRAW_ENGINE_MODE=3
DRAW_ENGINE_HYBRID_PROFILE=secure_ui
DRAW_ENGINE_HYBRID_SECURE=1
DRAW_ENGINE_HYBRID_SENSITIVE_ONLY=1
DRAW_ENGINE_HYBRID_CPU_MASK=7
```

### 案例 C：GPU 优先，仅敏感文本走 CPU
```
DRAW_ENGINE_MODE=3
DRAW_ENGINE_HYBRID_PROFILE=gpu_first
DRAW_ENGINE_HYBRID_SECURE=1
DRAW_ENGINE_HYBRID_SENSITIVE_ONLY=1
DRAW_ENGINE_HYBRID_CPU_MASK=1
```

### 案例 D：GPU 低分辨率渲染 + CPU 覆盖敏感内容
```
DRAW_ENGINE_MODE=3
DRAW_ENGINE_HYBRID_PROFILE=secure_text
DRAW_ENGINE_HYBRID_SECURE=1
DRAW_ENGINE_RENDER_SCALE=0.75
DRAW_ENGINE_SCALE_CPU=0
```

## 说明与建议
- `DRAW_ENGINE_HYBRID_SENSITIVE_ONLY=1` 时，仅在 `draw_engine_set_sensitive(1)` 标记范围内走 CPU。
- `DRAW_ENGINE_HYBRID_CPU_MASK` 可细分到具体绘制类型。
- 若需要更高安全性，建议只让敏感内容走 CPU，其余走 GPU。
