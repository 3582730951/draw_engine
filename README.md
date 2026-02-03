# MiDrawEngine (Core)

高性能 Android 绘制引擎内核，支持 CPU / GPU / 混合模式，兼容 Android 9.0–16，适用于透明画布叠加、2D/3D 绘制与内部调试工具。

## 特性
- CPU 绘制（纯软件）与 GPU 绘制（Vulkan 优先，GLES 备用）
- CPU + GPU 混合模式，支持敏感内容走 CPU
- 透明画布叠加，支持横竖屏切换、分辨率变更
- 统一 draw_* 绘制 API
- 轻量立即模式 UI（类 ImGui）：窗口、按钮、滑条、下拉、标签页、输入框等
- 可选只读触摸采集（/dev/input），不注入、不抢占
- 可选 IME 软键盘接入（Java 层提供 Context / View）

## 支持平台
- Android 9.0 – 16
- ABI: armeabi-v7a / arm64-v8a / x86 / x86_64

## 目录结构
```
core/
  apps/                # demo / benchmark / overlay
  docs/                # 用户文档、混合配置说明
  include/             # 公共头文件
  src/engine/          # 绘制引擎实现
  shaders/             # GPU 着色器
  third_party/         # 第三方依赖
```

## 快速构建（NDK + CMake）
```
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=Release \
  -DMIDRAW_BUILD_SHARED=ON \
  -DMIDRAW_BUILD_STATIC=ON \
  -DMIDRAW_BUILD_DEMO=ON

cmake --build build
```

构建产物包含：
- `libmidraw.so` / `libmidraw.a`
- `draw_demo` / `benchmark` / `overlay_engine`
- 公共 API 与文档（`include/` + `docs/user_dock.md`）

## 基本使用（DrawEngine）
```c
init_draw_engine(DRAW_ENGINE_MODE_AUTO);
init_draw_windows("MyOverlay", 1);
for (;;) {
  if (draw_begin_frame() == 0) {
    draw_rect(10, 10, 120, 60, 1, DRAW_COLOR_GREEN);
    draw_text("Hello", 20, 20, 200, 60, DRAW_COLOR_WHITE);
    draw_end_frame();
  }
}
shutdown_draw_engine();
```

## 立即模式 UI（类 ImGui）
支持窗口拖拽、按钮、滑条、复选框、下拉、标签页、文本输入等。
```
draw_ui_new_frame();
if (draw_ui_begin_window("UI", &x, &y, 360, 300, DRAW_UI_WINDOW_MOVABLE)) {
  draw_ui_button("OK", 0, 0, 120, 32);
  draw_ui_slider_int("FPS", 0, 40, 240, 30, 240, &fps);
  draw_ui_input_text("Name", 0, 90, 240, text, sizeof(text));
}
draw_ui_end_window();
```

## 触摸与 IME
- 触摸：可选 `draw_ui_touch_open()` 从 `/dev/input` 只读采集，不注入、不抢占。
- IME：调用 `draw_ui_ime_set_context()` 注册 Java Context/View，可自动显示软键盘。
- 文本输入：由 Java 层将字符回传 `draw_ui_input_char()`。

## 主要文档
- `docs/user_dock.md`：API 使用说明与示例
- `docs/HYBRID_CONFIG.md`：混合模式策略与安全配置

## 鸣谢 / Acknowledgements
感谢以下开源项目与标准：
- OS-ImGui-Android: https://github.com/Jiang-Night/OS-ImGui-Android
- stb (stb_image / stb_truetype): https://github.com/nothings/stb
- Khronos Vulkan: https://www.khronos.org/vulkan/
- Khronos OpenGL ES: https://www.khronos.org/opengles/

