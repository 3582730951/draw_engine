# MiDrawEngine 用户文档（user_dock）

本文档随构建产物一起分发，用于说明 `.a` / `.so` 的使用方式、初始化流程与公共 API 的功能与用法。

## 1. 构建产物结构

Release / Debug 产物都会包含：
- `libmidraw.so` / `libmidraw.a`
- `draw_demo` / `benchmark` / `overlay_engine`
- 公共头文件：`include/midraw.h`、`include/draw_engine.h`
- 本文档：`user_dock.md`

## 2. 链接方式

### 2.1 使用 `.so`
- 运行时将 `libmidraw.so` 放入应用的 `lib/<ABI>/`，并确保运行时可加载。
- 编译链接时建议链接：`-lmidraw -ldl -llog`
- 如果启用 GPU：额外链接 `-lvulkan`（Vulkan）或 `-lEGL -lGLESv3`（OpenGL ES）。

### 2.2 使用 `.a`
- 将 `libmidraw.a` 加入链接。
- 需要显式链接其依赖：`-ldl -llog`，以及 GPU 相关库（Vulkan / EGL / GLES）。

### 2.3 CMake 示例
```cmake
add_library(midraw STATIC IMPORTED)
set_target_properties(midraw PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libmidraw.a")

target_include_directories(app PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(app PRIVATE midraw dl log)
# 如启用 GPU:
# target_link_libraries(app PRIVATE vulkan)
# 或 target_link_libraries(app PRIVATE EGL GLESv3)
```

## 3. 使用流程（推荐高层 DrawEngine API）

典型流程：
1) `init_engine_mode(mode)` 或 `init_draw_engine(mode)`
2) `init_draw_windows(name, randomize_name)`
3) 进入主循环：
   - `draw_begin_frame()`
   - 调用绘制函数（draw_*）
   - `draw_end_frame()`
4) 退出时 `shutdown_draw_engine()`

### 3.1 模式说明
- `DRAW_ENGINE_MODE_AUTO (0)`：自动选择最优后端
- `DRAW_ENGINE_MODE_GPU (1)`：强制 GPU
- `DRAW_ENGINE_MODE_CPU (2)`：强制 CPU
- `DRAW_ENGINE_MODE_HYBRID (3)`：CPU + GPU 混合

### 3.2 FPS 行为
- `draw_engine_set_fps(fps)`：
  - GPU：目标 fps，允许一定波动
  - CPU/混合：自动基于 CPU 负载调整
- 未设置 fps 时，GPU 默认使用当前显示刷新率（获取失败则回退 60fps）
- `get_mode_is_need_set_fps()`：提示当前后端是否需要/建议设置 fps（GPU 返回 true）

## 4. 公共 API 说明（DrawEngine）

### 初始化/关闭
- `int init_engine_mode(int mode)`
  - 等同 `init_draw_engine(mode)`，设置模式并初始化。
- `int init_draw_engine(int mode)`
  - 初始化引擎，返回 0 成功。
- `int init_draw_windows(const char* name, int randomize_name)`
  - 创建透明画布窗口。`randomize_name!=0` 可随机化窗口名。
- `void shutdown_draw_engine(void)`
  - 释放所有资源。

### 帧控制
- `int draw_begin_frame(void)`
  - 开始一帧绘制，必须在任何 draw_* 之前调用。
- `void draw_end_frame(void)`
  - 结束一帧并提交。

### 画布信息/设置
- `int draw_screen_width(void)` / `int draw_screen_height(void)`
  - 返回当前逻辑宽高。
- `int draw_set_render_scale(float scale)`
  - 设定渲染缩放（0.25~1.0）。
- `int draw_set_render_size(int width, int height)`
  - 强制渲染尺寸。

### 绘制函数
- `void draw_text(const char* text, int x0, int y0, int x1, int y1, uint32_t color)`
  - 绘制文本（支持裁剪区域）。
- `void draw_rect(int x, int y, int w, int h, int filled, uint32_t color)`
  - 绘制矩形，`filled=1` 实心。
- `void draw_circle(int cx, int cy, int radius, uint32_t color)`
  - 绘制圆形。
- `void draw_line(int x1, int y1, int x2, int y2, uint32_t color)`
  - 绘制线段。

### 图像
- `DrawImage* draw_load_image_from_memory(const unsigned char* data, int size)`
  - 从内存加载 PNG/JPG 等图像。
- `void draw_free_image(DrawImage* image)`
  - 释放图像。
- `void draw_image(const DrawImage* image, int x, int y)`
  - 绘制图像。

### 设置/查询
- `void draw_engine_set_fps(int fps)`
  - 设置目标帧率（见 3.2）。
- `void draw_engine_set_hybrid_cpu_mask(uint32_t mask)`
  - 混合模式下指定 CPU 绘制类别（按位掩码）。
- `void draw_engine_set_sensitive(int enable)`
  - 标记敏感绘制区域（混合模式可强制走 CPU）。
- `int draw_engine_set_option(int option, int value)`
  - 设置选项（例如渲染尺寸/目标 fps/质量等级等）。
- `int draw_engine_get_option(int option, int* out_value)`
  - 查询选项值。
- `int draw_engine_get_capabilities(DrawEngineCaps* out_caps)`
  - 获取设备/后端能力。
- `bool get_mode_is_need_set_fps(void)`
  - 自动模式下是否建议设置 fps。
- `uint32_t draw_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)`
  - 生成引擎使用的颜色值（格式为 0xAARRGGBB）。
  - 常用颜色可直接用 `DRAW_COLOR_*` 宏。

### UI 窗口（类似 ImGui 的轻量立即模式）
用于在透明画布上快速构建调试/控制窗口。所有 UI 绘制仍复用 `draw_*`，不引入新的渲染路径。

使用要点：
1) 在每帧 `draw_begin_frame()` 之后调用 `draw_ui_new_frame()`。
2) 通过 `draw_ui_set_touch()` 提供触摸坐标（逻辑坐标），或使用可选触摸 helper。
3) `draw_ui_begin_window()` / `draw_ui_end_window()` 包裹窗口内容。
4) `draw_ui_button()` / `draw_ui_text()` 的坐标是相对于窗口内容区左上角。
5) 若使用 `draw_ui_input_text()`，请先注入按键/字符（`draw_ui_input_key()` / `draw_ui_input_char()`），建议在 `draw_ui_new_frame()` 之后调用。
6) 需要软键盘时，可配置 IME 上下文（见下方 `draw_ui_ime_*`）。

API：
- `void draw_ui_set_style(const DrawUiStyle* style)` / `void draw_ui_get_style(DrawUiStyle* out_style)`
  - 设置/获取 UI 颜色与尺寸风格。
- `void draw_ui_set_touch(int down, float x, float y)`
  - 注入触摸状态（x/y 为逻辑坐标，down=1 表示按下）。
- `void draw_ui_new_frame(void)`
  - 每帧开始时调用，更新 UI 输入状态。
- `int draw_ui_begin_window(const char* title, int* x, int* y, int w, int h, int flags)`
  - 绘制窗口并处理拖拽，`x/y` 会在拖动时被更新。
  - `flags`：`DRAW_UI_WINDOW_MOVABLE` / `DRAW_UI_WINDOW_NO_TITLE` / `DRAW_UI_WINDOW_NO_BORDER` / `DRAW_UI_WINDOW_NO_BG`。
- `void draw_ui_end_window(void)`
  - 结束窗口。
- `int draw_ui_button(const char* label, int x, int y, int w, int h)`
  - 绘制按钮并返回点击（1=点击）。
- `void draw_ui_text(const char* text, int x, int y, uint32_t color)`
  - 绘制文本。
- `int draw_ui_radio(const char* label, int x, int y, int value, int* current)`
  - 单选按钮，点击后把 `current` 设为 `value`。
- `int draw_ui_listbox(const char* label, int x, int y, int w, int h, const char* const* items, int item_count, int* current)`
  - 列表选择框。
- `int draw_ui_combo(const char* label, int x, int y, int w, const char* const* items, int item_count, int* current)`
  - 下拉选择框。
- `int draw_ui_tabs(int x, int y, int w, int h, const char* const* labels, int label_count, int* current)`
  - 标签页切换。
- `int draw_ui_checkbox(const char* label, int x, int y, int* value)`
  - 绘制复选框，点击切换 `value`（1=选中）。
- `int draw_ui_slider_int(const char* label, int x, int y, int w, int min_value, int max_value, int* value)`
  - 整数滑条，拖动更新 `value`。
- `int draw_ui_slider_float(const char* label, int x, int y, int w, float min_value, float max_value, float* value)`
  - 浮点滑条，拖动更新 `value`。
- `int draw_ui_toggle(const char* label, int x, int y, int* value)`
  - 开关控件（开关样式），点击切换 `value`。
- `void draw_ui_progress(const char* label, int x, int y, int w, float value)`
  - 进度条，`value` 为 0~1。
- `void draw_ui_separator(int x, int y, int w)`
  - 分割线。
- `void draw_ui_input_char(uint32_t codepoint)` / `void draw_ui_input_key(int key, int down)`
  - 注入字符与按键事件（参考 ImGui 输入队列风格）。
  - 常用按键常量：`DRAW_UI_KEY_BACKSPACE` / `DRAW_UI_KEY_ENTER` / `DRAW_UI_KEY_LEFT` /
    `DRAW_UI_KEY_RIGHT` / `DRAW_UI_KEY_HOME` / `DRAW_UI_KEY_END` / `DRAW_UI_KEY_DELETE` /
    `DRAW_UI_KEY_TAB` / `DRAW_UI_KEY_ESCAPE`。
- `int draw_ui_input_text(const char* label, int x, int y, int w, char* buffer, int buffer_size)`
  - 文本输入框，返回 1 表示内容变更。
- `int draw_ui_ime_set_context(const DrawUiImeConfig* config)`
  - 设置 IME 上下文（`java_vm`/`context` 必填，`view` 可选）。
- `int draw_ui_ime_show(int show)`
  - 显示/隐藏软键盘；当 `draw_ui_input_text()` 获得焦点时会自动尝试显示（若已设置 IME）。

可选触摸 helper（仅 Android，纯只读 /dev/input，不创建虚拟设备、不注入触摸）：
- `int draw_ui_touch_open(void)`
  - 打开触摸设备；可用环境变量 `DRAW_UI_TOUCH_EVENT` 指定 event 编号。
- `int draw_ui_touch_poll(void)`
  - 轮询触摸并自动调用 `draw_ui_set_touch()`，返回 1 表示状态更新。
- `void draw_ui_touch_close(void)`
  - 关闭触摸设备。

#### IME 备用接入示例（显示软键盘）
> 说明：本接口只负责显示/隐藏软键盘，**文本字符仍需由你们在 Java 层获取并转发到 `draw_ui_input_char()`**。

Java/Kotlin 侧：
```java
// Java
public native void nativeSetImeContext(Context context, View view);

@Override
protected void onCreate(Bundle savedInstanceState) {
  super.onCreate(savedInstanceState);
  SurfaceView sv = new SurfaceView(this);
  setContentView(sv);
  nativeSetImeContext(this, sv);
}
```

JNI 侧：
```c
extern "C" JNIEXPORT void JNICALL
Java_com_example_app_NativeBridge_nativeSetImeContext(JNIEnv* env,
                                                      jobject thiz,
                                                      jobject context,
                                                      jobject view) {
  JavaVM* vm = nullptr;
  env->GetJavaVM(&vm);
  DrawUiImeConfig cfg{};
  cfg.java_vm = vm;
  cfg.jni_env = env;
  cfg.context = context;
  cfg.view = view; // 可为空，但有 view 更稳定
  draw_ui_ime_set_context(&cfg);
}
```

在输入框获得焦点时引擎会自动尝试 `draw_ui_ime_show(1)`；必要时可手动调用：
```c
draw_ui_ime_show(1); // 显示
draw_ui_ime_show(0); // 隐藏
```

## 5. 低层 API（Midraw）

适合需要直接控制 CPU 绘制的场景。

### 初始化/关闭
- `int midraw_init(MidrawContext** out_ctx, const MidrawConfig* config)`
  - 创建上下文，`config` 可指定窗口名/旋转/尺寸/字体。
- `void midraw_shutdown(MidrawContext* ctx)`
  - 释放上下文资源。

### 帧控制
- `int midraw_lock(MidrawContext* ctx)`
  - 锁定帧缓冲，开始绘制。
- `void midraw_unlock_post(MidrawContext* ctx)`
  - 解锁并提交。

### 尺寸/窗口
- `int midraw_logical_width(const MidrawContext* ctx)` / `int midraw_logical_height(const MidrawContext* ctx)`
  - 逻辑宽高。
- `int midraw_resize(MidrawContext* ctx, int width, int height)`
  - 调整尺寸。
- `void* midraw_get_native_window(MidrawContext* ctx)`
  - 返回底层 ANativeWindow*。
- `int midraw_set_layer(MidrawContext* ctx, int32_t layer)`
  - 设置图层 Z-order。
- `int midraw_display_rotation(MidrawContext* ctx, int* out_rotation, int* out_width, int* out_height)`
  - 查询显示旋转与尺寸。

### 绘制函数
- `void midraw_draw_pixel(MidrawContext* ctx, int x, int y, uint32_t color)`
- `void midraw_draw_line(MidrawContext* ctx, int x1, int y1, int x2, int y2, uint32_t color)`
- `void midraw_draw_rect(MidrawContext* ctx, int x, int y, int w, int h, int filled, uint32_t color)`
- `void midraw_draw_circle(MidrawContext* ctx, int cx, int cy, int radius, uint32_t color)`
- `void midraw_draw_text(MidrawContext* ctx, const char* text, int x, int y, uint32_t color)`
- `void midraw_draw_text_rect(MidrawContext* ctx, const char* text, int x0, int y0, int x1, int y1, uint32_t color)`
- `void midraw_draw_image(MidrawContext* ctx, const uint32_t* pixels, int img_w, int img_h, int x, int y)`

## 6. 错误码

- `*_OK = 0`
- `*_EINVAL`：参数非法
- `*_ENOTINIT`：未初始化
- `*_EFAILED` / `*_ENOBACKEND`：失败或后端不可用

## 7. 典型示例（DrawEngine）

```c
init_draw_engine(DRAW_ENGINE_MODE_AUTO);
init_draw_windows("MyOverlay", 1);
for (;;) {
  if (draw_begin_frame() == 0) {
    draw_rect(10, 10, 100, 50, 1, 0x80FF0000);
    draw_text("Hello", 20, 20, 200, 60, 0xFFFFFFFF);
    draw_end_frame();
  }
}
shutdown_draw_engine();
```

---
如需更详细的参数或高级配置，请查看 `include/midraw.h` 与 `include/draw_engine.h`。
