#include "include/draw_engine.h"
#include "include/midraw.h"
#include "shaders/vk_shaders.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <string>

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
#include "third_party/stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype/stb_truetype.h"

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
};

static void gpu_shutdown(struct GpuState& gpu);
static bool gpu_init(struct GpuState& gpu, BackendType backend, ANativeWindow* window, int width,
                     int height, int rotation);
static void gpu_refresh_size_and_rotation();
static uint64_t now_ns();

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
  std::vector<VkFramebuffer> framebuffers;
  VkRenderPass render_pass;
  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline_tri_2d;
  VkPipeline pipeline_line_2d;
  VkPipeline pipeline_tri_3d;
  VkPipeline pipeline_line_3d;
  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;
  VkSemaphore image_available;
  VkSemaphore render_finished;
  VkFence in_flight;
  VkDescriptorSetLayout desc_layout;
  VkDescriptorPool desc_pool;
  VkSampler sampler;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_memory;
  size_t vertex_capacity;
  void* vertex_map;
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
  GLint u_mvp;
  GLint u_tex;
  int major;
  int minor;
  bool supports_anisotropy;
  float max_anisotropy;
  int samples;
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
  std::vector<stbtt_bakedchar> baked(96);
  std::vector<unsigned char> bitmap(static_cast<size_t>(atlas_w * atlas_h));
  const int ok = stbtt_BakeFontBitmap(ttf.data(),
                                      offset,
                                      static_cast<float>(pixel_height),
                                      bitmap.data(),
                                      atlas_w,
                                      atlas_h,
                                      32,
                                      96,
                                      baked.data());
  if (ok <= 0) {
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

  unsigned char* rgba = static_cast<unsigned char*>(malloc(atlas_w * atlas_h * 4));
  if (!rgba) {
    return false;
  }
  for (int i = 0; i < atlas_w * atlas_h; ++i) {
    const unsigned char a = bitmap[static_cast<size_t>(i)];
    rgba[i * 4 + 0] = 255;
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 255;
    rgba[i * 4 + 3] = a;
  }

  for (int i = 0; i < 96; ++i) {
    const stbtt_bakedchar& g = baked[i];
    gpu.font.u0[i] = static_cast<float>(g.x0) / static_cast<float>(atlas_w);
    gpu.font.v0[i] = static_cast<float>(g.y0) / static_cast<float>(atlas_h);
    gpu.font.u1[i] = static_cast<float>(g.x1) / static_cast<float>(atlas_w);
    gpu.font.v1[i] = static_cast<float>(g.y1) / static_cast<float>(atlas_h);
    gpu.font.xoff[i] = g.xoff;
    gpu.font.yoff[i] = g.yoff;
    gpu.font.xadvance[i] = g.xadvance;
    gpu.font.glyph_w[i] = static_cast<float>(g.x1 - g.x0);
    gpu.font.glyph_h[i] = static_cast<float>(g.y1 - g.y0);
  }

  gpu.font.width = atlas_w;
  gpu.font.height = atlas_h;
  gpu.font.ready = true;

  gpu.font_image.width = atlas_w;
  gpu.font_image.height = atlas_h;
  gpu.font_image.rgba = rgba;
  gpu.font_image.pixels = nullptr;
  fprintf(stderr, "GPU font: %s size=%d atlas=%dx%d\n", path, pixel_height, atlas_w, atlas_h);
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
    int worst = expected * (segments * 2);
    if (worst < 16384) {
      worst = 16384;
    } else if (worst > 131072) {
      worst = 131072;
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
  if (gpu.vertices.capacity() < static_cast<size_t>(reserve_vertices)) {
    gpu.vertices.reserve(static_cast<size_t>(reserve_vertices));
  }
  if (gpu.commands.capacity() < static_cast<size_t>(reserve_commands)) {
    gpu.commands.reserve(static_cast<size_t>(reserve_commands));
  }
  if (gpu.batches.capacity() < static_cast<size_t>(reserve_batches)) {
    gpu.batches.reserve(static_cast<size_t>(reserve_batches));
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
  return entry.vertices.size() * sizeof(CachedTextVertex) + entry.text.size() + 1;
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
                                       uint32_t color) {
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
    const int first = static_cast<int>(gpu.vertices.size());
    gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y0, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y0, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y1, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x1, y1, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x0, y1, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x0, y1, 0.0f, 0.0f, 0.0f, col, true);
    gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, col, true);
    gpu_add_batch(gpu, first, 8, tex, true, false);
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
  const float thickness = env_float("DRAW_ENGINE_LINE_WIDTH", 1.5f);
  if (cached_aa != 0 && thickness > 1.0f) {
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
    const float nx = -dy * inv * (thickness * 0.5f);
    const float ny = dx * inv * (thickness * 0.5f);

    const int first = static_cast<int>(gpu.vertices.size());
    gpu_push_vertex(gpu, fx1 - nx, fy1 - ny, 0.0f, 0.0f, 0.0f, color, true);
    gpu_push_vertex(gpu, fx1 + nx, fy1 + ny, 0.0f, 0.0f, 0.0f, color, true);
    gpu_push_vertex(gpu, fx2 + nx, fy2 + ny, 0.0f, 0.0f, 0.0f, color, true);
    gpu_push_vertex(gpu, fx1 - nx, fy1 - ny, 0.0f, 0.0f, 0.0f, color, true);
    gpu_push_vertex(gpu, fx2 + nx, fy2 + ny, 0.0f, 0.0f, 0.0f, color, true);
    gpu_push_vertex(gpu, fx2 - nx, fy2 - ny, 0.0f, 0.0f, 0.0f, color, true);
    gpu_add_batch(gpu, first, 6, &gpu.white_image, false, false);
    return;
  }

  const int first = static_cast<int>(gpu.vertices.size());
  gpu_push_vertex(gpu, static_cast<float>(x1), static_cast<float>(y1), 0.0f, 0.0f, 0.0f,
                  color, true);
  gpu_push_vertex(gpu, static_cast<float>(x2), static_cast<float>(y2), 0.0f, 0.0f, 0.0f,
                  color, true);
  gpu_add_batch(gpu, first, 2, &gpu.white_image, true, false);
}

static void gpu_push_circle(GpuState& gpu, int cx, int cy, int radius, uint32_t color) {
  if (radius <= 0) {
    return;
  }
  int segments = g_engine.circle_segments;
  if (segments <= 0) {
    segments = env_int("DRAW_ENGINE_CIRCLE_SEGMENTS", 64);
  }
  if (segments < 8) {
    segments = 8;
  }
  ensure_circle_lut(gpu, segments);
  const int first = static_cast<int>(gpu.vertices.size());
  for (int i = 0; i < segments; ++i) {
    const int j = (i + 1) < segments ? (i + 1) : 0;
    const float x0 = static_cast<float>(cx) +
                     gpu.circle_lut[static_cast<size_t>(i * 2 + 0)] *
                         static_cast<float>(radius);
    const float y0 = static_cast<float>(cy) +
                     gpu.circle_lut[static_cast<size_t>(i * 2 + 1)] *
                         static_cast<float>(radius);
    const float x1 = static_cast<float>(cx) +
                     gpu.circle_lut[static_cast<size_t>(j * 2 + 0)] *
                         static_cast<float>(radius);
    const float y1 = static_cast<float>(cy) +
                     gpu.circle_lut[static_cast<size_t>(j * 2 + 1)] *
                         static_cast<float>(radius);
    gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, color, true);
    gpu_push_vertex(gpu, x1, y1, 0.0f, 0.0f, 0.0f, color, true);
  }
  gpu_add_batch(gpu, first, segments * 2, &gpu.white_image, true, false);
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
  const int clip_x0 = x0;
  const int clip_y0 = y0;
  const int clip_x1 = (x1 > x0) ? x1 : 0;
  const int clip_y1 = (y1 > y0) ? y1 : 0;

  if (use_cache) {
    CachedText* cached =
        gpu_text_cache_find(gpu, text_ptr, x0, y0, x1, y1, color);
    if (cached && !cached->vertices.empty()) {
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

  std::vector<CachedTextVertex> cached_vertices;
  if (use_cache) {
    cached_vertices.reserve(strlen(text_ptr) * 6);
  }

  auto emit = [&](float x, float y, float u, float v) {
    gpu_push_vertex(gpu, x, y, 0.0f, u, v, color, true);
    if (use_cache) {
      CachedTextVertex vtx{};
      vtx.x = x;
      vtx.y = y;
      vtx.z = 0.0f;
      vtx.u = u;
      vtx.v = v;
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
  const int first = static_cast<int>(gpu.vertices.size());
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x1, y0, 0.0f, 1.0f, 0.0f, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x1, y1, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x0, y0, 0.0f, 0.0f, 0.0f, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x1, y1, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu, true);
  gpu_push_vertex(gpu, x0, y1, 0.0f, 0.0f, 1.0f, 0xFFFFFFFFu, true);
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
  for (auto fb : vk.framebuffers) {
    vkDestroyFramebuffer(vk.device, fb, nullptr);
  }
  vk.framebuffers.clear();
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
  for (auto mode : present_modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      present_mode = mode;
      break;
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

  vk.framebuffers.resize(vk.image_views.size());
  for (size_t i = 0; i < vk.image_views.size(); ++i) {
    VkImageView attachments[3];
    uint32_t attachment_count = 0;
    if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
      attachments[attachment_count++] = vk.msaa_color_view;
      attachments[attachment_count++] = vk.image_views[i];
      attachments[attachment_count++] = vk.depth_view;
    } else {
      attachments[attachment_count++] = vk.image_views[i];
      attachments[attachment_count++] = vk.depth_view;
    }
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = vk.render_pass;
    fb.attachmentCount = attachment_count;
    fb.pAttachments = attachments;
    fb.width = extent.width;
    fb.height = extent.height;
    fb.layers = 1;
    if (vkCreateFramebuffer(vk.device, &fb, nullptr, &vk.framebuffers[i]) != VK_SUCCESS) {
      return false;
    }
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
  const int msaa_req = env_int("DRAW_ENGINE_MSAA", 4);
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
  for (auto mode : present_modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      present_mode = mode;
      break;
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
  subpass.pDepthStencilAttachment = &depth_ref;

  VkAttachmentDescription attachments[3];
  uint32_t attachment_count = 0;
  attachments[attachment_count++] = color;
  if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
    attachments[attachment_count++] = resolve;
  }
  attachments[attachment_count++] = depth;

  VkRenderPassCreateInfo rp{};
  rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.attachmentCount = attachment_count;
  rp.pAttachments = attachments;
  rp.subpassCount = 1;
  rp.pSubpasses = &subpass;
  vkCreateRenderPass(vk.device, &rp, nullptr, &vk.render_pass);

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
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
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

  auto create_pipeline = [&](VkPrimitiveTopology topology, bool depth_enable) -> VkPipeline {
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
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = depth_enable ? &ds : nullptr;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = vk.pipeline_layout;
    gp.renderPass = vk.render_pass;
    gp.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline) !=
        VK_SUCCESS) {
      return VK_NULL_HANDLE;
    }
    return pipeline;
  };

  vk.pipeline_tri_2d = create_pipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false);
  vk.pipeline_line_2d = create_pipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false);
  vk.pipeline_tri_3d = create_pipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true);
  vk.pipeline_line_3d = create_pipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true);

  if (vk.pipeline_tri_2d == VK_NULL_HANDLE || vk.pipeline_line_2d == VK_NULL_HANDLE ||
      vk.pipeline_tri_3d == VK_NULL_HANDLE || vk.pipeline_line_3d == VK_NULL_HANDLE) {
    fprintf(stderr, "vkCreateGraphicsPipelines failed\n");
    return false;
  }
  vkDestroyShaderModule(vk.device, vert, nullptr);
  vkDestroyShaderModule(vk.device, frag, nullptr);

  vk.framebuffers.resize(vk.image_views.size());
  for (size_t i = 0; i < vk.image_views.size(); ++i) {
    VkImageView attachments[3];
    uint32_t attachment_count = 0;
    if (vk.msaa_samples != VK_SAMPLE_COUNT_1_BIT) {
      attachments[attachment_count++] = vk.msaa_color_view;
      attachments[attachment_count++] = vk.image_views[i];
      attachments[attachment_count++] = vk.depth_view;
    } else {
      attachments[attachment_count++] = vk.image_views[i];
      attachments[attachment_count++] = vk.depth_view;
    }
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = vk.render_pass;
    fb.attachmentCount = attachment_count;
    fb.pAttachments = attachments;
    fb.width = extent.width;
    fb.height = extent.height;
    fb.layers = 1;
    vkCreateFramebuffer(vk.device, &fb, nullptr, &vk.framebuffers[i]);
  }

  VkCommandPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool.queueFamilyIndex = vk.queue_family;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkCreateCommandPool(vk.device, &pool, nullptr, &vk.command_pool);

  VkCommandBufferAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = vk.command_pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  vkAllocateCommandBuffers(vk.device, &alloc, &vk.command_buffer);

  VkSemaphoreCreateInfo sem{};
  sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  vkCreateSemaphore(vk.device, &sem, nullptr, &vk.image_available);
  vkCreateSemaphore(vk.device, &sem, nullptr, &vk.render_finished);
  VkFenceCreateInfo fence{};
  fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vkCreateFence(vk.device, &fence, nullptr, &vk.in_flight);

  if (vk.supports_timestamps) {
    VkQueryPoolCreateInfo qp{};
    qp.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qp.queryCount = 2;
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

  const size_t max_vertices = 200000;
  vk.vertex_capacity = max_vertices * sizeof(GpuVertex);
  if (!vk_create_buffer(vk,
                        vk.vertex_capacity,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &vk.vertex_buffer,
                        &vk.vertex_memory)) {
    fprintf(stderr, "vk vertex buffer failed\n");
    return false;
  }
  vkMapMemory(vk.device, vk.vertex_memory, 0, vk.vertex_capacity, 0, &vk.vertex_map);

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
  if (vk.sampler) {
    vkDestroySampler(vk.device, vk.sampler, nullptr);
  }
  if (vk.desc_pool) {
    vkDestroyDescriptorPool(vk.device, vk.desc_pool, nullptr);
  }
  if (vk.desc_layout) {
    vkDestroyDescriptorSetLayout(vk.device, vk.desc_layout, nullptr);
  }
  if (vk.image_available) {
    vkDestroySemaphore(vk.device, vk.image_available, nullptr);
  }
  if (vk.render_finished) {
    vkDestroySemaphore(vk.device, vk.render_finished, nullptr);
  }
  if (vk.in_flight) {
    vkDestroyFence(vk.device, vk.in_flight, nullptr);
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
  if (vk.pipeline_tri_3d) {
    vkDestroyPipeline(vk.device, vk.pipeline_tri_3d, nullptr);
  }
  if (vk.pipeline_line_3d) {
    vkDestroyPipeline(vk.device, vk.pipeline_line_3d, nullptr);
  }
  if (vk.pipeline_layout) {
    vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, nullptr);
  }
  if (vk.render_pass) {
    vkDestroyRenderPass(vk.device, vk.render_pass, nullptr);
  }
  for (auto fb : vk.framebuffers) {
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
  if (gpu.vertices.empty()) {
    return true;
  }
  if (gpu.vertices.size() * sizeof(GpuVertex) > vk.vertex_capacity) {
    fprintf(stderr, "vk vertex overflow\n");
    return false;
  }

  memcpy(vk.vertex_map, gpu.vertices.data(), gpu.vertices.size() * sizeof(GpuVertex));

  uint32_t image_index = 0;
  vkWaitForFences(vk.device, 1, &vk.in_flight, VK_TRUE, UINT64_MAX);
  vkResetFences(vk.device, 1, &vk.in_flight);

  if (vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.image_available,
                            VK_NULL_HANDLE, &image_index) != VK_SUCCESS) {
    return false;
  }

  vkResetCommandBuffer(vk.command_buffer, 0);
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(vk.command_buffer, &begin);

  if (allow_timestamps && vk.supports_timestamps && vk.query_pool) {
    vkCmdResetQueryPool(vk.command_buffer, vk.query_pool, 0, 2);
    vkCmdWriteTimestamp(vk.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        vk.query_pool, 0);
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
  rp.renderPass = vk.render_pass;
  rp.framebuffer = vk.framebuffers[image_index];
  rp.renderArea.offset = {0, 0};
  rp.renderArea.extent = vk.extent;
  rp.clearValueCount = (vk.msaa_samples == VK_SAMPLE_COUNT_1_BIT) ? 2 : 3;
  rp.pClearValues = clear;
  vkCmdBeginRenderPass(vk.command_buffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(vk.extent.width);
  viewport.height = static_cast<float>(vk.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{};
  scissor.extent = vk.extent;
  vkCmdSetViewport(vk.command_buffer, 0, 1, &viewport);
  vkCmdSetScissor(vk.command_buffer, 0, 1, &scissor);

  VkBuffer vb = vk.vertex_buffer;
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(vk.command_buffer, 0, 1, &vb, offsets);

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
      vkCmdBindPipeline(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, desired);
      current_pipeline = desired;
    }
    const DrawImage* texture = static_cast<const DrawImage*>(batch.texture);
    VkDescriptorSet desc = texture ? texture->vk_desc : VK_NULL_HANDLE;
    if (desc != current_desc) {
      vkCmdBindDescriptorSets(vk.command_buffer,
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
    vkCmdPushConstants(vk.command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float) * 16, mvp);
    vkCmdDraw(vk.command_buffer, batch.count, 1, batch.first, 0);
  }

  vkCmdEndRenderPass(vk.command_buffer);

  if (allow_timestamps && vk.supports_timestamps && vk.query_pool) {
    vkCmdWriteTimestamp(vk.command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        vk.query_pool, 1);
  }
  vkEndCommandBuffer(vk.command_buffer);

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &vk.image_available;
  submit.pWaitDstStageMask = &wait_stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &vk.command_buffer;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &vk.render_finished;
  vkQueueSubmit(vk.queue, 1, &submit, vk.in_flight);

  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &vk.render_finished;
  present.swapchainCount = 1;
  present.pSwapchains = &vk.swapchain;
  present.pImageIndices = &image_index;
  vkQueuePresentKHR(vk.queue, &present);

  double gpu_ms = 0.0;
  if (allow_timestamps && vk.supports_timestamps && vk.query_pool) {
    uint64_t timestamps[2] = {};
    if (vkGetQueryPoolResults(vk.device,
                              vk.query_pool,
                              0,
                              2,
                              sizeof(timestamps),
                              timestamps,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
      if (timestamps[1] > timestamps[0]) {
        gpu_ms = (timestamps[1] - timestamps[0]) * vk.timestamp_period / 1e6;
      }
    }
  }
  const uint64_t now = now_ns();
  if (gpu.log_interval <= 0) {
    gpu.log_interval = env_int("DRAW_ENGINE_LOG_INTERVAL", 120);
  }
  if (gpu.last_log_ns == 0) {
    gpu.last_log_ns = now;
  }
  if (gpu.log_interval > 0 && (gpu.frame_index % static_cast<uint64_t>(gpu.log_interval)) == 0) {
    fprintf(stderr, "GPU frame: %.3f ms (Vulkan)\n", gpu_ms);
    gpu.last_log_ns = now;
  }
  gpu.frame_index++;
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

  const int msaa_req = env_int("DRAW_ENGINE_MSAA", 4);
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
      "precision mediump float;\n"
      "in vec2 vUV;\n"
      "in vec4 vColor;\n"
      "uniform sampler2D uTex;\n"
      "out vec4 fragColor;\n"
      "void main(){\n"
      "  vec4 tex = texture(uTex, vUV);\n"
      "  fragColor = tex * vColor;\n"
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

  glGenVertexArrays(1, &gl.vao);
  glGenBuffers(1, &gl.vbo);

  glBindVertexArray(gl.vao);
  glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GpuVertex) * 200000, nullptr, GL_DYNAMIC_DRAW);
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
  glDeleteBuffers(1, &gl.vbo);
  glDeleteVertexArrays(1, &gl.vao);
  glDeleteProgram(gl.program);
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
  if (gpu.vertices.empty()) {
    return true;
  }
  const uint64_t start = now_ns();
  glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, gpu.vertices.size() * sizeof(GpuVertex),
                  gpu.vertices.data());

  glViewport(0, 0, gpu.physical_width, gpu.physical_height);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(gl.program);
  glBindVertexArray(gl.vao);

  float ortho[16];
  mat4_ortho(ortho, 0.0f, static_cast<float>(gpu.physical_width),
             static_cast<float>(gpu.physical_height), 0.0f);
  float perspective[16];
  mat4_perspective(perspective, 1.0f, static_cast<float>(gpu.physical_width) /
                                           static_cast<float>(gpu.physical_height),
                   0.1f, 100.0f);

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
    if (g_engine.backend != BACKEND_CPU && !g_engine.hybrid) {
      maybe_apply_default_gpu_fps();
    }
    return;
  }
  if (fps > 1000) {
    fps = 1000;
  }
  g_engine.target_fps = fps;
  g_engine.fps_manual = true;
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
  if (g_engine.backend == BACKEND_CPU) {
    MidrawContext* ctx = active_cpu_ctx();
    if (!ctx || midraw_lock(ctx) != 0) {
      return DRAW_ENGINE_EFAILED;
    }
  } else {
    gpu_refresh_size_and_rotation();
    gpu_reset_frame(g_gpu);
    if (g_engine.hybrid && g_engine.overlay_ctx) {
      if (midraw_lock(g_engine.overlay_ctx) != 0) {
        return DRAW_ENGINE_EFAILED;
      }
    }
  }
  g_engine.frame_start_ns = now_ns();
  g_engine.in_frame = true;
  return DRAW_ENGINE_OK;
}

void draw_end_frame(void) {
  if (!g_engine.cpu_ctx || !g_engine.in_frame) {
    return;
  }
  if (g_engine.backend == BACKEND_CPU) {
    MidrawContext* ctx = active_cpu_ctx();
    if (ctx) {
      midraw_unlock_post(ctx);
    }
  } else if (g_engine.backend == BACKEND_VULKAN) {
    vk_draw_frame(g_gpu);
  } else if (g_engine.backend == BACKEND_GLES) {
    gl_draw_frame(g_gpu);
  }
  if (g_engine.backend != BACKEND_CPU && g_engine.hybrid && g_engine.overlay_ctx) {
    midraw_unlock_post(g_engine.overlay_ctx);
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
    if (target_ns > 0 && draw_ns < target_ns) {
      const uint64_t sleep_ns = target_ns - draw_ns;
      usleep(static_cast<useconds_t>(sleep_ns / 1000ull));
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
