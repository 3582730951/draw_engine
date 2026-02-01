#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define DRAW_ENGINE_API __attribute__((visibility("default")))
#else
#define DRAW_ENGINE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DRAW_ENGINE_API_VERSION 1

enum {
  DRAW_ENGINE_OK = 0,
  DRAW_ENGINE_EINVAL = -1,
  DRAW_ENGINE_ENOTINIT = -2,
  DRAW_ENGINE_ENOBACKEND = -3,
  DRAW_ENGINE_EFAILED = -4
};

typedef struct DrawImage DrawImage;

enum {
  DRAW_ENGINE_MODE_AUTO = 0,
  DRAW_ENGINE_MODE_GPU = 1,
  DRAW_ENGINE_MODE_CPU = 2,
  DRAW_ENGINE_MODE_HYBRID = 3
};

enum {
  DRAW_ENGINE_HYBRID_CPU_TEXT = 1 << 0,
  DRAW_ENGINE_HYBRID_CPU_LINE = 1 << 1,
  DRAW_ENGINE_HYBRID_CPU_CIRCLE = 1 << 2,
  DRAW_ENGINE_HYBRID_CPU_RECT = 1 << 3,
  DRAW_ENGINE_HYBRID_CPU_IMAGE = 1 << 4
};

enum {
  DRAW_ENGINE_OPT_RENDER_SCALE_X1000 = 1,
  DRAW_ENGINE_OPT_RENDER_WIDTH = 2,
  DRAW_ENGINE_OPT_RENDER_HEIGHT = 3,
  DRAW_ENGINE_OPT_TARGET_FPS = 4,
  DRAW_ENGINE_OPT_HYBRID_CPU_MASK = 5,
  DRAW_ENGINE_OPT_HYBRID_SENSITIVE_ONLY = 6,
  DRAW_ENGINE_OPT_SENSITIVE = 7,
  DRAW_ENGINE_OPT_CIRCLE_SEGMENTS = 8,
  DRAW_ENGINE_OPT_AUTO_QUALITY = 9,
  DRAW_ENGINE_OPT_QUALITY_LEVEL = 10
};

typedef struct DrawEngineCaps {
  int api_version;
  int has_vulkan;
  int has_gles;
  int backend;
  int mode;
  int hybrid;
  int render_scale_x1000;
  int render_width;
  int render_height;
  int circle_segments;
  int max_msaa;
  int supports_wide_lines;
  int supports_anisotropy;
  float max_anisotropy;
} DrawEngineCaps;

DRAW_ENGINE_API int init_engine_mode(int mode);
DRAW_ENGINE_API int init_draw_engine(int mode);
DRAW_ENGINE_API int init_draw_windows(const char* name, int randomize_name);
DRAW_ENGINE_API void shutdown_draw_engine(void);

DRAW_ENGINE_API void draw_engine_set_fps(int fps);
DRAW_ENGINE_API void draw_engine_set_hybrid_cpu_mask(uint32_t mask);
DRAW_ENGINE_API void draw_engine_set_sensitive(int enable);
DRAW_ENGINE_API int draw_engine_set_option(int option, int value);
DRAW_ENGINE_API int draw_engine_get_option(int option, int* out_value);
DRAW_ENGINE_API int draw_engine_get_capabilities(DrawEngineCaps* out_caps);
DRAW_ENGINE_API bool get_mode_is_need_set_fps(void);

DRAW_ENGINE_API int draw_begin_frame(void);
DRAW_ENGINE_API void draw_end_frame(void);
DRAW_ENGINE_API int draw_screen_width(void);
DRAW_ENGINE_API int draw_screen_height(void);
DRAW_ENGINE_API int draw_set_render_scale(float scale);
DRAW_ENGINE_API int draw_set_render_size(int width, int height);

DRAW_ENGINE_API void draw_text(const char* text,
                               int x0,
                               int y0,
                               int x1,
                               int y1,
                               uint32_t color);
DRAW_ENGINE_API void draw_rect(int x, int y, int w, int h, int filled, uint32_t color);
DRAW_ENGINE_API void draw_circle(int cx, int cy, int radius, uint32_t color);
DRAW_ENGINE_API void draw_line(int x1, int y1, int x2, int y2, uint32_t color);

DRAW_ENGINE_API DrawImage* draw_load_image_from_memory(const unsigned char* data, int size);
DRAW_ENGINE_API void draw_free_image(DrawImage* image);
DRAW_ENGINE_API void draw_image(const DrawImage* image, int x, int y);

#ifdef __cplusplus
}
#endif
