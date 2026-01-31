#include "include/draw_engine.h"
#include "include/midraw.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

struct DrawImage {
  int width;
  int height;
  uint32_t* pixels;
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
  char* surface_name;
  int rotation;
  bool in_frame;
  bool initialized;
};

static DrawEngineState g_engine{};

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

static uint32_t lcg(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
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

static void warn_no_frame() {
  static bool warned = false;
  if (!warned) {
    fprintf(stderr, "draw_engine: draw_begin_frame() not called\n");
    warned = true;
  }
}

int init_draw_engine(int mode) {
  memset(&g_engine, 0, sizeof(g_engine));
  g_engine.mode = mode;
  g_engine.backend = BACKEND_CPU;

  if (mode == 1) {
    if (has_library("libvulkan.so")) {
      g_engine.backend = BACKEND_VULKAN;
    } else if (has_library("libGLESv3.so") || has_library("libGLESv2.so")) {
      g_engine.backend = BACKEND_GLES;
    } else {
      g_engine.backend = BACKEND_CPU;
    }
  }

  if (g_engine.backend != BACKEND_CPU) {
    fprintf(stderr,
            "draw_engine: GPU backend selected (vk/gl). Demo build will fall back to CPU.\n");
    g_engine.backend = BACKEND_CPU;
  }

  g_engine.initialized = true;
  return 0;
}

int init_draw_windows(const char* name, int randomize_name) {
  if (!g_engine.initialized) {
    init_draw_engine(0);
  }

  if (g_engine.cpu_ctx) {
    midraw_shutdown(g_engine.cpu_ctx);
    g_engine.cpu_ctx = nullptr;
  }
  if (g_engine.surface_name) {
    free(g_engine.surface_name);
    g_engine.surface_name = nullptr;
  }

  const char* base = (name && name[0]) ? name : "DrawEngine";
  const size_t len = strlen(base);
  g_engine.surface_name = static_cast<char*>(calloc(len + 1, 1));
  if (!g_engine.surface_name) {
    return -1;
  }
  memcpy(g_engine.surface_name, base, len);
  if (randomize_name) {
    shuffle_name(g_engine.surface_name);
  }

  g_engine.rotation = env_int("MIDRAW_ROTATION", 0);

  MidrawConfig cfg{};
  cfg.surface_name = g_engine.surface_name;
  cfg.rotation = g_engine.rotation;
  cfg.width = 0;
  cfg.height = 0;
  cfg.font_path = getenv("MIDRAW_FONT_PATH");
  cfg.font_size = env_int("MIDRAW_FONT_SIZE", 0);
  cfg.atlas_size = env_int("MIDRAW_ATLAS_SIZE", 512);

  if (midraw_init(&g_engine.cpu_ctx, &cfg) != 0) {
    fprintf(stderr, "draw_engine: midraw_init failed\n");
    return -1;
  }
  return 0;
}

void shutdown_draw_engine(void) {
  if (g_engine.cpu_ctx) {
    midraw_shutdown(g_engine.cpu_ctx);
    g_engine.cpu_ctx = nullptr;
  }
  if (g_engine.surface_name) {
    free(g_engine.surface_name);
    g_engine.surface_name = nullptr;
  }
  memset(&g_engine, 0, sizeof(g_engine));
}

int draw_begin_frame(void) {
  if (!g_engine.cpu_ctx) {
    return -1;
  }
  if (g_engine.in_frame) {
    return 0;
  }
  if (midraw_lock(g_engine.cpu_ctx) != 0) {
    return -1;
  }
  g_engine.in_frame = true;
  return 0;
}

void draw_end_frame(void) {
  if (!g_engine.cpu_ctx || !g_engine.in_frame) {
    return;
  }
  midraw_unlock_post(g_engine.cpu_ctx);
  g_engine.in_frame = false;
}

int draw_screen_width(void) {
  if (!g_engine.cpu_ctx) {
    return 0;
  }
  return midraw_logical_width(g_engine.cpu_ctx);
}

int draw_screen_height(void) {
  if (!g_engine.cpu_ctx) {
    return 0;
  }
  return midraw_logical_height(g_engine.cpu_ctx);
}

void draw_text(const char* text, int x0, int y0, int x1, int y1, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  if (x1 > x0 && y1 > y0) {
    midraw_draw_text_rect(g_engine.cpu_ctx, text, x0, y0, x1, y1, color);
  } else {
    midraw_draw_text(g_engine.cpu_ctx, text, x0, y0, color);
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
  midraw_draw_rect(g_engine.cpu_ctx, x, y, w, h, filled, color);
}

void draw_circle(int cx, int cy, int radius, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  midraw_draw_circle(g_engine.cpu_ctx, cx, cy, radius, color);
}

void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
  if (!g_engine.cpu_ctx) {
    return;
  }
  if (!g_engine.in_frame) {
    warn_no_frame();
    return;
  }
  midraw_draw_line(g_engine.cpu_ctx, x1, y1, x2, y2, color);
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
  stbi_image_free(decoded);
  return image;
}

void draw_free_image(DrawImage* image) {
  if (!image) {
    return;
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
  midraw_draw_image(g_engine.cpu_ctx, image->pixels, image->width, image->height, x, y);
}
