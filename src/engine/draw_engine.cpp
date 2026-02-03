#include "draw_engine.h"
#include "midraw.h"
#include "vk_shaders.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <string>

#if defined(__ANDROID__)
#include <dirent.h>
#include <jni.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifndef DRAW_ENGINE_HAS_VULKAN
#define DRAW_ENGINE_HAS_VULKAN 1
#endif
#ifndef DRAW_ENGINE_HAS_GLES
#define DRAW_ENGINE_HAS_GLES 1
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype/stb_truetype.h"

struct AChoreographer;

struct DrawImage {
  int width;
  int height;
  uint32_t* pixels;
  unsigned char* rgba;
  bool vk_ready;
  VkImage vk_image;
  VkImageView vk_view;
  VkDeviceMemory vk_memory;
  VkDescriptorSet vk_desc;
  bool gl_ready;
  GLuint gl_tex;
};

enum BackendType {
  BACKEND_CPU = 0,
  BACKEND_VULKAN = 1,
  BACKEND_GLES = 2,
};

struct DrawEngineState {
  int mode;
  BackendType backend;
  MidrawContext* cpu_ctx;
  MidrawContext* overlay_ctx;
  char* surface_name;
  int rotation;
  bool rotation_forced;
  bool in_frame;
  bool initialized;
  bool hybrid;
  bool overlay_locked;
  float render_scale;
  int render_width;
  int render_height;
  bool render_scale_manual;
  bool render_size_manual;
  int target_fps;
  bool fps_manual;
  float auto_fps;
  double avg_draw_ns;
  uint64_t frame_start_ns;
  uint32_t hybrid_cpu_mask;
  bool hybrid_sensitive_only;
  bool sensitive;
  int circle_segments;
  bool circle_segments_manual;
  int auto_quality;
  int quality_level;
  int quality_min;
  int quality_max;
  uint64_t last_quality_ns;
  uint64_t sleep_bias_ns;
  uint64_t sleep_bias_max_ns;
  bool sleep_bias_enabled;
};

static void gpu_shutdown(struct GpuState& gpu);
static bool gpu_init(struct GpuState& gpu, BackendType backend, ANativeWindow* window, int width,
                     int height, int rotation);
static void gpu_refresh_size_and_rotation();
static uint64_t now_ns();
static void init_sleep_bias();
static uint64_t apply_sleep_bias(uint64_t deadline_ns, uint64_t now_ns);
static void update_sleep_bias(uint64_t deadline_ns, uint64_t woke_ns);

struct GpuVertex {
  float x;
  float y;
  float z;
  float u;
  float v;
  uint32_t color;
};

struct CachedTextVertex {
  float x;
  float y;
  float z;
  float u;
  float v;
  uint32_t color;
};

struct TextInstance {
  float x;
  float y;
  float w;
  float h;
  float u0;
  float v0;
  float u1;
  float v1;
  uint32_t color;
};

enum class GpuCmdType : uint8_t { Rect, Line, Circle, Text, Image };

struct GpuCommand {
  GpuCmdType type;
  int a;
  int b;
  int c;
  int d;
  uint32_t color;
  uintptr_t ptr;
};

struct GpuBatch {
  int first;
  int count;
  bool use_texture;
  bool use_3d;
  bool line;
  const void* texture;
};

struct GpuFontAtlas {
  int width;
  int height;
  float u0[96];
  float v0[96];
  float u1[96];
  float v1[96];
  float xoff[96];
  float yoff[96];
  float xadvance[96];
  float glyph_w[96];
  float glyph_h[96];
  float ascent;
  float line_advance;
  bool truetype;
  bool sdf;
  bool ready;
};

struct CachedText {
  std::string text;
  int x0;
  int y0;
  int x1;
  int y1;
  uint32_t color;
  int font_w;
  int font_h;
  float font_ascent;
  float font_line;
  bool font_truetype;
  bool font_sdf;
  bool instanced;
  std::vector<TextInstance> instances;
  std::vector<CachedTextVertex> vertices;
  uint64_t last_used;
};

struct VulkanContext {
  bool ready;
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  uint32_t queue_family;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  VkFormat swapchain_format;
  VkExtent2D extent;
  std::vector<VkImage> images;
  std::vector<VkImageView> image_views;
  std::vector<VkFramebuffer> framebuffers_2d;
  std::vector<VkFramebuffer> framebuffers_3d;
  VkRenderPass render_pass_2d;
  VkRenderPass render_pass_3d;
  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline_tri_2d;
  VkPipeline pipeline_line_2d;
  VkPipeline pipeline_text_2d;
  VkPipeline pipeline_tri_3d;
  VkPipeline pipeline_line_3d;
  VkCommandPool command_pool;
  uint32_t frame_count;
  uint32_t frame_index;
  std::vector<VkCommandBuffer> command_buffers;
  std::vector<VkSemaphore> image_available;
  std::vector<VkSemaphore> render_finished;
  std::vector<VkFence> in_flight;
  VkDescriptorSetLayout desc_layout;
  VkDescriptorPool desc_pool;
  VkSampler sampler;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_memory;
  size_t vertex_capacity;
  size_t vertex_buffer_size;
  size_t vertex_stride;
  void* vertex_map;
  VkBuffer text_buffer;
  VkDeviceMemory text_memory;
  size_t text_capacity;
  size_t text_buffer_size;
  void* text_map;
  VkSampleCountFlagBits msaa_samples;
  VkImage msaa_color_image;
  VkDeviceMemory msaa_color_memory;
  VkImageView msaa_color_view;
  VkImage depth_image;
  VkDeviceMemory depth_memory;
  VkImageView depth_view;
  VkFormat depth_format;
  VkSurfaceTransformFlagBitsKHR pre_transform;
  float max_anisotropy;
  bool supports_anisotropy;
  bool supports_wide_lines;
  bool supports_timestamps;
  float timestamp_period;
  VkQueryPool query_pool;
};

struct GlesContext {
  bool ready;
  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;
  EGLConfig config;
  GLuint program;
  GLuint vao;
  GLuint vbo;
  GLuint program_text;
  GLuint vao_text;
  GLuint vbo_text;
  GLint u_mvp;
  GLint u_tex;
  GLint u_mvp_text;
  GLint u_tex_text;
  int major;
  int minor;
  bool supports_anisotropy;
  float max_anisotropy;
  int samples;
  size_t text_buffer_size;
};

struct GpuState {
  BackendType backend;
  ANativeWindow* window;
  int logical_width;
  int logical_height;
  int physical_width;
  int physical_height;
  int rotation;
  float scale_x;
  float scale_y;
  VulkanContext vk;
  GlesContext gl;
  std::vector<GpuCommand> commands;
  std::vector<GpuVertex> vertices;
  std::vector<GpuBatch> batches;
  std::vector<TextInstance> text_instances;
  std::vector<float> circle_lut;
  int circle_segments;
  char text_pool[65536];
  size_t text_offset;
  std::vector<CachedText> text_cache;
  size_t text_cache_bytes;
  size_t text_cache_limit;
  int text_cache_max_entries;
  GpuFontAtlas font;
  DrawImage font_image;
  DrawImage white_image;
  uint64_t frame_index;
  uint64_t last_log_ns;
  int log_interval;
};

static DrawEngineState g_engine{};
static GpuState g_gpu{};

static int env_int(const char* name, int default_value);

static bool has_library(const char* name) {
  if (!name || !name[0]) {
    return false;
  }
  void* handle = dlopen(name, RTLD_NOW);
  if (!handle) {
    return false;
  }
  dlclose(handle);
  return true;
}

struct UiInputState {
  bool down;
  bool pressed;
  bool released;
  bool prev_down;
  float x;
  float y;
};

struct UiState {
  UiInputState input;
  DrawUiStyle style;
  bool style_set;
  uint32_t hot_id;
  uint32_t active_id;
  uint32_t drag_id;
  float drag_off_x;
  float drag_off_y;
  int window_x;
  int window_y;
  int window_w;
  int window_h;
  int content_x;
  int content_y;
  int content_w;
  int content_h;
  uint32_t window_id;
  bool in_window;
  uint32_t combo_id;
  bool combo_open;
  uint32_t text_id;
  int text_cursor;
  uint32_t input_chars[64];
  int input_char_count;
  bool key_down[DRAW_UI_KEY_MAX + 1];
  bool key_pressed[DRAW_UI_KEY_MAX + 1];
  float scroll_drag_off;
};

static UiState g_ui{};

#if defined(__ANDROID__)
struct UiImeState {
  JavaVM* vm;
  jobject context;
  jobject view;
  bool ready;
};

static UiImeState g_ime{};
#endif

static uint32_t ui_hash(const char* text) {
  if (!text) {
    return 0;
  }
  uint32_t hash = 2166136261u;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    hash ^= static_cast<uint32_t>(*p);
    hash *= 16777619u;
  }
  return hash ? hash : 1u;
}

static bool ui_point_in_rect(float x, float y, int rx, int ry, int rw, int rh) {
  return (x >= static_cast<float>(rx) &&
          y >= static_cast<float>(ry) &&
          x < static_cast<float>(rx + rw) &&
          y < static_cast<float>(ry + rh));
}

static void ui_init_style() {
  if (g_ui.style_set) {
    return;
  }
  g_ui.style.window_bg = 0xCC1F1F1F;
  g_ui.style.title_bg = 0xCC2C2C2C;
  g_ui.style.border = 0xFF000000;
  g_ui.style.text = 0xFFFFFFFF;
  g_ui.style.button_bg = 0xFF3A3A3A;
  g_ui.style.button_hover = 0xFF505050;
  g_ui.style.button_active = 0xFF2A74FF;
  g_ui.style.title_height = 36;
  g_ui.style.padding = 10;
  g_ui.style.border_size = 1;
  g_ui.style_set = true;
}

void draw_ui_set_style(const DrawUiStyle* style) {
  if (!style) {
    return;
  }
  g_ui.style = *style;
  g_ui.style_set = true;
}

void draw_ui_get_style(DrawUiStyle* out_style) {
  if (!out_style) {
    return;
  }
  ui_init_style();
  *out_style = g_ui.style;
}

void draw_ui_set_touch(int down, float x, float y) {
  g_ui.input.down = (down != 0);
  g_ui.input.x = x;
  g_ui.input.y = y;
}

void draw_ui_new_frame(void) {
  ui_init_style();
  g_ui.hot_id = 0;
  g_ui.input.pressed = (!g_ui.input.prev_down && g_ui.input.down);
  g_ui.input.released = (g_ui.input.prev_down && !g_ui.input.down);
  g_ui.input.prev_down = g_ui.input.down;
  g_ui.in_window = false;
  g_ui.window_id = 0;
  g_ui.input_char_count = 0;
  for (int i = 0; i <= DRAW_UI_KEY_MAX; ++i) {
    g_ui.key_pressed[i] = false;
  }
}

int draw_ui_begin_window(const char* title,
                         int* x,
                         int* y,
                         int w,
                         int h,
                         int flags) {
  if (!g_engine.cpu_ctx || !x || !y || !title || !title[0]) {
    return 0;
  }
  ui_init_style();

  const int screen_w = draw_screen_width();
  const int screen_h = draw_screen_height();
  int win_w = (w > 0) ? w : (screen_w > 0 ? screen_w / 2 : 320);
  int win_h = (h > 0) ? h : (screen_h > 0 ? screen_h / 3 : 200);
  if (win_w < 160) {
    win_w = 160;
  }
  if (win_h < 120) {
    win_h = 120;
  }

  int win_x = *x;
  int win_y = *y;
  const int title_h = (flags & DRAW_UI_WINDOW_NO_TITLE) ? 0 : g_ui.style.title_height;
  const bool movable = (flags & DRAW_UI_WINDOW_MOVABLE) != 0;
  const uint32_t id = ui_hash(title);
  if (movable && title_h > 0) {
    if (g_ui.input.pressed &&
        ui_point_in_rect(g_ui.input.x, g_ui.input.y, win_x, win_y, win_w, title_h)) {
      g_ui.drag_id = id;
      g_ui.active_id = id;
      g_ui.drag_off_x = g_ui.input.x - static_cast<float>(win_x);
      g_ui.drag_off_y = g_ui.input.y - static_cast<float>(win_y);
    }
    if (g_ui.drag_id == id && g_ui.input.down) {
      win_x = static_cast<int>(g_ui.input.x - g_ui.drag_off_x + 0.5f);
      win_y = static_cast<int>(g_ui.input.y - g_ui.drag_off_y + 0.5f);
      if (screen_w > 0) {
        if (win_x < 0) {
          win_x = 0;
        } else if (win_x + win_w > screen_w) {
          win_x = screen_w - win_w;
        }
      }
      if (screen_h > 0) {
        if (win_y < 0) {
          win_y = 0;
        } else if (win_y + win_h > screen_h) {
          win_y = screen_h - win_h;
        }
      }
      *x = win_x;
      *y = win_y;
    }
    if (g_ui.input.released && g_ui.drag_id == id) {
      g_ui.drag_id = 0;
      g_ui.active_id = 0;
    }
  }

  if ((flags & DRAW_UI_WINDOW_NO_BG) == 0) {
    draw_rect(win_x, win_y, win_w, win_h, 1, g_ui.style.window_bg);
  }
  if (title_h > 0) {
    draw_rect(win_x, win_y, win_w, title_h, 1, g_ui.style.title_bg);
  }
  if ((flags & DRAW_UI_WINDOW_NO_BORDER) == 0) {
    draw_rect(win_x, win_y, win_w, win_h, 0, g_ui.style.border);
  }
  if (title_h > 0 && (flags & DRAW_UI_WINDOW_NO_TITLE) == 0) {
    const int text_x = win_x + g_ui.style.padding;
    const int text_y = win_y + (title_h / 2) - 8;
    draw_text(title, text_x + 1, text_y + 1, win_x + win_w, win_y + title_h,
              0x80000000);
    draw_text(title, text_x, text_y, win_x + win_w, win_y + title_h, g_ui.style.text);
  }

  g_ui.window_x = win_x;
  g_ui.window_y = win_y;
  g_ui.window_w = win_w;
  g_ui.window_h = win_h;
  g_ui.content_x = win_x + g_ui.style.padding;
  g_ui.content_y = win_y + title_h + g_ui.style.padding;
  g_ui.content_w = win_w - g_ui.style.padding * 2;
  g_ui.content_h = win_h - title_h - g_ui.style.padding * 2;
  g_ui.window_id = id;
  g_ui.in_window = true;
  return 1;
}

void draw_ui_end_window(void) {
  g_ui.in_window = false;
  g_ui.window_id = 0;
}

int draw_ui_button(const char* label, int x, int y, int w, int h) {
  if (!g_engine.cpu_ctx || !label || !label[0]) {
    return 0;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  int bx = base_x + x;
  int by = base_y + y;
  if (w <= 0) {
    w = 100;
  }
  if (h <= 0) {
    h = 30;
  }
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));

  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, h);
  if (hovered) {
    g_ui.hot_id = id;
  }
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  bool clicked = false;
  if (g_ui.input.released && g_ui.active_id == id) {
    clicked = hovered;
    g_ui.active_id = 0;
  }

  uint32_t color = g_ui.style.button_bg;
  if (g_ui.active_id == id && g_ui.input.down) {
    color = g_ui.style.button_active;
  } else if (hovered) {
    color = g_ui.style.button_hover;
  }
  draw_rect(bx, by, w, h, 1, color);
  draw_rect(bx, by, w, h, 0, g_ui.style.border);
  const int text_x = bx + g_ui.style.padding;
  const int text_y = by + (h / 2) - 8;
  draw_text(label, text_x, text_y, bx + w - g_ui.style.padding, by + h,
            g_ui.style.text);
  return clicked ? 1 : 0;
}

void draw_ui_text(const char* text, int x, int y, uint32_t color) {
  if (!g_engine.cpu_ctx || !text) {
    return;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  int clip_x1 = base_x + x + (g_ui.in_window ? g_ui.content_w : 2048);
  int clip_y1 = base_y + y + (g_ui.in_window ? g_ui.content_h : 2048);
  draw_text(text, base_x + x, base_y + y, clip_x1, clip_y1, color);
}

void draw_ui_input_char(uint32_t codepoint) {
  if (g_ui.input_char_count >= static_cast<int>(sizeof(g_ui.input_chars) /
                                                sizeof(g_ui.input_chars[0]))) {
    return;
  }
  g_ui.input_chars[g_ui.input_char_count++] = codepoint;
}

void draw_ui_input_key(int key, int down) {
  if (key <= 0 || key > DRAW_UI_KEY_MAX) {
    return;
  }
  const bool is_down = (down != 0);
  if (is_down && !g_ui.key_down[key]) {
    g_ui.key_pressed[key] = true;
  }
  g_ui.key_down[key] = is_down;
}

static float ui_clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static int ui_utf8_encode(uint32_t codepoint, char* out, int out_size) {
  if (!out || out_size < 4) {
    return 0;
  }
  if (codepoint <= 0x7Fu) {
    out[0] = static_cast<char>(codepoint);
    return 1;
  }
  if (codepoint <= 0x7FFu) {
    out[0] = static_cast<char>(0xC0u | ((codepoint >> 6) & 0x1Fu));
    out[1] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
    return 2;
  }
  if (codepoint <= 0xFFFFu) {
    out[0] = static_cast<char>(0xE0u | ((codepoint >> 12) & 0x0Fu));
    out[1] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
    return 3;
  }
  if (codepoint <= 0x10FFFFu) {
    out[0] = static_cast<char>(0xF0u | ((codepoint >> 18) & 0x07u));
    out[1] = static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
    return 4;
  }
  return 0;
}

static int ui_utf8_prev(const char* text, int pos) {
  if (!text || pos <= 0) {
    return 0;
  }
  int i = pos - 1;
  while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) {
    --i;
  }
  return i;
}

static int ui_utf8_next(const char* text, int len, int pos) {
  if (!text || pos >= len) {
    return len;
  }
  int i = pos + 1;
  while (i < len && (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) {
    ++i;
  }
  return i;
}

static int ui_utf8_count(const char* text, int bytes) {
  if (!text || bytes <= 0) {
    return 0;
  }
  int count = 0;
  for (int i = 0; i < bytes; ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u) {
      ++count;
    }
  }
  return count;
}

static void ui_clear_input_events() {
  g_ui.input_char_count = 0;
  for (int i = 0; i <= DRAW_UI_KEY_MAX; ++i) {
    g_ui.key_pressed[i] = false;
  }
}

#if defined(__ANDROID__)
static JNIEnv* ui_ime_get_env(bool* out_attached) {
  if (out_attached) {
    *out_attached = false;
  }
  if (!g_ime.vm) {
    return nullptr;
  }
  JNIEnv* env = nullptr;
  const jint res = g_ime.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (res == JNI_OK && env) {
    return env;
  }
  if (g_ime.vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
    if (out_attached) {
      *out_attached = true;
    }
    return env;
  }
  return nullptr;
}

static void ui_ime_detach(bool attached) {
  if (attached && g_ime.vm) {
    g_ime.vm->DetachCurrentThread();
  }
}

static void ui_ime_request(bool show) {
  if (!g_ime.ready) {
    return;
  }
  draw_ui_ime_show(show ? 1 : 0);
}
#endif

int draw_ui_input_text(const char* label,
                       int x,
                       int y,
                       int w,
                       char* buffer,
                       int buffer_size) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !buffer || buffer_size <= 1) {
    return 0;
  }
  ui_init_style();
  if (w <= 60) {
    w = 200;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int label_x = base_x + x;
  const int label_y = base_y + y;
  const int field_y = label_y + 18;
  const int field_h = 26;
  const int bx = base_x + x;
  const int by = field_y;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, field_h);
  if (g_ui.input.pressed && hovered) {
    if (g_ui.text_id != id) {
      g_ui.text_id = id;
      g_ui.text_cursor = static_cast<int>(strlen(buffer));
#if defined(__ANDROID__)
      ui_ime_request(true);
#endif
    }
  } else if (g_ui.input.pressed && g_ui.text_id == id && !hovered) {
    g_ui.text_id = 0;
#if defined(__ANDROID__)
    ui_ime_request(false);
#endif
  }

  bool changed = false;
  draw_text(label, label_x, label_y, label_x + w, label_y + 16, g_ui.style.text);
  const bool active = (g_ui.text_id == id);
  const uint32_t bg = active ? g_ui.style.button_hover : g_ui.style.button_bg;
  draw_rect(bx, by, w, field_h, 1, bg);
  draw_rect(bx, by, w, field_h, 0, g_ui.style.border);
  draw_text(buffer,
            bx + g_ui.style.padding,
            by + 4,
            bx + w - g_ui.style.padding,
            by + field_h,
            g_ui.style.text);

  int len = static_cast<int>(strlen(buffer));
  if (g_ui.text_cursor > len) {
    g_ui.text_cursor = len;
  }
  if (active) {
    if (g_ui.key_pressed[DRAW_UI_KEY_LEFT]) {
      g_ui.text_cursor = ui_utf8_prev(buffer, g_ui.text_cursor);
    } else if (g_ui.key_pressed[DRAW_UI_KEY_RIGHT]) {
      g_ui.text_cursor = ui_utf8_next(buffer, len, g_ui.text_cursor);
    }
    if (g_ui.key_pressed[DRAW_UI_KEY_HOME]) {
      g_ui.text_cursor = 0;
    } else if (g_ui.key_pressed[DRAW_UI_KEY_END]) {
      g_ui.text_cursor = len;
    }
    if (g_ui.key_pressed[DRAW_UI_KEY_BACKSPACE] && g_ui.text_cursor > 0) {
      const int prev = ui_utf8_prev(buffer, g_ui.text_cursor);
      memmove(buffer + prev, buffer + g_ui.text_cursor, len - g_ui.text_cursor + 1);
      g_ui.text_cursor = prev;
      len = static_cast<int>(strlen(buffer));
      changed = true;
    }
    if (g_ui.key_pressed[DRAW_UI_KEY_DELETE] && g_ui.text_cursor < len) {
      const int next = ui_utf8_next(buffer, len, g_ui.text_cursor);
      memmove(buffer + g_ui.text_cursor, buffer + next, len - next + 1);
      len = static_cast<int>(strlen(buffer));
      changed = true;
    }
    if (g_ui.input_char_count > 0) {
      for (int i = 0; i < g_ui.input_char_count; ++i) {
        const uint32_t cp = g_ui.input_chars[i];
        if (cp == 0 || cp == '\n' || cp == '\r' || cp == '\t') {
          continue;
        }
        char tmp[4];
        const int add = ui_utf8_encode(cp, tmp, static_cast<int>(sizeof(tmp)));
        if (add <= 0) {
          continue;
        }
        if (len + add >= buffer_size) {
          continue;
        }
        memmove(buffer + g_ui.text_cursor + add,
                buffer + g_ui.text_cursor,
                len - g_ui.text_cursor + 1);
        memcpy(buffer + g_ui.text_cursor, tmp, static_cast<size_t>(add));
        g_ui.text_cursor += add;
        len += add;
        changed = true;
      }
    }
    const int caret_chars = ui_utf8_count(buffer, g_ui.text_cursor);
    const int caret_x = bx + g_ui.style.padding + caret_chars * 8;
    draw_line(caret_x, by + 4, caret_x, by + field_h - 4, g_ui.style.text);
  }
  return changed ? 1 : 0;
}

int draw_ui_radio(const char* label, int x, int y, int value, int* current) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !current) {
    return 0;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int radius = 9;
  const int bx = base_x + x;
  const int by = base_y + y;
  const int hit_w = radius * 2 + 6;
  const int hit_h = radius * 2 + 6;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8)) ^
      static_cast<uint32_t>(value);
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, hit_w, hit_h);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  int changed = 0;
  if (g_ui.input.released && g_ui.active_id == id) {
    if (hovered && *current != value) {
      *current = value;
      changed = 1;
    }
    g_ui.active_id = 0;
  }
  const int cx = bx + radius + 2;
  const int cy = by + radius + 2;
  draw_circle(cx, cy, radius, g_ui.style.border);
  if (*current == value) {
    for (int r = radius - 3; r > 0; --r) {
      draw_circle(cx, cy, r, g_ui.style.button_active);
    }
  }
  draw_text(label,
            bx + hit_w + g_ui.style.padding,
            by + 2,
            bx + hit_w + 320,
            by + hit_h,
            g_ui.style.text);
  return changed;
}

int draw_ui_listbox(const char* label,
                    int x,
                    int y,
                    int w,
                    int h,
                    const char* const* items,
                    int item_count,
                    int* current) {
  if (!g_engine.cpu_ctx || !label || !items || item_count <= 0 || !current) {
    return 0;
  }
  ui_init_style();
  if (w <= 60) {
    w = 200;
  }
  const int item_h = 22;
  if (h <= 0) {
    h = item_h * item_count + 6;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int label_x = base_x + x;
  const int label_y = base_y + y;
  const int list_y = label_y + 18;
  const int bx = base_x + x;
  const int by = list_y;
  draw_text(label, label_x, label_y, label_x + w, label_y + 16, g_ui.style.text);
  draw_rect(bx, by, w, h, 1, g_ui.style.window_bg);
  draw_rect(bx, by, w, h, 0, g_ui.style.border);

  int changed = 0;
  int y_cursor = by + 3;
  for (int i = 0; i < item_count; ++i) {
    const int item_y = y_cursor + i * item_h;
    const bool hovered =
        ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx + 2, item_y, w - 4, item_h);
    const uint32_t id =
        ui_hash(items[i]) ^ (g_ui.window_id * 16777619u) ^
        static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8)) ^
        static_cast<uint32_t>(i);
    if (g_ui.input.pressed && hovered) {
      g_ui.active_id = id;
    }
    if (g_ui.input.released && g_ui.active_id == id) {
      if (hovered && *current != i) {
        *current = i;
        changed = 1;
      }
      g_ui.active_id = 0;
    }
    uint32_t color = g_ui.style.button_bg;
    if (*current == i) {
      color = g_ui.style.button_active;
    } else if (hovered) {
      color = g_ui.style.button_hover;
    }
    draw_rect(bx + 2, item_y, w - 4, item_h - 1, 1, color);
    draw_text(items[i] ? items[i] : "",
              bx + g_ui.style.padding,
              item_y + 3,
              bx + w - g_ui.style.padding,
              item_y + item_h,
              g_ui.style.text);
  }
  return changed;
}

int draw_ui_listbox_multi(const char* label,
                          int x,
                          int y,
                          int w,
                          int h,
                          const char* const* items,
                          int item_count,
                          uint32_t* mask) {
  if (!g_engine.cpu_ctx || !label || !items || item_count <= 0 || !mask) {
    return 0;
  }
  ui_init_style();
  if (w <= 60) {
    w = 200;
  }
  const int item_h = 22;
  if (h <= 0) {
    h = item_h * item_count + 6;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int label_x = base_x + x;
  const int label_y = base_y + y;
  const int list_y = label_y + 18;
  const int bx = base_x + x;
  const int by = list_y;
  draw_text(label, label_x, label_y, label_x + w, label_y + 16, g_ui.style.text);
  draw_rect(bx, by, w, h, 1, g_ui.style.window_bg);
  draw_rect(bx, by, w, h, 0, g_ui.style.border);

  int changed = 0;
  int y_cursor = by + 3;
  for (int i = 0; i < item_count; ++i) {
    const int item_y = y_cursor + i * item_h;
    const bool hovered =
        ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx + 2, item_y, w - 4, item_h);
    const uint32_t id =
        ui_hash(items[i]) ^ (g_ui.window_id * 16777619u) ^
        static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8)) ^
        static_cast<uint32_t>(i);
    if (g_ui.input.pressed && hovered) {
      g_ui.active_id = id;
    }
    const bool can_toggle = (i < 32);
    if (g_ui.input.released && g_ui.active_id == id) {
      if (hovered && can_toggle) {
        *mask ^= (1u << static_cast<uint32_t>(i));
        changed = 1;
      }
      g_ui.active_id = 0;
    }
    const bool selected = can_toggle && ((*mask & (1u << static_cast<uint32_t>(i))) != 0);
    uint32_t color = g_ui.style.button_bg;
    if (selected) {
      color = g_ui.style.button_active;
    } else if (hovered) {
      color = g_ui.style.button_hover;
    }
    draw_rect(bx + 2, item_y, w - 4, item_h - 1, 1, color);
    draw_text(items[i] ? items[i] : "",
              bx + g_ui.style.padding,
              item_y + 3,
              bx + w - g_ui.style.padding,
              item_y + item_h,
              g_ui.style.text);
  }
  return changed;
}

int draw_ui_scrollbar(const char* label,
                      int x,
                      int y,
                      int h,
                      int content_h,
                      int* scroll_y) {
  if (!g_engine.cpu_ctx || !label || !scroll_y) {
    return 0;
  }
  ui_init_style();
  if (h <= 0) {
    return 0;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int bx = base_x + x;
  const int by = base_y + y;
  const int bar_w = 12;
  const int max_scroll = (content_h > h) ? (content_h - h) : 0;
  if (*scroll_y < 0) {
    *scroll_y = 0;
  } else if (*scroll_y > max_scroll) {
    *scroll_y = max_scroll;
  }
  int thumb_h = h;
  if (content_h > 0) {
    const float ratio = static_cast<float>(h) / static_cast<float>(content_h);
    thumb_h = static_cast<int>(static_cast<float>(h) * ratio + 0.5f);
  }
  if (thumb_h < 20) {
    thumb_h = 20;
  }
  if (thumb_h > h) {
    thumb_h = h;
  }
  int thumb_y = by;
  if (max_scroll > 0 && h > thumb_h) {
    thumb_y = by + static_cast<int>((static_cast<float>(*scroll_y) /
                                     static_cast<float>(max_scroll)) *
                                        static_cast<float>(h - thumb_h) +
                                    0.5f);
  }

  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, thumb_y, bar_w, thumb_h);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
    g_ui.scroll_drag_off = g_ui.input.y - static_cast<float>(thumb_y);
  }
  bool changed = false;
  if (g_ui.active_id == id && g_ui.input.down) {
    float new_thumb = g_ui.input.y - g_ui.scroll_drag_off;
    if (new_thumb < static_cast<float>(by)) {
      new_thumb = static_cast<float>(by);
    }
    if (new_thumb > static_cast<float>(by + h - thumb_h)) {
      new_thumb = static_cast<float>(by + h - thumb_h);
    }
    if (h > thumb_h && max_scroll > 0) {
      const float t = (new_thumb - static_cast<float>(by)) /
                      static_cast<float>(h - thumb_h);
      const int new_scroll =
          static_cast<int>(t * static_cast<float>(max_scroll) + 0.5f);
      if (new_scroll != *scroll_y) {
        *scroll_y = new_scroll;
        changed = true;
      }
    }
  }
  if (g_ui.input.released && g_ui.active_id == id) {
    g_ui.active_id = 0;
  }

  draw_rect(bx, by, bar_w, h, 1, g_ui.style.window_bg);
  draw_rect(bx, by, bar_w, h, 0, g_ui.style.border);
  uint32_t thumb_color = g_ui.style.button_hover;
  if (g_ui.active_id == id && g_ui.input.down) {
    thumb_color = g_ui.style.button_active;
  } else if (hovered) {
    thumb_color = g_ui.style.button_hover;
  }
  draw_rect(bx + 2, thumb_y, bar_w - 4, thumb_h, 1, thumb_color);
  return changed ? 1 : 0;
}

int draw_ui_combo(const char* label,
                  int x,
                  int y,
                  int w,
                  const char* const* items,
                  int item_count,
                  int* current) {
  if (!g_engine.cpu_ctx || !label || !items || item_count <= 0 || !current) {
    return 0;
  }
  ui_init_style();
  if (w <= 60) {
    w = 200;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int label_x = base_x + x;
  const int label_y = base_y + y;
  const int box_y = label_y + 18;
  const int box_h = 26;
  const int bx = base_x + x;
  const int by = box_y;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, box_h);
  draw_text(label, label_x, label_y, label_x + w, label_y + 16, g_ui.style.text);

  if (g_ui.input.pressed && hovered) {
    if (g_ui.combo_open && g_ui.combo_id == id) {
      g_ui.combo_open = false;
      g_ui.combo_id = 0;
    } else {
      g_ui.combo_open = true;
      g_ui.combo_id = id;
    }
  }

  draw_rect(bx, by, w, box_h, 1, g_ui.style.button_bg);
  draw_rect(bx, by, w, box_h, 0, g_ui.style.border);
  const int cur_index = (*current >= 0 && *current < item_count) ? *current : 0;
  const char* cur_label = items[cur_index] ? items[cur_index] : "";
  draw_text(cur_label,
            bx + g_ui.style.padding,
            by + 4,
            bx + w - g_ui.style.padding,
            by + box_h,
            g_ui.style.text);
  draw_text("v",
            bx + w - g_ui.style.padding - 8,
            by + 4,
            bx + w,
            by + box_h,
            g_ui.style.text);

  int changed = 0;
  if (g_ui.combo_open && g_ui.combo_id == id) {
    const int item_h = 22;
    const int list_h = item_h * item_count + 6;
    const int list_x = bx;
    const int list_y = by + box_h + 4;
    draw_rect(list_x, list_y, w, list_h, 1, g_ui.style.window_bg);
    draw_rect(list_x, list_y, w, list_h, 0, g_ui.style.border);
    for (int i = 0; i < item_count; ++i) {
      const int item_y = list_y + 3 + i * item_h;
      const bool item_hover =
          ui_point_in_rect(g_ui.input.x, g_ui.input.y, list_x + 2, item_y, w - 4, item_h);
      const uint32_t item_id = id ^ static_cast<uint32_t>(i + 1);
      if (g_ui.input.pressed && item_hover) {
        g_ui.active_id = item_id;
      }
      if (g_ui.input.released && g_ui.active_id == item_id) {
        if (item_hover && *current != i) {
          *current = i;
          changed = 1;
        }
        g_ui.active_id = 0;
        g_ui.combo_open = false;
        g_ui.combo_id = 0;
      }
      uint32_t color = g_ui.style.button_bg;
      if (*current == i) {
        color = g_ui.style.button_active;
      } else if (item_hover) {
        color = g_ui.style.button_hover;
      }
      draw_rect(list_x + 2, item_y, w - 4, item_h - 1, 1, color);
      draw_text(items[i] ? items[i] : "",
                list_x + g_ui.style.padding,
                item_y + 3,
                list_x + w - g_ui.style.padding,
                item_y + item_h,
                g_ui.style.text);
    }
    if (g_ui.input.pressed) {
      const bool in_box = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, box_h);
      const bool in_list =
          ui_point_in_rect(g_ui.input.x, g_ui.input.y, list_x, list_y, w, list_h);
      if (!in_box && !in_list) {
        g_ui.combo_open = false;
        g_ui.combo_id = 0;
      }
    }
  }
  return changed;
}

int draw_ui_tabs(int x,
                 int y,
                 int w,
                 int h,
                 const char* const* labels,
                 int label_count,
                 int* current) {
  if (!g_engine.cpu_ctx || !labels || label_count <= 0 || !current) {
    return 0;
  }
  ui_init_style();
  if (h <= 0) {
    h = 26;
  }
  if (w <= 0) {
    w = label_count * 90;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int bx = base_x + x;
  const int by = base_y + y;
  int tab_w = w / label_count;
  if (tab_w < 60) {
    tab_w = 60;
  }
  int changed = 0;
  for (int i = 0; i < label_count; ++i) {
    const int tx = bx + i * tab_w;
    const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, tx, by, tab_w, h);
    const uint32_t id =
        ui_hash(labels[i] ? labels[i] : "") ^ (g_ui.window_id * 16777619u) ^
        static_cast<uint32_t>(i);
    if (g_ui.input.pressed && hovered) {
      g_ui.active_id = id;
    }
    if (g_ui.input.released && g_ui.active_id == id) {
      if (hovered && *current != i) {
        *current = i;
        changed = 1;
      }
      g_ui.active_id = 0;
    }
    uint32_t color = g_ui.style.button_bg;
    if (*current == i) {
      color = g_ui.style.button_active;
    } else if (hovered) {
      color = g_ui.style.button_hover;
    }
    draw_rect(tx, by, tab_w, h, 1, color);
    draw_rect(tx, by, tab_w, h, 0, g_ui.style.border);
    draw_text(labels[i] ? labels[i] : "",
              tx + g_ui.style.padding,
              by + 4,
              tx + tab_w - g_ui.style.padding,
              by + h,
              g_ui.style.text);
  }
  return changed;
}

int draw_ui_tree_node(const char* label, int x, int y, int* open) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !open) {
    return 0;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int bx = base_x + x;
  const int by = base_y + y;
  const int hit_w = 220;
  const int hit_h = 20;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, hit_w, hit_h);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  int changed = 0;
  if (g_ui.input.released && g_ui.active_id == id) {
    if (hovered) {
      *open = (*open == 0) ? 1 : 0;
      changed = 1;
    }
    g_ui.active_id = 0;
  }

  const int arrow_x = bx + 4;
  const int arrow_y = by + 6;
  if (*open) {
    draw_line(arrow_x, arrow_y, arrow_x + 8, arrow_y, g_ui.style.text);
    draw_line(arrow_x + 2, arrow_y + 2, arrow_x + 6, arrow_y + 6, g_ui.style.text);
    draw_line(arrow_x + 6, arrow_y + 6, arrow_x + 10, arrow_y + 2, g_ui.style.text);
  } else {
    draw_line(arrow_x, arrow_y, arrow_x + 6, arrow_y + 4, g_ui.style.text);
    draw_line(arrow_x + 6, arrow_y + 4, arrow_x, arrow_y + 8, g_ui.style.text);
  }
  draw_text(label,
            bx + 20,
            by + 2,
            bx + hit_w,
            by + hit_h,
            g_ui.style.text);
  return changed;
}

int draw_ui_color_picker_rgba(const char* label, int x, int y, uint32_t* color) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !color) {
    return 0;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int bx = base_x + x;
  const int by = base_y + y;
  const int preview = 36;

  uint32_t col = *color;
  int a = static_cast<int>((col >> 24) & 0xFF);
  int r = static_cast<int>((col >> 16) & 0xFF);
  int g = static_cast<int>((col >> 8) & 0xFF);
  int b = static_cast<int>(col & 0xFF);

  draw_text(label, bx, by, bx + 220, by + 16, g_ui.style.text);
  draw_rect(bx + 220, by, preview, preview, 1, col);
  draw_rect(bx + 220, by, preview, preview, 0, g_ui.style.border);

  char lab_r[64];
  char lab_g[64];
  char lab_b[64];
  char lab_a[64];
  snprintf(lab_r, sizeof(lab_r), "%s.R", label);
  snprintf(lab_g, sizeof(lab_g), "%s.G", label);
  snprintf(lab_b, sizeof(lab_b), "%s.B", label);
  snprintf(lab_a, sizeof(lab_a), "%s.A", label);

  int row_y = by + 18;
  bool changed = false;
  if (draw_ui_slider_int(lab_r, x, row_y, 200, 0, 255, &r)) {
    changed = true;
  }
  row_y += 42;
  if (draw_ui_slider_int(lab_g, x, row_y, 200, 0, 255, &g)) {
    changed = true;
  }
  row_y += 42;
  if (draw_ui_slider_int(lab_b, x, row_y, 200, 0, 255, &b)) {
    changed = true;
  }
  row_y += 42;
  if (draw_ui_slider_int(lab_a, x, row_y, 200, 0, 255, &a)) {
    changed = true;
  }

  if (changed) {
    *color = draw_color_rgba(static_cast<uint8_t>(r),
                             static_cast<uint8_t>(g),
                             static_cast<uint8_t>(b),
                             static_cast<uint8_t>(a));
  }
  return changed ? 1 : 0;
}

int draw_ui_checkbox(const char* label, int x, int y, int* value) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !value) {
    return 0;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int box = 20;
  const int bx = base_x + x;
  const int by = base_y + y;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, box, box);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  int toggled = 0;
  if (g_ui.input.released && g_ui.active_id == id) {
    if (hovered) {
      *value = (*value == 0) ? 1 : 0;
      toggled = 1;
    }
    g_ui.active_id = 0;
  }
  draw_rect(bx, by, box, box, 1, g_ui.style.button_bg);
  draw_rect(bx, by, box, box, 0, g_ui.style.border);
  if (*value) {
    draw_rect(bx + 4, by + 4, box - 8, box - 8, 1, g_ui.style.button_active);
  }
  draw_text(label,
            bx + box + g_ui.style.padding,
            by + 2,
            bx + box + 320,
            by + box,
            g_ui.style.text);
  return toggled;
}

static float ui_calc_slider_t(int bx, int w) {
  if (w <= 1) {
    return 0.0f;
  }
  const float t = (g_ui.input.x - static_cast<float>(bx)) / static_cast<float>(w);
  return ui_clampf(t, 0.0f, 1.0f);
}

int draw_ui_slider_int(const char* label,
                       int x,
                       int y,
                       int w,
                       int min_value,
                       int max_value,
                       int* value) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !value) {
    return 0;
  }
  ui_init_style();
  if (w <= 40) {
    w = 160;
  }
  if (min_value > max_value) {
    const int tmp = min_value;
    min_value = max_value;
    max_value = tmp;
  }
  if (*value < min_value) {
    *value = min_value;
  } else if (*value > max_value) {
    *value = max_value;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int label_x = base_x + x;
  const int label_y = base_y + y;
  const int slider_y = label_y + 18;
  const int slider_h = 20;
  const int bx = base_x + x;
  const int by = slider_y;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, slider_h);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  bool changed = false;
  if (g_ui.active_id == id && g_ui.input.down) {
    const float t = ui_calc_slider_t(bx, w);
    const int new_value =
        min_value + static_cast<int>((max_value - min_value) * t + 0.5f);
    if (new_value != *value) {
      *value = new_value;
      changed = true;
    }
  }
  if (g_ui.input.released && g_ui.active_id == id) {
    g_ui.active_id = 0;
  }

  const float t = (max_value == min_value)
                      ? 0.0f
                      : (static_cast<float>(*value - min_value) /
                         static_cast<float>(max_value - min_value));
  const int fill_w = static_cast<int>(static_cast<float>(w) * t + 0.5f);
  draw_text(label, label_x, label_y, label_x + w, label_y + 16, g_ui.style.text);
  draw_rect(bx, by, w, slider_h, 1, g_ui.style.button_bg);
  draw_rect(bx, by, fill_w, slider_h, 1, g_ui.style.button_active);
  draw_rect(bx, by, w, slider_h, 0, g_ui.style.border);
  const int knob_w = 10;
  int knob_x = bx + fill_w - knob_w / 2;
  if (knob_x < bx) {
    knob_x = bx;
  } else if (knob_x + knob_w > bx + w) {
    knob_x = bx + w - knob_w;
  }
  draw_rect(knob_x, by - 2, knob_w, slider_h + 4, 1, g_ui.style.button_hover);

  char buf[32];
  snprintf(buf, sizeof(buf), "%d", *value);
  draw_text(buf, bx + w - 48, by + 2, bx + w, by + slider_h, g_ui.style.text);
  return changed ? 1 : 0;
}

int draw_ui_slider_float(const char* label,
                         int x,
                         int y,
                         int w,
                         float min_value,
                         float max_value,
                         float* value) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !value) {
    return 0;
  }
  ui_init_style();
  if (w <= 40) {
    w = 160;
  }
  if (min_value > max_value) {
    const float tmp = min_value;
    min_value = max_value;
    max_value = tmp;
  }
  if (*value < min_value) {
    *value = min_value;
  } else if (*value > max_value) {
    *value = max_value;
  }
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int label_x = base_x + x;
  const int label_y = base_y + y;
  const int slider_y = label_y + 18;
  const int slider_h = 20;
  const int bx = base_x + x;
  const int by = slider_y;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, slider_h);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  bool changed = false;
  if (g_ui.active_id == id && g_ui.input.down) {
    const float t = ui_calc_slider_t(bx, w);
    const float new_value = min_value + (max_value - min_value) * t;
    if (fabsf(new_value - *value) > 0.0001f) {
      *value = new_value;
      changed = true;
    }
  }
  if (g_ui.input.released && g_ui.active_id == id) {
    g_ui.active_id = 0;
  }

  const float t = (max_value == min_value)
                      ? 0.0f
                      : ((*value - min_value) / (max_value - min_value));
  const int fill_w = static_cast<int>(static_cast<float>(w) * t + 0.5f);
  draw_text(label, label_x, label_y, label_x + w, label_y + 16, g_ui.style.text);
  draw_rect(bx, by, w, slider_h, 1, g_ui.style.button_bg);
  draw_rect(bx, by, fill_w, slider_h, 1, g_ui.style.button_active);
  draw_rect(bx, by, w, slider_h, 0, g_ui.style.border);
  const int knob_w = 10;
  int knob_x = bx + fill_w - knob_w / 2;
  if (knob_x < bx) {
    knob_x = bx;
  } else if (knob_x + knob_w > bx + w) {
    knob_x = bx + w - knob_w;
  }
  draw_rect(knob_x, by - 2, knob_w, slider_h + 4, 1, g_ui.style.button_hover);

  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", *value);
  draw_text(buf, bx + w - 64, by + 2, bx + w, by + slider_h, g_ui.style.text);
  return changed ? 1 : 0;
}

int draw_ui_toggle(const char* label, int x, int y, int* value) {
  if (!g_engine.cpu_ctx || !label || !label[0] || !value) {
    return 0;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  const int bx = base_x + x;
  const int by = base_y + y;
  const int w = 52;
  const int h = 24;
  const uint32_t id =
      ui_hash(label) ^ (g_ui.window_id * 16777619u) ^
      static_cast<uint32_t>((x & 0xFF) | ((y & 0xFF) << 8));
  const bool hovered = ui_point_in_rect(g_ui.input.x, g_ui.input.y, bx, by, w, h);
  if (g_ui.input.pressed && hovered) {
    g_ui.active_id = id;
  }
  int toggled = 0;
  if (g_ui.input.released && g_ui.active_id == id) {
    if (hovered) {
      *value = (*value == 0) ? 1 : 0;
      toggled = 1;
    }
    g_ui.active_id = 0;
  }
  const uint32_t bg = (*value != 0) ? g_ui.style.button_active : g_ui.style.button_bg;
  draw_rect(bx, by, w, h, 1, bg);
  draw_rect(bx, by, w, h, 0, g_ui.style.border);
  const int knob = h - 6;
  int knob_x = (*value != 0) ? (bx + w - knob - 3) : (bx + 3);
  draw_rect(knob_x, by + 3, knob, knob, 1, g_ui.style.button_hover);
  draw_text(label,
            bx + w + g_ui.style.padding,
            by + 2,
            bx + w + 320,
            by + h,
            g_ui.style.text);
  return toggled;
}

void draw_ui_progress(const char* label, int x, int y, int w, float value) {
  if (!g_engine.cpu_ctx || !label) {
    return;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  if (w <= 40) {
    w = 200;
  }
  const int bx = base_x + x;
  const int by = base_y + y;
  const int h = 18;
  float v = ui_clampf(value, 0.0f, 1.0f);
  const int fill_w = static_cast<int>(static_cast<float>(w) * v + 0.5f);
  draw_text(label, bx, by - 16, bx + w, by, g_ui.style.text);
  draw_rect(bx, by, w, h, 1, g_ui.style.button_bg);
  draw_rect(bx, by, fill_w, h, 1, g_ui.style.button_active);
  draw_rect(bx, by, w, h, 0, g_ui.style.border);
}

void draw_ui_separator(int x, int y, int w) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  ui_init_style();
  int base_x = g_ui.in_window ? g_ui.content_x : 0;
  int base_y = g_ui.in_window ? g_ui.content_y : 0;
  if (w <= 0) {
    w = g_ui.in_window ? g_ui.content_w : 200;
  }
  const int bx = base_x + x;
  const int by = base_y + y;
  draw_line(bx, by, bx + w, by, g_ui.style.border);
}

#if defined(__ANDROID__)
struct TouchReader {
  int fd;
  int max_x;
  int max_y;
  int slot;
  bool down;
  int raw_x;
  int raw_y;
};

static TouchReader g_touch_reader{ -1, 0, 0, 0, false, 0, 0 };

static bool test_abs_code(int fd, int code) {
  unsigned long bits[(ABS_MAX + 1 + (sizeof(unsigned long) * 8 - 1)) /
                     (sizeof(unsigned long) * 8)] = {};
  const int res = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
  if (res < 0) {
    return false;
  }
  const unsigned int idx = static_cast<unsigned int>(code) /
                           (sizeof(unsigned long) * 8);
  const unsigned int bit = static_cast<unsigned int>(code) %
                           (sizeof(unsigned long) * 8);
  return (bits[idx] & (1ul << bit)) != 0;
}

static bool open_touch_device(const char* path, TouchReader* out_reader) {
  if (!path || !out_reader) {
    return false;
  }
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    return false;
  }
  if (!test_abs_code(fd, ABS_MT_POSITION_X) ||
      !test_abs_code(fd, ABS_MT_POSITION_Y) ||
      !test_abs_code(fd, ABS_MT_TRACKING_ID)) {
    close(fd);
    return false;
  }
  input_absinfo abs_x{};
  input_absinfo abs_y{};
  if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) != 0 ||
      ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) != 0) {
    close(fd);
    return false;
  }
  out_reader->fd = fd;
  out_reader->max_x = abs_x.maximum > 0 ? abs_x.maximum : 1;
  out_reader->max_y = abs_y.maximum > 0 ? abs_y.maximum : 1;
  out_reader->slot = 0;
  out_reader->down = false;
  out_reader->raw_x = 0;
  out_reader->raw_y = 0;
  return true;
}

static bool map_touch_to_logical(int raw_x,
                                 int raw_y,
                                 int max_x,
                                 int max_y,
                                 float* out_x,
                                 float* out_y) {
  const int logical_w = draw_screen_width();
  const int logical_h = draw_screen_height();
  if (logical_w <= 0 || logical_h <= 0 || max_x <= 0 || max_y <= 0) {
    return false;
  }
  int phys_w = logical_w;
  int phys_h = logical_h;
  if (g_engine.rotation == 90 || g_engine.rotation == 270) {
    phys_w = logical_h;
    phys_h = logical_w;
  }
  const float sx = static_cast<float>(raw_x) / static_cast<float>(max_x);
  const float sy = static_cast<float>(raw_y) / static_cast<float>(max_y);
  float px = sx * static_cast<float>(phys_w);
  float py = sy * static_cast<float>(phys_h);
  float lx = px;
  float ly = py;
  switch (g_engine.rotation) {
    case 90:
      lx = py;
      ly = static_cast<float>(phys_w) - 1.0f - px;
      break;
    case 180:
      lx = static_cast<float>(phys_w) - 1.0f - px;
      ly = static_cast<float>(phys_h) - 1.0f - py;
      break;
    case 270:
      lx = static_cast<float>(phys_h) - 1.0f - py;
      ly = px;
      break;
    default:
      break;
  }
  if (lx < 0.0f) {
    lx = 0.0f;
  }
  if (ly < 0.0f) {
    ly = 0.0f;
  }
  if (lx > static_cast<float>(logical_w - 1)) {
    lx = static_cast<float>(logical_w - 1);
  }
  if (ly > static_cast<float>(logical_h - 1)) {
    ly = static_cast<float>(logical_h - 1);
  }
  *out_x = lx;
  *out_y = ly;
  return true;
}
#endif

int draw_ui_touch_open(void) {
#if !defined(__ANDROID__)
  return DRAW_ENGINE_EFAILED;
#else
  if (g_touch_reader.fd >= 0) {
    return DRAW_ENGINE_OK;
  }
  const char* env = getenv("DRAW_UI_TOUCH_EVENT");
  if (env && env[0]) {
    char path[64];
    snprintf(path, sizeof(path), "/dev/input/event%d", atoi(env));
    if (open_touch_device(path, &g_touch_reader)) {
      return DRAW_ENGINE_OK;
    }
  }
  DIR* dir = opendir("/dev/input");
  if (!dir) {
    return DRAW_ENGINE_EFAILED;
  }
  dirent* ent = nullptr;
  while ((ent = readdir(dir)) != nullptr) {
    if (strncmp(ent->d_name, "event", 5) != 0) {
      continue;
    }
    char path[128];
    snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
    if (open_touch_device(path, &g_touch_reader)) {
      closedir(dir);
      return DRAW_ENGINE_OK;
    }
  }
  closedir(dir);
  return DRAW_ENGINE_EFAILED;
#endif
}

void draw_ui_touch_close(void) {
#if defined(__ANDROID__)
  if (g_touch_reader.fd >= 0) {
    close(g_touch_reader.fd);
    g_touch_reader.fd = -1;
  }
#endif
}

int draw_ui_touch_poll(void) {
#if !defined(__ANDROID__)
  return DRAW_ENGINE_EFAILED;
#else
  if (g_touch_reader.fd < 0) {
    return DRAW_ENGINE_ENOTINIT;
  }
  input_event events[32];
  const ssize_t n = read(g_touch_reader.fd, events, sizeof(events));
  if (n <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return DRAW_ENGINE_EFAILED;
  }
  if (n % static_cast<ssize_t>(sizeof(input_event)) != 0) {
    return 0;
  }
  bool updated = false;
  const size_t count = static_cast<size_t>(n) / sizeof(input_event);
  for (size_t i = 0; i < count; ++i) {
    const input_event& ev = events[i];
    if (ev.type == EV_ABS) {
      if (ev.code == ABS_MT_SLOT) {
        g_touch_reader.slot = ev.value;
      } else if (ev.code == ABS_MT_TRACKING_ID) {
        if (g_touch_reader.slot == 0) {
          g_touch_reader.down = (ev.value != -1);
          updated = true;
        }
      } else if (ev.code == ABS_MT_POSITION_X) {
        if (g_touch_reader.slot == 0) {
          g_touch_reader.raw_x = ev.value;
          updated = true;
        }
      } else if (ev.code == ABS_MT_POSITION_Y) {
        if (g_touch_reader.slot == 0) {
          g_touch_reader.raw_y = ev.value;
          updated = true;
        }
      }
    } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
      g_touch_reader.down = (ev.value != 0);
      updated = true;
    }
  }
  if (!updated) {
    return 0;
  }
  float lx = 0.0f;
  float ly = 0.0f;
  if (!map_touch_to_logical(g_touch_reader.raw_x,
                            g_touch_reader.raw_y,
                            g_touch_reader.max_x,
                            g_touch_reader.max_y,
                            &lx,
                            &ly)) {
    return DRAW_ENGINE_EFAILED;
  }
  draw_ui_set_touch(g_touch_reader.down ? 1 : 0, lx, ly);
  return 1;
#endif
}

int draw_ui_ime_set_context(const DrawUiImeConfig* config) {
#if !defined(__ANDROID__)
  (void)config;
  return DRAW_ENGINE_EFAILED;
#else
  if (!config || !config->java_vm || !config->context) {
    return DRAW_ENGINE_EINVAL;
  }
  JavaVM* vm = reinterpret_cast<JavaVM*>(config->java_vm);
  JNIEnv* env = reinterpret_cast<JNIEnv*>(config->jni_env);
  bool attached = false;
  if (!env) {
    env = ui_ime_get_env(&attached);
  }
  if (!env) {
    return DRAW_ENGINE_EFAILED;
  }
  if (g_ime.context) {
    env->DeleteGlobalRef(g_ime.context);
    g_ime.context = nullptr;
  }
  if (g_ime.view) {
    env->DeleteGlobalRef(g_ime.view);
    g_ime.view = nullptr;
  }
  g_ime.context = env->NewGlobalRef(reinterpret_cast<jobject>(config->context));
  if (config->view) {
    g_ime.view = env->NewGlobalRef(reinterpret_cast<jobject>(config->view));
  }
  g_ime.vm = vm;
  g_ime.ready = (g_ime.context != nullptr);
  ui_ime_detach(attached);
  return g_ime.ready ? DRAW_ENGINE_OK : DRAW_ENGINE_EFAILED;
#endif
}

int draw_ui_ime_show(int show) {
#if !defined(__ANDROID__)
  (void)show;
  return DRAW_ENGINE_EFAILED;
#else
  if (!g_ime.ready || !g_ime.vm || !g_ime.context) {
    return DRAW_ENGINE_ENOTINIT;
  }
  bool attached = false;
  JNIEnv* env = ui_ime_get_env(&attached);
  if (!env) {
    return DRAW_ENGINE_EFAILED;
  }
  jclass context_cls = env->GetObjectClass(g_ime.context);
  if (!context_cls) {
    ui_ime_detach(attached);
    return DRAW_ENGINE_EFAILED;
  }
  jmethodID get_service =
      env->GetMethodID(context_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  if (!get_service) {
    env->DeleteLocalRef(context_cls);
    ui_ime_detach(attached);
    return DRAW_ENGINE_EFAILED;
  }
  jstring service_name = env->NewStringUTF("input_method");
  jobject imm = env->CallObjectMethod(g_ime.context, get_service, service_name);
  env->DeleteLocalRef(service_name);
  env->DeleteLocalRef(context_cls);
  if (!imm) {
    ui_ime_detach(attached);
    return DRAW_ENGINE_EFAILED;
  }
  jclass imm_cls = env->GetObjectClass(imm);
  if (!imm_cls) {
    env->DeleteLocalRef(imm);
    ui_ime_detach(attached);
    return DRAW_ENGINE_EFAILED;
  }

  if (show) {
    if (g_ime.view) {
      jmethodID show_soft_input =
          env->GetMethodID(imm_cls, "showSoftInput", "(Landroid/view/View;I)Z");
      if (show_soft_input) {
        env->CallBooleanMethod(imm, show_soft_input, g_ime.view, 0);
      }
    } else {
      jmethodID toggle =
          env->GetMethodID(imm_cls, "toggleSoftInput", "(II)V");
      if (toggle) {
        env->CallVoidMethod(imm, toggle, 2, 0);
      }
    }
  } else {
    if (g_ime.view) {
      jclass view_cls = env->GetObjectClass(g_ime.view);
      if (view_cls) {
        jmethodID get_token =
            env->GetMethodID(view_cls, "getWindowToken", "()Landroid/os/IBinder;");
        jobject token = nullptr;
        if (get_token) {
          token = env->CallObjectMethod(g_ime.view, get_token);
        }
        jmethodID hide_soft_input =
            env->GetMethodID(imm_cls, "hideSoftInputFromWindow",
                             "(Landroid/os/IBinder;I)Z");
        if (hide_soft_input && token) {
          env->CallBooleanMethod(imm, hide_soft_input, token, 0);
        }
        if (token) {
          env->DeleteLocalRef(token);
        }
        env->DeleteLocalRef(view_cls);
      }
    } else {
      jmethodID toggle =
          env->GetMethodID(imm_cls, "toggleSoftInput", "(II)V");
      if (toggle) {
        env->CallVoidMethod(imm, toggle, 0, 0);
      }
    }
  }

  env->DeleteLocalRef(imm_cls);
  env->DeleteLocalRef(imm);
  ui_ime_detach(attached);
  return DRAW_ENGINE_OK;
#endif
}

static BackendType choose_best_backend() {
  if (DRAW_ENGINE_HAS_VULKAN && has_library("libvulkan.so")) {
    return BACKEND_VULKAN;
  }
  if (DRAW_ENGINE_HAS_GLES &&
      (has_library("libGLESv3.so") || has_library("libGLESv2.so"))) {
    return BACKEND_GLES;
  }
  return BACKEND_CPU;
}

static MidrawContext* active_cpu_ctx() {
  if (g_engine.hybrid && g_engine.overlay_ctx) {
    if (!g_engine.overlay_locked) {
      if (midraw_lock(g_engine.overlay_ctx) != 0) {
        return nullptr;
      }
      g_engine.overlay_locked = true;
    }
    return g_engine.overlay_ctx;
  }
  return g_engine.cpu_ctx;
}

static bool hybrid_use_cpu(GpuCmdType type) {
  if (!g_engine.hybrid || !g_engine.overlay_ctx) {
    return false;
  }
  if (g_engine.hybrid_sensitive_only && !g_engine.sensitive) {
    return false;
  }
  const uint32_t mask = g_engine.hybrid_cpu_mask;
  switch (type) {
    case GpuCmdType::Text:
      return (mask & DRAW_ENGINE_HYBRID_CPU_TEXT) != 0;
    case GpuCmdType::Line:
      return (mask & DRAW_ENGINE_HYBRID_CPU_LINE) != 0;
    case GpuCmdType::Circle:
      return (mask & DRAW_ENGINE_HYBRID_CPU_CIRCLE) != 0;
    case GpuCmdType::Rect:
      return (mask & DRAW_ENGINE_HYBRID_CPU_RECT) != 0;
    case GpuCmdType::Image:
      return (mask & DRAW_ENGINE_HYBRID_CPU_IMAGE) != 0;
    default:
      break;
  }
  return false;
}

static bool hybrid_secure_enabled() {
  if (!g_engine.hybrid) {
    return false;
  }
  return env_int("DRAW_ENGINE_HYBRID_SECURE", 1) != 0;
}

static bool should_scale_cpu() {
  return env_int("DRAW_ENGINE_SCALE_CPU", 0) != 0;
}

static uint32_t lcg(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

static int sample_count_to_int(VkSampleCountFlagBits samples) {
  switch (samples) {
    case VK_SAMPLE_COUNT_2_BIT:
      return 2;
    case VK_SAMPLE_COUNT_4_BIT:
      return 4;
    case VK_SAMPLE_COUNT_8_BIT:
      return 8;
    case VK_SAMPLE_COUNT_16_BIT:
      return 16;
    default:
      return 1;
  }
}

static VkSampleCountFlagBits choose_sample_count(VkSampleCountFlags flags, int request) {
  if (request >= 16 && (flags & VK_SAMPLE_COUNT_16_BIT)) {
    return VK_SAMPLE_COUNT_16_BIT;
  }
  if (request >= 8 && (flags & VK_SAMPLE_COUNT_8_BIT)) {
    return VK_SAMPLE_COUNT_8_BIT;
  }
  if (request >= 4 && (flags & VK_SAMPLE_COUNT_4_BIT)) {
    return VK_SAMPLE_COUNT_4_BIT;
  }
  if (request >= 2 && (flags & VK_SAMPLE_COUNT_2_BIT)) {
    return VK_SAMPLE_COUNT_2_BIT;
  }
  return VK_SAMPLE_COUNT_1_BIT;
}

static void shuffle_name(char* name) {
  if (!name) {
    return;
  }
  const size_t len = strlen(name);
  if (len <= 1) {
    return;
  }
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint32_t seed = static_cast<uint32_t>(ts.tv_nsec) ^ static_cast<uint32_t>(ts.tv_sec);
  for (size_t i = len - 1; i > 0; --i) {
    size_t j = static_cast<size_t>(lcg(seed) % static_cast<uint32_t>(i + 1));
    char tmp = name[i];
    name[i] = name[j];
    name[j] = tmp;
  }
}

static int env_int(const char* name, int default_value) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return default_value;
  }
  return atoi(value);
}

static float env_float(const char* name, float default_value) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return default_value;
  }
  return static_cast<float>(atof(value));
}

static bool env_present(const char* name) {
  const char* value = getenv(name);
  return value && value[0];
}

static bool debug_timing_enabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = env_int("DRAW_ENGINE_DEBUG_TIMING", 0) != 0 ? 1 : 0;
  }
  return cached != 0;
}

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static inline uint64_t fps_to_frame_ns(float fps) {
  if (fps <= 0.0f) {
    return 0;
  }
  return static_cast<uint64_t>(1000000000.0 / fps);
}

static float update_auto_fps(uint64_t draw_ns) {
  if (draw_ns == 0) {
    if (g_engine.auto_fps > 0.0f) {
      return g_engine.auto_fps;
    }
    return 60.0f;
  }
  const double sample = static_cast<double>(draw_ns);
  if (g_engine.avg_draw_ns <= 0.0) {
    g_engine.avg_draw_ns = sample;
  } else {
    g_engine.avg_draw_ns = g_engine.avg_draw_ns * 0.9 + sample * 0.1;
  }

  float target_cpu = env_float("DRAW_ENGINE_AUTO_CPU_UTIL", 0.6f);
  target_cpu = clampf(target_cpu, 0.2f, 0.9f);
  int min_fps = env_int("DRAW_ENGINE_AUTO_MIN_FPS", 45);
  int max_fps = env_int("DRAW_ENGINE_AUTO_MAX_FPS", 120);
  if (min_fps < 10) {
    min_fps = 10;
  }
  if (max_fps < min_fps) {
    max_fps = min_fps;
  }

  const double ideal_fps = (g_engine.avg_draw_ns > 0.0)
                               ? (1000000000.0 * target_cpu / g_engine.avg_draw_ns)
                               : static_cast<double>(max_fps);
  float target = static_cast<float>(ideal_fps);
  if (target > static_cast<float>(max_fps)) {
    target = static_cast<float>(max_fps);
  }
  const double min_frame_ns = 1000000000.0 / static_cast<double>(min_fps);
  if (g_engine.avg_draw_ns <= min_frame_ns && target < static_cast<float>(min_fps)) {
    target = static_cast<float>(min_fps);
  }
  if (target < 10.0f) {
    target = 10.0f;
  }

  if (g_engine.auto_fps <= 0.0f) {
    g_engine.auto_fps = target;
  } else {
    g_engine.auto_fps = g_engine.auto_fps * 0.85f + target * 0.15f;
  }
  return g_engine.auto_fps;
}

static void init_hybrid_policy() {
  g_engine.hybrid_cpu_mask = DRAW_ENGINE_HYBRID_CPU_TEXT;
  g_engine.hybrid_sensitive_only = true;
  g_engine.sensitive = env_int("DRAW_ENGINE_SENSITIVE_DEFAULT", 0) != 0;

  const char* profile = getenv("DRAW_ENGINE_HYBRID_PROFILE");
  if (profile && profile[0]) {
    if (strcmp(profile, "secure_text") == 0) {
      g_engine.hybrid_cpu_mask = DRAW_ENGINE_HYBRID_CPU_TEXT;
      g_engine.hybrid_sensitive_only = true;
    } else if (strcmp(profile, "secure_ui") == 0) {
      g_engine.hybrid_cpu_mask = DRAW_ENGINE_HYBRID_CPU_TEXT |
                                 DRAW_ENGINE_HYBRID_CPU_LINE |
                                 DRAW_ENGINE_HYBRID_CPU_CIRCLE;
      g_engine.hybrid_sensitive_only = true;
    } else if (strcmp(profile, "balanced") == 0) {
      g_engine.hybrid_cpu_mask = DRAW_ENGINE_HYBRID_CPU_TEXT;
      g_engine.hybrid_sensitive_only = false;
    } else if (strcmp(profile, "gpu_first") == 0) {
      g_engine.hybrid_cpu_mask = 0;
      g_engine.hybrid_sensitive_only = true;
    }
  }

  const int mask_env = env_int("DRAW_ENGINE_HYBRID_CPU_MASK", -1);
  if (mask_env >= 0) {
    g_engine.hybrid_cpu_mask = static_cast<uint32_t>(mask_env);
  }
  const int sensitive_env = env_int("DRAW_ENGINE_HYBRID_SENSITIVE_ONLY", -1);
  if (sensitive_env >= 0) {
    g_engine.hybrid_sensitive_only = (sensitive_env != 0);
  }
}

static bool get_display_logical(MidrawContext* ctx, int* out_w, int* out_h) {
  if (!ctx) {
    return false;
  }
  int rot = 0;
  int w = 0;
  int h = 0;
  if (midraw_display_rotation(ctx, &rot, &w, &h) != 0) {
    return false;
  }
  int logical_w = w;
  int logical_h = h;
  if (w > 0 && h > 0) {
    const int long_side = (w > h) ? w : h;
    const int short_side = (w > h) ? h : w;
    const bool rot_landscape = (rot == 90 || rot == 270);
    const bool size_landscape = w >= h;
    if (rot_landscape != size_landscape) {
      logical_w = long_side;
      logical_h = short_side;
    }
  }
  if (out_w) {
    *out_w = logical_w;
  }
  if (out_h) {
    *out_h = logical_h;
  }
  return logical_w > 0 && logical_h > 0;
}

static void compute_render_target(int base_w, int base_h, int* out_w, int* out_h) {
  int target_w = base_w;
  int target_h = base_h;
  if (g_engine.render_width > 0 && g_engine.render_height > 0) {
    target_w = g_engine.render_width;
    target_h = g_engine.render_height;
  } else if (g_engine.render_scale > 0.0f && g_engine.render_scale < 0.999f &&
             base_w > 0 && base_h > 0) {
    target_w = static_cast<int>(base_w * g_engine.render_scale + 0.5f);
    target_h = static_cast<int>(base_h * g_engine.render_scale + 0.5f);
  }
  if (out_w) {
    *out_w = target_w;
  }
  if (out_h) {
    *out_h = target_h;
  }
}

static void apply_render_target() {
  MidrawContext* base_ctx = g_engine.cpu_ctx ? g_engine.cpu_ctx : g_engine.overlay_ctx;
  int base_w = 0;
  int base_h = 0;
  if (!get_display_logical(base_ctx, &base_w, &base_h)) {
    if (base_ctx) {
      base_w = midraw_logical_width(base_ctx);
      base_h = midraw_logical_height(base_ctx);
    }
  }
  if (base_w <= 0 || base_h <= 0) {
    return;
  }
  int target_w = base_w;
  int target_h = base_h;
  compute_render_target(base_w, base_h, &target_w, &target_h);
  if (target_w <= 0 || target_h <= 0) {
    return;
  }
  if (g_engine.backend == BACKEND_CPU) {
    MidrawContext* ctx = active_cpu_ctx();
    if (ctx) {
      midraw_resize(ctx, target_w, target_h);
    }
    return;
  }
  if (g_engine.cpu_ctx) {
    midraw_resize(g_engine.cpu_ctx, target_w, target_h);
  }
  if (g_engine.hybrid && g_engine.overlay_ctx && should_scale_cpu()) {
    midraw_resize(g_engine.overlay_ctx, target_w, target_h);
  }
}

static void apply_quality_level(int level) {
  struct QualityEntry {
    float scale;
    int segments;
  };
  static const QualityEntry kTable[] = {
      {0.5f, 24},
      {0.7f, 32},
      {0.85f, 48},
      {1.0f, 64},
  };
  if (level < 0) {
    level = 0;
  } else if (level > 3) {
    level = 3;
  }
  if (!g_engine.render_scale_manual) {
    g_engine.render_scale = kTable[level].scale;
  }
  if (!g_engine.circle_segments_manual) {
    g_engine.circle_segments = kTable[level].segments;
  }
  apply_render_target();
  if (g_engine.backend != BACKEND_CPU) {
    gpu_refresh_size_and_rotation();
  }
}

static void maybe_adjust_quality(uint64_t draw_ns) {
  if (g_engine.auto_quality == 0) {
    return;
  }
  if (g_engine.backend == BACKEND_CPU) {
    return;
  }
  if (g_engine.render_size_manual || g_engine.render_scale_manual) {
    return;
  }
  const int target_fps = (g_engine.target_fps > 0) ? g_engine.target_fps : 60;
  const uint64_t target_ns = fps_to_frame_ns(static_cast<float>(target_fps));
  if (target_ns == 0) {
    return;
  }
  if (g_engine.avg_draw_ns <= 0.0) {
    g_engine.avg_draw_ns = static_cast<double>(draw_ns);
  } else {
    g_engine.avg_draw_ns = g_engine.avg_draw_ns * 0.9 +
                           static_cast<double>(draw_ns) * 0.1;
  }
  const uint64_t now = now_ns();
  if (g_engine.last_quality_ns == 0) {
    g_engine.last_quality_ns = now;
    return;
  }
  if (now - g_engine.last_quality_ns < 1000000000ull) {
    return;
  }
  const double avg = g_engine.avg_draw_ns;
  bool changed = false;
  if (avg > static_cast<double>(target_ns) * 1.10 &&
      g_engine.quality_level > g_engine.quality_min) {
    g_engine.quality_level -= 1;
    changed = true;
  } else if (avg < static_cast<double>(target_ns) * 0.75 &&
             g_engine.quality_level < g_engine.quality_max) {
    g_engine.quality_level += 1;
    changed = true;
  }
  if (changed) {
    apply_quality_level(g_engine.quality_level);
  }
  g_engine.last_quality_ns = now;
}

static uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

static void init_sleep_bias() {
  g_engine.sleep_bias_ns = 0;
  g_engine.sleep_bias_enabled = env_int("DRAW_ENGINE_SLEEP_BIAS", 1) != 0;
  int max_us = env_int("DRAW_ENGINE_SLEEP_BIAS_MAX_US", 2000);
  if (max_us < 0) {
    max_us = 0;
  } else if (max_us > 10000) {
    max_us = 10000;
  }
  g_engine.sleep_bias_max_ns = static_cast<uint64_t>(max_us) * 1000ull;
}

static bool sleep_bias_active() {
  if (!g_engine.sleep_bias_enabled) {
    return false;
  }
  if (g_engine.backend == BACKEND_CPU || g_engine.hybrid) {
    return false;
  }
  return g_engine.sleep_bias_max_ns > 0;
}

static uint64_t apply_sleep_bias(uint64_t deadline_ns, uint64_t now_ns) {
  if (!sleep_bias_active()) {
    return deadline_ns;
  }
  const uint64_t bias = g_engine.sleep_bias_ns;
  if (bias == 0) {
    return deadline_ns;
  }
  if (deadline_ns <= now_ns + 1000ull) {
    return deadline_ns;
  }
  if (deadline_ns > bias + now_ns) {
    return deadline_ns - bias;
  }
  return now_ns + 1;
}

static void update_sleep_bias(uint64_t deadline_ns, uint64_t woke_ns) {
  if (!sleep_bias_active()) {
    return;
  }
  if (woke_ns > deadline_ns) {
    const uint64_t overshoot = woke_ns - deadline_ns;
    uint64_t delta = overshoot / 2;
    if (delta < 1000ull) {
      delta = 1000ull;
    }
    if (g_engine.sleep_bias_ns + delta >= g_engine.sleep_bias_max_ns) {
      g_engine.sleep_bias_ns = g_engine.sleep_bias_max_ns;
    } else {
      g_engine.sleep_bias_ns += delta;
    }
  } else if (g_engine.sleep_bias_ns > 0) {
    g_engine.sleep_bias_ns = (g_engine.sleep_bias_ns * 9) / 10;
  }
}

static bool precise_sleep_enabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = env_int("DRAW_ENGINE_PRECISE_SLEEP", 1) != 0 ? 1 : 0;
  }
  return cached != 0;
}

static uint64_t spin_threshold_ns() {
  static int cached = -1;
  if (cached < 0) {
    cached = env_int("DRAW_ENGINE_SPIN_US", 200);
    if (cached < 0) {
      cached = 0;
    }
  }
  return static_cast<uint64_t>(cached) * 1000ull;
}

static void sleep_until_ns(uint64_t deadline_ns) {
  if (deadline_ns == 0) {
    return;
  }
  const uint64_t spin_ns = spin_threshold_ns();
  for (;;) {
    const uint64_t now = now_ns();
    if (now >= deadline_ns) {
      return;
    }
    const uint64_t remaining = deadline_ns - now;
    if (remaining > spin_ns + 1000000ull) {
      const uint64_t sleep_ns = remaining - spin_ns;
      timespec ts{};
      ts.tv_sec = static_cast<time_t>(sleep_ns / 1000000000ull);
      ts.tv_nsec = static_cast<long>(sleep_ns % 1000000000ull);
      nanosleep(&ts, nullptr);
    } else {
      while (now_ns() < deadline_ns) {
      }
      return;
    }
  }
}

static bool query_native_window_size(ANativeWindow* window, int* out_w, int* out_h) {
  using PFN_GetWidth = int32_t (*)(ANativeWindow*);
  using PFN_GetHeight = int32_t (*)(ANativeWindow*);
  static void* libandroid = nullptr;
  static PFN_GetWidth get_width = nullptr;
  static PFN_GetHeight get_height = nullptr;
  if (!libandroid) {
    libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
      get_width = reinterpret_cast<PFN_GetWidth>(dlsym(libandroid, "ANativeWindow_getWidth"));
      get_height = reinterpret_cast<PFN_GetHeight>(dlsym(libandroid, "ANativeWindow_getHeight"));
    }
  }
  if (!window || !get_width || !get_height) {
    return false;
  }
  const int w = get_width(window);
  const int h = get_height(window);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (out_w) {
    *out_w = w;
  }
  if (out_h) {
    *out_h = h;
  }
  return true;
}

static void apply_window_frame_rate(ANativeWindow* window, float fps) {
#if defined(__ANDROID__)
  if (!window || fps <= 0.0f) {
    return;
  }
  static int cached = -1;
  if (cached < 0) {
    cached = env_int("DRAW_ENGINE_SET_FRAME_RATE", 1) != 0 ? 1 : 0;
  }
  if (cached == 0) {
    return;
  }
  using PFN_SetFrameRate = int (*)(ANativeWindow*, float, int8_t);
  using PFN_SetFrameRateStrategy = int (*)(ANativeWindow*, float, int8_t, int8_t);
  static void* libandroid = nullptr;
  static PFN_SetFrameRate set_rate = nullptr;
  static PFN_SetFrameRateStrategy set_rate_strategy = nullptr;
  static bool tried = false;
  if (!tried) {
    libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
      set_rate = reinterpret_cast<PFN_SetFrameRate>(
          dlsym(libandroid, "ANativeWindow_setFrameRate"));
      set_rate_strategy = reinterpret_cast<PFN_SetFrameRateStrategy>(
          dlsym(libandroid, "ANativeWindow_setFrameRateWithChangeStrategy"));
    }
    tried = true;
  }
  if (!set_rate && !set_rate_strategy) {
    return;
  }
#ifndef ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE
#define ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE 1
#endif
#ifndef ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS
#define ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS 0
#endif
#ifndef ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS
#define ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS 1
#endif
  const int8_t compat = ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE;
  const int strategy_env = env_int("DRAW_ENGINE_FRAME_RATE_STRATEGY", 0);
  const int8_t strategy = (strategy_env != 0) ? ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS
                                             : ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS;
  if (set_rate_strategy) {
    set_rate_strategy(window, fps, compat, strategy);
  } else if (set_rate) {
    set_rate(window, fps, compat);
  }
#else
  (void)window;
  (void)fps;
#endif
}

static float query_native_window_refresh_rate(ANativeWindow* window) {
  using PFN_GetRefreshRate = float (*)(ANativeWindow*);
  static void* libandroid = nullptr;
  static PFN_GetRefreshRate get_refresh = nullptr;
  static bool tried = false;
  if (!tried) {
    libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
      get_refresh =
          reinterpret_cast<PFN_GetRefreshRate>(dlsym(libandroid, "ANativeWindow_getRefreshRate"));
    }
    tried = true;
  }
  if (!window || !get_refresh) {
    return 0.0f;
  }
  return get_refresh(window);
}

static float query_choreographer_refresh_rate() {
  using PFN_GetChoreo = AChoreographer* (*)();
  using PFN_GetRefresh = float (*)(AChoreographer*);
  static void* libandroid = nullptr;
  static PFN_GetChoreo get_choreo = nullptr;
  static PFN_GetRefresh get_refresh = nullptr;
  static bool tried = false;
  if (!tried) {
    libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
      get_choreo = reinterpret_cast<PFN_GetChoreo>(dlsym(libandroid, "AChoreographer_getInstance"));
      get_refresh =
          reinterpret_cast<PFN_GetRefresh>(dlsym(libandroid, "AChoreographer_getRefreshRate"));
    }
    tried = true;
  }
  if (!get_choreo || !get_refresh) {
    return 0.0f;
  }
  AChoreographer* choreo = get_choreo();
  if (!choreo) {
    return 0.0f;
  }
  return get_refresh(choreo);
}

static int sanitize_display_fps(float rate) {
  if (rate < 1.0f) {
    return 0;
  }
  int fps = static_cast<int>(rate + 0.5f);
  if (fps < 30) {
    return 0;
  }
  if (fps > 1000) {
    fps = 1000;
  }
  return fps;
}

static void maybe_apply_default_gpu_fps() {
  if (g_engine.backend == BACKEND_CPU || g_engine.hybrid) {
    return;
  }
  if (g_engine.fps_manual || g_engine.target_fps > 0) {
    return;
  }
  const float window_rate = query_native_window_refresh_rate(g_gpu.window);
  int fps = sanitize_display_fps(window_rate);
  if (fps <= 0) {
    const float choreo_rate = query_choreographer_refresh_rate();
    fps = sanitize_display_fps(choreo_rate);
  }
  if (fps <= 0) {
    fps = 60;
  }
  g_engine.target_fps = fps;
  apply_window_frame_rate(g_gpu.window, static_cast<float>(fps));
}

static bool set_native_window_geometry(ANativeWindow* window, int width, int height) {
  using PFN_SetGeometry = int32_t (*)(ANativeWindow*, int32_t, int32_t, int32_t);
  static void* libandroid = nullptr;
  static PFN_SetGeometry set_geometry = nullptr;
  if (!libandroid) {
    libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
      set_geometry =
          reinterpret_cast<PFN_SetGeometry>(dlsym(libandroid, "ANativeWindow_setBuffersGeometry"));
    }
  }
  if (!window || !set_geometry) {
    return false;
  }
  const int32_t kFormat = 1;  // WINDOW_FORMAT_RGBA_8888
  return set_geometry(window, width, height, kFormat) == 0;
}

static bool set_native_window_transform(ANativeWindow* window, int32_t transform) {
  using PFN_SetTransform = int32_t (*)(ANativeWindow*, int32_t);
  static void* libandroid = nullptr;
  static PFN_SetTransform set_transform = nullptr;
  if (!libandroid) {
    libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (libandroid) {
      set_transform = reinterpret_cast<PFN_SetTransform>(
          dlsym(libandroid, "ANativeWindow_setBuffersTransform"));
    }
  }
  if (!window || !set_transform) {
    return false;
  }
  return set_transform(window, transform) == 0;
}

static void warn_no_frame() {
  static bool warned = false;
  if (!warned) {
    fprintf(stderr, "draw_engine: draw_begin_frame() not called\n");
    warned = true;
  }
}

static inline uint32_t color_to_rgba(uint32_t argb) {
  const uint8_t a = static_cast<uint8_t>((argb >> 24) & 0xFF);
  const uint8_t r = static_cast<uint8_t>((argb >> 16) & 0xFF);
  const uint8_t g = static_cast<uint8_t>((argb >> 8) & 0xFF);
  const uint8_t b = static_cast<uint8_t>(argb & 0xFF);
  return (static_cast<uint32_t>(a) << 24) |
         (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(g) << 8) |
         static_cast<uint32_t>(r);
}

uint32_t draw_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return (static_cast<uint32_t>(a) << 24) |
         (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) |
         static_cast<uint32_t>(b);
}

static void mat4_identity(float* out) {
  for (int i = 0; i < 16; ++i) {
    out[i] = 0.0f;
  }
  out[0] = out[5] = out[10] = out[15] = 1.0f;
}

static void mat4_ortho(float* out, float left, float right, float bottom, float top) {
  mat4_identity(out);
  out[0] = 2.0f / (right - left);
  out[5] = 2.0f / (top - bottom);
  out[10] = -1.0f;
  out[12] = -(right + left) / (right - left);
  out[13] = -(top + bottom) / (top - bottom);
}

static void mat4_perspective(float* out, float fovy, float aspect, float znear, float zfar) {
  const float f = 1.0f / tanf(fovy * 0.5f);
  for (int i = 0; i < 16; ++i) {
    out[i] = 0.0f;
  }
  out[0] = f / aspect;
  out[5] = f;
  out[10] = (zfar + znear) / (znear - zfar);
  out[11] = -1.0f;
  out[14] = (2.0f * zfar * znear) / (znear - zfar);
}

static void mat4_mul(float* out, const float* a, const float* b) {
  float r[16];
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      r[row * 4 + col] =
          a[row * 4 + 0] * b[0 * 4 + col] +
          a[row * 4 + 1] * b[1 * 4 + col] +
          a[row * 4 + 2] * b[2 * 4 + col] +
          a[row * 4 + 3] * b[3 * 4 + col];
    }
  }
  memcpy(out, r, sizeof(r));
}

static void mat4_translate(float* out, float x, float y, float z) {
  mat4_identity(out);
  out[12] = x;
  out[13] = y;
  out[14] = z;
}

static void mat4_rotate_y(float* out, float angle) {
  mat4_identity(out);
  const float c = cosf(angle);
  const float s = sinf(angle);
  out[0] = c;
  out[2] = s;
  out[8] = -s;
  out[10] = c;
}

static const uint8_t kFont8x8Digits[10][8] = {
    {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00}, // 0
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 1
    {0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00}, // 2
    {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00}, // 3
    {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00}, // 4
    {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00}, // 5
    {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}, // 6
    {0x7E, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x00}, // 7
    {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00}, // 8
    {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00}  // 9
};

static const uint8_t kFont8x8Upper[26][8] = {
    {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00}, // A
    {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}, // B
    {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}, // C
    {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}, // D
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00}, // E
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}, // F
    {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00}, // G
    {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // H
    {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // I
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}, // J
    {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}, // K
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}, // M
    {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00}, // N
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // O
    {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}, // P
    {0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x0E, 0x00}, // Q
    {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00}, // R
    {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}, // S
    {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // T
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}, // X
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00}, // Y
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}  // Z
};

static const uint8_t kFont8x8Space[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kFont8x8Colon[8] = {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00};

static const uint8_t* font_for_char(char c) {
  if (c >= '0' && c <= '9') {
    return kFont8x8Digits[c - '0'];
  }
  if (c >= 'A' && c <= 'Z') {
    return kFont8x8Upper[c - 'A'];
  }
  if (c >= 'a' && c <= 'z') {
    return kFont8x8Upper[c - 'a'];
  }
  if (c == ':') {
    return kFont8x8Colon;
  }
  return kFont8x8Space;
}

static bool load_file_bytes(const char* path, std::vector<unsigned char>& out) {
  if (!path || !path[0]) {
    return false;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  if (size <= 0) {
    fclose(file);
    return false;
  }
  fseek(file, 0, SEEK_SET);
  out.resize(static_cast<size_t>(size));
  size_t read = fread(out.data(), 1, out.size(), file);
  fclose(file);
  return read == out.size();
}

static bool init_gpu_font_truetype(GpuState& gpu,
                                   const char* path,
                                   int pixel_height,
                                   int atlas_w,
                                   int atlas_h) {
  std::vector<unsigned char> ttf;
  if (!load_file_bytes(path, ttf)) {
    return false;
  }
  const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
  if (offset < 0) {
    return false;
  }
  stbtt_fontinfo font{};
  if (!stbtt_InitFont(&font, ttf.data(), offset)) {
    return false;
  }
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
  const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(pixel_height));
  gpu.font.ascent = ascent * scale;
  gpu.font.line_advance = (ascent - descent + line_gap) * scale;
  gpu.font.truetype = true;
  gpu.font.sdf = true;

  const int first_char = 32;
  const int glyph_count = 96;
  const int cols = 16;
  const int rows = 6;
  int padding = env_int("DRAW_ENGINE_SDF_PADDING", 4);
  if (padding < 1) {
    padding = 1;
  }
  int onedge = env_int("DRAW_ENGINE_SDF_ONEDGE", 128);
  if (onedge <= 0 || onedge > 255) {
    onedge = 128;
  }
  float dist_scale = env_float("DRAW_ENGINE_SDF_SCALE", 64.0f);
  if (dist_scale <= 0.0f) {
    dist_scale = 64.0f;
  }

  struct SdfGlyph {
    int w;
    int h;
    int xoff;
    int yoff;
    float xadvance;
    unsigned char* data;
  };

  std::vector<SdfGlyph> glyphs(static_cast<size_t>(glyph_count));
  int max_w = 0;
  int max_h = 0;
  for (int i = 0; i < glyph_count; ++i) {
    const int code = first_char + i;
    int advance = 0;
    int lsb = 0;
    stbtt_GetCodepointHMetrics(&font, code, &advance, &lsb);
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(&font,
                                               scale,
                                               code,
                                               padding,
                                               onedge,
                                               dist_scale,
                                               &w,
                                               &h,
                                               &xoff,
                                               &yoff);
    glyphs[static_cast<size_t>(i)] = {w, h, xoff, yoff, advance * scale, sdf};
    if (w > max_w) {
      max_w = w;
    }
    if (h > max_h) {
      max_h = h;
    }
  }

  int cell_w = max_w + 2;
  int cell_h = max_h + 2;
  if (cell_w < 1) {
    cell_w = 1;
  }
  if (cell_h < 1) {
    cell_h = 1;
  }
  int target_w = atlas_w;
  int target_h = atlas_h;
  const int needed_w = cols * cell_w;
  const int needed_h = rows * cell_h;
  if (target_w < needed_w) {
    target_w = needed_w;
  }
  if (target_h < needed_h) {
    target_h = needed_h;
  }

  unsigned char* rgba = static_cast<unsigned char*>(malloc(target_w * target_h * 4));
  if (!rgba) {
    for (auto& g : glyphs) {
      if (g.data) {
        free(g.data);
      }
    }
    return false;
  }
  memset(rgba, 0, static_cast<size_t>(target_w * target_h * 4));

  for (int i = 0; i < glyph_count; ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int x0 = col * cell_w;
    const int y0 = row * cell_h;
    const SdfGlyph& g = glyphs[static_cast<size_t>(i)];
    if (g.data && g.w > 0 && g.h > 0) {
      for (int y = 0; y < g.h; ++y) {
        const unsigned char* src = g.data + y * g.w;
        unsigned char* dst = rgba + ((y0 + y) * target_w + x0) * 4;
        for (int x = 0; x < g.w; ++x) {
          const unsigned char a = src[x];
          dst[x * 4 + 0] = 255;
          dst[x * 4 + 1] = 255;
          dst[x * 4 + 2] = 255;
          dst[x * 4 + 3] = a;
        }
      }
    }
    gpu.font.u0[i] = static_cast<float>(x0) / static_cast<float>(target_w);
    gpu.font.v0[i] = static_cast<float>(y0) / static_cast<float>(target_h);
    gpu.font.u1[i] = static_cast<float>(x0 + g.w) / static_cast<float>(target_w);
    gpu.font.v1[i] = static_cast<float>(y0 + g.h) / static_cast<float>(target_h);
    gpu.font.xoff[i] = static_cast<float>(g.xoff);
    gpu.font.yoff[i] = static_cast<float>(g.yoff);
    gpu.font.xadvance[i] = g.xadvance;
    gpu.font.glyph_w[i] = static_cast<float>(g.w);
    gpu.font.glyph_h[i] = static_cast<float>(g.h);
  }

  for (auto& g : glyphs) {
    if (g.data) {
      free(g.data);
    }
  }

  gpu.font.width = target_w;
  gpu.font.height = target_h;
  gpu.font.ready = true;

  gpu.font_image.width = target_w;
  gpu.font_image.height = target_h;
  gpu.font_image.rgba = rgba;
  gpu.font_image.pixels = nullptr;
  fprintf(stderr, "GPU font: %s size=%d atlas=%dx%d SDF\n",
          path, pixel_height, target_w, target_h);
  return true;
}

static void init_gpu_font(GpuState& gpu) {
  if (gpu.font.ready) {
    return;
  }
  const char* env_path = getenv("DRAW_ENGINE_FONT_PATH");
  if (!env_path || !env_path[0]) {
    env_path = getenv("MIDRAW_FONT_PATH");
  }
  const int pixel_height = env_int("DRAW_ENGINE_FONT_SIZE", 24);
  const int atlas_w = env_int("DRAW_ENGINE_FONT_ATLAS", 1024);
  const int atlas_h = env_int("DRAW_ENGINE_FONT_ATLAS", 1024);
  if (env_path && env_path[0]) {
    if (init_gpu_font_truetype(gpu, env_path, pixel_height, atlas_w, atlas_h)) {
      return;
    }
  }
  const char* fallback_paths[] = {
      "/system/fonts/Roboto-Regular.ttf",
      "/system/fonts/RobotoStatic-Regular.ttf",
      "/system/fonts/NotoSansCJK-Regular.ttc",
      "/system/fonts/NotoSans-Regular.ttf",
      nullptr,
  };
  for (int i = 0; fallback_paths[i]; ++i) {
    if (init_gpu_font_truetype(gpu, fallback_paths[i], pixel_height, atlas_w, atlas_h)) {
      return;
    }
  }
  fprintf(stderr, "GPU font: fallback 8x8\n");

  const int glyph_w = 8;
  const int glyph_h = 8;
  const int cols = 16;
  const int rows = 6;
  const int font_w = cols * glyph_w;
  const int font_h = rows * glyph_h;

  unsigned char* rgba = static_cast<unsigned char*>(malloc(font_w * font_h * 4));
  if (!rgba) {
    return;
  }
  memset(rgba, 0, font_w * font_h * 4);

  const int first_char = 32;
  for (int i = 0; i < 96; ++i) {
    const int code = first_char + i;
    const int col = i % cols;
    const int row = i / cols;
    const int x0 = col * glyph_w;
    const int y0 = row * glyph_h;
    const uint8_t* glyph = font_for_char(static_cast<char>(code));
    for (int y = 0; y < glyph_h; ++y) {
      const uint8_t bits = glyph[y];
      for (int x = 0; x < glyph_w; ++x) {
        if (!(bits & (1u << (7 - x)))) {
          continue;
        }
        const int px = x0 + x;
        const int py = y0 + y;
        unsigned char* dst = rgba + (py * font_w + px) * 4;
        dst[0] = 255;
        dst[1] = 255;
        dst[2] = 255;
        dst[3] = 255;
      }
    }
    gpu.font.u0[i] = static_cast<float>(x0) / static_cast<float>(font_w);
    gpu.font.v0[i] = static_cast<float>(y0) / static_cast<float>(font_h);
    gpu.font.u1[i] = static_cast<float>(x0 + glyph_w) / static_cast<float>(font_w);
    gpu.font.v1[i] = static_cast<float>(y0 + glyph_h) / static_cast<float>(font_h);
    gpu.font.xoff[i] = 0.0f;
    gpu.font.yoff[i] = 0.0f;
    gpu.font.xadvance[i] = static_cast<float>(glyph_w);
    gpu.font.glyph_w[i] = static_cast<float>(glyph_w);
    gpu.font.glyph_h[i] = static_cast<float>(glyph_h);
  }

  gpu.font.width = font_w;
  gpu.font.height = font_h;
  gpu.font.ascent = static_cast<float>(glyph_h);
  gpu.font.line_advance = static_cast<float>(glyph_h);
  gpu.font.truetype = false;
  gpu.font.sdf = false;
  gpu.font.ready = true;

  gpu.font_image.width = font_w;
  gpu.font_image.height = font_h;
  gpu.font_image.rgba = rgba;
  gpu.font_image.pixels = nullptr;
}

static void gpu_reserve(GpuState& gpu);

static void gpu_reset_frame(GpuState& gpu) {
  gpu_reserve(gpu);
  gpu.commands.clear();
  gpu.vertices.clear();
  gpu.batches.clear();
  gpu.text_instances.clear();
  gpu.text_offset = 0;
}

static void gpu_reserve(GpuState& gpu) {
  int expected = env_int("DRAW_ENGINE_EXPECT_SHAPES", 300);
  if (expected < 0) {
    expected = 0;
  }
  int segments = g_engine.circle_segments > 0 ? g_engine.circle_segments : 64;
  int reserve_vertices = env_int("DRAW_ENGINE_RESERVE_VERTICES", 0);
  if (reserve_vertices <= 0) {
    int worst = expected * (segments * 18);
    if (worst < 32768) {
      worst = 32768;
    } else if (worst > 262144) {
      worst = 262144;
    }
    reserve_vertices = worst;
  }
  int reserve_commands = env_int("DRAW_ENGINE_RESERVE_COMMANDS", 0);
  if (reserve_commands <= 0) {
    reserve_commands = expected * 2 + 256;
  }
  int reserve_batches = env_int("DRAW_ENGINE_RESERVE_BATCHES", 0);
  if (reserve_batches <= 0) {
    reserve_batches = expected + 64;
  }
  int reserve_text = env_int("DRAW_ENGINE_RESERVE_TEXT_INST", 0);
  if (reserve_text <= 0) {
    reserve_text = expected * 12;
    if (reserve_text < 256) {
      reserve_text = 256;
    } else if (reserve_text > 65536) {
      reserve_text = 65536;
    }
  }
  if (gpu.vertices.capacity() < static_cast<size_t>(reserve_vertices)) {
    gpu.vertices.reserve(static_cast<size_t>(reserve_vertices));
  }
  if (gpu.commands.capacity() < static_cast<size_t>(reserve_commands)) {
    gpu.commands.reserve(static_cast<size_t>(reserve_commands));
  }
  if (gpu.batches.capacity() < static_cast<size_t>(reserve_batches)) {
    gpu.batches.reserve(static_cast<size_t>(reserve_batches));
  }
  if (gpu.text_instances.capacity() < static_cast<size_t>(reserve_text)) {
    gpu.text_instances.reserve(static_cast<size_t>(reserve_text));
  }
}

static void ensure_circle_lut(GpuState& gpu, int segments) {
  if (segments < 8) {
    segments = 8;
  }
  if (gpu.circle_segments == segments && !gpu.circle_lut.empty()) {
    return;
  }
  gpu.circle_segments = segments;
  gpu.circle_lut.resize(static_cast<size_t>(segments * 2));
  const float step = (2.0f * 3.1415926f) / static_cast<float>(segments);
  for (int i = 0; i < segments; ++i) {
    const float a = step * static_cast<float>(i);
    gpu.circle_lut[static_cast<size_t>(i * 2 + 0)] = cosf(a);
    gpu.circle_lut[static_cast<size_t>(i * 2 + 1)] = sinf(a);
  }
}

static void gpu_text_cache_init(GpuState& gpu) {
  if (gpu.text_cache_limit != 0 || gpu.text_cache_max_entries != 0) {
    return;
  }
  int limit = env_int("DRAW_ENGINE_TEXT_CACHE_BYTES", 512 * 1024);
  int max_entries = env_int("DRAW_ENGINE_TEXT_CACHE_ENTRIES", 64);
  if (limit < 0) {
    limit = 0;
  }
  if (max_entries < 0) {
    max_entries = 0;
  }
  gpu.text_cache_limit = static_cast<size_t>(limit);
  gpu.text_cache_max_entries = max_entries;
  gpu.text_cache_bytes = 0;
}

static size_t text_entry_bytes(const CachedText& entry) {
  return entry.vertices.size() * sizeof(CachedTextVertex) +
         entry.instances.size() * sizeof(TextInstance) +
         entry.text.size() + 1;
}

static void gpu_text_cache_evict(GpuState& gpu, size_t needed_bytes) {
  if (gpu.text_cache_limit == 0 || gpu.text_cache_max_entries == 0) {
    return;
  }
  while ((!gpu.text_cache.empty()) &&
         (gpu.text_cache_bytes + needed_bytes > gpu.text_cache_limit ||
          static_cast<int>(gpu.text_cache.size()) >= gpu.text_cache_max_entries)) {
    size_t oldest_index = 0;
    uint64_t oldest_frame = gpu.text_cache[0].last_used;
    for (size_t i = 1; i < gpu.text_cache.size(); ++i) {
      if (gpu.text_cache[i].last_used < oldest_frame) {
        oldest_frame = gpu.text_cache[i].last_used;
        oldest_index = i;
      }
    }
    gpu.text_cache_bytes -= text_entry_bytes(gpu.text_cache[oldest_index]);
    gpu.text_cache.erase(gpu.text_cache.begin() + static_cast<long>(oldest_index));
  }
}

static CachedText* gpu_text_cache_find(GpuState& gpu,
                                       const char* text,
                                       int x0,
                                       int y0,
                                       int x1,
                                       int y1,
                                       uint32_t color,
                                       bool instanced,
                                       bool sdf) {
  if (!text || gpu.text_cache.empty()) {
    return nullptr;
  }
  for (auto& entry : gpu.text_cache) {
    if (entry.color != color ||
        entry.x0 != x0 || entry.y0 != y0 || entry.x1 != x1 || entry.y1 != y1) {
      continue;
    }
    if (entry.font_w != gpu.font.width || entry.font_h != gpu.font.height ||
        entry.font_truetype != gpu.font.truetype ||
        entry.font_ascent != gpu.font.ascent ||
        entry.font_line != gpu.font.line_advance) {
      continue;
    }
    if (entry.font_sdf != sdf || entry.instanced != instanced) {
      continue;
    }
    if (entry.text == text) {
      return &entry;
    }
  }
  return nullptr;
}

static void gpu_add_batch(GpuState& gpu,
                          int first,
                          int count,
                          const DrawImage* texture,
                          bool line,
                          bool use_3d) {
  if (!gpu.batches.empty()) {
    GpuBatch& last = gpu.batches.back();
    if (last.line == line &&
        last.use_3d == use_3d &&
        last.use_texture == (texture != nullptr) &&
        last.texture == texture &&
        (last.first + last.count) == first) {
      last.count += count;
      return;
    }
  }
  GpuBatch batch{};
  batch.first = first;
  batch.count = count;
  batch.use_texture = texture != nullptr;
  batch.use_3d = use_3d;
  batch.line = line;
  batch.texture = texture;
  gpu.batches.push_back(batch);
}

static inline void map_logical_to_physical(const GpuState& gpu,
                                           float x,
                                           float y,
                                           float* out_x,
                                           float* out_y) {
  switch (gpu.rotation) {
    case 90:
      *out_x = static_cast<float>(gpu.physical_width - 1) - y;
      *out_y = x;
      break;
    case 180:
      *out_x = static_cast<float>(gpu.physical_width - 1) - x;
      *out_y = static_cast<float>(gpu.physical_height - 1) - y;
      break;
    case 270:
      *out_x = y;
      *out_y = static_cast<float>(gpu.physical_height - 1) - x;
      break;
    default:
      *out_x = x;
      *out_y = y;
      break;
  }
}

static inline void gpu_map_point(const GpuState& gpu,
                                 float x,
                                 float y,
                                 float* out_x,
                                 float* out_y) {
  float sx = x * gpu.scale_x;
  float sy = y * gpu.scale_y;
  if (gpu.rotation != 0) {
    map_logical_to_physical(gpu, sx, sy, out_x, out_y);
  } else {
    *out_x = sx;
    *out_y = sy;
  }
}

static inline void gpu_map_rect(const GpuState& gpu,
                                float x,
                                float y,
                                float w,
                                float h,
                                float* out_x,
                                float* out_y,
                                float* out_w,
                                float* out_h) {
  float x0 = x;
  float y0 = y;
  float x1 = x + w;
  float y1 = y + h;
  float px0 = 0.0f;
  float py0 = 0.0f;
  float px1 = 0.0f;
  float py1 = 0.0f;
  gpu_map_point(gpu, x0, y0, &px0, &py0);
  gpu_map_point(gpu, x1, y1, &px1, &py1);
  const float min_x = (px0 < px1) ? px0 : px1;
  const float max_x = (px0 > px1) ? px0 : px1;
  const float min_y = (py0 < py1) ? py0 : py1;
  const float max_y = (py0 > py1) ? py0 : py1;
  *out_x = min_x;
  *out_y = min_y;
  *out_w = max_x - min_x;
  *out_h = max_y - min_y;
}

static void gpu_push_vertex(GpuState& gpu,
                            float x,
                            float y,
                            float z,
                            float u,
                            float v,
                            uint32_t color,
                            bool map2d) {
  GpuVertex vert{};
  float sx = x;
  float sy = y;
  if (map2d) {
    sx *= gpu.scale_x;
    sy *= gpu.scale_y;
  }
  if (map2d && gpu.rotation != 0) {
    float px = 0.0f;
    float py = 0.0f;
    map_logical_to_physical(gpu, sx, sy, &px, &py);
    vert.x = px;
    vert.y = py;
  } else {
    vert.x = sx;
    vert.y = sy;
  }
  vert.z = z;
  vert.u = u;
  vert.v = v;
  vert.color = color_to_rgba(color);
  gpu.vertices.push_back(vert);
}

static inline uint32_t color_scale_alpha(uint32_t color, float alpha_mul) {
  if (alpha_mul >= 0.999f) {
    return color;
  }
  if (alpha_mul <= 0.0f) {
    return color & 0x00FFFFFFu;
  }
  uint32_t a = (color >> 24) & 0xFFu;
  uint32_t scaled = static_cast<uint32_t>(static_cast<float>(a) * alpha_mul + 0.5f);
  if (scaled > 255u) {
    scaled = 255u;
  }
  return (color & 0x00FFFFFFu) | (scaled << 24);
}

static inline void gpu_push_quad(GpuState& gpu,
                                 float x0,
                                 float y0,
                                 float x1,
                                 float y1,
                                 float x2,
                                 float y2,
                                 float x3,
                                 float y3,
                                 uint32_t c0,
                                 uint32_t c1,
                                 uint32_t c2,
                                 uint32_t c3) {
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, c0, true);
  gpu_push_vertex(gpu, x1, y1, 0.0f, 0.0f, 0.0f, c1, true);
  gpu_push_vertex(gpu, x2, y2, 0.0f, 0.0f, 0.0f, c2, true);
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, c0, true);
  gpu_push_vertex(gpu, x2, y2, 0.0f, 0.0f, 0.0f, c2, true);
  gpu_push_vertex(gpu, x3, y3, 0.0f, 0.0f, 0.0f, c3, true);
}

static void gpu_push_line(GpuState& gpu, int x1, int y1, int x2, int y2, uint32_t color);

static void gpu_push_rect(GpuState& gpu,
                          int x,
                          int y,
                          int w,
                          int h,
                          uint32_t color,
                          bool filled) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const float x0 = static_cast<float>(x);
  const float y0 = static_cast<float>(y);
  const float x1 = static_cast<float>(x + w);
  const float y1 = static_cast<float>(y + h);
  const uint32_t col = color;
  const DrawImage* tex = &gpu.white_image;

  if (filled) {
    const int first = static_cast<int>(gpu.vertices.size());
    gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y0, 0.0f, 1.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y1, 0.0f, 1.0f, 1.0f, col, true);
    gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y1, 0.0f, 1.0f, 1.0f, col, true);
    gpu_push_vertex(gpu, x0, y1, 0.0f, 0.0f, 1.0f, col, true);
    gpu_add_batch(gpu, first, 6, tex, false, false);
  } else {
    gpu_push_line(gpu, static_cast<int>(x0), static_cast<int>(y0),
                  static_cast<int>(x1), static_cast<int>(y0), col);
    gpu_push_line(gpu, static_cast<int>(x1), static_cast<int>(y0),
                  static_cast<int>(x1), static_cast<int>(y1), col);
    gpu_push_line(gpu, static_cast<int>(x1), static_cast<int>(y1),
                  static_cast<int>(x0), static_cast<int>(y1), col);
    gpu_push_line(gpu, static_cast<int>(x0), static_cast<int>(y1),
                  static_cast<int>(x0), static_cast<int>(y0), col);
  }
}

static void gpu_push_line(GpuState& gpu,
                          int x1,
                          int y1,
                          int x2,
                          int y2,
                          uint32_t color) {
  static int cached_aa = -1;
  if (cached_aa < 0) {
    cached_aa = env_int("DRAW_ENGINE_LINE_AA", 1);
  }
  const float thickness = env_float("DRAW_ENGINE_LINE_WIDTH", 1.0f);
  const float aa = (cached_aa != 0) ? env_float("DRAW_ENGINE_AA_SIZE", 1.0f) : 0.0f;
  const float fx1 = static_cast<float>(x1);
  const float fy1 = static_cast<float>(y1);
  const float fx2 = static_cast<float>(x2);
  const float fy2 = static_cast<float>(y2);
  const float dx = fx2 - fx1;
  const float dy = fy2 - fy1;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len <= 0.0001f) {
    return;
  }
  const float inv = 1.0f / len;
  const float tx = dx * inv;
  const float ty = dy * inv;
  const float nx = -dy * inv;
  const float ny = dx * inv;
  const float half = thickness * 0.5f;
  const float outer = half + aa;

  const float inx = nx * half;
  const float iny = ny * half;
  const float outx = nx * outer;
  const float outy = ny * outer;
  const float capx = tx * aa;
  const float capy = ty * aa;

  const float i1x = fx1 + inx;
  const float i1y = fy1 + iny;
  const float i2x = fx2 + inx;
  const float i2y = fy2 + iny;
  const float i3x = fx2 - inx;
  const float i3y = fy2 - iny;
  const float i4x = fx1 - inx;
  const float i4y = fy1 - iny;

  const float o1x = fx1 + outx;
  const float o1y = fy1 + outy;
  const float o2x = fx2 + outx;
  const float o2y = fy2 + outy;
  const float o3x = fx2 - outx;
  const float o3y = fy2 - outy;
  const float o4x = fx1 - outx;
  const float o4y = fy1 - outy;

  const uint32_t col_inner = color;
  const uint32_t col_outer = (aa > 0.0f) ? color_scale_alpha(color, 0.0f) : color;

  const int first = static_cast<int>(gpu.vertices.size());
  gpu_push_quad(gpu, i1x, i1y, i2x, i2y, i3x, i3y, i4x, i4y,
                col_inner, col_inner, col_inner, col_inner);

  if (aa > 0.0f) {
    gpu_push_quad(gpu, o1x, o1y, o2x, o2y, i2x, i2y, i1x, i1y,
                  col_outer, col_outer, col_inner, col_inner);
    gpu_push_quad(gpu, i4x, i4y, i3x, i3y, o3x, o3y, o4x, o4y,
                  col_inner, col_inner, col_outer, col_outer);

    const float so1x = o1x - capx;
    const float so1y = o1y - capy;
    const float so4x = o4x - capx;
    const float so4y = o4y - capy;
    gpu_push_quad(gpu, so4x, so4y, so1x, so1y, i1x, i1y, i4x, i4y,
                  col_outer, col_outer, col_inner, col_inner);

    const float eo2x = o2x + capx;
    const float eo2y = o2y + capy;
    const float eo3x = o3x + capx;
    const float eo3y = o3y + capy;
    gpu_push_quad(gpu, i3x, i3y, i2x, i2y, eo2x, eo2y, eo3x, eo3y,
                  col_inner, col_inner, col_outer, col_outer);
  }

  const int count = static_cast<int>(gpu.vertices.size()) - first;
  if (count > 0) {
    gpu_add_batch(gpu, first, count, &gpu.white_image, false, false);
  }
}

static inline int gpu_circle_segments_for_radius(int radius) {
  int max_segments = g_engine.circle_segments;
  if (max_segments <= 0) {
    max_segments = env_int("DRAW_ENGINE_CIRCLE_SEGMENTS", 64);
  }
  if (max_segments < 8) {
    max_segments = 8;
  }
  int min_segments = env_int("DRAW_ENGINE_CIRCLE_MIN_SEGMENTS", 12);
  if (min_segments < 8) {
    min_segments = 8;
  }
  int auto_segments = static_cast<int>(radius * 0.5f) + min_segments;
  if (auto_segments < min_segments) {
    auto_segments = min_segments;
  }
  if (auto_segments > max_segments) {
    auto_segments = max_segments;
  }
  return auto_segments;
}

static inline void gpu_push_ring_segment(GpuState& gpu,
                                         float cx,
                                         float cy,
                                         float r0,
                                         float r1,
                                         float cos0,
                                         float sin0,
                                         float cos1,
                                         float sin1,
                                         uint32_t col0,
                                         uint32_t col1) {
  if (r1 <= r0) {
    return;
  }
  const float x0 = cx + cos0 * r0;
  const float y0 = cy + sin0 * r0;
  const float x1 = cx + cos1 * r0;
  const float y1 = cy + sin1 * r0;
  const float x2 = cx + cos1 * r1;
  const float y2 = cy + sin1 * r1;
  const float x3 = cx + cos0 * r1;
  const float y3 = cy + sin0 * r1;
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, col0, true);
  gpu_push_vertex(gpu, x1, y1, 0.0f, 0.0f, 0.0f, col0, true);
  gpu_push_vertex(gpu, x2, y2, 0.0f, 0.0f, 0.0f, col1, true);
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, col0, true);
  gpu_push_vertex(gpu, x2, y2, 0.0f, 0.0f, 0.0f, col1, true);
  gpu_push_vertex(gpu, x3, y3, 0.0f, 0.0f, 0.0f, col1, true);
}

static void gpu_push_circle(GpuState& gpu, int cx, int cy, int radius, uint32_t color) {
  if (radius <= 0) {
    return;
  }
  const float thickness = env_float("DRAW_ENGINE_CIRCLE_WIDTH", 1.0f);
  const float aa = env_float("DRAW_ENGINE_AA_SIZE", 1.0f);
  int segments = gpu_circle_segments_for_radius(radius);
  ensure_circle_lut(gpu, segments);
  const int first = static_cast<int>(gpu.vertices.size());
  const float half = thickness * 0.5f;
  float r_inner = static_cast<float>(radius) - half;
  float r_outer = static_cast<float>(radius) + half;
  float r_inner_aa = r_inner - aa;
  float r_outer_aa = r_outer + aa;
  if (r_inner < 0.0f) {
    r_inner = 0.0f;
  }
  if (r_inner_aa < 0.0f) {
    r_inner_aa = 0.0f;
  }
  if (r_outer < 0.0f) {
    r_outer = 0.0f;
  }
  if (r_outer_aa < r_outer) {
    r_outer_aa = r_outer;
  }

  const uint32_t col_inner = color;
  const uint32_t col_outer = color_scale_alpha(color, 0.0f);

  for (int i = 0; i < segments; ++i) {
    const int j = (i + 1) < segments ? (i + 1) : 0;
    const float cos0 = gpu.circle_lut[static_cast<size_t>(i * 2 + 0)];
    const float sin0 = gpu.circle_lut[static_cast<size_t>(i * 2 + 1)];
    const float cos1 = gpu.circle_lut[static_cast<size_t>(j * 2 + 0)];
    const float sin1 = gpu.circle_lut[static_cast<size_t>(j * 2 + 1)];

    if (aa > 0.0f && r_inner > 0.0f) {
      gpu_push_ring_segment(gpu,
                            static_cast<float>(cx),
                            static_cast<float>(cy),
                            r_inner_aa,
                            r_inner,
                            cos0,
                            sin0,
                            cos1,
                            sin1,
                            col_outer,
                            col_inner);
    }

    gpu_push_ring_segment(gpu,
                          static_cast<float>(cx),
                          static_cast<float>(cy),
                          r_inner,
                          r_outer,
                          cos0,
                          sin0,
                          cos1,
                          sin1,
                          col_inner,
                          col_inner);

    if (aa > 0.0f) {
      gpu_push_ring_segment(gpu,
                            static_cast<float>(cx),
                            static_cast<float>(cy),
                            r_outer,
                            r_outer_aa,
                            cos0,
                            sin0,
                            cos1,
                            sin1,
                            col_inner,
                            col_outer);
    }
  }
  const int count = static_cast<int>(gpu.vertices.size()) - first;
  if (count > 0) {
    gpu_add_batch(gpu, first, count, &gpu.white_image, false, false);
  }
}

static void gpu_push_line3d(GpuState& gpu,
                            float x1,
                            float y1,
                            float z1,
                            float x2,
                            float y2,
                            float z2,
                            uint32_t color) {
  const int first = static_cast<int>(gpu.vertices.size());
  gpu_push_vertex(gpu, x1, y1, z1, 0.0f, 0.0f, color, false);
  gpu_push_vertex(gpu, x2, y2, z2, 0.0f, 0.0f, color, false);
  gpu_add_batch(gpu, first, 2, &gpu.white_image, true, true);
}

static void gpu_push_text(GpuState& gpu,
                          const char* text,
                          int x0,
                          int y0,
                          int x1,
                          int y1,
                          uint32_t color) {
  if (!text || !gpu.font.ready) {
    return;
  }
  gpu_text_cache_init(gpu);
  const char* text_ptr = text;
  const bool use_cache =
      (gpu.text_cache_limit > 0 && gpu.text_cache_max_entries > 0);
  static int cached_instancing = -1;
  if (cached_instancing < 0) {
    cached_instancing = env_int("DRAW_ENGINE_TEXT_INSTANCE", 1);
  }
  const bool instanced = (cached_instancing != 0);
  const float v_offset = gpu.font.sdf ? 0.0f : 2.0f;
  const uint32_t color_rgba = color_to_rgba(color);
  const int clip_x0 = x0;
  const int clip_y0 = y0;
  const int clip_x1 = (x1 > x0) ? x1 : 0;
  const int clip_y1 = (y1 > y0) ? y1 : 0;

  if (use_cache) {
    CachedText* cached =
        gpu_text_cache_find(gpu, text_ptr, x0, y0, x1, y1, color, instanced, gpu.font.sdf);
    if (cached) {
      if (instanced) {
        if (!cached->instances.empty()) {
          gpu.text_instances.insert(gpu.text_instances.end(),
                                    cached->instances.begin(),
                                    cached->instances.end());
          cached->last_used = gpu.frame_index;
          return;
        }
      } else if (!cached->vertices.empty()) {
        const int first = static_cast<int>(gpu.vertices.size());
        for (const auto& v : cached->vertices) {
          gpu_push_vertex(gpu, v.x, v.y, v.z, v.u, v.v, v.color, true);
        }
        const int count = static_cast<int>(gpu.vertices.size()) - first;
        if (count > 0) {
          gpu_add_batch(gpu, first, count, &gpu.font_image, false, false);
        }
        cached->last_used = gpu.frame_index;
        return;
      }
    }
  }

  if (instanced) {
    std::vector<TextInstance> cached_instances;
    if (use_cache) {
      cached_instances.reserve(strlen(text_ptr));
    }
    float cursor_x = static_cast<float>(x0);
    float cursor_y = static_cast<float>(y0);
    float baseline = cursor_y + gpu.font.ascent;
    const int first_char = 32;
    const int last_char = 127;

    while (*text) {
      char c = *text++;
      if (c == '\n') {
        cursor_x = static_cast<float>(x0);
        cursor_y += gpu.font.line_advance;
        baseline = cursor_y + gpu.font.ascent;
        continue;
      }
      if (c < first_char || c > last_char) {
        cursor_x += gpu.font.xadvance[0];
        continue;
      }
      int idx = c - first_char;
      const float u0 = gpu.font.u0[idx];
      const float v0 = gpu.font.v0[idx];
      const float u1 = gpu.font.u1[idx];
      const float v1 = gpu.font.v1[idx];

      const float gw = gpu.font.glyph_w[idx];
      const float gh = gpu.font.glyph_h[idx];
      if (gw <= 0.0f || gh <= 0.0f) {
        cursor_x += gpu.font.xadvance[idx];
        continue;
      }
      const float gx0 = cursor_x + gpu.font.xoff[idx];
      const float gy0 = baseline + gpu.font.yoff[idx];
      const float gx1 = gx0 + gw;
      const float gy1 = gy0 + gh;
      if (clip_x1 > clip_x0 && clip_y1 > clip_y0) {
        if (gx1 <= static_cast<float>(clip_x0) ||
            gx0 >= static_cast<float>(clip_x1) ||
            gy1 <= static_cast<float>(clip_y0) ||
            gy0 >= static_cast<float>(clip_y1)) {
          cursor_x += gpu.font.xadvance[idx];
          continue;
        }
      }

      float rx = 0.0f;
      float ry = 0.0f;
      float rw = 0.0f;
      float rh = 0.0f;
      gpu_map_rect(gpu, gx0, gy0, gw, gh, &rx, &ry, &rw, &rh);

      TextInstance inst{};
      inst.x = rx;
      inst.y = ry;
      inst.w = rw;
      inst.h = rh;
      inst.u0 = u0;
      inst.v0 = v0 + v_offset;
      inst.u1 = u1;
      inst.v1 = v1 + v_offset;
      inst.color = color_rgba;
      gpu.text_instances.push_back(inst);
      if (use_cache) {
        cached_instances.push_back(inst);
      }
      cursor_x += gpu.font.xadvance[idx];
    }

    if (use_cache && !cached_instances.empty()) {
      CachedText entry{};
      entry.text = text_ptr;
      entry.x0 = x0;
      entry.y0 = y0;
      entry.x1 = x1;
      entry.y1 = y1;
      entry.color = color;
      entry.font_w = gpu.font.width;
      entry.font_h = gpu.font.height;
      entry.font_ascent = gpu.font.ascent;
      entry.font_line = gpu.font.line_advance;
      entry.font_truetype = gpu.font.truetype;
      entry.font_sdf = gpu.font.sdf;
      entry.instanced = true;
      entry.last_used = gpu.frame_index;
      entry.instances.swap(cached_instances);
      const size_t bytes = text_entry_bytes(entry);
      if (bytes <= gpu.text_cache_limit) {
        gpu_text_cache_evict(gpu, bytes);
        if (gpu.text_cache_bytes + bytes <= gpu.text_cache_limit &&
            static_cast<int>(gpu.text_cache.size()) < gpu.text_cache_max_entries) {
          gpu.text_cache_bytes += bytes;
          gpu.text_cache.push_back(std::move(entry));
        }
      }
    }
    return;
  }

  std::vector<CachedTextVertex> cached_vertices;
  if (use_cache) {
    cached_vertices.reserve(strlen(text_ptr) * 6);
  }

  auto emit = [&](float x, float y, float u, float v) {
    gpu_push_vertex(gpu, x, y, 0.0f, u, v + v_offset, color, true);
    if (use_cache) {
      CachedTextVertex vtx{};
      vtx.x = x;
      vtx.y = y;
      vtx.z = 0.0f;
      vtx.u = u;
      vtx.v = v + v_offset;
      vtx.color = color;
      cached_vertices.push_back(vtx);
    }
  };

  float cursor_x = static_cast<float>(x0);
  float cursor_y = static_cast<float>(y0);
  float baseline = cursor_y + gpu.font.ascent;
  const int first_char = 32;
  const int last_char = 127;
  const int first = static_cast<int>(gpu.vertices.size());

  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = static_cast<float>(x0);
      cursor_y += gpu.font.line_advance;
      baseline = cursor_y + gpu.font.ascent;
      continue;
    }
    if (c < first_char || c > last_char) {
      cursor_x += gpu.font.xadvance[0];
      continue;
    }
    int idx = c - first_char;
    const float u0 = gpu.font.u0[idx];
    const float v0 = gpu.font.v0[idx];
    const float u1 = gpu.font.u1[idx];
    const float v1 = gpu.font.v1[idx];

    const float gw = gpu.font.glyph_w[idx];
    const float gh = gpu.font.glyph_h[idx];
    if (gw <= 0.0f || gh <= 0.0f) {
      cursor_x += gpu.font.xadvance[idx];
      continue;
    }
    const float gx0 = cursor_x + gpu.font.xoff[idx];
    const float gy0 = baseline + gpu.font.yoff[idx];
    const float gx1 = gx0 + gw;
    const float gy1 = gy0 + gh;
    if (clip_x1 > clip_x0 && clip_y1 > clip_y0) {
      if (gx1 <= static_cast<float>(clip_x0) ||
          gx0 >= static_cast<float>(clip_x1) ||
          gy1 <= static_cast<float>(clip_y0) ||
          gy0 >= static_cast<float>(clip_y1)) {
        cursor_x += gpu.font.xadvance[idx];
        continue;
      }
    }

    emit(gx0, gy0, u0, v0);
    emit(gx1, gy0, u1, v0);
    emit(gx1, gy1, u1, v1);
    emit(gx0, gy0, u0, v0);
    emit(gx1, gy1, u1, v1);
    emit(gx0, gy1, u0, v1);
    cursor_x += gpu.font.xadvance[idx];
  }

  const int count = static_cast<int>(gpu.vertices.size()) - first;
  if (count > 0) {
    gpu_add_batch(gpu, first, count, &gpu.font_image, false, false);
  }

  if (use_cache && !cached_vertices.empty()) {
    CachedText entry{};
    entry.text = text_ptr;
    entry.x0 = x0;
    entry.y0 = y0;
    entry.x1 = x1;
    entry.y1 = y1;
    entry.color = color;
    entry.font_w = gpu.font.width;
    entry.font_h = gpu.font.height;
    entry.font_ascent = gpu.font.ascent;
    entry.font_line = gpu.font.line_advance;
    entry.font_truetype = gpu.font.truetype;
    entry.font_sdf = gpu.font.sdf;
    entry.instanced = false;
    entry.last_used = gpu.frame_index;
    entry.vertices.swap(cached_vertices);
    const size_t bytes = text_entry_bytes(entry);
    if (bytes <= gpu.text_cache_limit) {
      gpu_text_cache_evict(gpu, bytes);
      if (gpu.text_cache_bytes + bytes <= gpu.text_cache_limit &&
          static_cast<int>(gpu.text_cache.size()) < gpu.text_cache_max_entries) {
        gpu.text_cache_bytes += bytes;
        gpu.text_cache.push_back(std::move(entry));
      }
    }
  }
}

static void gpu_push_image(GpuState& gpu, const DrawImage* image, int x, int y) {
  if (!image) {
    return;
  }
  const int w = image->width;
  const int h = image->height;
  if (w <= 0 || h <= 0) {
    return;
  }
  const float x0 = static_cast<float>(x);
  const float y0 = static_cast<float>(y);
  const float x1 = static_cast<float>(x + w);
  const float y1 = static_cast<float>(y + h);
  const float v_off = 2.0f;
  const int first = static_cast<int>(gpu.vertices.size());
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f + v_off, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x1, y0, 0.0f, 1.0f, 0.0f + v_off, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x1, y1, 0.0f, 1.0f, 1.0f + v_off, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f + v_off, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x1, y1, 0.0f, 1.0f, 1.0f + v_off, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x0, y1, 0.0f, 0.0f, 1.0f + v_off, 0xFFFFFFFFu, true);
  gpu_add_batch(gpu, first, 6, image, false, false);
}

static void gpu_push_triangle3d(GpuState& gpu,
                                float x0,
                                float y0,
                                float z0,
                                float x1,
                                float y1,
                                float z1,
                                float x2,
                                float y2,
                                float z2,
                                uint32_t color) {
  const int first = static_cast<int>(gpu.vertices.size());
  gpu_push_vertex(gpu, x0, y0, z0, 0.0f, 0.0f, color, false);
  gpu_push_vertex(gpu, x1, y1, z1, 0.0f, 0.0f, color, false);
  gpu_push_vertex(gpu, x2, y2, z2, 0.0f, 0.0f, color, false);
  gpu_add_batch(gpu, first, 3, &gpu.white_image, false, true);
}

#if DRAW_ENGINE_HAS_VULKAN
static uint32_t vk_find_memory_type(VkPhysicalDevice physical,
                                    uint32_t type_filter,
                                    VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    if ((type_filter & (1u << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }
  return UINT32_MAX;
}

static VkFormat vk_find_depth_format(VkPhysicalDevice physical) {
  VkFormat candidates[] = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
  };
  for (VkFormat fmt : candidates) {
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physical, fmt, &props);
    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return fmt;
    }
  }
  return VK_FORMAT_D24_UNORM_S8_UINT;
}

static bool vk_create_buffer(VulkanContext& vk,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props,
                             VkBuffer* out_buffer,
                             VkDeviceMemory* out_memory) {
  VkBufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(vk.device, &info, nullptr, out_buffer) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements mem_req{};
  vkGetBufferMemoryRequirements(vk.device, *out_buffer, &mem_req);
  uint32_t type = vk_find_memory_type(vk.physical, mem_req.memoryTypeBits, props);
  if (type == UINT32_MAX) {
    return false;
  }
  VkMemoryAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.allocationSize = mem_req.size;
  alloc.memoryTypeIndex = type;
  if (vkAllocateMemory(vk.device, &alloc, nullptr, out_memory) != VK_SUCCESS) {
    return false;
  }
  vkBindBufferMemory(vk.device, *out_buffer, *out_memory, 0);
  return true;
}

static bool vk_create_image(VulkanContext& vk,
                            int width,
                            int height,
                            VkImage* out_image,
                            VkDeviceMemory* out_memory) {
  VkImageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.extent.width = static_cast<uint32_t>(width);
  info.extent.height = static_cast<uint32_t>(height);
  info.extent.depth = 1;
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(vk.device, &info, nullptr, out_image) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements mem_req{};
  vkGetImageMemoryRequirements(vk.device, *out_image, &mem_req);
  uint32_t type = vk_find_memory_type(vk.physical,
                                      mem_req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) {
    return false;
  }
  VkMemoryAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.allocationSize = mem_req.size;
  alloc.memoryTypeIndex = type;
  if (vkAllocateMemory(vk.device, &alloc, nullptr, out_memory) != VK_SUCCESS) {
    return false;
  }
  vkBindImageMemory(vk.device, *out_image, *out_memory, 0);
  return true;
}

static bool vk_create_depth_image(VulkanContext& vk,
                                  int width,
                                  int height,
                                  VkSampleCountFlagBits samples) {
  vk.depth_format = vk_find_depth_format(vk.physical);
  VkImageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.extent.width = static_cast<uint32_t>(width);
  info.extent.height = static_cast<uint32_t>(height);
  info.extent.depth = 1;
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.format = vk.depth_format;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  info.samples = samples;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(vk.device, &info, nullptr, &vk.depth_image) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements mem_req{};
  vkGetImageMemoryRequirements(vk.device, vk.depth_image, &mem_req);
  uint32_t type = vk_find_memory_type(vk.physical,
                                      mem_req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) {
    return false;
  }
  VkMemoryAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.allocationSize = mem_req.size;
  alloc.memoryTypeIndex = type;
  if (vkAllocateMemory(vk.device, &alloc, nullptr, &vk.depth_memory) != VK_SUCCESS) {
    return false;
  }
  vkBindImageMemory(vk.device, vk.depth_image, vk.depth_memory, 0);

  VkImageViewCreateInfo view{};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = vk.depth_image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = vk.depth_format;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  view.subresourceRange.baseMipLevel = 0;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.baseArrayLayer = 0;
  view.subresourceRange.layerCount = 1;
  if (vkCreateImageView(vk.device, &view, nullptr, &vk.depth_view) != VK_SUCCESS) {
    return false;
  }
  return true;
}

static bool vk_create_msaa_color_image(VulkanContext& vk, int width, int height) {
  if (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT) {
    return true;
  }
  VkImageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.extent.width = static_cast<uint32_t>(width);
  info.extent.height = static_cast<uint32_t>(height);
  info.extent.depth = 1;
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.format = vk.swapchain_format;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.samples = vk.msaa_samples;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(vk.device, &info, nullptr, &vk.msaa_color_image) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements mem_req{};
  vkGetImageMemoryRequirements(vk.device, vk.msaa_color_image, &mem_req);
  uint32_t type = vk_find_memory_type(vk.physical,
                                      mem_req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) {
    return false;
  }
  VkMemoryAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.allocationSize = mem_req.size;
  alloc.memoryTypeIndex = type;
  if (vkAllocateMemory(vk.device, &alloc, nullptr, &vk.msaa_color_memory) != VK_SUCCESS) {
    return false;
  }
  vkBindImageMemory(vk.device, vk.msaa_color_image, vk.msaa_color_memory, 0);

  VkImageViewCreateInfo view{};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = vk.msaa_color_image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = vk.swapchain_format;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.baseMipLevel = 0;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.baseArrayLayer = 0;
  view.subresourceRange.layerCount = 1;
  if (vkCreateImageView(vk.device, &view, nullptr, &vk.msaa_color_view) != VK_SUCCESS) {
    return false;
  }
  return true;
}

static void vk_destroy_swapchain(VulkanContext& vk) {
  for (auto fb : vk.framebuffers_2d) {
    vkDestroyFramebuffer(vk.device, fb, nullptr);
  }
  vk.framebuffers_2d.clear();
  for (auto fb : vk.framebuffers_3d) {
    vkDestroyFramebuffer(vk.device, fb, nullptr);
  }
  vk.framebuffers_3d.clear();
  for (auto view : vk.image_views) {
    vkDestroyImageView(vk.device, view, nullptr);
  }
  vk.image_views.clear();
  if (vk.msaa_color_view) {
    vkDestroyImageView(vk.device, vk.msaa_color_view, nullptr);
    vk.msaa_color_view = VK_NULL_HANDLE;
  }
  if (vk.msaa_color_image) {
    vkDestroyImage(vk.device, vk.msaa_color_image, nullptr);
    vk.msaa_color_image = VK_NULL_HANDLE;
  }
  if (vk.msaa_color_memory) {
    vkFreeMemory(vk.device, vk.msaa_color_memory, nullptr);
    vk.msaa_color_memory = VK_NULL_HANDLE;
  }
  if (vk.depth_view) {
    vkDestroyImageView(vk.device, vk.depth_view, nullptr);
    vk.depth_view = VK_NULL_HANDLE;
  }
  if (vk.depth_image) {
    vkDestroyImage(vk.device, vk.depth_image, nullptr);
    vk.depth_image = VK_NULL_HANDLE;
  }
  if (vk.depth_memory) {
    vkFreeMemory(vk.device, vk.depth_memory, nullptr);
    vk.depth_memory = VK_NULL_HANDLE;
  }
  if (vk.swapchain) {
    vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
    vk.swapchain = VK_NULL_HANDLE;
  }
  vk.images.clear();
}

static bool vk_create_swapchain_resources(VulkanContext& vk, int width, int height) {
  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &caps);
  uint32_t fmt_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &fmt_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fmt_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &fmt_count, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (const auto& fmt : formats) {
    if (fmt.format == vk.swapchain_format) {
      chosen = fmt;
      break;
    }
    if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM ||
        fmt.format == VK_FORMAT_B8G8R8A8_UNORM) {
      chosen = fmt;
    }
  }
  vk.swapchain_format = chosen.format;

  uint32_t present_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &present_count, nullptr);
  std::vector<VkPresentModeKHR> present_modes(present_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &present_count,
                                            present_modes.data());
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  const int present_pref = env_int("DRAW_ENGINE_VK_PRESENT_MODE", 0);
  if (present_pref == 2) {
    for (auto mode : present_modes) {
      if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        present_mode = mode;
        break;
      }
    }
  }
  if (present_mode == VK_PRESENT_MODE_FIFO_KHR) {
    for (auto mode : present_modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        present_mode = mode;
        break;
      }
    }
  }

  VkExtent2D extent{};
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
    const int force_extent = env_int("DRAW_ENGINE_VK_FORCE_EXTENT", 1);
    if (force_extent != 0 && width > 0 && height > 0 &&
        (extent.width != static_cast<uint32_t>(width) ||
         extent.height != static_cast<uint32_t>(height))) {
      extent.width = static_cast<uint32_t>(width);
      extent.height = static_cast<uint32_t>(height);
    }
  } else {
    extent.width = static_cast<uint32_t>(width);
    extent.height = static_cast<uint32_t>(height);
  }
  vk.extent = extent;

  uint32_t image_count = caps.minImageCount + 1;
  int req_images = env_int("DRAW_ENGINE_VK_SWAPCHAIN_IMAGES", 0);
  if (req_images > 0) {
    image_count = static_cast<uint32_t>(req_images);
  } else if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
    image_count = caps.minImageCount + 2;
  }
  if (image_count < caps.minImageCount) {
    image_count = caps.minImageCount;
  }
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
    image_count = caps.maxImageCount;
  }

  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & composite)) {
    composite = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
  }
  if (!(caps.supportedCompositeAlpha & composite)) {
    composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  }

  VkSwapchainCreateInfoKHR sw{};
  sw.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sw.surface = vk.surface;
  sw.minImageCount = image_count;
  sw.imageFormat = vk.swapchain_format;
  sw.imageColorSpace = chosen.colorSpace;
  sw.imageExtent = extent;
  sw.imageArrayLayers = 1;
  sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkSurfaceTransformFlagBitsKHR pre_transform = caps.currentTransform;
  if (env_int("DRAW_ENGINE_VK_PREFER_IDENTITY", 0) != 0 &&
      (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
    pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }
  sw.preTransform = pre_transform;
  vk.pre_transform = pre_transform;
  sw.compositeAlpha = composite;
  sw.presentMode = present_mode;
  sw.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(vk.device, &sw, nullptr, &vk.swapchain) != VK_SUCCESS) {
    return false;
  }

  uint32_t img_count = 0;
  vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &img_count, nullptr);
  vk.images.resize(img_count);
  vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &img_count, vk.images.data());

  vk.image_views.resize(vk.images.size());
  for (size_t i = 0; i < vk.images.size(); ++i) {
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = vk.images[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = vk.swapchain_format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk.device, &view, nullptr, &vk.image_views[i]) != VK_SUCCESS) {
      return false;
    }
  }

  if (!vk_create_depth_image(vk,
                             static_cast<int>(extent.width),
                             static_cast<int>(extent.height),
                             vk.msaa_samples)) {
    return false;
  }
  if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    if (!vk_create_msaa_color_image(vk,
                                    static_cast<int>(extent.width),
                                    static_cast<int>(extent.height))) {
      return false;
    }
  }

  auto create_framebuffers = [&](VkRenderPass pass,
                                 bool with_depth,
                                 std::vector<VkFramebuffer>& out) -> bool {
    out.resize(vk.image_views.size());
    for (size_t i = 0; i < vk.image_views.size(); ++i) {
      VkImageView attachments[3];
      uint32_t attachment_count = 0;
      if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
        attachments[attachment_count++] = vk.msaa_color_view;
        attachments[attachment_count++] = vk.image_views[i];
        if (with_depth) {
          attachments[attachment_count++] = vk.depth_view;
        }
      } else {
        attachments[attachment_count++] = vk.image_views[i];
        if (with_depth) {
          attachments[attachment_count++] = vk.depth_view;
        }
      }
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = pass;
      fb.attachmentCount = attachment_count;
      fb.pAttachments = attachments;
      fb.width = extent.width;
      fb.height = extent.height;
      fb.layers = 1;
      if (vkCreateFramebuffer(vk.device, &fb, nullptr, &out[i]) != VK_SUCCESS) {
        return false;
      }
    }
    return true;
  };
  if (!create_framebuffers(vk.render_pass_2d, false, vk.framebuffers_2d)) {
    return false;
  }
  if (!create_framebuffers(vk.render_pass_3d, true, vk.framebuffers_3d)) {
    return false;
  }
  return true;
}

static VkCommandBuffer vk_begin_single(VulkanContext& vk) {
  VkCommandBufferAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = vk.command_pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(vk.device, &alloc, &cmd) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);
  return cmd;
}

static void vk_end_single(VulkanContext& vk, VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE);
  vkQueueWaitIdle(vk.queue);
  vkFreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
}

static void vk_transition_image(VulkanContext& vk,
                                VkImage image,
                                VkImageLayout old_layout,
                                VkImageLayout new_layout) {
  VkCommandBuffer cmd = vk_begin_single(vk);
  if (cmd == VK_NULL_HANDLE) {
    return;
  }
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
      new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  vk_end_single(vk, cmd);
}

static bool vk_upload_texture(VulkanContext& vk, DrawImage* image) {
  if (!image || !image->rgba || image->width <= 0 || image->height <= 0) {
    return false;
  }
  if (image->vk_ready) {
    return true;
  }
  if (!vk_create_image(vk, image->width, image->height, &image->vk_image, &image->vk_memory)) {
    return false;
  }

  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem = VK_NULL_HANDLE;
  const VkDeviceSize size = static_cast<VkDeviceSize>(image->width * image->height * 4);
  if (!vk_create_buffer(vk,
                        size,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &staging,
                        &staging_mem)) {
    return false;
  }
  void* mapped = nullptr;
  vkMapMemory(vk.device, staging_mem, 0, size, 0, &mapped);
  memcpy(mapped, image->rgba, static_cast<size_t>(size));
  vkUnmapMemory(vk.device, staging_mem);

  vk_transition_image(vk, image->vk_image, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkCommandBuffer cmd = vk_begin_single(vk);
  if (cmd == VK_NULL_HANDLE) {
    return false;
  }
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = static_cast<uint32_t>(image->width);
  region.imageExtent.height = static_cast<uint32_t>(image->height);
  region.imageExtent.depth = 1;
  vkCmdCopyBufferToImage(cmd, staging, image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);
  vk_end_single(vk, cmd);

  vk_transition_image(vk, image->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(vk.device, staging, nullptr);
  vkFreeMemory(vk.device, staging_mem, nullptr);

  VkImageViewCreateInfo view{};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = image->vk_image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = VK_FORMAT_R8G8B8A8_UNORM;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.baseMipLevel = 0;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.baseArrayLayer = 0;
  view.subresourceRange.layerCount = 1;
  if (vkCreateImageView(vk.device, &view, nullptr, &image->vk_view) != VK_SUCCESS) {
    return false;
  }

  VkDescriptorSetAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc.descriptorPool = vk.desc_pool;
  alloc.descriptorSetCount = 1;
  alloc.pSetLayouts = &vk.desc_layout;
  if (vkAllocateDescriptorSets(vk.device, &alloc, &image->vk_desc) != VK_SUCCESS) {
    return false;
  }
  VkDescriptorImageInfo img_info{};
  img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  img_info.imageView = image->vk_view;
  img_info.sampler = vk.sampler;
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = image->vk_desc;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &img_info;
  vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);

  image->vk_ready = true;
  return true;
}

static bool vk_init_context(VulkanContext& vk, ANativeWindow* window, int width, int height) {
  memset(&vk, 0, sizeof(vk));

  uint32_t api_version = VK_API_VERSION_1_1;
  PFN_vkEnumerateInstanceVersion enumerate_instance_version =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
          vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
  if (enumerate_instance_version) {
    uint32_t available = 0;
    if (enumerate_instance_version(&available) == VK_SUCCESS) {
      api_version = available;
    }
  }
  if (VK_VERSION_MAJOR(api_version) < 1 ||
      (VK_VERSION_MAJOR(api_version) == 1 && VK_VERSION_MINOR(api_version) < 1)) {
    fprintf(stderr, "Vulkan 1.1 not supported\n");
    return false;
  }

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "DrawEngine";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "DrawEngine";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = api_version;

  const char* extensions[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
      VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
  };

  VkInstanceCreateInfo inst{};
  inst.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst.pApplicationInfo = &app;
  inst.enabledExtensionCount = 2;
  inst.ppEnabledExtensionNames = extensions;
  if (vkCreateInstance(&inst, nullptr, &vk.instance) != VK_SUCCESS) {
    fprintf(stderr, "vkCreateInstance failed\n");
    return false;
  }

  VkAndroidSurfaceCreateInfoKHR surface_info{};
  surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  surface_info.window = window;
  if (vkCreateAndroidSurfaceKHR(vk.instance, &surface_info, nullptr, &vk.surface) != VK_SUCCESS) {
    fprintf(stderr, "vkCreateAndroidSurfaceKHR failed\n");
    return false;
  }

  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(vk.instance, &device_count, nullptr);
  if (device_count == 0) {
    fprintf(stderr, "No Vulkan devices\n");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(vk.instance, &device_count, devices.data());
  for (VkPhysicalDevice dev : devices) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_family_count, families.data());
    for (uint32_t i = 0; i < queue_family_count; ++i) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, vk.surface, &present);
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
        vk.physical = dev;
        vk.queue_family = i;
        break;
      }
    }
    if (vk.physical != VK_NULL_HANDLE) {
      break;
    }
  }
  if (vk.physical == VK_NULL_HANDLE) {
    fprintf(stderr, "No suitable Vulkan device\n");
    return false;
  }

  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceFeatures supported{};
  vkGetPhysicalDeviceProperties(vk.physical, &props);
  vkGetPhysicalDeviceFeatures(vk.physical, &supported);
  vk.supports_anisotropy = supported.samplerAnisotropy == VK_TRUE;
  vk.supports_wide_lines = supported.wideLines == VK_TRUE;
  vk.supports_timestamps = props.limits.timestampComputeAndGraphics == VK_TRUE;
  vk.timestamp_period = props.limits.timestampPeriod;
  vk.max_anisotropy = vk.supports_anisotropy ? props.limits.maxSamplerAnisotropy : 1.0f;
  const int msaa_req = env_int("DRAW_ENGINE_MSAA", 1);
  VkSampleCountFlags counts =
      props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
  vk.msaa_samples = choose_sample_count(counts, msaa_req);
  fprintf(stderr, "Vulkan device: %s api=%u.%u.%u\n",
          props.deviceName,
          VK_VERSION_MAJOR(props.apiVersion),
          VK_VERSION_MINOR(props.apiVersion),
          VK_VERSION_PATCH(props.apiVersion));
  fprintf(stderr, "Vulkan MSAA: %dx\n", sample_count_to_int(vk.msaa_samples));

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qinfo{};
  qinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qinfo.queueFamilyIndex = vk.queue_family;
  qinfo.queueCount = 1;
  qinfo.pQueuePriorities = &priority;

  const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkPhysicalDeviceFeatures features{};
  if (vk.supports_anisotropy) {
    features.samplerAnisotropy = VK_TRUE;
  }
  if (vk.supports_wide_lines) {
    features.wideLines = VK_TRUE;
  }
  VkDeviceCreateInfo dinfo{};
  dinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dinfo.queueCreateInfoCount = 1;
  dinfo.pQueueCreateInfos = &qinfo;
  dinfo.enabledExtensionCount = 1;
  dinfo.ppEnabledExtensionNames = dev_exts;
  dinfo.pEnabledFeatures = &features;
  if (vkCreateDevice(vk.physical, &dinfo, nullptr, &vk.device) != VK_SUCCESS) {
    fprintf(stderr, "vkCreateDevice failed\n");
    return false;
  }
  vkGetDeviceQueue(vk.device, vk.queue_family, 0, &vk.queue);

  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &caps);
  uint32_t fmt_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &fmt_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fmt_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &fmt_count, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (const auto& fmt : formats) {
    if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM ||
        fmt.format == VK_FORMAT_B8G8R8A8_UNORM) {
      chosen = fmt;
      break;
    }
  }
  vk.swapchain_format = chosen.format;

  uint32_t present_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &present_count, nullptr);
  std::vector<VkPresentModeKHR> present_modes(present_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &present_count,
                                            present_modes.data());
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  const int present_pref = env_int("DRAW_ENGINE_VK_PRESENT_MODE", 0);
  if (present_pref == 2) {
    for (auto mode : present_modes) {
      if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        present_mode = mode;
        break;
      }
    }
  }
  if (present_mode == VK_PRESENT_MODE_FIFO_KHR) {
    for (auto mode : present_modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        present_mode = mode;
        break;
      }
    }
  }

  VkExtent2D extent{};
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
    const int force_extent = env_int("DRAW_ENGINE_VK_FORCE_EXTENT", 1);
    if (force_extent != 0 && width > 0 && height > 0 &&
        (extent.width != static_cast<uint32_t>(width) ||
         extent.height != static_cast<uint32_t>(height))) {
      extent.width = static_cast<uint32_t>(width);
      extent.height = static_cast<uint32_t>(height);
    }
  } else {
    extent.width = static_cast<uint32_t>(width);
    extent.height = static_cast<uint32_t>(height);
  }
  VkSurfaceTransformFlagBitsKHR pre_transform = caps.currentTransform;
  if (env_int("DRAW_ENGINE_VK_PREFER_IDENTITY", 0) != 0 &&
      (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
    pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }
  vk.extent = extent;
  vk.pre_transform = pre_transform;
  fprintf(stderr,
          "Vulkan swapchain extent: caps=%ux%u desired=%dx%d final=%ux%u\n",
          caps.currentExtent.width,
          caps.currentExtent.height,
          width,
          height,
          extent.width,
          extent.height);
  fprintf(stderr,
          "Vulkan swapchain extent: caps=%ux%u desired=%dx%d final=%ux%u\n",
          caps.currentExtent.width,
          caps.currentExtent.height,
          width,
          height,
          extent.width,
          extent.height);

  uint32_t image_count = caps.minImageCount + 1;
  int req_images = env_int("DRAW_ENGINE_VK_SWAPCHAIN_IMAGES", 0);
  if (req_images > 0) {
    image_count = static_cast<uint32_t>(req_images);
  } else if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
    image_count = caps.minImageCount + 2;
  }
  if (image_count < caps.minImageCount) {
    image_count = caps.minImageCount;
  }
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
    image_count = caps.maxImageCount;
  }

  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & composite)) {
    composite = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
  }
  if (!(caps.supportedCompositeAlpha & composite)) {
    composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  }

  VkSwapchainCreateInfoKHR sw{};
  sw.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sw.surface = vk.surface;
  sw.minImageCount = image_count;
  sw.imageFormat = vk.swapchain_format;
  sw.imageColorSpace = chosen.colorSpace;
  sw.imageExtent = extent;
  sw.imageArrayLayers = 1;
  sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sw.preTransform = pre_transform;
  sw.compositeAlpha = composite;
  sw.presentMode = present_mode;
  sw.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(vk.device, &sw, nullptr, &vk.swapchain) != VK_SUCCESS) {
    fprintf(stderr, "vkCreateSwapchainKHR failed\n");
    return false;
  }

  uint32_t swap_count = 0;
  vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swap_count, nullptr);
  vk.images.resize(swap_count);
  vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &swap_count, vk.images.data());

  vk.image_views.resize(vk.images.size());
  for (size_t i = 0; i < vk.images.size(); ++i) {
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = vk.images[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = vk.swapchain_format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;
    vkCreateImageView(vk.device, &view, nullptr, &vk.image_views[i]);
  }

  if (!vk_create_depth_image(vk,
                             static_cast<int>(extent.width),
                             static_cast<int>(extent.height),
                             vk.msaa_samples)) {
    fprintf(stderr, "vk depth image failed\n");
    return false;
  }
  if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    if (!vk_create_msaa_color_image(vk,
                                    static_cast<int>(extent.width),
                                    static_cast<int>(extent.height))) {
      fprintf(stderr, "vk msaa color image failed\n");
      return false;
    }
  }

  VkAttachmentDescription color{};
  color.format = vk.swapchain_format;
  color.samples = vk.msaa_samples;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT)
                      ? VK_ATTACHMENT_STORE_OP_STORE
                      : VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT)
                          ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                          : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription resolve{};
  resolve.format = vk.swapchain_format;
  resolve.samples = VK_SAMPLE_COUNT_1_BIT;
  resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depth{};
  depth.format = vk.depth_format;
  depth.samples = vk.msaa_samples;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference resolve_ref{};
  resolve_ref.attachment = 1;
  resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  auto create_render_pass = [&](bool with_depth, VkRenderPass* out_pass) -> bool {
    VkAttachmentDescription attachments[3];
    uint32_t attachment_count = 0;
    attachments[attachment_count++] = color;
    if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
      attachments[attachment_count++] = resolve;
    }
    if (with_depth) {
      attachments[attachment_count++] = depth;
    }

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? 1 : 2;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
      subpass.pResolveAttachments = &resolve_ref;
    }
    subpass.pDepthStencilAttachment = with_depth ? &depth_ref : nullptr;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = attachment_count;
    rp.pAttachments = attachments;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    if (vkCreateRenderPass(vk.device, &rp, nullptr, out_pass) != VK_SUCCESS) {
      return false;
    }
    return true;
  };

  if (!create_render_pass(false, &vk.render_pass_2d) ||
      !create_render_pass(true, &vk.render_pass_3d)) {
    fprintf(stderr, "vkCreateRenderPass failed\n");
    return false;
  }

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layout{};
  layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout.bindingCount = 1;
  layout.pBindings = &binding;
  vkCreateDescriptorSetLayout(vk.device, &layout, nullptr, &vk.desc_layout);

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push.offset = 0;
  push.size = sizeof(float) * 16;

  VkPipelineLayoutCreateInfo pl{};
  pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &vk.desc_layout;
  pl.pushConstantRangeCount = 1;
  pl.pPushConstantRanges = &push;
  vkCreatePipelineLayout(vk.device, &pl, nullptr, &vk.pipeline_layout);

  auto create_shader = [&](const uint32_t* code, size_t count) -> VkShaderModule {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = count * sizeof(uint32_t);
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vk.device, &info, nullptr, &module) != VK_SUCCESS) {
      return VK_NULL_HANDLE;
    }
    return module;
  };

  VkShaderModule vert = create_shader(kSimpleVertSpv, sizeof(kSimpleVertSpv) / sizeof(uint32_t));
  VkShaderModule frag = create_shader(kSimpleFragSpv, sizeof(kSimpleFragSpv) / sizeof(uint32_t));
  VkShaderModule text_vert =
      create_shader(kTextInstVertSpv, sizeof(kTextInstVertSpv) / sizeof(uint32_t));
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE || text_vert == VK_NULL_HANDLE) {
    fprintf(stderr, "vkCreateShaderModule failed\n");
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkPipelineShaderStageCreateInfo stages_text[2]{};
  stages_text[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages_text[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages_text[0].module = text_vert;
  stages_text[0].pName = "main";
  stages_text[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages_text[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages_text[1].module = frag;
  stages_text[1].pName = "main";

  VkVertexInputBindingDescription binding_desc{};
  binding_desc.binding = 0;
  binding_desc.stride = sizeof(GpuVertex);
  binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attrs[3]{};
  attrs[0].location = 0;
  attrs[0].binding = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = 0;
  attrs[1].location = 1;
  attrs[1].binding = 0;
  attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
  attrs[1].offset = 12;
  attrs[2].location = 2;
  attrs[2].binding = 0;
  attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  attrs[2].offset = 20;

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding_desc;
  vi.vertexAttributeDescriptionCount = 3;
  vi.pVertexAttributeDescriptions = attrs;

  VkVertexInputBindingDescription text_binding{};
  text_binding.binding = 0;
  text_binding.stride = sizeof(TextInstance);
  text_binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

  VkVertexInputAttributeDescription text_attrs[3]{};
  text_attrs[0].location = 0;
  text_attrs[0].binding = 0;
  text_attrs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  text_attrs[0].offset = 0;
  text_attrs[1].location = 1;
  text_attrs[1].binding = 0;
  text_attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  text_attrs[1].offset = 16;
  text_attrs[2].location = 2;
  text_attrs[2].binding = 0;
  text_attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  text_attrs[2].offset = 32;

  VkPipelineVertexInputStateCreateInfo vi_text{};
  vi_text.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi_text.vertexBindingDescriptionCount = 1;
  vi_text.pVertexBindingDescriptions = &text_binding;
  vi_text.vertexAttributeDescriptionCount = 3;
  vi_text.pVertexAttributeDescriptions = text_attrs;

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.extent = extent;

  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.pViewports = &viewport;
  vp.scissorCount = 1;
  vp.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.lineWidth = 1.0f;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = vk.msaa_samples;
  ms.sampleShadingEnable = (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) ? VK_TRUE : VK_FALSE;
  ms.minSampleShading = 1.0f;

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blend.blendEnable = VK_TRUE;
  blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &blend;

  VkPipelineDynamicStateCreateInfo dyn{};
  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dyn_states;

  auto create_pipeline = [&](VkPrimitiveTopology topology,
                             bool depth_enable,
                             VkPipelineVertexInputStateCreateInfo* vertex_input,
                             VkPipelineShaderStageCreateInfo* stage_state,
                             VkRenderPass pass) -> VkPipeline {
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = topology;
    ia.primitiveRestartEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depth_enable ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth_enable ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stage_state;
    gp.pVertexInputState = vertex_input;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = depth_enable ? &ds : nullptr;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = vk.pipeline_layout;
    gp.renderPass = pass;
    gp.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline) !=
        VK_SUCCESS) {
      return VK_NULL_HANDLE;
    }
    return pipeline;
  };

  vk.pipeline_tri_2d =
      create_pipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, &vi, stages, vk.render_pass_2d);
  vk.pipeline_line_2d =
      create_pipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, &vi, stages, vk.render_pass_2d);
  vk.pipeline_text_2d = create_pipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false,
                                        &vi_text, stages_text, vk.render_pass_2d);
  vk.pipeline_tri_3d =
      create_pipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, &vi, stages, vk.render_pass_3d);
  vk.pipeline_line_3d =
      create_pipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, &vi, stages, vk.render_pass_3d);

  if (vk.pipeline_tri_2d == VK_NULL_HANDLE || vk.pipeline_line_2d == VK_NULL_HANDLE ||
      vk.pipeline_text_2d == VK_NULL_HANDLE || vk.pipeline_tri_3d == VK_NULL_HANDLE ||
      vk.pipeline_line_3d == VK_NULL_HANDLE) {
    fprintf(stderr, "vkCreateGraphicsPipelines failed\n");
    return false;
  }
  vkDestroyShaderModule(vk.device, vert, nullptr);
  vkDestroyShaderModule(vk.device, frag, nullptr);
  vkDestroyShaderModule(vk.device, text_vert, nullptr);

  auto create_framebuffers = [&](VkRenderPass pass,
                                 bool with_depth,
                                 std::vector<VkFramebuffer>& out) -> bool {
    out.resize(vk.image_views.size());
    for (size_t i = 0; i < vk.image_views.size(); ++i) {
      VkImageView attachments[3];
      uint32_t attachment_count = 0;
      if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
        attachments[attachment_count++] = vk.msaa_color_view;
        attachments[attachment_count++] = vk.image_views[i];
        if (with_depth) {
          attachments[attachment_count++] = vk.depth_view;
        }
      } else {
        attachments[attachment_count++] = vk.image_views[i];
        if (with_depth) {
          attachments[attachment_count++] = vk.depth_view;
        }
      }
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = pass;
      fb.attachmentCount = attachment_count;
      fb.pAttachments = attachments;
      fb.width = extent.width;
      fb.height = extent.height;
      fb.layers = 1;
      if (vkCreateFramebuffer(vk.device, &fb, nullptr, &out[i]) != VK_SUCCESS) {
        return false;
      }
    }
    return true;
  };
  if (!create_framebuffers(vk.render_pass_2d, false, vk.framebuffers_2d) ||
      !create_framebuffers(vk.render_pass_3d, true, vk.framebuffers_3d)) {
    fprintf(stderr, "vkCreateFramebuffer failed\n");
    return false;
  }

  VkCommandPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool.queueFamilyIndex = vk.queue_family;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkCreateCommandPool(vk.device, &pool, nullptr, &vk.command_pool);

  int frames_in_flight = env_int("DRAW_ENGINE_VK_FRAMES", 3);
  if (frames_in_flight < 1) {
    frames_in_flight = 1;
  }
  if (frames_in_flight > 3) {
    frames_in_flight = 3;
  }
  if (!vk.images.empty() &&
      frames_in_flight > static_cast<int>(vk.images.size())) {
    frames_in_flight = static_cast<int>(vk.images.size());
  }
  vk.frame_count = static_cast<uint32_t>(frames_in_flight);
  vk.frame_index = 0;

  VkCommandBufferAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = vk.command_pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = vk.frame_count;
  vk.command_buffers.resize(vk.frame_count);
  vkAllocateCommandBuffers(vk.device, &alloc, vk.command_buffers.data());

  VkSemaphoreCreateInfo sem{};
  sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fence{};
  fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vk.image_available.resize(vk.frame_count);
  vk.render_finished.resize(vk.frame_count);
  vk.in_flight.resize(vk.frame_count);
  for (uint32_t i = 0; i < vk.frame_count; ++i) {
    vkCreateSemaphore(vk.device, &sem, nullptr, &vk.image_available[i]);
    vkCreateSemaphore(vk.device, &sem, nullptr, &vk.render_finished[i]);
    vkCreateFence(vk.device, &fence, nullptr, &vk.in_flight[i]);
  }

  if (vk.supports_timestamps) {
    VkQueryPoolCreateInfo qp{};
    qp.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qp.queryCount = vk.frame_count * 2;
    if (vkCreateQueryPool(vk.device, &qp, nullptr, &vk.query_pool) != VK_SUCCESS) {
      vk.query_pool = VK_NULL_HANDLE;
      vk.supports_timestamps = false;
    }
  }

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = 64;
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  pool_info.maxSets = 64;
  vkCreateDescriptorPool(vk.device, &pool_info, nullptr, &vk.desc_pool);

  VkSamplerCreateInfo samp{};
  samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samp.magFilter = VK_FILTER_LINEAR;
  samp.minFilter = VK_FILTER_LINEAR;
  samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samp.anisotropyEnable = vk.supports_anisotropy ? VK_TRUE : VK_FALSE;
  samp.maxAnisotropy = vk.supports_anisotropy ? vk.max_anisotropy : 1.0f;
  vkCreateSampler(vk.device, &samp, nullptr, &vk.sampler);

  int max_vertices = env_int("DRAW_ENGINE_VK_MAX_VERTICES", 300000);
  if (max_vertices < 65536) {
    max_vertices = 65536;
  }
  vk.vertex_stride = sizeof(GpuVertex);
  vk.vertex_capacity = static_cast<size_t>(max_vertices) * vk.vertex_stride;
  vk.vertex_buffer_size = vk.vertex_capacity * vk.frame_count;
  if (!vk_create_buffer(vk,
                        vk.vertex_buffer_size,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &vk.vertex_buffer,
                        &vk.vertex_memory)) {
    fprintf(stderr, "vk vertex buffer failed\n");
    return false;
  }
  vkMapMemory(vk.device, vk.vertex_memory, 0, vk.vertex_buffer_size, 0, &vk.vertex_map);

  int max_text = env_int("DRAW_ENGINE_TEXT_MAX_INST", 8192);
  if (max_text < 256) {
    max_text = 256;
  }
  vk.text_capacity = static_cast<size_t>(max_text) * sizeof(TextInstance);
  vk.text_buffer_size = vk.text_capacity * vk.frame_count;
  if (!vk_create_buffer(vk,
                        vk.text_buffer_size,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &vk.text_buffer,
                        &vk.text_memory)) {
    fprintf(stderr, "vk text buffer failed\n");
    return false;
  }
  vkMapMemory(vk.device, vk.text_memory, 0, vk.text_buffer_size, 0, &vk.text_map);

  vk.ready = true;
  return true;
}

static void vk_cleanup(VulkanContext& vk) {
  if (!vk.ready) {
    return;
  }
  vkDeviceWaitIdle(vk.device);
  if (vk.vertex_map) {
    vkUnmapMemory(vk.device, vk.vertex_memory);
  }
  if (vk.vertex_buffer) {
    vkDestroyBuffer(vk.device, vk.vertex_buffer, nullptr);
  }
  if (vk.vertex_memory) {
    vkFreeMemory(vk.device, vk.vertex_memory, nullptr);
  }
  if (vk.text_map) {
    vkUnmapMemory(vk.device, vk.text_memory);
  }
  if (vk.text_buffer) {
    vkDestroyBuffer(vk.device, vk.text_buffer, nullptr);
  }
  if (vk.text_memory) {
    vkFreeMemory(vk.device, vk.text_memory, nullptr);
  }
  if (vk.sampler) {
    vkDestroySampler(vk.device, vk.sampler, nullptr);
  }
  if (vk.desc_pool) {
    vkDestroyDescriptorPool(vk.device, vk.desc_pool, nullptr);
  }
  if (vk.desc_layout) {
    vkDestroyDescriptorSetLayout(vk.device, vk.desc_layout, nullptr);
  }
  for (auto fence : vk.in_flight) {
    if (fence) {
      vkDestroyFence(vk.device, fence, nullptr);
    }
  }
  for (auto sem : vk.render_finished) {
    if (sem) {
      vkDestroySemaphore(vk.device, sem, nullptr);
    }
  }
  for (auto sem : vk.image_available) {
    if (sem) {
      vkDestroySemaphore(vk.device, sem, nullptr);
    }
  }
  if (!vk.command_buffers.empty()) {
    vkFreeCommandBuffers(vk.device,
                         vk.command_pool,
                         static_cast<uint32_t>(vk.command_buffers.size()),
                         vk.command_buffers.data());
  }
  if (vk.command_pool) {
    vkDestroyCommandPool(vk.device, vk.command_pool, nullptr);
  }
  if (vk.query_pool) {
    vkDestroyQueryPool(vk.device, vk.query_pool, nullptr);
  }
  if (vk.pipeline_tri_2d) {
    vkDestroyPipeline(vk.device, vk.pipeline_tri_2d, nullptr);
  }
  if (vk.pipeline_line_2d) {
    vkDestroyPipeline(vk.device, vk.pipeline_line_2d, nullptr);
  }
  if (vk.pipeline_text_2d) {
    vkDestroyPipeline(vk.device, vk.pipeline_text_2d, nullptr);
  }
  if (vk.pipeline_tri_3d) {
    vkDestroyPipeline(vk.device, vk.pipeline_tri_3d, nullptr);
  }
  if (vk.pipeline_line_3d) {
    vkDestroyPipeline(vk.device, vk.pipeline_line_3d, nullptr);
  }
  if (vk.pipeline_layout) {
    vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, nullptr);
  }
  if (vk.render_pass_2d) {
    vkDestroyRenderPass(vk.device, vk.render_pass_2d, nullptr);
  }
  if (vk.render_pass_3d) {
    vkDestroyRenderPass(vk.device, vk.render_pass_3d, nullptr);
  }
  for (auto fb : vk.framebuffers_2d) {
    vkDestroyFramebuffer(vk.device, fb, nullptr);
  }
  for (auto fb : vk.framebuffers_3d) {
    vkDestroyFramebuffer(vk.device, fb, nullptr);
  }
  for (auto view : vk.image_views) {
    vkDestroyImageView(vk.device, view, nullptr);
  }
  if (vk.msaa_color_view) {
    vkDestroyImageView(vk.device, vk.msaa_color_view, nullptr);
  }
  if (vk.msaa_color_image) {
    vkDestroyImage(vk.device, vk.msaa_color_image, nullptr);
  }
  if (vk.msaa_color_memory) {
    vkFreeMemory(vk.device, vk.msaa_color_memory, nullptr);
  }
  if (vk.depth_view) {
    vkDestroyImageView(vk.device, vk.depth_view, nullptr);
  }
  if (vk.depth_image) {
    vkDestroyImage(vk.device, vk.depth_image, nullptr);
  }
  if (vk.depth_memory) {
    vkFreeMemory(vk.device, vk.depth_memory, nullptr);
  }
  if (vk.swapchain) {
    vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
  }
  if (vk.surface) {
    vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
  }
  if (vk.device) {
    vkDestroyDevice(vk.device, nullptr);
  }
  if (vk.instance) {
    vkDestroyInstance(vk.instance, nullptr);
  }
  memset(&vk, 0, sizeof(vk));
}

static bool vk_draw_frame(GpuState& gpu) {
  VulkanContext& vk = gpu.vk;
  if (!vk.ready) {
    return false;
  }
  const bool allow_timestamps = !hybrid_secure_enabled();
  const bool has_vertices = !gpu.vertices.empty();
  const bool has_text = !gpu.text_instances.empty();
  if (!has_vertices && !has_text) {
    return true;
  }
  if (gpu.log_interval <= 0) {
    gpu.log_interval = env_int("DRAW_ENGINE_LOG_INTERVAL", 120);
  }
  const bool want_log =
      (gpu.log_interval > 0 &&
       (gpu.frame_index % static_cast<uint64_t>(gpu.log_interval)) == 0);

  const size_t frame_bytes = has_vertices ? (gpu.vertices.size() * sizeof(GpuVertex)) : 0;
  if (has_vertices && frame_bytes > vk.vertex_capacity) {
    fprintf(stderr, "vk vertex overflow\n");
    return false;
  }
  const size_t text_bytes = has_text ? (gpu.text_instances.size() * sizeof(TextInstance)) : 0;
  if (has_text && text_bytes > vk.text_capacity) {
    fprintf(stderr, "vk text overflow\n");
    return false;
  }
  const uint32_t frame = (vk.frame_count > 0) ? (vk.frame_index % vk.frame_count) : 0;
  VkFence fence = vk.in_flight[frame];
  vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(vk.device, 1, &fence);

  double gpu_ms = 0.0;
  if (want_log && allow_timestamps && vk.supports_timestamps && vk.query_pool) {
    uint64_t timestamps[2] = {};
    const uint32_t query_index = frame * 2;
    if (vkGetQueryPoolResults(vk.device,
                              vk.query_pool,
                              query_index,
                              2,
                              sizeof(timestamps),
                              timestamps,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ==
        VK_SUCCESS) {
      if (timestamps[1] > timestamps[0]) {
        gpu_ms = (timestamps[1] - timestamps[0]) * vk.timestamp_period / 1e6;
      }
    }
  }

  if (has_vertices) {
    uint8_t* dst = static_cast<uint8_t*>(vk.vertex_map) + (vk.vertex_capacity * frame);
    memcpy(dst, gpu.vertices.data(), frame_bytes);
  }
  if (has_text) {
    uint8_t* dst = static_cast<uint8_t*>(vk.text_map) + (vk.text_capacity * frame);
    memcpy(dst, gpu.text_instances.data(), text_bytes);
  }

  uint32_t image_index = 0;
  if (vkAcquireNextImageKHR(vk.device,
                            vk.swapchain,
                            UINT64_MAX,
                            vk.image_available[frame],
                            VK_NULL_HANDLE,
                            &image_index) != VK_SUCCESS) {
    return false;
  }

  VkCommandBuffer cmd = vk.command_buffers[frame];
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &begin);

  if (allow_timestamps && vk.supports_timestamps && vk.query_pool) {
    const uint32_t query_index = frame * 2;
    vkCmdResetQueryPool(cmd, vk.query_pool, query_index, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        vk.query_pool, query_index);
  }

  bool has_3d = false;
  for (const auto& batch : gpu.batches) {
    if (batch.use_3d) {
      has_3d = true;
      break;
    }
  }

  VkClearValue clear[3]{};
  clear[0].color.float32[0] = 0.0f;
  clear[0].color.float32[1] = 0.0f;
  clear[0].color.float32[2] = 0.0f;
  clear[0].color.float32[3] = 0.0f;
  if (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT) {
    clear[1].depthStencil.depth = 1.0f;
    clear[1].depthStencil.stencil = 0;
  } else {
    clear[1].color.float32[0] = 0.0f;
    clear[1].color.float32[1] = 0.0f;
    clear[1].color.float32[2] = 0.0f;
    clear[1].color.float32[3] = 0.0f;
    clear[2].depthStencil.depth = 1.0f;
    clear[2].depthStencil.stencil = 0;
  }

  VkRenderPassBeginInfo rp{};
  rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp.renderPass = has_3d ? vk.render_pass_3d : vk.render_pass_2d;
  rp.framebuffer = has_3d ? vk.framebuffers_3d[image_index] : vk.framebuffers_2d[image_index];
  rp.renderArea.offset = {0, 0};
  rp.renderArea.extent = vk.extent;
  if (has_3d) {
    rp.clearValueCount = (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? 2 : 3;
  } else {
    rp.clearValueCount = (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? 1 : 2;
  }
  rp.pClearValues = clear;
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(vk.extent.width);
  viewport.height = static_cast<float>(vk.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{};
  scissor.extent = vk.extent;
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  if (has_vertices) {
    VkBuffer vb = vk.vertex_buffer;
    VkDeviceSize offsets[] = {static_cast<VkDeviceSize>(vk.vertex_capacity * frame)};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
  }

  float ortho[16];
  mat4_ortho(ortho, 0.0f, static_cast<float>(gpu.physical_width),
             static_cast<float>(gpu.physical_height), 0.0f);
  float perspective[16];
  mat4_perspective(perspective, 1.0f, static_cast<float>(gpu.physical_width) /
                                           static_cast<float>(gpu.physical_height),
                   0.1f, 100.0f);

  VkPipeline current_pipeline = VK_NULL_HANDLE;
  VkDescriptorSet current_desc = VK_NULL_HANDLE;
  bool current_3d = false;

  for (const auto& batch : gpu.batches) {
    VkPipeline desired = VK_NULL_HANDLE;
    if (batch.use_3d) {
      desired = batch.line ? vk.pipeline_line_3d : vk.pipeline_tri_3d;
    } else {
      desired = batch.line ? vk.pipeline_line_2d : vk.pipeline_tri_2d;
    }
    if (desired != current_pipeline) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desired);
      current_pipeline = desired;
    }
    const DrawImage* texture = static_cast<const DrawImage*>(batch.texture);
    VkDescriptorSet desc = texture ? texture->vk_desc : VK_NULL_HANDLE;
    if (desc != current_desc) {
      vkCmdBindDescriptorSets(cmd,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              vk.pipeline_layout,
                              0,
                              1,
                              &desc,
                              0,
                              nullptr);
      current_desc = desc;
    }
    if (batch.use_3d != current_3d) {
      current_3d = batch.use_3d;
    }
    const float* mvp = current_3d ? perspective : ortho;
    vkCmdPushConstants(cmd, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float) * 16, mvp);
    vkCmdDraw(cmd, batch.count, 1, batch.first, 0);
  }

  if (has_text && vk.pipeline_text_2d != VK_NULL_HANDLE && gpu.font_image.vk_desc) {
    VkBuffer tb = vk.text_buffer;
    VkDeviceSize offsets[] = {static_cast<VkDeviceSize>(vk.text_capacity * frame)};
    vkCmdBindVertexBuffers(cmd, 0, 1, &tb, offsets);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_text_2d);
    VkDescriptorSet desc = gpu.font_image.vk_desc;
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk.pipeline_layout,
                            0,
                            1,
                            &desc,
                            0,
                            nullptr);
    vkCmdPushConstants(cmd, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float) * 16, ortho);
    vkCmdDraw(cmd, 6, static_cast<uint32_t>(gpu.text_instances.size()), 0, 0);
  }

  vkCmdEndRenderPass(cmd);

  if (allow_timestamps && vk.supports_timestamps && vk.query_pool) {
    const uint32_t query_index = frame * 2 + 1;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        vk.query_pool, query_index);
  }
  vkEndCommandBuffer(cmd);

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &vk.image_available[frame];
  submit.pWaitDstStageMask = &wait_stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &vk.render_finished[frame];
  vkQueueSubmit(vk.queue, 1, &submit, vk.in_flight[frame]);

  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &vk.render_finished[frame];
  present.swapchainCount = 1;
  present.pSwapchains = &vk.swapchain;
  present.pImageIndices = &image_index;
  vkQueuePresentKHR(vk.queue, &present);

  const uint64_t now = now_ns();
  if (gpu.last_log_ns == 0) {
    gpu.last_log_ns = now;
  }
  if (want_log) {
    fprintf(stderr, "GPU frame: %.3f ms (Vulkan)\n", gpu_ms);
    gpu.last_log_ns = now;
  }
  gpu.frame_index++;
  if (vk.frame_count > 0) {
    vk.frame_index = (frame + 1) % vk.frame_count;
  }
  return true;
}

#else  // DRAW_ENGINE_HAS_VULKAN

static bool vk_upload_texture(VulkanContext&, DrawImage*) {
  return false;
}

static bool vk_init_context(VulkanContext&, ANativeWindow*, int, int) {
  return false;
}

static void vk_cleanup(VulkanContext&) {}

static bool vk_draw_frame(GpuState&) {
  return false;
}

#endif  // DRAW_ENGINE_HAS_VULKAN

#if DRAW_ENGINE_HAS_GLES
static GLuint gl_compile(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    fprintf(stderr, "GL shader compile error: %s\n", log);
  }
  return shader;
}

static bool gl_init_context(GlesContext& gl, ANativeWindow* window) {
  memset(&gl, 0, sizeof(gl));
  gl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (gl.display == EGL_NO_DISPLAY) {
    return false;
  }
  if (!eglInitialize(gl.display, nullptr, nullptr)) {
    return false;
  }

  const int msaa_req = env_int("DRAW_ENGINE_MSAA", 1);
  const EGLint config_attrs_msaa[] = {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 16,
      EGL_SAMPLE_BUFFERS, 1,
      EGL_SAMPLES, msaa_req,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
  };
  const EGLint config_attrs[] = {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 16,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
  };
  EGLint num_configs = 0;
  if (msaa_req > 1) {
    if (eglChooseConfig(gl.display, config_attrs_msaa, &gl.config, 1, &num_configs) &&
        num_configs > 0) {
      gl.samples = msaa_req;
    }
  }
  if (num_configs == 0) {
    if (!eglChooseConfig(gl.display, config_attrs, &gl.config, 1, &num_configs) ||
        num_configs == 0) {
      return false;
    }
  }

  EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  gl.context = eglCreateContext(gl.display, gl.config, EGL_NO_CONTEXT, ctx_attribs);
  if (gl.context == EGL_NO_CONTEXT) {
    return false;
  }
  gl.surface = eglCreateWindowSurface(gl.display, gl.config, window, nullptr);
  if (gl.surface == EGL_NO_SURFACE) {
    return false;
  }
  if (!eglMakeCurrent(gl.display, gl.surface, gl.surface, gl.context)) {
    return false;
  }

  const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  if (gl_version) {
    fprintf(stderr, "GLES version: %s\n", gl_version);
  }
  if (gl_renderer) {
    fprintf(stderr, "GLES renderer: %s\n", gl_renderer);
  }

  GLint ext_count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);
  for (GLint i = 0; i < ext_count; ++i) {
    const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
    if (!ext) {
      continue;
    }
    if (strcmp(ext, "GL_EXT_texture_filter_anisotropic") == 0) {
      gl.supports_anisotropy = true;
    }
  }
  if (gl.supports_anisotropy) {
    GLfloat max_aniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
    gl.max_anisotropy = max_aniso;
    fprintf(stderr, "GLES anisotropy: %.1f\n", gl.max_anisotropy);
  }

  const char* vs_src =
      "#version 300 es\n"
      "layout(location=0) in vec3 aPos;\n"
      "layout(location=1) in vec2 aUV;\n"
      "layout(location=2) in vec4 aColor;\n"
      "uniform mat4 uMVP;\n"
      "out vec2 vUV;\n"
      "out vec4 vColor;\n"
      "void main(){\n"
      "  gl_Position = uMVP * vec4(aPos,1.0);\n"
      "  vUV = aUV;\n"
      "  vColor = aColor;\n"
      "}\n";
  const char* fs_src =
      "#version 300 es\n"
      "precision highp float;\n"
      "in vec2 vUV;\n"
      "in vec4 vColor;\n"
      "uniform sampler2D uTex;\n"
      "out vec4 fragColor;\n"
      "void main(){\n"
      "  if (vUV.y > 1.5) {\n"
      "    vec2 uv = vec2(vUV.x, vUV.y - 2.0);\n"
      "    vec4 tex = texture(uTex, uv);\n"
      "    fragColor = tex * vColor;\n"
      "  } else {\n"
      "    float sdf = texture(uTex, vUV).a;\n"
      "    float w = fwidth(sdf);\n"
      "    float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);\n"
      "    fragColor = vec4(vColor.rgb, vColor.a * alpha);\n"
      "  }\n"
      "}\n";
  const char* vs_text_src =
      "#version 300 es\n"
      "layout(location=0) in vec4 aRect;\n"
      "layout(location=1) in vec4 aUV;\n"
      "layout(location=2) in vec4 aColor;\n"
      "uniform mat4 uMVP;\n"
      "out vec2 vUV;\n"
      "out vec4 vColor;\n"
      "void main(){\n"
      "  int vid = gl_VertexID;\n"
      "  int idx = (vid == 0) ? 0 : (vid == 1) ? 1 : (vid == 2) ? 2 : (vid == 3) ? 0 : (vid == 4) ? 2 : 3;\n"
      "  vec2 corner = (idx == 0) ? vec2(0.0, 0.0) : (idx == 1) ? vec2(1.0, 0.0) : (idx == 2) ? vec2(1.0, 1.0) : vec2(0.0, 1.0);\n"
      "  vec2 pos = aRect.xy + corner * aRect.zw;\n"
      "  vUV = mix(aUV.xy, aUV.zw, corner);\n"
      "  vColor = aColor;\n"
      "  gl_Position = uMVP * vec4(pos, 0.0, 1.0);\n"
      "}\n";
  GLuint vs = gl_compile(GL_VERTEX_SHADER, vs_src);
  GLuint fs = gl_compile(GL_FRAGMENT_SHADER, fs_src);
  gl.program = glCreateProgram();
  glAttachShader(gl.program, vs);
  glAttachShader(gl.program, fs);
  glLinkProgram(gl.program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint linked = 0;
  glGetProgramiv(gl.program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[512];
    glGetProgramInfoLog(gl.program, sizeof(log), nullptr, log);
    fprintf(stderr, "GL program link error: %s\n", log);
    return false;
  }

  GLuint vs_text = gl_compile(GL_VERTEX_SHADER, vs_text_src);
  GLuint fs_text = gl_compile(GL_FRAGMENT_SHADER, fs_src);
  gl.program_text = glCreateProgram();
  glAttachShader(gl.program_text, vs_text);
  glAttachShader(gl.program_text, fs_text);
  glLinkProgram(gl.program_text);
  glDeleteShader(vs_text);
  glDeleteShader(fs_text);
  glGetProgramiv(gl.program_text, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[512];
    glGetProgramInfoLog(gl.program_text, sizeof(log), nullptr, log);
    fprintf(stderr, "GL text program link error: %s\n", log);
    return false;
  }

  glGenVertexArrays(1, &gl.vao);
  glGenBuffers(1, &gl.vbo);

  glBindVertexArray(gl.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
  int max_vertices = env_int("DRAW_ENGINE_GL_MAX_VERTICES", 300000);
  if (max_vertices < 65536) {
    max_vertices = 65536;
  }
  glBufferData(GL_ARRAY_BUFFER, sizeof(GpuVertex) * static_cast<size_t>(max_vertices),
               nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(12));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuVertex),
                        reinterpret_cast<void*>(20));

  gl.u_mvp = glGetUniformLocation(gl.program, "uMVP");
  gl.u_tex = glGetUniformLocation(gl.program, "uTex");

  gl.u_mvp_text = glGetUniformLocation(gl.program_text, "uMVP");
  gl.u_tex_text = glGetUniformLocation(gl.program_text, "uTex");

  glGenVertexArrays(1, &gl.vao_text);
  glGenBuffers(1, &gl.vbo_text);
  glBindVertexArray(gl.vao_text);
  glBindBuffer(GL_ARRAY_BUFFER, gl.vbo_text);
  int max_instances = env_int("DRAW_ENGINE_GL_TEXT_INSTANCES", 8192);
  if (max_instances < 256) {
    max_instances = 256;
  }
  gl.text_buffer_size = sizeof(TextInstance) * static_cast<size_t>(max_instances);
  glBufferData(GL_ARRAY_BUFFER, gl.text_buffer_size, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(TextInstance),
                        reinterpret_cast<void*>(0));
  glVertexAttribDivisor(0, 1);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(TextInstance),
                        reinterpret_cast<void*>(16));
  glVertexAttribDivisor(1, 1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TextInstance),
                        reinterpret_cast<void*>(32));
  glVertexAttribDivisor(2, 1);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  if (gl.samples > 1) {
#ifdef GL_MULTISAMPLE
    glEnable(GL_MULTISAMPLE);
#endif
#ifdef GL_SAMPLE_ALPHA_TO_COVERAGE
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
#endif
    fprintf(stderr, "GLES MSAA: %dx\n", gl.samples);
  }

  gl.ready = true;
  return true;
}

static bool gl_upload_texture(DrawImage* image) {
  if (!image || !image->rgba || image->width <= 0 || image->height <= 0) {
    return false;
  }
  if (image->gl_ready) {
    return true;
  }
  glGenTextures(1, &image->gl_tex);
  glBindTexture(GL_TEXTURE_2D, image->gl_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (g_gpu.gl.supports_anisotropy) {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_gpu.gl.max_anisotropy);
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image->rgba);
  image->gl_ready = true;
  return true;
}

static void gl_cleanup(GlesContext& gl) {
  if (!gl.ready) {
    return;
  }
  if (gl.vbo) {
    glDeleteBuffers(1, &gl.vbo);
  }
  if (gl.vao) {
    glDeleteVertexArrays(1, &gl.vao);
  }
  if (gl.vbo_text) {
    glDeleteBuffers(1, &gl.vbo_text);
  }
  if (gl.vao_text) {
    glDeleteVertexArrays(1, &gl.vao_text);
  }
  if (gl.program) {
    glDeleteProgram(gl.program);
  }
  if (gl.program_text) {
    glDeleteProgram(gl.program_text);
  }
  eglMakeCurrent(gl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (gl.surface != EGL_NO_SURFACE) {
    eglDestroySurface(gl.display, gl.surface);
  }
  if (gl.context != EGL_NO_CONTEXT) {
    eglDestroyContext(gl.display, gl.context);
  }
  eglTerminate(gl.display);
  memset(&gl, 0, sizeof(gl));
}

static bool gl_draw_frame(GpuState& gpu) {
  GlesContext& gl = gpu.gl;
  if (!gl.ready) {
    return false;
  }
  const bool has_vertices = !gpu.vertices.empty();
  const bool has_text = !gpu.text_instances.empty();
  if (!has_vertices && !has_text) {
    return true;
  }
  bool has_3d = false;
  for (const auto& batch : gpu.batches) {
    if (batch.use_3d) {
      has_3d = true;
      break;
    }
  }
  const uint64_t start = now_ns();
  if (has_vertices) {
    glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gpu.vertices.size() * sizeof(GpuVertex),
                    gpu.vertices.data());
  }

  glViewport(0, 0, gpu.physical_width, gpu.physical_height);
  glClearColor(0, 0, 0, 0);
  if (has_3d) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  } else {
    glClear(GL_COLOR_BUFFER_BIT);
  }

  float ortho[16];
  mat4_ortho(ortho, 0.0f, static_cast<float>(gpu.physical_width),
             static_cast<float>(gpu.physical_height), 0.0f);
  float perspective[16];
  mat4_perspective(perspective, 1.0f, static_cast<float>(gpu.physical_width) /
                                           static_cast<float>(gpu.physical_height),
                   0.1f, 100.0f);

  if (has_vertices) {
    glUseProgram(gl.program);
    glBindVertexArray(gl.vao);
    for (const auto& batch : gpu.batches) {
      const DrawImage* texture = static_cast<const DrawImage*>(batch.texture);
      if (texture && texture->gl_ready) {
        glBindTexture(GL_TEXTURE_2D, texture->gl_tex);
      }
      glUniform1i(gl.u_tex, 0);
      glUniformMatrix4fv(gl.u_mvp, 1, GL_FALSE, batch.use_3d ? perspective : ortho);
      if (batch.use_3d) {
        glEnable(GL_DEPTH_TEST);
      } else {
        glDisable(GL_DEPTH_TEST);
      }
      GLenum mode = batch.line ? GL_LINES : GL_TRIANGLES;
      glDrawArrays(mode, batch.first, batch.count);
    }
  }
  if (has_text) {
    const size_t bytes = gpu.text_instances.size() * sizeof(TextInstance);
    if (bytes > gl.text_buffer_size) {
      gl.text_buffer_size = bytes;
      glBindBuffer(GL_ARRAY_BUFFER, gl.vbo_text);
      glBufferData(GL_ARRAY_BUFFER, gl.text_buffer_size, nullptr, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, gl.vbo_text);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, gpu.text_instances.data());
    glUseProgram(gl.program_text);
    glBindVertexArray(gl.vao_text);
    const DrawImage* texture = &gpu.font_image;
    if (texture && texture->gl_ready) {
      glBindTexture(GL_TEXTURE_2D, texture->gl_tex);
    }
    glUniform1i(gl.u_tex_text, 0);
    glUniformMatrix4fv(gl.u_mvp_text, 1, GL_FALSE, ortho);
    glDisable(GL_DEPTH_TEST);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(gpu.text_instances.size()));
  }
  const int finish = env_int("DRAW_ENGINE_GL_FINISH", 0);
  if (finish != 0) {
    glFinish();
  }
  eglSwapBuffers(gl.display, gl.surface);
  const uint64_t end = now_ns();
  if (gpu.log_interval <= 0) {
    gpu.log_interval = env_int("DRAW_ENGINE_LOG_INTERVAL", 120);
  }
  if (gpu.log_interval > 0 && (gpu.frame_index % static_cast<uint64_t>(gpu.log_interval)) == 0) {
    fprintf(stderr, "GPU frame: %.3f ms (GLES)\n", (end - start) / 1000000.0);
  }
  gpu.frame_index++;
  return true;
}

#else  // DRAW_ENGINE_HAS_GLES

static bool gl_init_context(GlesContext&, ANativeWindow*) {
  return false;
}

static bool gl_upload_texture(DrawImage*) {
  return false;
}

static void gl_cleanup(GlesContext&) {}

static bool gl_draw_frame(GpuState&) {
  return false;
}

#endif  // DRAW_ENGINE_HAS_GLES

static void gpu_destroy_image(GpuState& gpu, DrawImage* image) {
  if (!image) {
    return;
  }
  if (gpu.backend == BACKEND_VULKAN && image->vk_ready) {
#if DRAW_ENGINE_HAS_VULKAN
    VulkanContext& vk = gpu.vk;
    if (image->vk_desc && vk.desc_pool) {
      vkFreeDescriptorSets(vk.device, vk.desc_pool, 1, &image->vk_desc);
    }
    if (image->vk_view) {
      vkDestroyImageView(vk.device, image->vk_view, nullptr);
    }
    if (image->vk_image) {
      vkDestroyImage(vk.device, image->vk_image, nullptr);
    }
    if (image->vk_memory) {
      vkFreeMemory(vk.device, image->vk_memory, nullptr);
    }
    image->vk_ready = false;
#else
    (void)gpu;
    (void)image;
#endif
  }
  if (gpu.backend == BACKEND_GLES && image->gl_ready) {
#if DRAW_ENGINE_HAS_GLES
    glDeleteTextures(1, &image->gl_tex);
    image->gl_ready = false;
#else
    (void)gpu;
    (void)image;
#endif
  }
}

static void init_white_image(GpuState& gpu) {
  if (gpu.white_image.rgba) {
    return;
  }
  gpu.white_image.width = 1;
  gpu.white_image.height = 1;
  gpu.white_image.rgba = static_cast<unsigned char*>(malloc(4));
  gpu.white_image.pixels = nullptr;
  if (gpu.white_image.rgba) {
    gpu.white_image.rgba[0] = 255;
    gpu.white_image.rgba[1] = 255;
    gpu.white_image.rgba[2] = 255;
    gpu.white_image.rgba[3] = 255;
  }
}

static bool gpu_prepare_texture(GpuState& gpu, DrawImage* image) {
  if (!image) {
    return false;
  }
  if (gpu.backend == BACKEND_VULKAN) {
    return vk_upload_texture(gpu.vk, image);
  }
  if (gpu.backend == BACKEND_GLES) {
    return gl_upload_texture(image);
  }
  return false;
}

static void gpu_refresh_size_and_rotation() {
  if (g_engine.backend == BACKEND_CPU || !g_gpu.window) {
    return;
  }
  static int last_w = 0;
  static int last_h = 0;
  static int last_rot = -1;
  static uint64_t last_query_ns = 0;
  const int throttle_ms = env_int("DRAW_ENGINE_ROTATION_THROTTLE_MS", 200);
  const bool force_update = g_engine.render_size_manual || g_engine.render_scale_manual;
  if (!force_update && throttle_ms > 0) {
    const uint64_t now = now_ns();
    if (last_query_ns != 0 &&
        (now - last_query_ns) < static_cast<uint64_t>(throttle_ms) * 1000000ull) {
      return;
    }
    last_query_ns = now;
  }
  int display_rot = 0;
  int display_w = 0;
  int display_h = 0;
  const bool got_display =
      (midraw_display_rotation(g_engine.cpu_ctx, &display_rot, &display_w, &display_h) == 0);
  int display_logical_w = display_w;
  int display_logical_h = display_h;
  if (display_w > 0 && display_h > 0) {
    const int long_side = (display_w > display_h) ? display_w : display_h;
    const int short_side = (display_w > display_h) ? display_h : display_w;
    const bool rot_landscape = (display_rot == 90 || display_rot == 270);
    const bool size_landscape = display_w >= display_h;
    if (rot_landscape != size_landscape) {
      display_logical_w = long_side;
      display_logical_h = short_side;
    } else {
      display_logical_w = display_w;
      display_logical_h = display_h;
    }
  }

  int aw_w = 0;
  int aw_h = 0;
  if (!query_native_window_size(g_gpu.window, &aw_w, &aw_h)) {
    aw_w = 0;
    aw_h = 0;
  }

  const int manual_mode = env_int("DRAW_ENGINE_MANUAL_ROTATION", 0);
  const int use_display_size = env_int("DRAW_ENGINE_USE_DISPLAY_SIZE", 0);
  const int resize_on_rotation = env_int("DRAW_ENGINE_RESIZE_ON_ROTATION", 0);
  const int counter_rotation = env_int("DRAW_ENGINE_COUNTER_ROTATION", 1);
  int target_w = aw_w;
  int target_h = aw_h;
  const bool has_render_size = (g_engine.render_width > 0 && g_engine.render_height > 0);
  const bool has_render_scale =
      (g_engine.render_scale > 0.0f && g_engine.render_scale < 0.999f);
  if (has_render_size) {
    target_w = g_engine.render_width;
    target_h = g_engine.render_height;
  } else if (has_render_scale && got_display && display_logical_w > 0 && display_logical_h > 0) {
    target_w = static_cast<int>(display_logical_w * g_engine.render_scale + 0.5f);
    target_h = static_cast<int>(display_logical_h * g_engine.render_scale + 0.5f);
  } else if (manual_mode == 0 && use_display_size != 0 && got_display && display_logical_w > 0 &&
             display_logical_h > 0) {
    target_w = display_logical_w;
    target_h = display_logical_h;
  }
  if (target_w < 1 || target_h < 1) {
    target_w = aw_w;
    target_h = aw_h;
  }
  const bool want_resize = has_render_size || has_render_scale || (resize_on_rotation != 0);
  if (target_w > 0 && target_h > 0 && want_resize) {
    if (g_gpu.physical_width != target_w || g_gpu.physical_height != target_h) {
      bool resized = false;
      if (g_engine.cpu_ctx && (manual_mode == 0 || has_render_size || has_render_scale)) {
        if (midraw_resize(g_engine.cpu_ctx, target_w, target_h) == 0) {
          g_gpu.physical_width = target_w;
          g_gpu.physical_height = target_h;
          resized = true;
        }
      }
      if (!resized && aw_w > 0 && aw_h > 0) {
        g_gpu.physical_width = aw_w;
        g_gpu.physical_height = aw_h;
      }
    }
  } else if (aw_w > 0 && aw_h > 0) {
    g_gpu.physical_width = aw_w;
    g_gpu.physical_height = aw_h;
  }
  if (!g_engine.rotation_forced && env_int("DRAW_ENGINE_AUTO_ROTATE", 1) != 0) {
    if (manual_mode != 0) {
      if (got_display) {
        g_engine.rotation = display_rot;
        g_gpu.rotation = display_rot;
      } else if (aw_w > 0 && aw_h > 0) {
        const int guess = (aw_w > aw_h) ? 90 : 0;
        g_engine.rotation = guess;
        g_gpu.rotation = guess;
      }
    } else if (counter_rotation != 0 && got_display) {
      bool window_matches_display = false;
      if (aw_w > 0 && aw_h > 0 && display_logical_w > 0 && display_logical_h > 0) {
        window_matches_display =
            (aw_w == display_logical_w && aw_h == display_logical_h);
      }
      if (window_matches_display) {
        g_engine.rotation = 0;
        g_gpu.rotation = 0;
      } else {
        int rot = 0;
        if (display_rot == 90) {
          rot = 270;
        } else if (display_rot == 270) {
          rot = 90;
        } else if (display_rot == 180) {
          rot = 180;
        }
        g_engine.rotation = rot;
        g_gpu.rotation = rot;
      }
    } else {
      g_engine.rotation = 0;
      g_gpu.rotation = 0;
    }
  }

  const int set_transform = env_int("DRAW_ENGINE_SET_BUFFER_TRANSFORM", 1);
  if (set_transform != 0 && manual_mode == 0 && got_display &&
      g_engine.backend != BACKEND_VULKAN) {
    bool window_matches_display = false;
    if (aw_w > 0 && aw_h > 0 && display_logical_w > 0 && display_logical_h > 0) {
      window_matches_display =
          (aw_w == display_logical_w && aw_h == display_logical_h);
    }
    int desired_transform = 0;
    if (!window_matches_display) {
      if (display_rot == 90) {
        desired_transform = 4;
      } else if (display_rot == 270) {
        desired_transform = 7;
      } else if (display_rot == 180) {
        desired_transform = 3;
      }
    }
    static int last_transform = INT_MIN;
    if (desired_transform != last_transform) {
      if (set_native_window_transform(g_gpu.window,
                                      static_cast<int32_t>(desired_transform))) {
        fprintf(stderr, "GPU set buffers transform=%d\n", desired_transform);
      }
      last_transform = desired_transform;
    }
  }

  if (got_display && display_logical_w > 0 && display_logical_h > 0) {
    g_gpu.logical_width = display_logical_w;
    g_gpu.logical_height = display_logical_h;
  } else if (g_gpu.rotation == 90 || g_gpu.rotation == 270) {
    g_gpu.logical_width = g_gpu.physical_height;
    g_gpu.logical_height = g_gpu.physical_width;
  } else {
    g_gpu.logical_width = g_gpu.physical_width;
    g_gpu.logical_height = g_gpu.physical_height;
  }
  if (g_gpu.logical_width > 0 && g_gpu.logical_height > 0 &&
      g_gpu.physical_width > 0 && g_gpu.physical_height > 0) {
    g_gpu.scale_x = static_cast<float>(g_gpu.physical_width) /
                    static_cast<float>(g_gpu.logical_width);
    g_gpu.scale_y = static_cast<float>(g_gpu.physical_height) /
                    static_cast<float>(g_gpu.logical_height);
  } else {
    g_gpu.scale_x = 1.0f;
    g_gpu.scale_y = 1.0f;
  }
  static int last_disp_w = 0;
  static int last_disp_h = 0;
  static int last_disp_rot = -1;
  static int last_aw_w = 0;
  static int last_aw_h = 0;
  if (got_display &&
      (display_w != last_disp_w || display_h != last_disp_h || display_rot != last_disp_rot)) {
    fprintf(stderr,
            "GPU display info: rot=%d raw=%dx%d logical=%dx%d\n",
            display_rot,
            display_w,
            display_h,
            display_logical_w,
            display_logical_h);
    last_disp_w = display_w;
    last_disp_h = display_h;
    last_disp_rot = display_rot;
  }
  if (aw_w != last_aw_w || aw_h != last_aw_h) {
    fprintf(stderr, "GPU window size: %dx%d\n", aw_w, aw_h);
    last_aw_w = aw_w;
    last_aw_h = aw_h;
  }
  static int last_display_logical_w = 0;
  static int last_display_logical_h = 0;
  static int last_display_logical_rot = INT_MIN;
  if (got_display && display_logical_w > 0 && display_logical_h > 0 &&
      (display_logical_w != last_display_logical_w ||
       display_logical_h != last_display_logical_h ||
       g_gpu.rotation != last_display_logical_rot)) {
    fprintf(stderr, "GPU display logical: %dx%d mapped_rot=%d\n",
            display_logical_w, display_logical_h, g_gpu.rotation);
    last_display_logical_w = display_logical_w;
    last_display_logical_h = display_logical_h;
    last_display_logical_rot = g_gpu.rotation;
  }
  if (g_engine.backend == BACKEND_VULKAN) {
    static VkSurfaceTransformFlagBitsKHR last_vk_transform =
        VK_SURFACE_TRANSFORM_FLAG_BITS_MAX_ENUM_KHR;
    if (g_gpu.vk.pre_transform != last_vk_transform) {
      fprintf(stderr, "GPU Vulkan preTransform=0x%x\n",
              static_cast<unsigned int>(g_gpu.vk.pre_transform));
      last_vk_transform = g_gpu.vk.pre_transform;
    }
  }
  if (g_gpu.physical_width != last_w || g_gpu.physical_height != last_h ||
      g_gpu.rotation != last_rot) {
    fprintf(stderr, "GPU rotate update: rot=%d phys=%dx%d logical=%dx%d\n",
            g_gpu.rotation,
            g_gpu.physical_width,
            g_gpu.physical_height,
            g_gpu.logical_width,
            g_gpu.logical_height);
#if DRAW_ENGINE_HAS_VULKAN
    if (g_engine.backend == BACKEND_VULKAN && g_gpu.vk.ready &&
        (g_gpu.vk.extent.width != static_cast<uint32_t>(g_gpu.physical_width) ||
         g_gpu.vk.extent.height != static_cast<uint32_t>(g_gpu.physical_height))) {
      vkDeviceWaitIdle(g_gpu.vk.device);
      vk_destroy_swapchain(g_gpu.vk);
      if (vk_create_swapchain_resources(g_gpu.vk,
                                        g_gpu.physical_width,
                                        g_gpu.physical_height)) {
        g_gpu.physical_width = static_cast<int>(g_gpu.vk.extent.width);
        g_gpu.physical_height = static_cast<int>(g_gpu.vk.extent.height);
        if (g_gpu.rotation == 90 || g_gpu.rotation == 270) {
          g_gpu.logical_width = g_gpu.physical_height;
          g_gpu.logical_height = g_gpu.physical_width;
        } else {
          g_gpu.logical_width = g_gpu.physical_width;
          g_gpu.logical_height = g_gpu.physical_height;
        }
      }
    }
#endif
    last_w = g_gpu.physical_width;
    last_h = g_gpu.physical_height;
    last_rot = g_gpu.rotation;
  }
}

static int recreate_engine_surface() {
  if (!g_engine.cpu_ctx) {
    return -1;
  }
  const BackendType backend = g_engine.backend;
  const int rotation = g_engine.rotation;
  const bool rotation_forced = g_engine.rotation_forced;
  const bool hybrid = g_engine.hybrid;

  midraw_shutdown(g_engine.cpu_ctx);
  g_engine.cpu_ctx = nullptr;
  if (g_engine.overlay_ctx) {
    midraw_shutdown(g_engine.overlay_ctx);
    g_engine.overlay_ctx = nullptr;
  }
  gpu_shutdown(g_gpu);

  MidrawConfig cfg{};
  cfg.rotation = rotation;
  cfg.width = 0;
  cfg.height = 0;
  cfg.font_path = getenv("MIDRAW_FONT_PATH");
  cfg.font_size = env_int("MIDRAW_FONT_SIZE", 0);
  cfg.atlas_size = env_int("MIDRAW_ATLAS_SIZE", 512);

  const char* gpu_surface = g_engine.surface_name;
  char gpu_name[256];
  char cpu_name[256];
  if (hybrid) {
    snprintf(gpu_name, sizeof(gpu_name), "%s_gpu", g_engine.surface_name);
    gpu_surface = gpu_name;
  }
  cfg.surface_name = gpu_surface;
  if (midraw_init(&g_engine.cpu_ctx, &cfg) != 0) {
    fprintf(stderr, "draw_engine: recreate midraw_init failed\n");
    return -1;
  }
  if (hybrid) {
    snprintf(cpu_name, sizeof(cpu_name), "%s_cpu", g_engine.surface_name);
    cfg.surface_name = cpu_name;
    if (midraw_init(&g_engine.overlay_ctx, &cfg) != 0) {
      fprintf(stderr, "draw_engine: recreate overlay midraw_init failed\n");
      g_engine.overlay_ctx = nullptr;
      g_engine.hybrid = false;
    } else {
      midraw_set_layer(g_engine.cpu_ctx, INT_MAX - 1);
      midraw_set_layer(g_engine.overlay_ctx, INT_MAX);
    }
  }
  g_engine.rotation_forced = rotation_forced;
  g_engine.rotation = rotation;

  if (backend != BACKEND_CPU) {
    ANativeWindow* window =
        reinterpret_cast<ANativeWindow*>(midraw_get_native_window(g_engine.cpu_ctx));
    const int w = midraw_logical_width(g_engine.cpu_ctx);
    const int h = midraw_logical_height(g_engine.cpu_ctx);
    if (!window || !gpu_init(g_gpu, backend, window, w, h, g_engine.rotation)) {
      const bool can_try_gles =
          (backend == BACKEND_VULKAN) && DRAW_ENGINE_HAS_GLES &&
          (has_library("libGLESv3.so") || has_library("libGLESv2.so"));
      fprintf(stderr, "draw_engine: recreate GPU init failed (%s)\n",
              backend == BACKEND_VULKAN ? "Vulkan" : "GLES");
      gpu_shutdown(g_gpu);
      if (can_try_gles) {
        if (gpu_init(g_gpu, BACKEND_GLES, window, w, h, g_engine.rotation)) {
          g_engine.backend = BACKEND_GLES;
          fprintf(stderr,
                  "draw_engine: recreate fallback to GLES logical=%dx%d physical=%dx%d rot=%d\n",
                  g_gpu.logical_width,
                  g_gpu.logical_height,
                  g_gpu.physical_width,
                  g_gpu.physical_height,
                  g_engine.rotation);
        } else {
          fprintf(stderr, "draw_engine: recreate GLES init failed, fallback CPU\n");
          gpu_shutdown(g_gpu);
          g_engine.backend = BACKEND_CPU;
        }
      } else {
        fprintf(stderr, "draw_engine: recreate fallback CPU\n");
        g_engine.backend = BACKEND_CPU;
      }
    } else {
      g_engine.backend = backend;
      fprintf(stderr, "draw_engine: GPU surface logical=%dx%d physical=%dx%d rot=%d\n",
              g_gpu.logical_width,
              g_gpu.logical_height,
              g_gpu.physical_width,
              g_gpu.physical_height,
              g_engine.rotation);
    }
  } else {
    g_engine.backend = BACKEND_CPU;
  }
  if (g_engine.backend != BACKEND_CPU) {
    maybe_apply_default_gpu_fps();
  }
  if (g_engine.backend == BACKEND_CPU && g_engine.overlay_ctx) {
    midraw_shutdown(g_engine.overlay_ctx);
    g_engine.overlay_ctx = nullptr;
    g_engine.hybrid = false;
  }
  if (g_engine.auto_quality != 0) {
    apply_quality_level(g_engine.quality_level);
  } else {
    apply_render_target();
  }
  return 0;
}

static void maybe_recreate_on_rotation() {
  if (!g_engine.cpu_ctx || g_engine.backend == BACKEND_CPU) {
    return;
  }
  if (env_int("DRAW_ENGINE_RECREATE_ON_ROTATION", 1) == 0) {
    return;
  }
  static uint64_t last_query_ns = 0;
  const int throttle_ms = env_int("DRAW_ENGINE_RECREATE_THROTTLE_MS", 200);
  if (throttle_ms > 0) {
    const uint64_t now = now_ns();
    if (last_query_ns != 0 &&
        (now - last_query_ns) < static_cast<uint64_t>(throttle_ms) * 1000000ull) {
      return;
    }
    last_query_ns = now;
  }
  static int last_display_rot = INT_MIN;
  int display_rot = 0;
  int display_w = 0;
  int display_h = 0;
  if (midraw_display_rotation(g_engine.cpu_ctx, &display_rot, &display_w, &display_h) != 0) {
    return;
  }
  if (last_display_rot == INT_MIN) {
    last_display_rot = display_rot;
    return;
  }
  if (display_rot != last_display_rot) {
    fprintf(stderr, "draw_engine: rotation changed %d -> %d, recreating surface\n",
            last_display_rot, display_rot);
    last_display_rot = display_rot;
    recreate_engine_surface();
  }
}

static bool gpu_init(GpuState& gpu, BackendType backend, ANativeWindow* window, int width,
                     int height, int rotation) {
  memset(&gpu, 0, sizeof(gpu));
  gpu.backend = backend;
  gpu.window = window;
  gpu.logical_width = width;
  gpu.logical_height = height;
  gpu.physical_width = width;
  gpu.physical_height = height;
  gpu.rotation = rotation;
  gpu.scale_x = 1.0f;
  gpu.scale_y = 1.0f;
  gpu.circle_segments = 0;
  gpu.circle_lut.clear();
  gpu.text_cache.clear();
  gpu.text_cache_bytes = 0;
  gpu.text_cache_limit = 0;
  gpu.text_cache_max_entries = 0;

  if (rotation == 90 || rotation == 270) {
    gpu.physical_width = height;
    gpu.physical_height = width;
  }

  init_white_image(gpu);
  init_gpu_font(gpu);
  gpu_text_cache_init(gpu);

  bool ok = false;
  if (backend == BACKEND_VULKAN) {
    if (DRAW_ENGINE_HAS_VULKAN) {
      ok = vk_init_context(gpu.vk, window, gpu.physical_width, gpu.physical_height);
      if (ok) {
        gpu.physical_width = static_cast<int>(gpu.vk.extent.width);
        gpu.physical_height = static_cast<int>(gpu.vk.extent.height);
        if (rotation == 90 || rotation == 270) {
          gpu.logical_width = gpu.physical_height;
          gpu.logical_height = gpu.physical_width;
        } else {
          gpu.logical_width = gpu.physical_width;
          gpu.logical_height = gpu.physical_height;
        }
      }
    }
  } else if (backend == BACKEND_GLES) {
    if (DRAW_ENGINE_HAS_GLES) {
      ok = gl_init_context(gpu.gl, window);
      if (ok) {
        EGLint w = 0;
        EGLint h = 0;
        if (eglQuerySurface(gpu.gl.display, gpu.gl.surface, EGL_WIDTH, &w) &&
            eglQuerySurface(gpu.gl.display, gpu.gl.surface, EGL_HEIGHT, &h) &&
            w > 0 && h > 0) {
          gpu.physical_width = static_cast<int>(w);
          gpu.physical_height = static_cast<int>(h);
          if (rotation == 90 || rotation == 270) {
            gpu.logical_width = gpu.physical_height;
            gpu.logical_height = gpu.physical_width;
          } else {
            gpu.logical_width = gpu.physical_width;
            gpu.logical_height = gpu.physical_height;
          }
        }
      }
    }
  }
  if (!ok) {
    return false;
  }
  if (backend != BACKEND_CPU && !g_engine.hybrid) {
    float fps = (g_engine.target_fps > 0) ? static_cast<float>(g_engine.target_fps)
                                          : query_native_window_refresh_rate(window);
    if (fps <= 0.0f) {
      fps = query_choreographer_refresh_rate();
    }
    if (fps <= 0.0f) {
      fps = 60.0f;
    }
    apply_window_frame_rate(window, fps);
  }
  gpu_prepare_texture(gpu, &gpu.white_image);
  gpu_prepare_texture(gpu, &gpu.font_image);
  return true;
}

static void gpu_shutdown(GpuState& gpu) {
  gpu_destroy_image(gpu, &gpu.font_image);
  gpu_destroy_image(gpu, &gpu.white_image);
  if (gpu.backend == BACKEND_VULKAN) {
    vk_cleanup(gpu.vk);
  } else if (gpu.backend == BACKEND_GLES) {
    gl_cleanup(gpu.gl);
  }
  if (gpu.font_image.rgba) {
    free(gpu.font_image.rgba);
    gpu.font_image.rgba = nullptr;
  }
  if (gpu.white_image.rgba) {
    free(gpu.white_image.rgba);
    gpu.white_image.rgba = nullptr;
  }
  gpu.text_cache.clear();
  gpu.text_cache_bytes = 0;
  gpu.text_cache_limit = 0;
  gpu.text_cache_max_entries = 0;
  gpu.circle_lut.clear();
  gpu.circle_segments = 0;
  memset(&gpu, 0, sizeof(gpu));
}

int init_draw_engine(int mode) {
  memset(&g_engine, 0, sizeof(g_engine));
  if (mode < DRAW_ENGINE_MODE_AUTO || mode > DRAW_ENGINE_MODE_HYBRID) {
    mode = DRAW_ENGINE_MODE_AUTO;
  }
  g_engine.mode = mode;
  g_engine.backend = BACKEND_CPU;
  g_engine.render_scale_manual = env_present("DRAW_ENGINE_RENDER_SCALE");
  g_engine.render_scale =
      g_engine.render_scale_manual ? env_float("DRAW_ENGINE_RENDER_SCALE", 1.0f) : 1.0f;
  if (g_engine.render_scale < 0.25f) {
    g_engine.render_scale = 0.25f;
  } else if (g_engine.render_scale > 1.0f) {
    g_engine.render_scale = 1.0f;
  }
  g_engine.render_size_manual =
      env_present("DRAW_ENGINE_RENDER_WIDTH") || env_present("DRAW_ENGINE_RENDER_HEIGHT");
  g_engine.render_width = g_engine.render_size_manual ? env_int("DRAW_ENGINE_RENDER_WIDTH", 0) : 0;
  g_engine.render_height =
      g_engine.render_size_manual ? env_int("DRAW_ENGINE_RENDER_HEIGHT", 0) : 0;
  g_engine.circle_segments_manual = env_present("DRAW_ENGINE_CIRCLE_SEGMENTS");
  g_engine.circle_segments =
      g_engine.circle_segments_manual ? env_int("DRAW_ENGINE_CIRCLE_SEGMENTS", 64) : 64;
  if (g_engine.circle_segments < 8) {
    g_engine.circle_segments = 8;
  }
  const bool fps_env_present = env_present("DRAW_ENGINE_FPS");
  int fps_env = env_int("DRAW_ENGINE_FPS", 0);
  if (fps_env > 1000) {
    fps_env = 1000;
  }
  if (fps_env_present && fps_env > 0) {
    g_engine.target_fps = fps_env;
    g_engine.fps_manual = true;
  } else {
    g_engine.target_fps = 0;
    g_engine.fps_manual = false;
  }
  g_engine.auto_fps = 0.0f;
  g_engine.avg_draw_ns = 0.0;
  g_engine.frame_start_ns = 0;
  init_sleep_bias();
  init_hybrid_policy();
  g_engine.auto_quality = env_int("DRAW_ENGINE_AUTO_QUALITY", 0);
  g_engine.quality_min = env_int("DRAW_ENGINE_QUALITY_MIN", 0);
  g_engine.quality_max = env_int("DRAW_ENGINE_QUALITY_MAX", 3);
  if (g_engine.quality_min < 0) {
    g_engine.quality_min = 0;
  }
  if (g_engine.quality_max > 3) {
    g_engine.quality_max = 3;
  }
  if (g_engine.quality_max < g_engine.quality_min) {
    g_engine.quality_max = g_engine.quality_min;
  }
  g_engine.quality_level = env_int("DRAW_ENGINE_QUALITY_LEVEL", g_engine.quality_max);
  if (g_engine.quality_level < g_engine.quality_min) {
    g_engine.quality_level = g_engine.quality_min;
  }
  if (g_engine.quality_level > g_engine.quality_max) {
    g_engine.quality_level = g_engine.quality_max;
  }
  g_engine.last_quality_ns = 0;

  if (mode == DRAW_ENGINE_MODE_CPU) {
    g_engine.backend = BACKEND_CPU;
  } else {
    const BackendType chosen = choose_best_backend();
    if (mode == DRAW_ENGINE_MODE_GPU) {
      if (chosen == BACKEND_CPU) {
        return DRAW_ENGINE_ENOBACKEND;
      }
      g_engine.backend = chosen;
    } else if (mode == DRAW_ENGINE_MODE_HYBRID) {
      if (chosen != BACKEND_CPU) {
        g_engine.backend = chosen;
        g_engine.hybrid = true;
      } else {
        g_engine.backend = BACKEND_CPU;
        g_engine.hybrid = false;
      }
    } else {
      g_engine.backend = chosen;
    }
  }

  g_engine.initialized = true;
  return DRAW_ENGINE_OK;
}

int init_engine_mode(int mode) {
  return init_draw_engine(mode);
}

void draw_engine_set_fps(int fps) {
  if (fps <= 0) {
    g_engine.target_fps = 0;
    g_engine.fps_manual = false;
    g_engine.sleep_bias_ns = 0;
    if (g_engine.backend != BACKEND_CPU && !g_engine.hybrid) {
      maybe_apply_default_gpu_fps();
    }
    if (g_engine.backend != BACKEND_CPU && !g_engine.hybrid) {
      apply_window_frame_rate(g_gpu.window, static_cast<float>(g_engine.target_fps));
    }
    return;
  }
  if (fps > 1000) {
    fps = 1000;
  }
  g_engine.target_fps = fps;
  g_engine.fps_manual = true;
  g_engine.sleep_bias_ns = 0;
  if (g_engine.backend != BACKEND_CPU && !g_engine.hybrid) {
    apply_window_frame_rate(g_gpu.window, static_cast<float>(g_engine.target_fps));
  }
}

void draw_engine_set_hybrid_cpu_mask(uint32_t mask) {
  g_engine.hybrid_cpu_mask = mask;
}

void draw_engine_set_sensitive(int enable) {
  g_engine.sensitive = (enable != 0);
}

bool get_mode_is_need_set_fps(void) {
  if (!g_engine.initialized) {
    return false;
  }
  if (g_engine.backend == BACKEND_CPU || g_engine.hybrid) {
    return false;
  }
  return true;
}

int draw_engine_set_option(int option, int value) {
  switch (option) {
    case DRAW_ENGINE_OPT_RENDER_SCALE_X1000: {
      if (value <= 0) {
        g_engine.render_scale = 1.0f;
        g_engine.render_scale_manual = false;
        g_engine.render_width = 0;
        g_engine.render_height = 0;
        g_engine.render_size_manual = false;
        if (g_engine.cpu_ctx) {
          apply_render_target();
        }
        return DRAW_ENGINE_OK;
      }
      const float scale = static_cast<float>(value) / 1000.0f;
      return draw_set_render_scale(scale);
    }
    case DRAW_ENGINE_OPT_RENDER_WIDTH: {
      g_engine.render_width = value;
      g_engine.render_size_manual = (value > 0 || g_engine.render_height > 0);
      if (g_engine.render_width > 0 && g_engine.render_height > 0) {
        draw_set_render_size(g_engine.render_width, g_engine.render_height);
      } else if (g_engine.cpu_ctx) {
        apply_render_target();
      }
      return DRAW_ENGINE_OK;
    }
    case DRAW_ENGINE_OPT_RENDER_HEIGHT: {
      g_engine.render_height = value;
      g_engine.render_size_manual = (value > 0 || g_engine.render_width > 0);
      if (g_engine.render_width > 0 && g_engine.render_height > 0) {
        draw_set_render_size(g_engine.render_width, g_engine.render_height);
      } else if (g_engine.cpu_ctx) {
        apply_render_target();
      }
      return DRAW_ENGINE_OK;
    }
    case DRAW_ENGINE_OPT_TARGET_FPS:
      draw_engine_set_fps(value);
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_HYBRID_CPU_MASK:
      draw_engine_set_hybrid_cpu_mask(static_cast<uint32_t>(value));
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_HYBRID_SENSITIVE_ONLY:
      g_engine.hybrid_sensitive_only = (value != 0);
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_SENSITIVE:
      draw_engine_set_sensitive(value);
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_CIRCLE_SEGMENTS:
      if (value <= 0) {
        g_engine.circle_segments_manual = false;
        g_engine.circle_segments = 64;
      } else {
        if (value < 8) {
          value = 8;
        }
        g_engine.circle_segments = value;
        g_engine.circle_segments_manual = true;
      }
      g_gpu.circle_segments = 0;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_AUTO_QUALITY:
      g_engine.auto_quality = value != 0 ? 1 : 0;
      if (g_engine.auto_quality != 0) {
        apply_quality_level(g_engine.quality_level);
      }
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_QUALITY_LEVEL:
      if (value < g_engine.quality_min) {
        value = g_engine.quality_min;
      }
      if (value > g_engine.quality_max) {
        value = g_engine.quality_max;
      }
      g_engine.quality_level = value;
      apply_quality_level(g_engine.quality_level);
      return DRAW_ENGINE_OK;
    default:
      break;
  }
  return DRAW_ENGINE_EINVAL;
}

int draw_engine_get_option(int option, int* out_value) {
  if (!out_value) {
    return DRAW_ENGINE_EINVAL;
  }
  switch (option) {
    case DRAW_ENGINE_OPT_RENDER_SCALE_X1000:
      *out_value = static_cast<int>(g_engine.render_scale * 1000.0f + 0.5f);
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_RENDER_WIDTH:
      *out_value = g_engine.render_width;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_RENDER_HEIGHT:
      *out_value = g_engine.render_height;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_TARGET_FPS:
      *out_value = g_engine.target_fps;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_HYBRID_CPU_MASK:
      *out_value = static_cast<int>(g_engine.hybrid_cpu_mask);
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_HYBRID_SENSITIVE_ONLY:
      *out_value = g_engine.hybrid_sensitive_only ? 1 : 0;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_SENSITIVE:
      *out_value = g_engine.sensitive ? 1 : 0;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_CIRCLE_SEGMENTS:
      *out_value = g_engine.circle_segments;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_AUTO_QUALITY:
      *out_value = g_engine.auto_quality;
      return DRAW_ENGINE_OK;
    case DRAW_ENGINE_OPT_QUALITY_LEVEL:
      *out_value = g_engine.quality_level;
      return DRAW_ENGINE_OK;
    default:
      break;
  }
  return DRAW_ENGINE_EINVAL;
}

int draw_engine_get_capabilities(DrawEngineCaps* out_caps) {
  if (!out_caps) {
    return DRAW_ENGINE_EINVAL;
  }
  memset(out_caps, 0, sizeof(*out_caps));
  out_caps->api_version = DRAW_ENGINE_API_VERSION;
  out_caps->has_vulkan = (DRAW_ENGINE_HAS_VULKAN && has_library("libvulkan.so")) ? 1 : 0;
  out_caps->has_gles = (DRAW_ENGINE_HAS_GLES &&
                        (has_library("libGLESv3.so") || has_library("libGLESv2.so")))
                           ? 1
                           : 0;
  out_caps->backend = g_engine.backend;
  out_caps->mode = g_engine.mode;
  out_caps->hybrid = g_engine.hybrid ? 1 : 0;
  out_caps->render_scale_x1000 =
      static_cast<int>(g_engine.render_scale * 1000.0f + 0.5f);
  out_caps->render_width = g_engine.render_width;
  out_caps->render_height = g_engine.render_height;
  out_caps->circle_segments = g_engine.circle_segments;
  out_caps->supports_wide_lines = 0;
  out_caps->supports_anisotropy = 0;
  out_caps->max_anisotropy = 0.0f;
  out_caps->max_msaa = 1;
  if (g_engine.backend == BACKEND_VULKAN && g_gpu.vk.ready) {
    out_caps->supports_wide_lines = g_gpu.vk.supports_wide_lines ? 1 : 0;
    out_caps->supports_anisotropy = g_gpu.vk.supports_anisotropy ? 1 : 0;
    out_caps->max_anisotropy = g_gpu.vk.max_anisotropy;
    out_caps->max_msaa = sample_count_to_int(g_gpu.vk.msaa_samples);
  } else if (g_engine.backend == BACKEND_GLES && g_gpu.gl.ready) {
    out_caps->supports_anisotropy = g_gpu.gl.supports_anisotropy ? 1 : 0;
    out_caps->max_anisotropy = g_gpu.gl.max_anisotropy;
    out_caps->max_msaa = (g_gpu.gl.samples > 0) ? g_gpu.gl.samples : 1;
  }
  return DRAW_ENGINE_OK;
}

int init_draw_windows(const char* name, int randomize_name) {
  if (!g_engine.initialized) {
    const int init_result = init_draw_engine(DRAW_ENGINE_MODE_AUTO);
    if (init_result != DRAW_ENGINE_OK) {
      return init_result;
    }
  }

  if (g_engine.cpu_ctx) {
    midraw_shutdown(g_engine.cpu_ctx);
    g_engine.cpu_ctx = nullptr;
  }
  if (g_engine.overlay_ctx) {
    midraw_shutdown(g_engine.overlay_ctx);
    g_engine.overlay_ctx = nullptr;
  }
  if (g_engine.surface_name) {
    free(g_engine.surface_name);
    g_engine.surface_name = nullptr;
  }

  const char* base = (name && name[0]) ? name : "DrawEngine";
  const size_t len = strlen(base);
  g_engine.surface_name = static_cast<char*>(calloc(len + 1, 1));
  if (!g_engine.surface_name) {
    return DRAW_ENGINE_EFAILED;
  }
  memcpy(g_engine.surface_name, base, len);
  if (randomize_name) {
    shuffle_name(g_engine.surface_name);
  }

  const char* rot_env = getenv("MIDRAW_ROTATION");
  if (rot_env && rot_env[0]) {
    g_engine.rotation = atoi(rot_env);
    g_engine.rotation_forced = true;
  } else {
    g_engine.rotation = 0;
    g_engine.rotation_forced = false;
  }

  MidrawConfig cfg{};
  cfg.rotation = g_engine.rotation;
  cfg.width = 0;
  cfg.height = 0;
  cfg.font_path = getenv("MIDRAW_FONT_PATH");
  cfg.font_size = env_int("MIDRAW_FONT_SIZE", 0);
  cfg.atlas_size = env_int("MIDRAW_ATLAS_SIZE", 512);

  const bool want_gpu = (g_engine.backend != BACKEND_CPU);
  if (!want_gpu) {
    cfg.surface_name = g_engine.surface_name;
    if (midraw_init(&g_engine.cpu_ctx, &cfg) != 0) {
      fprintf(stderr, "draw_engine: midraw_init failed\n");
      return DRAW_ENGINE_EFAILED;
    }
    gpu_shutdown(g_gpu);
    apply_render_target();
    return DRAW_ENGINE_OK;
  }

  const char* gpu_surface = g_engine.surface_name;
  char gpu_name[256];
  char cpu_name[256];
  if (g_engine.hybrid) {
    snprintf(gpu_name, sizeof(gpu_name), "%s_gpu", g_engine.surface_name);
    gpu_surface = gpu_name;
  }

  cfg.surface_name = gpu_surface;
  if (midraw_init(&g_engine.cpu_ctx, &cfg) != 0) {
    fprintf(stderr, "draw_engine: midraw_init failed\n");
    return DRAW_ENGINE_EFAILED;
  }

  if (g_engine.hybrid) {
    snprintf(cpu_name, sizeof(cpu_name), "%s_cpu", g_engine.surface_name);
    cfg.surface_name = cpu_name;
    if (midraw_init(&g_engine.overlay_ctx, &cfg) != 0) {
      fprintf(stderr, "draw_engine: overlay midraw_init failed\n");
      g_engine.overlay_ctx = nullptr;
      g_engine.hybrid = false;
    } else {
      midraw_set_layer(g_engine.cpu_ctx, INT_MAX - 1);
      midraw_set_layer(g_engine.overlay_ctx, INT_MAX);
    }
  }

  if (g_engine.backend != BACKEND_CPU) {
    ANativeWindow* window =
        reinterpret_cast<ANativeWindow*>(midraw_get_native_window(g_engine.cpu_ctx));
    const int w = midraw_logical_width(g_engine.cpu_ctx);
    const int h = midraw_logical_height(g_engine.cpu_ctx);
    if (!window || !gpu_init(g_gpu, g_engine.backend, window, w, h, g_engine.rotation)) {
      const bool can_try_gles =
          (g_engine.backend == BACKEND_VULKAN) && DRAW_ENGINE_HAS_GLES &&
          (has_library("libGLESv3.so") || has_library("libGLESv2.so"));
      fprintf(stderr, "draw_engine: GPU init failed (%s)\n",
              g_engine.backend == BACKEND_VULKAN ? "Vulkan" : "GLES");
      gpu_shutdown(g_gpu);
      if (can_try_gles) {
        if (gpu_init(g_gpu, BACKEND_GLES, window, w, h, g_engine.rotation)) {
          g_engine.backend = BACKEND_GLES;
          fprintf(stderr,
                  "draw_engine: fallback to GLES logical=%dx%d physical=%dx%d rot=%d\n",
                  g_gpu.logical_width,
                  g_gpu.logical_height,
                  g_gpu.physical_width,
                  g_gpu.physical_height,
                  g_engine.rotation);
        } else {
          fprintf(stderr, "draw_engine: GLES init failed, fallback CPU\n");
          gpu_shutdown(g_gpu);
          g_engine.backend = BACKEND_CPU;
        }
      } else {
        fprintf(stderr, "draw_engine: fallback CPU\n");
        g_engine.backend = BACKEND_CPU;
      }
    } else {
      fprintf(stderr, "draw_engine: GPU surface logical=%dx%d physical=%dx%d rot=%d\n",
              g_gpu.logical_width,
              g_gpu.logical_height,
              g_gpu.physical_width,
              g_gpu.physical_height,
              g_engine.rotation);
    }
  }

  if (g_engine.backend != BACKEND_CPU) {
    maybe_apply_default_gpu_fps();
  }

  if (g_engine.backend == BACKEND_CPU) {
    g_engine.hybrid = false;
    gpu_shutdown(g_gpu);
    if (g_engine.overlay_ctx) {
      midraw_shutdown(g_engine.overlay_ctx);
      g_engine.overlay_ctx = nullptr;
    }
    if (g_engine.mode == DRAW_ENGINE_MODE_GPU) {
      if (g_engine.cpu_ctx) {
        midraw_shutdown(g_engine.cpu_ctx);
        g_engine.cpu_ctx = nullptr;
      }
      return DRAW_ENGINE_ENOBACKEND;
    }
  }
  if (g_engine.auto_quality != 0) {
    apply_quality_level(g_engine.quality_level);
  } else {
    apply_render_target();
  }
  return DRAW_ENGINE_OK;
}

void shutdown_draw_engine(void) {
  if (g_engine.cpu_ctx) {
    midraw_shutdown(g_engine.cpu_ctx);
    g_engine.cpu_ctx = nullptr;
  }
  if (g_engine.overlay_ctx) {
    midraw_shutdown(g_engine.overlay_ctx);
    g_engine.overlay_ctx = nullptr;
  }
  gpu_shutdown(g_gpu);
  if (g_engine.surface_name) {
    free(g_engine.surface_name);
    g_engine.surface_name = nullptr;
  }
  memset(&g_engine, 0, sizeof(g_engine));
}

int draw_begin_frame(void) {
  if (!g_engine.cpu_ctx) {
    return DRAW_ENGINE_ENOTINIT;
  }
  if (g_engine.in_frame) {
    return DRAW_ENGINE_OK;
  }
  maybe_recreate_on_rotation();
  const bool timing_debug = debug_timing_enabled();
  uint64_t lock_start_ns = 0;
  if (g_engine.backend == BACKEND_CPU) {
    MidrawContext* ctx = active_cpu_ctx();
    if (timing_debug) {
      lock_start_ns = now_ns();
    }
    if (!ctx || midraw_lock(ctx) != 0) {
      if (timing_debug && lock_start_ns != 0) {
        const uint64_t lock_ns = now_ns() - lock_start_ns;
        fprintf(stderr,
                "draw_engine timing: cpu lock failed after %.3f ms\n",
                lock_ns / 1000000.0);
      }
      return DRAW_ENGINE_EFAILED;
    }
    if (timing_debug && lock_start_ns != 0) {
      const uint64_t lock_ns = now_ns() - lock_start_ns;
      if (lock_ns > 5000000ull) {
        fprintf(stderr,
                "draw_engine timing: cpu lock %.3f ms\n",
                lock_ns / 1000000.0);
      }
    }
  } else {
    gpu_refresh_size_and_rotation();
    gpu_reset_frame(g_gpu);
    g_engine.overlay_locked = false;
  }
  g_engine.frame_start_ns = now_ns();
  g_engine.in_frame = true;
  return DRAW_ENGINE_OK;
}

void draw_end_frame(void) {
  if (!g_engine.cpu_ctx || !g_engine.in_frame) {
    return;
  }
  const bool timing_debug = debug_timing_enabled();
  uint64_t unlock_start_ns = 0;
  if (g_engine.backend == BACKEND_CPU) {
    MidrawContext* ctx = active_cpu_ctx();
    if (ctx) {
      if (timing_debug) {
        unlock_start_ns = now_ns();
      }
      midraw_unlock_post(ctx);
      if (timing_debug && unlock_start_ns != 0) {
        const uint64_t unlock_ns = now_ns() - unlock_start_ns;
        if (unlock_ns > 5000000ull) {
          fprintf(stderr,
                  "draw_engine timing: cpu unlock %.3f ms\n",
                  unlock_ns / 1000000.0);
        }
      }
    }
  } else if (g_engine.backend == BACKEND_VULKAN) {
    vk_draw_frame(g_gpu);
  } else if (g_engine.backend == BACKEND_GLES) {
    gl_draw_frame(g_gpu);
  }
  if (g_engine.backend != BACKEND_CPU && g_engine.hybrid && g_engine.overlay_ctx &&
      g_engine.overlay_locked) {
    midraw_unlock_post(g_engine.overlay_ctx);
    g_engine.overlay_locked = false;
  }
  if (g_engine.frame_start_ns != 0) {
    const uint64_t end_ns = now_ns();
    const uint64_t draw_ns =
        (end_ns > g_engine.frame_start_ns) ? (end_ns - g_engine.frame_start_ns) : 0;
    const bool cpu_like = (g_engine.backend == BACKEND_CPU) || g_engine.hybrid;
    if (!cpu_like) {
      maybe_adjust_quality(draw_ns);
    }
    float target_fps = 0.0f;
    if (cpu_like) {
      target_fps = update_auto_fps(draw_ns);
    } else if (g_engine.target_fps > 0) {
      target_fps = static_cast<float>(g_engine.target_fps);
    }
    const uint64_t target_ns = fps_to_frame_ns(target_fps);
    uint64_t sleep_ns = 0;
    if (target_ns > 0) {
      uint64_t deadline = 0;
      if (g_engine.frame_start_ns > 0) {
        deadline = g_engine.frame_start_ns + target_ns;
      } else if (end_ns >= draw_ns) {
        deadline = end_ns + (target_ns - draw_ns);
      }
      if (deadline > end_ns) {
        uint64_t adjusted_deadline = apply_sleep_bias(deadline, end_ns);
        if (adjusted_deadline > end_ns) {
          sleep_ns = adjusted_deadline - end_ns;
          if (precise_sleep_enabled()) {
            sleep_until_ns(adjusted_deadline);
          } else {
            usleep(static_cast<useconds_t>(sleep_ns / 1000ull));
          }
          const uint64_t woke_ns = now_ns();
          update_sleep_bias(deadline, woke_ns);
        }
      }
    }
    if (timing_debug) {
      static uint64_t last_log_ns = 0;
      const uint64_t now = end_ns;
      const bool slow_draw = draw_ns > 50000000ull;
      const bool slow_sleep = sleep_ns > 50000000ull;
      if (slow_draw || slow_sleep || (now - last_log_ns) > 1000000000ull) {
        fprintf(stderr,
                "draw_engine timing: mode=%d backend=%d draw=%.3f ms target_fps=%.2f "
                "auto_fps=%.2f target_ns=%.3f ms sleep=%.3f ms avg_draw=%.3f ms\n",
                g_engine.mode,
                g_engine.backend,
                draw_ns / 1000000.0,
                target_fps,
                g_engine.auto_fps,
                target_ns / 1000000.0,
                sleep_ns / 1000000.0,
                g_engine.avg_draw_ns / 1000000.0);
        last_log_ns = now;
      }
    }
  }
  g_engine.in_frame = false;
}

int draw_screen_width(void) {
  if (!g_engine.cpu_ctx) {
    return 0;
  }
  if (g_engine.backend != BACKEND_CPU) {
    return g_gpu.logical_width;
  }
  MidrawContext* ctx = active_cpu_ctx();
  return ctx ? midraw_logical_width(ctx) : 0;
}

int draw_screen_height(void) {
  if (!g_engine.cpu_ctx) {
    return 0;
  }
  if (g_engine.backend != BACKEND_CPU) {
    return g_gpu.logical_height;
  }
  MidrawContext* ctx = active_cpu_ctx();
  return ctx ? midraw_logical_height(ctx) : 0;
}

int draw_set_render_scale(float scale) {
  if (scale <= 0.0f) {
    scale = 1.0f;
  }
  if (scale < 0.25f) {
    scale = 0.25f;
  } else if (scale > 1.0f) {
    scale = 1.0f;
  }
  g_engine.render_scale = scale;
  g_engine.render_scale_manual = true;
  g_engine.render_width = 0;
  g_engine.render_height = 0;
  g_engine.render_size_manual = false;
  if (g_engine.cpu_ctx) {
    apply_render_target();
    if (g_engine.backend != BACKEND_CPU) {
      gpu_refresh_size_and_rotation();
    }
  }
  return 0;
}

int draw_set_render_size(int width, int height) {
  if (width <= 0 || height <= 0) {
    g_engine.render_width = 0;
    g_engine.render_height = 0;
    g_engine.render_size_manual = false;
    g_engine.render_scale_manual = false;
    if (g_engine.cpu_ctx) {
      apply_render_target();
      if (g_engine.backend != BACKEND_CPU) {
        gpu_refresh_size_and_rotation();
      }
    }
    return 0;
  }
  g_engine.render_width = width;
  g_engine.render_height = height;
  g_engine.render_scale = 1.0f;
  g_engine.render_size_manual = true;
  if (g_engine.cpu_ctx) {
    apply_render_target();
    if (g_engine.backend != BACKEND_CPU) {
      gpu_refresh_size_and_rotation();
    }
  }
  return 0;
}

void draw_text(const char* text, int x0, int y0, int x1, int y1, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  if (g_engine.backend != BACKEND_CPU) {
    if (hybrid_use_cpu(GpuCmdType::Text)) {
      MidrawContext* ctx = active_cpu_ctx();
      if (!ctx) {
        return;
      }
      if (x1 > x0 && y1 > y0) {
        midraw_draw_text_rect(ctx, text, x0, y0, x1, y1, color);
      } else {
        midraw_draw_text(ctx, text, x0, y0, color);
      }
      return;
    }
    gpu_push_text(g_gpu, text, x0, y0, x1, y1, color);
    return;
  }
  MidrawContext* ctx = active_cpu_ctx();
  if (!ctx) {
    return;
  }
  if (x1 > x0 && y1 > y0) {
    midraw_draw_text_rect(ctx, text, x0, y0, x1, y1, color);
  } else {
    midraw_draw_text(ctx, text, x0, y0, color);
  }
}

void draw_rect(int x, int y, int w, int h, int filled, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  if (g_engine.backend != BACKEND_CPU) {
    if (hybrid_use_cpu(GpuCmdType::Rect)) {
      MidrawContext* ctx = active_cpu_ctx();
      if (!ctx) {
        return;
      }
      midraw_draw_rect(ctx, x, y, w, h, filled, color);
      return;
    }
    gpu_push_rect(g_gpu, x, y, w, h, color, filled != 0);
    return;
  }
  MidrawContext* ctx = active_cpu_ctx();
  if (!ctx) {
    return;
  }
  midraw_draw_rect(ctx, x, y, w, h, filled, color);
}

void draw_circle(int cx, int cy, int radius, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  if (g_engine.backend != BACKEND_CPU) {
    if (hybrid_use_cpu(GpuCmdType::Circle)) {
      MidrawContext* ctx = active_cpu_ctx();
      if (!ctx) {
        return;
      }
      midraw_draw_circle(ctx, cx, cy, radius, color);
      return;
    }
    gpu_push_circle(g_gpu, cx, cy, radius, color);
    return;
  }
  MidrawContext* ctx = active_cpu_ctx();
  if (!ctx) {
    return;
  }
  midraw_draw_circle(ctx, cx, cy, radius, color);
}

void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  if (g_engine.backend != BACKEND_CPU) {
    if (hybrid_use_cpu(GpuCmdType::Line)) {
      MidrawContext* ctx = active_cpu_ctx();
      if (!ctx) {
        return;
      }
      midraw_draw_line(ctx, x1, y1, x2, y2, color);
      return;
    }
    gpu_push_line(g_gpu, x1, y1, x2, y2, color);
    return;
  }
  MidrawContext* ctx = active_cpu_ctx();
  if (!ctx) {
    return;
  }
  midraw_draw_line(ctx, x1, y1, x2, y2, color);
}

DrawImage* draw_load_image_from_memory(const unsigned char* data, int size) {
  if (!data || size <= 0) {
    return nullptr;
  }
  int w = 0;
  int h = 0;
  int comp = 0;
  unsigned char* decoded = stbi_load_from_memory(data, size, &w, &h, &comp, 4);
  if (!decoded || w <= 0 || h <= 0) {
    if (decoded) {
      stbi_image_free(decoded);
    }
    return nullptr;
  }

  DrawImage* image = static_cast<DrawImage*>(calloc(1, sizeof(DrawImage)));
  if (!image) {
    stbi_image_free(decoded);
    return nullptr;
  }
  image->width = w;
  image->height = h;
  image->rgba = decoded;
  image->pixels = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * w * h));
  if (!image->pixels) {
    free(image);
    stbi_image_free(decoded);
    return nullptr;
  }

  const unsigned char* src = decoded;
  for (int i = 0; i < w * h; ++i) {
    const uint8_t r = src[i * 4 + 0];
    const uint8_t g = src[i * 4 + 1];
    const uint8_t b = src[i * 4 + 2];
    const uint8_t a = src[i * 4 + 3];
    image->pixels[i] =
        (static_cast<uint32_t>(a) << 24) |
        (static_cast<uint32_t>(b) << 16) |
        (static_cast<uint32_t>(g) << 8) |
        static_cast<uint32_t>(r);
  }
  return image;
}

void draw_free_image(DrawImage* image) {
  if (!image) {
    return;
  }
  gpu_destroy_image(g_gpu, image);
  if (image->rgba) {
    stbi_image_free(image->rgba);
    image->rgba = nullptr;
  }
  if (image->pixels) {
    free(image->pixels);
    image->pixels = nullptr;
  }
  free(image);
}

void draw_image(const DrawImage* image, int x, int y) {
  if (!g_engine.cpu_ctx || !image || !image->pixels) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  if (g_engine.backend != BACKEND_CPU) {
    if (hybrid_use_cpu(GpuCmdType::Image)) {
      MidrawContext* ctx = active_cpu_ctx();
      if (!ctx) {
        return;
      }
      midraw_draw_image(ctx, image->pixels, image->width, image->height, x, y);
      return;
    }
    DrawImage* mutable_image = const_cast<DrawImage*>(image);
    gpu_prepare_texture(g_gpu, mutable_image);
    gpu_push_image(g_gpu, image, x, y);
    return;
  }
  MidrawContext* ctx = active_cpu_ctx();
  if (!ctx) {
    return;
  }
  midraw_draw_image(ctx, image->pixels, image->width, image->height, x, y);
}

extern "C" void draw_engine_demo_scene(uint64_t frame) {
  if (!g_engine.cpu_ctx || !g_engine.in_frame) {
    return;
  }
  const int w = midraw_logical_width(g_engine.cpu_ctx);
  const int h = midraw_logical_height(g_engine.cpu_ctx);
  if (w <= 0 || h <= 0) {
    return;
  }
  const float aspect = static_cast<float>(w) / static_cast<float>(h);
  const float angle = static_cast<float>(frame) * 0.02f;
  const float c = cosf(angle);
  const float s = sinf(angle);

  const float tri[3][3] = {
      {-0.6f, 0.4f, -2.5f},
      {0.6f, 0.4f, -2.5f},
      {0.0f, -0.6f, -2.5f},
  };
  const float ground_y = -0.9f;
  const float grid_half = 1.5f;
  const int grid_steps = 10;

  float vx[3], vy[3], vz[3];
  for (int i = 0; i < 3; ++i) {
    const float x = tri[i][0];
    const float y = tri[i][1];
    const float z = tri[i][2];
    vx[i] = x * c + z * s;
    vz[i] = -x * s + z * c;
    vy[i] = y;
  }

  if (g_engine.backend != BACKEND_CPU) {
    gpu_push_triangle3d(g_gpu, vx[0], vy[0], vz[0], vx[1], vy[1], vz[1],
                        vx[2], vy[2], vz[2], 0xFF0000FF);

    const float z0 = -2.0f;
    const float z1 = -4.0f;
    gpu_push_triangle3d(g_gpu, -1.5f, ground_y, z0, 1.5f, ground_y, z0,
                        1.5f, ground_y, z1, 0xFF00FF00);
    gpu_push_triangle3d(g_gpu, -1.5f, ground_y, z0, 1.5f, ground_y, z1,
                        -1.5f, ground_y, z1, 0xFF00FF00);

    for (int i = 0; i <= grid_steps; ++i) {
      const float t = -grid_half + (2.0f * grid_half * i) / grid_steps;
      gpu_push_line3d(g_gpu, -grid_half, ground_y, t, grid_half, ground_y, t, 0xFF404040);
      gpu_push_line3d(g_gpu, t, ground_y, -grid_half, t, ground_y, grid_half, 0xFF404040);
    }

    const float cube_size = 0.5f;
    const float cx = 0.0f;
    const float cy = -0.2f;
    const float cz = -3.2f;
    const float cube[8][3] = {
        {cx - cube_size, cy - cube_size, cz - cube_size},
        {cx + cube_size, cy - cube_size, cz - cube_size},
        {cx + cube_size, cy + cube_size, cz - cube_size},
        {cx - cube_size, cy + cube_size, cz - cube_size},
        {cx - cube_size, cy - cube_size, cz + cube_size},
        {cx + cube_size, cy - cube_size, cz + cube_size},
        {cx + cube_size, cy + cube_size, cz + cube_size},
        {cx - cube_size, cy + cube_size, cz + cube_size},
    };
    const int indices[36] = {
        0, 1, 2, 0, 2, 3,
        1, 5, 6, 1, 6, 2,
        5, 4, 7, 5, 7, 6,
        4, 0, 3, 4, 3, 7,
        3, 2, 6, 3, 6, 7,
        4, 5, 1, 4, 1, 0,
    };
    float rot[8][3];
    for (int i = 0; i < 8; ++i) {
      const float x = cube[i][0];
      const float z = cube[i][2];
      rot[i][0] = x * c + z * s;
      rot[i][2] = -x * s + z * c;
      rot[i][1] = cube[i][1];
    }
    for (int i = 0; i < 36; i += 3) {
      const int i0 = indices[i + 0];
      const int i1 = indices[i + 1];
      const int i2 = indices[i + 2];
      gpu_push_triangle3d(g_gpu,
                          rot[i0][0], rot[i0][1], rot[i0][2],
                          rot[i1][0], rot[i1][1], rot[i1][2],
                          rot[i2][0], rot[i2][1], rot[i2][2],
                          0xFF00FFFF);
    }
    return;
  }

  auto project = [&](float x, float y, float z, int* out_x, int* out_y) {
    const float f = 1.0f / tanf(0.5f);
    float ndc_x = (x / -z) * f / aspect;
    float ndc_y = (y / -z) * f;
    *out_x = static_cast<int>((ndc_x * 0.5f + 0.5f) * w);
    *out_y = static_cast<int>((1.0f - (ndc_y * 0.5f + 0.5f)) * h);
  };

  int sx[3], sy[3];
  for (int i = 0; i < 3; ++i) {
    project(vx[i], vy[i], vz[i], &sx[i], &sy[i]);
  }
  draw_line(sx[0], sy[0], sx[1], sy[1], 0xFF0000FF);
  draw_line(sx[1], sy[1], sx[2], sy[2], 0xFF0000FF);
  draw_line(sx[2], sy[2], sx[0], sy[0], 0xFF0000FF);

  for (int i = 0; i <= grid_steps; ++i) {
    const float t = -grid_half + (2.0f * grid_half * i) / grid_steps;
    int ax = 0, ay = 0, bx = 0, by = 0;
    project(-grid_half, ground_y, t, &ax, &ay);
    project(grid_half, ground_y, t, &bx, &by);
    draw_line(ax, ay, bx, by, 0xFF404040);
    project(t, ground_y, -grid_half, &ax, &ay);
    project(t, ground_y, grid_half, &bx, &by);
    draw_line(ax, ay, bx, by, 0xFF404040);
  }

  const float cube_size = 0.5f;
  const float cx = 0.0f;
  const float cy = -0.2f;
  const float cz = -3.2f;
  const float cube[8][3] = {
      {cx - cube_size, cy - cube_size, cz - cube_size},
      {cx + cube_size, cy - cube_size, cz - cube_size},
      {cx + cube_size, cy + cube_size, cz - cube_size},
      {cx - cube_size, cy + cube_size, cz - cube_size},
      {cx - cube_size, cy - cube_size, cz + cube_size},
      {cx + cube_size, cy - cube_size, cz + cube_size},
      {cx + cube_size, cy + cube_size, cz + cube_size},
      {cx - cube_size, cy + cube_size, cz + cube_size},
  };
  float rot[8][3];
  for (int i = 0; i < 8; ++i) {
    const float x = cube[i][0];
    const float z = cube[i][2];
    rot[i][0] = x * c + z * s;
    rot[i][2] = -x * s + z * c;
    rot[i][1] = cube[i][1];
  }
  const int edges[24] = {
      0,1, 1,2, 2,3, 3,0,
      4,5, 5,6, 6,7, 7,4,
      0,4, 1,5, 2,6, 3,7,
  };
  for (int i = 0; i < 24; i += 2) {
    int a = edges[i];
    int b = edges[i + 1];
    int ax = 0, ay = 0, bx = 0, by = 0;
    project(rot[a][0], rot[a][1], rot[a][2], &ax, &ay);
    project(rot[b][0], rot[b][1], rot[b][2], &bx, &by);
    draw_line(ax, ay, bx, by, 0xFF00FFFF);
  }
}
