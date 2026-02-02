#include "include/midraw.h"
#include "midraw_internal.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/NativeSurfaceUtils.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "generated/android_symbol_candidates.h"

#if defined(MIDRAW_USE_STB_TRUETYPE)
#include "stb_truetype.h"
#endif

#if defined(MIDRAW_USE_FONT8X8)
#include "font8x8_basic.h"
#endif

#ifndef GRALLOC_USAGE_SW_WRITE_OFTEN
#define GRALLOC_USAGE_SW_WRITE_OFTEN 0x00000030u
#endif

static constexpr int8_t kBufferTransparencyTranslucent = 1;

struct ANativeWindowBuffer {
  int32_t width;
  int32_t height;
  int32_t stride;
  int32_t format;
  int32_t usage;
  void* reserved[2];
};

struct GlyphInfo {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  int xoff = 0;
  int yoff = 0;
  int xadvance = 0;
};

struct FontAtlas {
  int width = 0;
  int height = 0;
  int first_char = 32;
  int num_chars = 95;
  int baseline = 0;
  int line_advance = 0;
  uint8_t* pixels = nullptr;
  GlyphInfo glyphs[96]{};
  bool valid = false;
};

enum class DrawCommandType : uint8_t { Pixel, Line, Rect, Circle, Text, Image };

struct DrawCommand {
  DrawCommandType type = DrawCommandType::Pixel;
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  int e = 0;
  int f = 0;
  uint32_t color = 0;
  uintptr_t ptr = 0;
};

static constexpr size_t kCommandCapacity = 4096;
static constexpr size_t kTextPoolCapacity = 65536;

struct MidrawContext {
  AndroidSymbols symbols;
  RenderContext render;
  int32_t requested_width = 0;
  int32_t requested_height = 0;
  FontAtlas atlas;
  bool defer_lock = true;
  bool recording = false;
  bool lock_size = false;
  bool ahb_only = false;
  DrawCommand command_buffer[kCommandCapacity]{};
  size_t command_head = 0;
  size_t command_count = 0;
  char text_pool[kTextPoolCapacity]{};
  size_t text_offset = 0;
  bool command_overflow = false;
  bool text_overflow = false;
};

#if !defined(MIDRAW_USE_FONT8X8)
// Minimal 8x8 font for digits and uppercase letters.
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
#endif

static const uint8_t* font_for_char(char c) {
#if defined(MIDRAW_USE_FONT8X8)
  return font8x8_basic[static_cast<uint8_t>(c)];
#else
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
#endif
}

static bool file_exists(const char* path) {
  if (!path || !path[0]) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && st.st_size > 0;
}

static bool load_file(const char* path, uint8_t** out_data, size_t* out_size) {
  if (!out_data || !out_size || !file_exists(path)) {
    return false;
  }
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return false;
  }
  long size = ftell(fp);
  if (size <= 0) {
    fclose(fp);
    return false;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return false;
  }
  uint8_t* data = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
  if (!data) {
    fclose(fp);
    return false;
  }
  size_t read_bytes = fread(data, 1, static_cast<size_t>(size), fp);
  fclose(fp);
  if (read_bytes != static_cast<size_t>(size)) {
    free(data);
    return false;
  }
  *out_data = data;
  *out_size = static_cast<size_t>(size);
  return true;
}

static int env_int(const char* name, int default_value) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return default_value;
  }
  return atoi(value);
}

static bool debug_timing_enabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = env_int("MIDRAW_DEBUG_TIMING", 0) != 0 ? 1 : 0;
  }
  return cached != 0;
}

static uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

static bool env_truthy(const char* name) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return false;
  }
  return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
}

static bool ahb_staging_enabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = env_int("MIDRAW_AHB_STAGING", 1) != 0 ? 1 : 0;
  }
  return cached != 0;
}

static int read_sdk_version() {
  const char* override = getenv("MIDRAW_SDK");
  if (override && override[0]) {
    return atoi(override);
  }
  char value[PROP_VALUE_MAX] = {};
  int len = __system_property_get("ro.build.version.sdk", value);
  if (len > 0) {
    return atoi(value);
  }
  return 0;
}

static bool setup_ahb_buffer(MidrawContext& ctx, int width, int height);

static const char* pick_default_font_path() {
  static const char* kPaths[] = {
      "/system/fonts/Roboto-Regular.ttf",
      "/system/fonts/NotoSans-Regular.ttf",
      "/system/fonts/NotoSansCJK-Regular.ttc",
      "/system/fonts/DroidSans.ttf",
  };
  for (const char* path : kPaths) {
    if (file_exists(path)) {
      return path;
    }
  }
  return nullptr;
}

#if defined(MIDRAW_USE_STB_TRUETYPE)
static bool init_font_atlas(MidrawContext& ctx, const MidrawConfig* config) {
  const char* font_path = (config && config->font_path) ? config->font_path : nullptr;
  if (!font_path || !file_exists(font_path)) {
    font_path = pick_default_font_path();
  }
  if (!font_path) {
    return false;
  }

  uint8_t* font_data = nullptr;
  size_t font_size_bytes = 0;
  if (!load_file(font_path, &font_data, &font_size_bytes)) {
    return false;
  }
  (void)font_size_bytes;

  int pixel_height = 20;
  if (config && config->font_size > 0) {
    pixel_height = config->font_size;
  }
  int atlas_size = 512;
  if (config && config->atlas_size > 0) {
    atlas_size = config->atlas_size;
  }
  if (atlas_size < 128) {
    atlas_size = 128;
  }

  stbtt_fontinfo font_info{};
  if (!stbtt_InitFont(&font_info, font_data, 0)) {
    free(font_data);
    return false;
  }

  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
  float scale = stbtt_ScaleForPixelHeight(&font_info, static_cast<float>(pixel_height));

  uint8_t* atlas_pixels = static_cast<uint8_t*>(
      calloc(static_cast<size_t>(atlas_size) * static_cast<size_t>(atlas_size), 1));
  if (!atlas_pixels) {
    free(font_data);
    return false;
  }

  const int first_char = 32;
  const int num_chars = 95;
  stbtt_bakedchar baked[num_chars];
  const int bake_result = stbtt_BakeFontBitmap(font_data, 0, static_cast<float>(pixel_height),
                                               atlas_pixels, atlas_size, atlas_size, first_char,
                                               num_chars, baked);
  free(font_data);
  if (bake_result <= 0) {
    free(atlas_pixels);
    return false;
  }

  ctx.atlas.width = atlas_size;
  ctx.atlas.height = atlas_size;
  ctx.atlas.first_char = first_char;
  ctx.atlas.num_chars = num_chars;
  ctx.atlas.baseline = static_cast<int>(floorf(ascent * scale + 0.5f));
  ctx.atlas.line_advance =
      static_cast<int>(floorf((ascent - descent + line_gap) * scale + 0.5f));
  ctx.atlas.pixels = atlas_pixels;
  ctx.atlas.valid = true;

  for (int i = 0; i < num_chars; ++i) {
    GlyphInfo& out = ctx.atlas.glyphs[i];
    const stbtt_bakedchar& bc = baked[i];
    out.x0 = bc.x0;
    out.y0 = bc.y0;
    out.x1 = bc.x1;
    out.y1 = bc.y1;
    out.xoff = static_cast<int>(floorf(bc.xoff));
    out.yoff = static_cast<int>(floorf(bc.yoff));
    out.xadvance = static_cast<int>(floorf(bc.xadvance + 0.5f));
  }

  return true;
}
#endif

static void release_font_atlas(FontAtlas& atlas) {
  if (atlas.pixels) {
    free(atlas.pixels);
    atlas.pixels = nullptr;
  }
  atlas.valid = false;
}

static void* load_symbol_candidates(void* handle,
                                    const char* logical_name,
                                    const char* const* candidates,
                                    size_t count) {
  if (!handle) {
    return nullptr;
  }
  for (size_t i = 0; i < count; ++i) {
    const char* name = candidates[i];
    if (!name || !name[0]) {
      continue;
    }
    dlerror();
    void* sym = dlsym(handle, name);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  if (logical_name && logical_name[0]) {
    dlerror();
    void* sym = dlsym(handle, logical_name);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  return nullptr;
}

static void* load_symbol_candidates_dual(void* primary,
                                         void* fallback,
                                         const char* logical_name,
                                         const char* const* candidates,
                                         size_t count) {
  void* sym = load_symbol_candidates(primary, logical_name, candidates, count);
  if (!sym && fallback && fallback != primary) {
    sym = load_symbol_candidates(fallback, logical_name, candidates, count);
  }
  return sym;
}

static void* load_symbol_any(const char* name, void* lib1, void* lib2, void* lib3) {
  if (!name || !name[0]) {
    return nullptr;
  }
  if (lib1) {
    dlerror();
    void* sym = dlsym(lib1, name);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  if (lib2) {
    dlerror();
    void* sym = dlsym(lib2, name);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  if (lib3) {
    dlerror();
    void* sym = dlsym(lib3, name);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  return nullptr;
}

static bool init_symbols(AndroidSymbols* symbols) {
  memset(symbols, 0, sizeof(*symbols));

  symbols->libandroid = dlopen("libandroid.so", RTLD_NOW);
  if (!symbols->libandroid) {
    fprintf(stderr, "dlopen libandroid.so failed: %s\n", dlerror());
    return false;
  }

  symbols->libgui = dlopen("libgui.so", RTLD_NOW);
  if (!symbols->libgui) {
    fprintf(stderr, "dlopen libgui.so failed: %s\n", dlerror());
    return false;
  }

  symbols->libui = dlopen("libui.so", RTLD_NOW);
  if (!symbols->libui) {
    fprintf(stderr, "dlopen libui.so failed: %s\n", dlerror());
  }

  symbols->libnativewindow = dlopen("libnativewindow.so", RTLD_NOW);
  if (!symbols->libnativewindow) {
    fprintf(stderr, "dlopen libnativewindow.so failed: %s\n", dlerror());
  }

  symbols->ASurfaceControl_create = reinterpret_cast<PFN_ASurfaceControl_create>(
      load_symbol_candidates_dual(symbols->libandroid, symbols->libgui, "ASurfaceControl_create",
                                  kCandidates_ASurfaceControl_create,
                                  sizeof(kCandidates_ASurfaceControl_create) /
                                      sizeof(kCandidates_ASurfaceControl_create[0])));
  static const char* kCandidates_ASurfaceControl_createFromWindow[] = {
      "ASurfaceControl_createFromWindow"};
  symbols->ASurfaceControl_createFromWindow =
      reinterpret_cast<PFN_ASurfaceControl_createFromWindow>(
          load_symbol_candidates_dual(symbols->libandroid,
                                      symbols->libgui,
                                      "ASurfaceControl_createFromWindow",
                                      kCandidates_ASurfaceControl_createFromWindow,
                                      sizeof(kCandidates_ASurfaceControl_createFromWindow) /
                                          sizeof(kCandidates_ASurfaceControl_createFromWindow[0])));
  symbols->ASurfaceControl_release = reinterpret_cast<PFN_ASurfaceControl_release>(
      load_symbol_candidates_dual(symbols->libandroid, symbols->libgui, "ASurfaceControl_release",
                                  kCandidates_ASurfaceControl_release,
                                  sizeof(kCandidates_ASurfaceControl_release) /
                                      sizeof(kCandidates_ASurfaceControl_release[0])));
  symbols->ASurfaceTransaction_create = reinterpret_cast<PFN_ASurfaceTransaction_create>(
      load_symbol_candidates_dual(symbols->libandroid,
                                  symbols->libgui,
                                  "ASurfaceTransaction_create",
                                  kCandidates_ASurfaceTransaction_create,
                                  sizeof(kCandidates_ASurfaceTransaction_create) /
                                      sizeof(kCandidates_ASurfaceTransaction_create[0])));
  symbols->ASurfaceTransaction_release = reinterpret_cast<PFN_ASurfaceTransaction_release>(
      load_symbol_candidates_dual(symbols->libandroid,
                                  symbols->libgui,
                                  "ASurfaceTransaction_release",
                                  kCandidates_ASurfaceTransaction_release,
                                  sizeof(kCandidates_ASurfaceTransaction_release) /
                                      sizeof(kCandidates_ASurfaceTransaction_release[0])));
  if (!symbols->ASurfaceTransaction_release) {
    static const char* kCandidates_ASurfaceTransaction_delete[] = {
        "ASurfaceTransaction_delete"};
    symbols->ASurfaceTransaction_release =
        reinterpret_cast<PFN_ASurfaceTransaction_release>(load_symbol_candidates_dual(
            symbols->libandroid,
            symbols->libgui,
            "ASurfaceTransaction_delete",
            kCandidates_ASurfaceTransaction_delete,
            sizeof(kCandidates_ASurfaceTransaction_delete) /
                sizeof(kCandidates_ASurfaceTransaction_delete[0])));
  }
  symbols->ASurfaceTransaction_setBufferSize =
      reinterpret_cast<PFN_ASurfaceTransaction_setBufferSize>(load_symbol_candidates_dual(
          symbols->libandroid,
          symbols->libgui,
          "ASurfaceTransaction_setBufferSize",
          kCandidates_ASurfaceTransaction_setBufferSize,
          sizeof(kCandidates_ASurfaceTransaction_setBufferSize) /
              sizeof(kCandidates_ASurfaceTransaction_setBufferSize[0])));
  symbols->ASurfaceTransaction_setVisibility =
      reinterpret_cast<PFN_ASurfaceTransaction_setVisibility>(load_symbol_candidates_dual(
          symbols->libandroid,
          symbols->libgui,
          "ASurfaceTransaction_setVisibility",
          kCandidates_ASurfaceTransaction_setVisibility,
          sizeof(kCandidates_ASurfaceTransaction_setVisibility) /
              sizeof(kCandidates_ASurfaceTransaction_setVisibility[0])));
  symbols->ASurfaceTransaction_setLayer =
      reinterpret_cast<PFN_ASurfaceTransaction_setLayer>(load_symbol_candidates_dual(
          symbols->libandroid,
          symbols->libgui,
          "ASurfaceTransaction_setLayer",
          kCandidates_ASurfaceTransaction_setLayer,
          sizeof(kCandidates_ASurfaceTransaction_setLayer) /
              sizeof(kCandidates_ASurfaceTransaction_setLayer[0])));
  static const char* kCandidates_ASurfaceTransaction_setZOrder[] = {
      "ASurfaceTransaction_setZOrder"};
  symbols->ASurfaceTransaction_setZOrder =
      reinterpret_cast<PFN_ASurfaceTransaction_setZOrder>(
          load_symbol_candidates_dual(symbols->libandroid,
                                      symbols->libgui,
                                      "ASurfaceTransaction_setZOrder",
                                      kCandidates_ASurfaceTransaction_setZOrder,
                                      sizeof(kCandidates_ASurfaceTransaction_setZOrder) /
                                          sizeof(kCandidates_ASurfaceTransaction_setZOrder[0])));
  symbols->ASurfaceTransaction_apply = reinterpret_cast<PFN_ASurfaceTransaction_apply>(
      load_symbol_candidates_dual(symbols->libandroid,
                                  symbols->libgui,
                                  "ASurfaceTransaction_apply",
                                  kCandidates_ASurfaceTransaction_apply,
                                  sizeof(kCandidates_ASurfaceTransaction_apply) /
                                      sizeof(kCandidates_ASurfaceTransaction_apply[0])));
  static const char* kCandidates_ASurfaceTransaction_setBuffer[] = {
      "ASurfaceTransaction_setBuffer"};
  symbols->ASurfaceTransaction_setBuffer =
      reinterpret_cast<PFN_ASurfaceTransaction_setBuffer>(
          load_symbol_candidates_dual(symbols->libandroid,
                                      symbols->libgui,
                                      "ASurfaceTransaction_setBuffer",
                                      kCandidates_ASurfaceTransaction_setBuffer,
                                      sizeof(kCandidates_ASurfaceTransaction_setBuffer) /
                                          sizeof(kCandidates_ASurfaceTransaction_setBuffer[0])));
  static const char* kCandidates_ASurfaceTransaction_setBufferTransparency[] = {
      "ASurfaceTransaction_setBufferTransparency"};
  symbols->ASurfaceTransaction_setBufferTransparency =
      reinterpret_cast<PFN_ASurfaceTransaction_setBufferTransparency>(
          load_symbol_candidates_dual(
              symbols->libandroid,
              symbols->libgui,
              "ASurfaceTransaction_setBufferTransparency",
              kCandidates_ASurfaceTransaction_setBufferTransparency,
              sizeof(kCandidates_ASurfaceTransaction_setBufferTransparency) /
                  sizeof(kCandidates_ASurfaceTransaction_setBufferTransparency[0])));
  static const char* kCandidates_ASurfaceTransaction_setGeometry[] = {
      "ASurfaceTransaction_setGeometry"};
  symbols->ASurfaceTransaction_setGeometry =
      reinterpret_cast<PFN_ASurfaceTransaction_setGeometry>(
          load_symbol_candidates_dual(symbols->libandroid,
                                      symbols->libgui,
                                      "ASurfaceTransaction_setGeometry",
                                      kCandidates_ASurfaceTransaction_setGeometry,
                                      sizeof(kCandidates_ASurfaceTransaction_setGeometry) /
                                          sizeof(kCandidates_ASurfaceTransaction_setGeometry[0])));

  symbols->ANativeWindow_fromSurfaceControl =
      reinterpret_cast<PFN_ANativeWindow_fromSurfaceControl>(load_symbol_candidates(
          symbols->libgui, "ANativeWindow_fromSurfaceControl",
          kCandidates_ANativeWindow_fromSurfaceControl,
          sizeof(kCandidates_ANativeWindow_fromSurfaceControl) /
              sizeof(kCandidates_ANativeWindow_fromSurfaceControl[0])));

  symbols->ANativeWindow_lock = reinterpret_cast<PFN_ANativeWindow_lock>(
      load_symbol_candidates(symbols->libandroid, "ANativeWindow_lock",
                             kCandidates_ANativeWindow_lock,
                             sizeof(kCandidates_ANativeWindow_lock) /
                                 sizeof(kCandidates_ANativeWindow_lock[0])));
  symbols->ANativeWindow_unlockAndPost = reinterpret_cast<PFN_ANativeWindow_unlockAndPost>(
      load_symbol_candidates(symbols->libandroid, "ANativeWindow_unlockAndPost",
                             kCandidates_ANativeWindow_unlockAndPost,
                             sizeof(kCandidates_ANativeWindow_unlockAndPost) /
                                 sizeof(kCandidates_ANativeWindow_unlockAndPost[0])));
  symbols->ANativeWindow_release = reinterpret_cast<PFN_ANativeWindow_release>(
      load_symbol_candidates(symbols->libandroid, "ANativeWindow_release",
                             kCandidates_ANativeWindow_release,
                             sizeof(kCandidates_ANativeWindow_release) /
                                 sizeof(kCandidates_ANativeWindow_release[0])));

  symbols->ANativeWindow_getWidth = reinterpret_cast<PFN_ANativeWindow_getWidth>(
      load_symbol_candidates(symbols->libandroid, "ANativeWindow_getWidth",
                             kCandidates_ANativeWindow_getWidth,
                             sizeof(kCandidates_ANativeWindow_getWidth) /
                                 sizeof(kCandidates_ANativeWindow_getWidth[0])));
  symbols->ANativeWindow_getHeight = reinterpret_cast<PFN_ANativeWindow_getHeight>(
      load_symbol_candidates(symbols->libandroid, "ANativeWindow_getHeight",
                             kCandidates_ANativeWindow_getHeight,
                             sizeof(kCandidates_ANativeWindow_getHeight) /
                                 sizeof(kCandidates_ANativeWindow_getHeight[0])));
  static const char* kCandidates_ANativeWindow_setBuffersGeometry[] = {
      "ANativeWindow_setBuffersGeometry"};
  symbols->ANativeWindow_setBuffersGeometry =
      reinterpret_cast<PFN_ANativeWindow_setBuffersGeometry>(
          load_symbol_candidates(symbols->libandroid, "ANativeWindow_setBuffersGeometry",
                                 kCandidates_ANativeWindow_setBuffersGeometry,
                                 sizeof(kCandidates_ANativeWindow_setBuffersGeometry) /
                                     sizeof(kCandidates_ANativeWindow_setBuffersGeometry[0])));

  symbols->AHardwareBuffer_allocate = reinterpret_cast<PFN_AHardwareBuffer_allocate>(
      load_symbol_any("AHardwareBuffer_allocate",
                      symbols->libandroid,
                      symbols->libnativewindow,
                      symbols->libgui));
  symbols->AHardwareBuffer_describe = reinterpret_cast<PFN_AHardwareBuffer_describe>(
      load_symbol_any("AHardwareBuffer_describe",
                      symbols->libandroid,
                      symbols->libnativewindow,
                      symbols->libgui));
  symbols->AHardwareBuffer_release = reinterpret_cast<PFN_AHardwareBuffer_release>(
      load_symbol_any("AHardwareBuffer_release",
                      symbols->libandroid,
                      symbols->libnativewindow,
                      symbols->libgui));
  symbols->AHardwareBuffer_lock = reinterpret_cast<PFN_AHardwareBuffer_lock>(
      load_symbol_any("AHardwareBuffer_lock",
                      symbols->libandroid,
                      symbols->libnativewindow,
                      symbols->libgui));
  symbols->AHardwareBuffer_unlock = reinterpret_cast<PFN_AHardwareBuffer_unlock>(
      load_symbol_any("AHardwareBuffer_unlock",
                      symbols->libandroid,
                      symbols->libnativewindow,
                      symbols->libgui));

  static const char* kSurfaceDequeueNames[] = {
      "_ZN7android7Surface13dequeueBufferEPP19ANativeWindowBufferPi",
  };
  static const char* kSurfaceQueue4Names[] = {
      "_ZN7android7Surface11queueBufferEP19ANativeWindowBufferiPNS_24SurfaceQueueBufferOutputE",
  };
  static const char* kSurfaceQueue3Names[] = {
      "_ZN7android7Surface11queueBufferEP19ANativeWindowBufferi",
  };
  static const char* kSurfaceCancelNames[] = {
      "_ZN7android7Surface12cancelBufferEP19ANativeWindowBufferi",
  };
  static const char* kGraphicBufferFromNames[] = {
      "_ZN7android13GraphicBuffer4fromEP19ANativeWindowBuffer",
  };
  static const char* kGraphicBufferLockNames[] = {
      "_ZN7android13GraphicBuffer4lockEjPPvPiS3_",
  };
  static const char* kGraphicBufferUnlockNames[] = {
      "_ZN7android13GraphicBuffer6unlockEv",
  };
  symbols->Surface_dequeueBuffer = reinterpret_cast<PFN_Surface_dequeueBuffer>(
      load_symbol_candidates(symbols->libgui, nullptr, kSurfaceDequeueNames,
                             sizeof(kSurfaceDequeueNames) / sizeof(kSurfaceDequeueNames[0])));
  symbols->Surface_queueBuffer4 = reinterpret_cast<PFN_Surface_queueBuffer4>(
      load_symbol_candidates(symbols->libgui, nullptr, kSurfaceQueue4Names,
                             sizeof(kSurfaceQueue4Names) / sizeof(kSurfaceQueue4Names[0])));
  symbols->Surface_queueBuffer3 = reinterpret_cast<PFN_Surface_queueBuffer3>(
      load_symbol_candidates(symbols->libgui, nullptr, kSurfaceQueue3Names,
                             sizeof(kSurfaceQueue3Names) / sizeof(kSurfaceQueue3Names[0])));
  symbols->Surface_cancelBuffer = reinterpret_cast<PFN_Surface_cancelBuffer>(
      load_symbol_candidates(symbols->libgui, nullptr, kSurfaceCancelNames,
                             sizeof(kSurfaceCancelNames) / sizeof(kSurfaceCancelNames[0])));
  symbols->GraphicBuffer_from = reinterpret_cast<PFN_GraphicBuffer_from>(
      load_symbol_candidates(symbols->libui, nullptr, kGraphicBufferFromNames,
                             sizeof(kGraphicBufferFromNames) / sizeof(kGraphicBufferFromNames[0])));
  symbols->GraphicBuffer_lock = reinterpret_cast<PFN_GraphicBuffer_lock>(
      load_symbol_candidates(symbols->libui, nullptr, kGraphicBufferLockNames,
                             sizeof(kGraphicBufferLockNames) / sizeof(kGraphicBufferLockNames[0])));
  symbols->GraphicBuffer_unlock = reinterpret_cast<PFN_GraphicBuffer_unlock>(
      load_symbol_candidates(symbols->libui, nullptr, kGraphicBufferUnlockNames,
                             sizeof(kGraphicBufferUnlockNames) /
                                 sizeof(kGraphicBufferUnlockNames[0])));

  const bool ok = symbols->ANativeWindow_lock && symbols->ANativeWindow_unlockAndPost &&
                  symbols->ANativeWindow_release;

  if (!ok) {
    fprintf(stderr, "dlsym failed: missing required symbols\n");
    return false;
  }

  return true;
}

static inline int logical_width(const RenderContext& ctx) {
  return (ctx.rotation == 90 || ctx.rotation == 270) ? ctx.height : ctx.width;
}

static inline int logical_height(const RenderContext& ctx) {
  return (ctx.rotation == 90 || ctx.rotation == 270) ? ctx.width : ctx.height;
}

static inline void map_coords(const RenderContext& ctx, int x, int y, int* out_x, int* out_y) {
  switch (ctx.rotation) {
    case 90:
      *out_x = ctx.width - 1 - y;
      *out_y = x;
      break;
    case 180:
      *out_x = ctx.width - 1 - x;
      *out_y = ctx.height - 1 - y;
      break;
    case 270:
      *out_x = y;
      *out_y = ctx.height - 1 - x;
      break;
    default:
      *out_x = x;
      *out_y = y;
      break;
  }
}

static inline int line_out_code(int x, int y, int w, int h) {
  int code = 0;
  if (x < 0) {
    code |= 1;
  } else if (x >= w) {
    code |= 2;
  }
  if (y < 0) {
    code |= 4;
  } else if (y >= h) {
    code |= 8;
  }
  return code;
}

static bool clip_line(int* x0, int* y0, int* x1, int* y1, int w, int h) {
  if (!x0 || !y0 || !x1 || !y1 || w <= 0 || h <= 0) {
    return false;
  }
  int x_start = *x0;
  int y_start = *y0;
  int x_end = *x1;
  int y_end = *y1;
  int out0 = line_out_code(x_start, y_start, w, h);
  int out1 = line_out_code(x_end, y_end, w, h);
  while (true) {
    if ((out0 | out1) == 0) {
      *x0 = x_start;
      *y0 = y_start;
      *x1 = x_end;
      *y1 = y_end;
      return true;
    }
    if (out0 & out1) {
      return false;
    }
    const int out = out0 ? out0 : out1;
    int x = 0;
    int y = 0;
    if (out & 8) {
      if (y_end == y_start) {
        return false;
      }
      x = x_start + (x_end - x_start) * (h - 1 - y_start) / (y_end - y_start);
      y = h - 1;
    } else if (out & 4) {
      if (y_end == y_start) {
        return false;
      }
      x = x_start + (x_end - x_start) * (0 - y_start) / (y_end - y_start);
      y = 0;
    } else if (out & 2) {
      if (x_end == x_start) {
        return false;
      }
      y = y_start + (y_end - y_start) * (w - 1 - x_start) / (x_end - x_start);
      x = w - 1;
    } else {
      if (x_end == x_start) {
        return false;
      }
      y = y_start + (y_end - y_start) * (0 - x_start) / (x_end - x_start);
      x = 0;
    }
    if (out == out0) {
      x_start = x;
      y_start = y;
      out0 = line_out_code(x_start, y_start, w, h);
    } else {
      x_end = x;
      y_end = y;
      out1 = line_out_code(x_end, y_end, w, h);
    }
  }
}

static inline void reset_rect(int& min_x, int& min_y, int& max_x, int& max_y) {
  min_x = INT_MAX;
  min_y = INT_MAX;
  max_x = INT_MIN;
  max_y = INT_MIN;
}

static inline bool rect_valid(int min_x, int min_y, int max_x, int max_y) {
  return min_x < max_x && min_y < max_y;
}

static inline bool intersect_rect(int ax0,
                                  int ay0,
                                  int ax1,
                                  int ay1,
                                  int bx0,
                                  int by0,
                                  int bx1,
                                  int by1,
                                  int* out_x0,
                                  int* out_y0,
                                  int* out_x1,
                                  int* out_y1) {
  const int rx0 = ax0 > bx0 ? ax0 : bx0;
  const int ry0 = ay0 > by0 ? ay0 : by0;
  const int rx1 = ax1 < bx1 ? ax1 : bx1;
  const int ry1 = ay1 < by1 ? ay1 : by1;
  if (rx0 >= rx1 || ry0 >= ry1) {
    return false;
  }
  if (out_x0) {
    *out_x0 = rx0;
  }
  if (out_y0) {
    *out_y0 = ry0;
  }
  if (out_x1) {
    *out_x1 = rx1;
  }
  if (out_y1) {
    *out_y1 = ry1;
  }
  return true;
}

static inline void reset_dirty(RenderContext& ctx) {
  reset_rect(ctx.dirty_min_x, ctx.dirty_min_y, ctx.dirty_max_x, ctx.dirty_max_y);
}

static inline void reset_present(RenderContext& ctx) {
  reset_rect(ctx.present_min_x, ctx.present_min_y, ctx.present_max_x, ctx.present_max_y);
}

static inline void reset_prev_dirty(RenderContext& ctx) {
  reset_rect(ctx.prev_dirty_min_x, ctx.prev_dirty_min_y, ctx.prev_dirty_max_x,
             ctx.prev_dirty_max_y);
}

static inline bool logical_to_physical_rect(const RenderContext& ctx,
                                            int x,
                                            int y,
                                            int w,
                                            int h,
                                            int* out_x,
                                            int* out_y,
                                            int* out_w,
                                            int* out_h) {
  if (w <= 0 || h <= 0) {
    return false;
  }
  int px = x;
  int py = y;
  int pw = w;
  int ph = h;
  switch (ctx.rotation) {
    case 90:
      px = ctx.width - (y + h);
      py = x;
      pw = h;
      ph = w;
      break;
    case 180:
      px = ctx.width - (x + w);
      py = ctx.height - (y + h);
      pw = w;
      ph = h;
      break;
    case 270:
      px = y;
      py = ctx.height - (x + w);
      pw = h;
      ph = w;
      break;
    default:
      break;
  }
  *out_x = px;
  *out_y = py;
  *out_w = pw;
  *out_h = ph;
  return true;
}

static inline void expand_dirty_rect(RenderContext& ctx, int x, int y, int w, int h) {
  if (ctx.width <= 0 || ctx.height <= 0) {
    return;
  }
  int px = 0;
  int py = 0;
  int pw = 0;
  int ph = 0;
  if (!logical_to_physical_rect(ctx, x, y, w, h, &px, &py, &pw, &ph)) {
    return;
  }
  if (px < 0) {
    pw += px;
    px = 0;
  }
  if (py < 0) {
    ph += py;
    py = 0;
  }
  if (px + pw > ctx.width) {
    pw = ctx.width - px;
  }
  if (py + ph > ctx.height) {
    ph = ctx.height - py;
  }
  if (pw <= 0 || ph <= 0) {
    return;
  }
  if (px < ctx.dirty_min_x) {
    ctx.dirty_min_x = px;
  }
  if (py < ctx.dirty_min_y) {
    ctx.dirty_min_y = py;
  }
  if (px + pw > ctx.dirty_max_x) {
    ctx.dirty_max_x = px + pw;
  }
  if (py + ph > ctx.dirty_max_y) {
    ctx.dirty_max_y = py + ph;
  }
}

struct ColorComponents {
  uint8_t a;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static inline uint8_t div255_u32(uint32_t x) {
  return static_cast<uint8_t>((x + 128 + ((x + 128) >> 8)) >> 8);
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
static inline uint8x8_t div255_u16x8(uint16x8_t x) {
  uint16x8_t t = vaddq_u16(x, vdupq_n_u16(128));
  t = vaddq_u16(t, vshrq_n_u16(t, 8));
  return vshrn_n_u16(t, 8);
}

static inline uint8x8_t mul_div255_u8(uint8x8_t a, uint8x8_t b) {
  uint16x8_t prod = vmull_u8(a, b);
  return div255_u16x8(prod);
}
#endif

static inline uint32_t blend_pixel(uint32_t dst, const ColorComponents& src, uint8_t coverage) {
  if (coverage == 0 || src.a == 0) {
    return dst;
  }
  uint32_t effective_a = div255_u32(static_cast<uint32_t>(coverage) * src.a);
  if (effective_a == 0) {
    return dst;
  }
  if (effective_a == 255) {
    return (static_cast<uint32_t>(src.a) << 24) | (static_cast<uint32_t>(src.r) << 16) |
           (static_cast<uint32_t>(src.g) << 8) | static_cast<uint32_t>(src.b);
  }
  const uint32_t inv = 255 - effective_a;
  const uint8_t dst_a = static_cast<uint8_t>((dst >> 24) & 0xFF);
  const uint8_t dst_r = static_cast<uint8_t>((dst >> 16) & 0xFF);
  const uint8_t dst_g = static_cast<uint8_t>((dst >> 8) & 0xFF);
  const uint8_t dst_b = static_cast<uint8_t>(dst & 0xFF);

  const uint8_t out_a = static_cast<uint8_t>(effective_a + div255_u32(dst_a * inv));
  const uint8_t out_r = div255_u32(static_cast<uint32_t>(src.r) * effective_a +
                                   static_cast<uint32_t>(dst_r) * inv);
  const uint8_t out_g = div255_u32(static_cast<uint32_t>(src.g) * effective_a +
                                   static_cast<uint32_t>(dst_g) * inv);
  const uint8_t out_b = div255_u32(static_cast<uint32_t>(src.b) * effective_a +
                                   static_cast<uint32_t>(dst_b) * inv);

  return (static_cast<uint32_t>(out_a) << 24) | (static_cast<uint32_t>(out_r) << 16) |
         (static_cast<uint32_t>(out_g) << 8) | static_cast<uint32_t>(out_b);
}

static inline void update_dimensions(MidrawContext& ctx) {
  if (ctx.lock_size) {
    if (ctx.requested_width > 0) {
      ctx.render.width = ctx.requested_width;
    }
    if (ctx.requested_height > 0) {
      ctx.render.height = ctx.requested_height;
    }
    return;
  }
  if (!ctx.render.window || !ctx.symbols.ANativeWindow_getWidth ||
      !ctx.symbols.ANativeWindow_getHeight) {
    if (ctx.requested_width > 0) {
      ctx.render.width = ctx.requested_width;
    }
    if (ctx.requested_height > 0) {
      ctx.render.height = ctx.requested_height;
    }
    return;
  }
  const int width = ctx.symbols.ANativeWindow_getWidth(ctx.render.window);
  const int height = ctx.symbols.ANativeWindow_getHeight(ctx.render.window);
  if (width > 0) {
    ctx.render.width = width;
  }
  if (height > 0) {
    ctx.render.height = height;
  }
  if (ctx.render.width <= 0 && ctx.requested_width > 0) {
    ctx.render.width = ctx.requested_width;
  }
  if (ctx.render.height <= 0 && ctx.requested_height > 0) {
    ctx.render.height = ctx.requested_height;
  }
}

static void refresh_display_state(MidrawContext& ctx) {
  const int auto_rotate = env_int("MIDRAW_AUTO_ROTATE", 1);
  const int resize_on_rotation = env_int("MIDRAW_RESIZE_ON_ROTATION", 1);
  if (ctx.lock_size) {
    update_dimensions(ctx);
  }
  if (auto_rotate == 0 && resize_on_rotation == 0) {
    return;
  }
  int display_rot = 0;
  int display_w = 0;
  int display_h = 0;
  if (midraw_query_display_rotation(ctx.symbols, &display_rot, &display_w, &display_h) != 0) {
    return;
  }
  if (display_rot < 0) {
    display_rot %= 360;
    if (display_rot < 0) {
      display_rot += 360;
    }
  }

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

  int window_w = ctx.render.width;
  int window_h = ctx.render.height;
  if (ctx.render.window && ctx.symbols.ANativeWindow_getWidth &&
      ctx.symbols.ANativeWindow_getHeight) {
    const int w = ctx.symbols.ANativeWindow_getWidth(ctx.render.window);
    const int h = ctx.symbols.ANativeWindow_getHeight(ctx.render.window);
    if (w > 0) {
      window_w = w;
    }
    if (h > 0) {
      window_h = h;
    }
  }
  const bool window_matches_display =
      (window_w > 0 && window_h > 0 && display_logical_w > 0 && display_logical_h > 0 &&
       window_w == display_logical_w && window_h == display_logical_h);

  if (auto_rotate != 0) {
    const int counter_rotation = env_int("MIDRAW_COUNTER_ROTATION", 1);
    int desired_rot = ctx.render.rotation;
    if (counter_rotation != 0) {
      if (window_matches_display) {
        desired_rot = 0;
      } else if (display_rot == 90) {
        desired_rot = 270;
      } else if (display_rot == 270) {
        desired_rot = 90;
      } else if (display_rot == 180) {
        desired_rot = 180;
      } else {
        desired_rot = 0;
      }
    } else {
      desired_rot = display_rot;
    }
    ctx.render.rotation = desired_rot;
  }

  if (resize_on_rotation != 0) {
    if (ctx.lock_size) {
      return;
    }
    const int use_display_size = env_int("MIDRAW_USE_DISPLAY_SIZE", 1);
    if (use_display_size != 0 && display_logical_w > 0 && display_logical_h > 0) {
      if (window_w != display_logical_w || window_h != display_logical_h) {
        if (midraw_resize_window(ctx.symbols, ctx.render, display_logical_w,
                                 display_logical_h) == 0) {
          ctx.render.width = display_logical_w;
          ctx.render.height = display_logical_h;
          if (ctx.render.use_ahb) {
            setup_ahb_buffer(ctx, display_logical_w, display_logical_h);
          }
        }
      }
    }
  }
}

static inline void reset_recording_buffers(MidrawContext& ctx) {
  ctx.command_head = 0;
  ctx.command_count = 0;
  ctx.text_offset = 0;
  ctx.command_overflow = false;
  ctx.text_overflow = false;
}

static inline bool push_command(MidrawContext& ctx, const DrawCommand& cmd) {
  if (ctx.command_count >= kCommandCapacity) {
    ctx.command_overflow = true;
    return false;
  }
  const size_t index = (ctx.command_head + ctx.command_count) % kCommandCapacity;
  ctx.command_buffer[index] = cmd;
  ++ctx.command_count;
  return true;
}

static inline bool should_defer_lock() {
  const char* env = getenv("MIDRAW_DEFER_LOCK");
  if (!env) {
    return true;
  }
  return env[0] != '0';
}

static inline void blend_pixel_mapped(RenderContext& ctx,
                                      int x,
                                      int y,
                                      const ColorComponents& src,
                                      uint8_t coverage) {
  if (!ctx.pixels) {
    return;
  }
  int px = 0;
  int py = 0;
  map_coords(ctx, x, y, &px, &py);
  if (px < 0 || py < 0 || px >= ctx.width || py >= ctx.height) {
    return;
  }
  uint32_t* dst = ctx.pixels + py * ctx.stride + px;
  *dst = blend_pixel(*dst, src, coverage);
}

static inline void plot_pixel_physical(RenderContext& ctx, int px, int py, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  if (px < 0 || py < 0 || px >= ctx.width || py >= ctx.height) {
    return;
  }
  ctx.pixels[py * ctx.stride + px] = color;
}

static inline void plot_pixel_physical_unchecked(RenderContext& ctx, int px, int py,
                                                 uint32_t color) {
  ctx.pixels[py * ctx.stride + px] = color;
}

static inline void plot_pixel(RenderContext& ctx, int x, int y, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const int w = logical_width(ctx);
  const int h = logical_height(ctx);
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return;
  }
  if (ctx.rotation == 0) {
    plot_pixel_physical(ctx, x, y, color);
    return;
  }
  int px = 0;
  int py = 0;
  map_coords(ctx, x, y, &px, &py);
  plot_pixel_physical(ctx, px, py, color);
}

static void draw_pixel(RenderContext& ctx, int x, int y, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const int w = logical_width(ctx);
  const int h = logical_height(ctx);
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return;
  }
  expand_dirty_rect(ctx, x, y, 1, 1);
  plot_pixel(ctx, x, y, color);
}

static void plot_line(RenderContext& ctx, int x1, int y1, int x2, int y2, uint32_t color) {
  int dx = abs(x2 - x1);
  int sx = x1 < x2 ? 1 : -1;
  int dy = -abs(y2 - y1);
  int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    plot_pixel(ctx, x1, y1, color);
    if (x1 == x2 && y1 == y2) {
      break;
    }
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }
}

static void fill_rect_physical(RenderContext& ctx, int x, int y, int w, int h, uint32_t color);

static void plot_line_physical(RenderContext& ctx,
                               int x1,
                               int y1,
                               int x2,
                               int y2,
                               uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  if (y1 == y2) {
    const int start = x1 < x2 ? x1 : x2;
    const int end = x1 < x2 ? x2 : x1;
    fill_rect_physical(ctx, start, y1, end - start + 1, 1, color);
    return;
  }
  if (x1 == x2) {
    const int start = y1 < y2 ? y1 : y2;
    const int end = y1 < y2 ? y2 : y1;
    fill_rect_physical(ctx, x1, start, 1, end - start + 1, color);
    return;
  }
  const int min_x = x1 < x2 ? x1 : x2;
  const int max_x = x1 < x2 ? x2 : x1;
  const int min_y = y1 < y2 ? y1 : y2;
  const int max_y = y1 < y2 ? y2 : y1;
  const bool in_bounds = (min_x >= 0 && min_y >= 0 && max_x < ctx.width &&
                          max_y < ctx.height);
  int dx = abs(x2 - x1);
  int sx = x1 < x2 ? 1 : -1;
  int dy = -abs(y2 - y1);
  int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;

  if (in_bounds) {
    while (true) {
      plot_pixel_physical_unchecked(ctx, x1, y1, color);
      if (x1 == x2 && y1 == y2) {
        break;
      }
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x1 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y1 += sy;
      }
    }
  } else {
    while (true) {
      plot_pixel_physical(ctx, x1, y1, color);
      if (x1 == x2 && y1 == y2) {
        break;
      }
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x1 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y1 += sy;
      }
    }
  }
}

static void plot_circle_physical(RenderContext& ctx, int cx, int cy, int radius, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const bool in_bounds = (cx - radius >= 0 && cy - radius >= 0 &&
                          cx + radius < ctx.width && cy + radius < ctx.height);
  int x = radius;
  int y = 0;
  int err = 1 - x;

  while (x >= y) {
    if (in_bounds) {
      plot_pixel_physical_unchecked(ctx, cx + x, cy + y, color);
      plot_pixel_physical_unchecked(ctx, cx + y, cy + x, color);
      plot_pixel_physical_unchecked(ctx, cx - y, cy + x, color);
      plot_pixel_physical_unchecked(ctx, cx - x, cy + y, color);
      plot_pixel_physical_unchecked(ctx, cx - x, cy - y, color);
      plot_pixel_physical_unchecked(ctx, cx - y, cy - x, color);
      plot_pixel_physical_unchecked(ctx, cx + y, cy - x, color);
      plot_pixel_physical_unchecked(ctx, cx + x, cy - y, color);
    } else {
      plot_pixel_physical(ctx, cx + x, cy + y, color);
      plot_pixel_physical(ctx, cx + y, cy + x, color);
      plot_pixel_physical(ctx, cx - y, cy + x, color);
      plot_pixel_physical(ctx, cx - x, cy + y, color);
      plot_pixel_physical(ctx, cx - x, cy - y, color);
      plot_pixel_physical(ctx, cx - y, cy - x, color);
      plot_pixel_physical(ctx, cx + y, cy - x, color);
      plot_pixel_physical(ctx, cx + x, cy - y, color);
    }

    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x + 1);
    }
  }
}

static inline ColorComponents color_components(uint32_t color) {
  return ColorComponents{
      static_cast<uint8_t>((color >> 24) & 0xFF),
      static_cast<uint8_t>((color >> 16) & 0xFF),
      static_cast<uint8_t>((color >> 8) & 0xFF),
      static_cast<uint8_t>(color & 0xFF),
  };
}

static inline uint8_t to_coverage(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 1.0f) {
    return 255;
  }
  const int v = static_cast<int>(value * 255.0f + 0.5f);
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline float fpart(float x) {
  return x - floorf(x);
}

static inline float rfpart(float x) {
  return 1.0f - fpart(x);
}

static bool aa_lines_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const int aa = env_int("MIDRAW_AA", 1);
    cached = env_int("MIDRAW_AA_LINE", aa);
  }
  return cached != 0;
}

static bool aa_circles_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const int aa = env_int("MIDRAW_AA", 1);
    cached = env_int("MIDRAW_AA_CIRCLE", aa);
  }
  return cached != 0;
}

static void draw_line_aa(RenderContext& ctx, int x0, int y0, int x1, int y1, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const ColorComponents comp = color_components(color);
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) {
    int tmp = x0; x0 = y0; y0 = tmp;
    tmp = x1; x1 = y1; y1 = tmp;
  }
  if (x0 > x1) {
    int tmp = x0; x0 = x1; x1 = tmp;
    tmp = y0; y0 = y1; y1 = tmp;
  }
  const float dx = static_cast<float>(x1 - x0);
  const float dy = static_cast<float>(y1 - y0);
  const float gradient = (dx == 0.0f) ? 1.0f : (dy / dx);

  float xend = roundf(static_cast<float>(x0));
  float yend = static_cast<float>(y0) + gradient * (xend - static_cast<float>(x0));
  float xgap = rfpart(static_cast<float>(x0) + 0.5f);
  int xpxl1 = static_cast<int>(xend);
  int ypxl1 = static_cast<int>(floorf(yend));
  if (steep) {
    blend_pixel_mapped(ctx, ypxl1, xpxl1, comp, to_coverage(rfpart(yend) * xgap));
    blend_pixel_mapped(ctx, ypxl1 + 1, xpxl1, comp, to_coverage(fpart(yend) * xgap));
  } else {
    blend_pixel_mapped(ctx, xpxl1, ypxl1, comp, to_coverage(rfpart(yend) * xgap));
    blend_pixel_mapped(ctx, xpxl1, ypxl1 + 1, comp, to_coverage(fpart(yend) * xgap));
  }
  float intery = yend + gradient;

  xend = roundf(static_cast<float>(x1));
  yend = static_cast<float>(y1) + gradient * (xend - static_cast<float>(x1));
  xgap = fpart(static_cast<float>(x1) + 0.5f);
  int xpxl2 = static_cast<int>(xend);
  int ypxl2 = static_cast<int>(floorf(yend));
  if (steep) {
    blend_pixel_mapped(ctx, ypxl2, xpxl2, comp, to_coverage(rfpart(yend) * xgap));
    blend_pixel_mapped(ctx, ypxl2 + 1, xpxl2, comp, to_coverage(fpart(yend) * xgap));
  } else {
    blend_pixel_mapped(ctx, xpxl2, ypxl2, comp, to_coverage(rfpart(yend) * xgap));
    blend_pixel_mapped(ctx, xpxl2, ypxl2 + 1, comp, to_coverage(fpart(yend) * xgap));
  }

  for (int x = xpxl1 + 1; x < xpxl2; ++x) {
    if (steep) {
      blend_pixel_mapped(ctx, static_cast<int>(floorf(intery)), x, comp,
                         to_coverage(rfpart(intery)));
      blend_pixel_mapped(ctx, static_cast<int>(floorf(intery)) + 1, x, comp,
                         to_coverage(fpart(intery)));
    } else {
      blend_pixel_mapped(ctx, x, static_cast<int>(floorf(intery)), comp,
                         to_coverage(rfpart(intery)));
      blend_pixel_mapped(ctx, x, static_cast<int>(floorf(intery)) + 1, comp,
                         to_coverage(fpart(intery)));
    }
    intery += gradient;
  }
}

static void draw_circle_aa(RenderContext& ctx, int cx, int cy, int radius, uint32_t color) {
  if (!ctx.pixels || radius <= 0) {
    return;
  }
  const ColorComponents comp = color_components(color);
  const float r = static_cast<float>(radius);
  const float r2 = r * r;
  for (int x = 0; x <= radius; ++x) {
    const float fx = static_cast<float>(x);
    const float fy = sqrtf(r2 - fx * fx);
    const int iy = static_cast<int>(floorf(fy));
    const float frac = fy - static_cast<float>(iy);
    const uint8_t a0 = to_coverage(1.0f - frac);
    const uint8_t a1 = to_coverage(frac);

    const int px = cx + x;
    const int nx = cx - x;
    const int py = cy + iy;
    const int ny = cy - iy;

    blend_pixel_mapped(ctx, px, py, comp, a0);
    blend_pixel_mapped(ctx, px, py + 1, comp, a1);
    blend_pixel_mapped(ctx, px, ny, comp, a0);
    blend_pixel_mapped(ctx, px, ny - 1, comp, a1);
    blend_pixel_mapped(ctx, nx, py, comp, a0);
    blend_pixel_mapped(ctx, nx, py + 1, comp, a1);
    blend_pixel_mapped(ctx, nx, ny, comp, a0);
    blend_pixel_mapped(ctx, nx, ny - 1, comp, a1);

    const int py2 = cy + x;
    const int ny2 = cy - x;
    const int px2 = cx + iy;
    const int nx2 = cx - iy;
    blend_pixel_mapped(ctx, px2, py2, comp, a0);
    blend_pixel_mapped(ctx, px2 + 1, py2, comp, a1);
    blend_pixel_mapped(ctx, px2, ny2, comp, a0);
    blend_pixel_mapped(ctx, px2 + 1, ny2, comp, a1);
    blend_pixel_mapped(ctx, nx2, py2, comp, a0);
    blend_pixel_mapped(ctx, nx2 - 1, py2, comp, a1);
    blend_pixel_mapped(ctx, nx2, ny2, comp, a0);
    blend_pixel_mapped(ctx, nx2 - 1, ny2, comp, a1);
  }
}

static void draw_line(RenderContext& ctx, int x1, int y1, int x2, int y2, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const int min_x = x1 < x2 ? x1 : x2;
  const int max_x = x1 > x2 ? x1 : x2;
  const int min_y = y1 < y2 ? y1 : y2;
  const int max_y = y1 > y2 ? y1 : y2;
  if (aa_lines_enabled()) {
    int cx1 = x1;
    int cy1 = y1;
    int cx2 = x2;
    int cy2 = y2;
    const int lw = logical_width(ctx);
    const int lh = logical_height(ctx);
    if (!clip_line(&cx1, &cy1, &cx2, &cy2, lw, lh)) {
      return;
    }
    const int pad = 1;
    const int cmin_x = cx1 < cx2 ? cx1 : cx2;
    const int cmax_x = cx1 > cx2 ? cx1 : cx2;
    const int cmin_y = cy1 < cy2 ? cy1 : cy2;
    const int cmax_y = cy1 > cy2 ? cy1 : cy2;
    expand_dirty_rect(ctx, cmin_x - pad, cmin_y - pad,
                      (cmax_x - cmin_x + 1) + pad * 2,
                      (cmax_y - cmin_y + 1) + pad * 2);
    draw_line_aa(ctx, cx1, cy1, cx2, cy2, color);
    return;
  }
  expand_dirty_rect(ctx, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
  if (ctx.rotation == 0) {
    plot_line_physical(ctx, x1, y1, x2, y2, color);
    return;
  }
  int px1 = 0;
  int py1 = 0;
  int px2 = 0;
  int py2 = 0;
  map_coords(ctx, x1, y1, &px1, &py1);
  map_coords(ctx, x2, y2, &px2, &py2);
  plot_line_physical(ctx, px1, py1, px2, py2, color);
}

static void draw_circle(RenderContext& ctx, int cx, int cy, int radius, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  if (aa_circles_enabled()) {
    const int pad = 1;
    expand_dirty_rect(ctx, cx - radius - pad, cy - radius - pad,
                      radius * 2 + 1 + pad * 2, radius * 2 + 1 + pad * 2);
    draw_circle_aa(ctx, cx, cy, radius, color);
    return;
  }
  expand_dirty_rect(ctx, cx - radius, cy - radius, radius * 2 + 1, radius * 2 + 1);
  if (ctx.rotation == 0) {
    plot_circle_physical(ctx, cx, cy, radius, color);
    return;
  }
  int pcx = 0;
  int pcy = 0;
  map_coords(ctx, cx, cy, &pcx, &pcy);
  plot_circle_physical(ctx, pcx, pcy, radius, color);
}

static void fill_rect_physical(RenderContext& ctx, int x, int y, int w, int h, uint32_t color) {
  if (!ctx.pixels || w <= 0 || h <= 0) {
    return;
  }
  int start_x = x;
  int start_y = y;
  int end_x = x + w;
  int end_y = y + h;
  if (start_x < 0) {
    start_x = 0;
  }
  if (start_y < 0) {
    start_y = 0;
  }
  if (end_x > ctx.width) {
    end_x = ctx.width;
  }
  if (end_y > ctx.height) {
    end_y = ctx.height;
  }
  if (start_x >= end_x || start_y >= end_y) {
    return;
  }

  const int row_pixels = end_x - start_x;
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  const uint32x4_t vcolor = vdupq_n_u32(color);
  for (int yy = start_y; yy < end_y; ++yy) {
    uint32_t* row = ctx.pixels + yy * ctx.stride + start_x;
    int xcount = row_pixels;
    while (xcount >= 4) {
      vst1q_u32(row, vcolor);
      row += 4;
      xcount -= 4;
    }
    while (xcount-- > 0) {
      *row++ = color;
    }
  }
#else
  for (int yy = start_y; yy < end_y; ++yy) {
    uint32_t* row = ctx.pixels + yy * ctx.stride + start_x;
    for (int xx = 0; xx < row_pixels; ++xx) {
      row[xx] = color;
    }
  }
#endif
}

static void draw_rect(RenderContext& ctx, int x, int y, int w, int h, bool filled,
                      uint32_t color) {
  if (!ctx.pixels || w <= 0 || h <= 0) {
    return;
  }
  expand_dirty_rect(ctx, x, y, w, h);
  if (!filled) {
    int px = 0;
    int py = 0;
    int pw = 0;
    int ph = 0;
    if (!logical_to_physical_rect(ctx, x, y, w, h, &px, &py, &pw, &ph)) {
      return;
    }
    const int x1 = px;
    const int y1 = py;
    const int x2 = px + pw - 1;
    const int y2 = py + ph - 1;
    plot_line_physical(ctx, x1, y1, x2, y1, color);
    plot_line_physical(ctx, x1, y2, x2, y2, color);
    plot_line_physical(ctx, x1, y1, x1, y2, color);
    plot_line_physical(ctx, x2, y1, x2, y2, color);
    return;
  }

  int px = 0;
  int py = 0;
  int pw = 0;
  int ph = 0;
  if (!logical_to_physical_rect(ctx, x, y, w, h, &px, &py, &pw, &ph)) {
    return;
  }
  fill_rect_physical(ctx, px, py, pw, ph, color);
}

static void draw_char(RenderContext& ctx, char c, int x, int y, uint32_t color) {
  const uint8_t* glyph = font_for_char(c);
  for (int row = 0; row < 8; ++row) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; ++col) {
      if (bits & (1u << (7 - col))) {
        plot_pixel(ctx, x + col, y + row, color);
      }
    }
  }
}

static void draw_text(RenderContext& ctx, const char* text, int x, int y, uint32_t color) {
  if (!text || !ctx.pixels) {
    return;
  }
  int cursor_x = x;
  int cursor_y = y;
  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = x;
      cursor_y += 8;
      continue;
    }
    expand_dirty_rect(ctx, cursor_x, cursor_y, 8, 8);
    draw_char(ctx, c, cursor_x, cursor_y, color);
    cursor_x += 8;
  }
}

static void draw_char_clipped(RenderContext& ctx,
                              char c,
                              int x,
                              int y,
                              int clip_x0,
                              int clip_y0,
                              int clip_x1,
                              int clip_y1,
                              uint32_t color) {
  const uint8_t* glyph = font_for_char(c);
  for (int row = 0; row < 8; ++row) {
    int py = y + row;
    if (py < clip_y0 || py >= clip_y1) {
      continue;
    }
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; ++col) {
      if (!(bits & (1u << (7 - col)))) {
        continue;
      }
      int px = x + col;
      if (px < clip_x0 || px >= clip_x1) {
        continue;
      }
      plot_pixel(ctx, px, py, color);
    }
  }
}

static void draw_text_clipped(RenderContext& ctx,
                              const char* text,
                              int x,
                              int y,
                              int clip_x0,
                              int clip_y0,
                              int clip_x1,
                              int clip_y1,
                              uint32_t color) {
  if (!text || !ctx.pixels) {
    return;
  }
  if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
    draw_text(ctx, text, x, y, color);
    return;
  }
  int cursor_x = x;
  int cursor_y = y;
  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = x;
      cursor_y += 8;
      continue;
    }
    int gx0 = cursor_x;
    int gy0 = cursor_y;
    int gx1 = cursor_x + 8;
    int gy1 = cursor_y + 8;
    int ix0 = 0;
    int iy0 = 0;
    int ix1 = 0;
    int iy1 = 0;
    if (intersect_rect(gx0, gy0, gx1, gy1, clip_x0, clip_y0, clip_x1, clip_y1,
                       &ix0, &iy0, &ix1, &iy1)) {
      expand_dirty_rect(ctx, ix0, iy0, ix1 - ix0, iy1 - iy0);
      draw_char_clipped(ctx, c, cursor_x, cursor_y, clip_x0, clip_y0, clip_x1, clip_y1,
                        color);
    }
    cursor_x += 8;
  }
}

#if defined(MIDRAW_USE_STB_TRUETYPE)
static void expand_text_dirty_atlas(MidrawContext& ctx, const char* text, int x, int y) {
  if (!text || !ctx.atlas.valid) {
    return;
  }
  int cursor_x = x;
  int cursor_y = y;
  const int first_char = ctx.atlas.first_char;
  const int last_char = ctx.atlas.first_char + ctx.atlas.num_chars - 1;

  int min_x = INT_MAX;
  int min_y = INT_MAX;
  int max_x = INT_MIN;
  int max_y = INT_MIN;

  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = x;
      cursor_y += (ctx.atlas.line_advance > 0 ? ctx.atlas.line_advance : 16);
      continue;
    }
    if (c < first_char || c > last_char) {
      cursor_x += 8;
      continue;
    }
    const GlyphInfo& glyph = ctx.atlas.glyphs[c - first_char];
    const int dst_x = cursor_x + glyph.xoff;
    const int dst_y = cursor_y + ctx.atlas.baseline + glyph.yoff;
    const int glyph_w = glyph.x1 - glyph.x0;
    const int glyph_h = glyph.y1 - glyph.y0;
    if (glyph_w > 0 && glyph_h > 0) {
      if (dst_x < min_x) {
        min_x = dst_x;
      }
      if (dst_y < min_y) {
        min_y = dst_y;
      }
      if (dst_x + glyph_w > max_x) {
        max_x = dst_x + glyph_w;
      }
      if (dst_y + glyph_h > max_y) {
        max_y = dst_y + glyph_h;
      }
    }
    cursor_x += glyph.xadvance;
  }

  if (min_x < max_x && min_y < max_y) {
    expand_dirty_rect(ctx.render, min_x, min_y, max_x - min_x, max_y - min_y);
  }
}
#endif

static void expand_text_dirty_font8x8(RenderContext& render, const char* text, int x, int y) {
  if (!text) {
    return;
  }
  int max_line = 0;
  int lines = 1;
  int current_line = 0;
  while (*text) {
    char c = *text++;
    if (c == '\n') {
      if (current_line > max_line) {
        max_line = current_line;
      }
      current_line = 0;
      ++lines;
      continue;
    }
    (void)c;
    ++current_line;
  }
  if (current_line > max_line) {
    max_line = current_line;
  }
  if (max_line <= 0 || lines <= 0) {
    return;
  }
  expand_dirty_rect(render, x, y, max_line * 8, lines * 8);
}

static inline void record_text_rect(MidrawContext& ctx,
                                    const char* text,
                                    int x,
                                    int y,
                                    int clip_x1,
                                    int clip_y1,
                                    uint32_t color) {
  if (!text) {
    return;
  }
  size_t len = strlen(text);
  if (len == 0) {
    return;
  }
  if (ctx.text_offset + len + 1 > kTextPoolCapacity) {
    ctx.text_overflow = true;
    return;
  }
  const size_t offset = ctx.text_offset;
  memcpy(ctx.text_pool + offset, text, len);
  ctx.text_pool[offset + len] = '\0';
  ctx.text_offset += len + 1;

  DrawCommand cmd;
  cmd.type = DrawCommandType::Text;
  cmd.a = x;
  cmd.b = y;
  cmd.c = clip_x1;
  cmd.d = clip_y1;
  cmd.e = static_cast<int>(offset);
  cmd.f = static_cast<int>(len);
  cmd.color = color;
  if (!push_command(ctx, cmd)) {
    return;
  }

#if defined(MIDRAW_USE_STB_TRUETYPE)
  if (ctx.atlas.valid) {
    expand_text_dirty_atlas(ctx, text, x, y);
  } else {
    expand_text_dirty_font8x8(ctx.render, text, x, y);
  }
#else
  expand_text_dirty_font8x8(ctx.render, text, x, y);
#endif

  if (clip_x1 > x && clip_y1 > y &&
      rect_valid(ctx.render.dirty_min_x, ctx.render.dirty_min_y,
                 ctx.render.dirty_max_x, ctx.render.dirty_max_y)) {
    int ix0 = 0;
    int iy0 = 0;
    int ix1 = 0;
    int iy1 = 0;
    if (intersect_rect(ctx.render.dirty_min_x, ctx.render.dirty_min_y,
                       ctx.render.dirty_max_x, ctx.render.dirty_max_y,
                       x, y, clip_x1, clip_y1,
                       &ix0, &iy0, &ix1, &iy1)) {
      ctx.render.dirty_min_x = ix0;
      ctx.render.dirty_min_y = iy0;
      ctx.render.dirty_max_x = ix1;
      ctx.render.dirty_max_y = iy1;
    }
  }
}

static inline void record_text(MidrawContext& ctx, const char* text, int x, int y,
                               uint32_t color) {
  record_text_rect(ctx, text, x, y, 0, 0, color);
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
static inline void blend8_pixels_neon(uint32_t* dst,
                                      const uint8_t* alpha,
                                      const ColorComponents& src_comp) {
  uint8x8_t a = vld1_u8(alpha);
  if (src_comp.a != 255) {
    a = mul_div255_u8(a, vdup_n_u8(src_comp.a));
  }
  uint8x8_t inv_a = vsub_u8(vdup_n_u8(255), a);

  uint8x8x4_t dst_px = vld4_u8(reinterpret_cast<const uint8_t*>(dst));
  const uint8x8_t src_b = vdup_n_u8(src_comp.b);
  const uint8x8_t src_g = vdup_n_u8(src_comp.g);
  const uint8x8_t src_r = vdup_n_u8(src_comp.r);

  uint16x8_t out_b = vaddq_u16(vmull_u8(src_b, a), vmull_u8(dst_px.val[0], inv_a));
  uint16x8_t out_g = vaddq_u16(vmull_u8(src_g, a), vmull_u8(dst_px.val[1], inv_a));
  uint16x8_t out_r = vaddq_u16(vmull_u8(src_r, a), vmull_u8(dst_px.val[2], inv_a));
  dst_px.val[0] = div255_u16x8(out_b);
  dst_px.val[1] = div255_u16x8(out_g);
  dst_px.val[2] = div255_u16x8(out_r);

  uint16x8_t dst_a = vmull_u8(dst_px.val[3], inv_a);
  uint8x8_t out_a = vadd_u8(a, div255_u16x8(dst_a));
  dst_px.val[3] = out_a;

  vst4_u8(reinterpret_cast<uint8_t*>(dst), dst_px);
}
#endif

static void blit_glyph_fast(RenderContext& ctx,
                            const FontAtlas& atlas,
                            const GlyphInfo& glyph,
                            int dst_x,
                            int dst_y,
                            uint32_t color) {
  if (!ctx.pixels || !atlas.pixels) {
    return;
  }
  int src_w = glyph.x1 - glyph.x0;
  int src_h = glyph.y1 - glyph.y0;
  if (src_w <= 0 || src_h <= 0) {
    return;
  }

  int src_x = glyph.x0;
  int src_y = glyph.y0;
  int dx = dst_x;
  int dy = dst_y;

  if (dx < 0) {
    src_x -= dx;
    src_w += dx;
    dx = 0;
  }
  if (dy < 0) {
    src_y -= dy;
    src_h += dy;
    dy = 0;
  }
  if (dx + src_w > ctx.width) {
    src_w = ctx.width - dx;
  }
  if (dy + src_h > ctx.height) {
    src_h = ctx.height - dy;
  }
  if (src_w <= 0 || src_h <= 0) {
    return;
  }

  ColorComponents src_comp{
      static_cast<uint8_t>((color >> 24) & 0xFF),
      static_cast<uint8_t>((color >> 16) & 0xFF),
      static_cast<uint8_t>((color >> 8) & 0xFF),
      static_cast<uint8_t>(color & 0xFF),
  };

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  const uint32x4_t vcolor = vdupq_n_u32(color);
#endif

  for (int row = 0; row < src_h; ++row) {
    const uint8_t* src = atlas.pixels + (src_y + row) * atlas.width + src_x;
    uint32_t* dst = ctx.pixels + (dy + row) * ctx.stride + dx;

    if (src_comp.a == 255) {
      int col = 0;
      while (col < src_w) {
        const uint8_t a = src[col];
        if (a == 0) {
          ++col;
          continue;
        }
        if (a == 255) {
          int run = 1;
          while (col + run < src_w && src[col + run] == 255) {
            ++run;
          }
          uint32_t* d = dst + col;
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
          int n = run;
          while (n >= 4) {
            vst1q_u32(d, vcolor);
            d += 4;
            n -= 4;
          }
          while (n-- > 0) {
            *d++ = color;
          }
#else
          for (int i = 0; i < run; ++i) {
            d[i] = color;
          }
#endif
          col += run;
          continue;
        }
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
        if (src_w - col >= 8) {
          blend8_pixels_neon(dst + col, src + col, src_comp);
          col += 8;
          continue;
        }
#endif
        dst[col] = blend_pixel(dst[col], src_comp, a);
        ++col;
      }
    } else {
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
      int col = 0;
      for (; col + 8 <= src_w; col += 8) {
        blend8_pixels_neon(dst + col, src + col, src_comp);
      }
      for (; col < src_w; ++col) {
#else
      for (int col = 0; col < src_w; ++col) {
#endif
        const uint8_t a = src[col];
        if (a == 0) {
          continue;
        }
        dst[col] = blend_pixel(dst[col], src_comp, a);
      }
    }
  }
}

static void blit_glyph_slow(RenderContext& ctx,
                            const FontAtlas& atlas,
                            const GlyphInfo& glyph,
                            int dst_x,
                            int dst_y,
                            const ColorComponents& src_comp) {
  if (!atlas.pixels) {
    return;
  }
  const int src_w = glyph.x1 - glyph.x0;
  const int src_h = glyph.y1 - glyph.y0;
  if (src_w <= 0 || src_h <= 0) {
    return;
  }

  for (int row = 0; row < src_h; ++row) {
    const int y = dst_y + row;
    const uint8_t* src = atlas.pixels + (glyph.y0 + row) * atlas.width + glyph.x0;
    for (int col = 0; col < src_w; ++col) {
      const uint8_t a = src[col];
      if (a == 0) {
        continue;
      }
      const int x = dst_x + col;
      blend_pixel_mapped(ctx, x, y, src_comp, a);
    }
  }
}

static void blit_glyph_fast_clipped(RenderContext& ctx,
                                    const FontAtlas& atlas,
                                    const GlyphInfo& glyph,
                                    int dst_x,
                                    int dst_y,
                                    uint32_t color,
                                    int clip_x0,
                                    int clip_y0,
                                    int clip_x1,
                                    int clip_y1) {
  if (!ctx.pixels || !atlas.pixels) {
    return;
  }
  if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
    blit_glyph_fast(ctx, atlas, glyph, dst_x, dst_y, color);
    return;
  }
  int src_w = glyph.x1 - glyph.x0;
  int src_h = glyph.y1 - glyph.y0;
  if (src_w <= 0 || src_h <= 0) {
    return;
  }

  const int gx0 = dst_x;
  const int gy0 = dst_y;
  const int gx1 = dst_x + src_w;
  const int gy1 = dst_y + src_h;

  int ix0 = 0;
  int iy0 = 0;
  int ix1 = 0;
  int iy1 = 0;
  if (!intersect_rect(gx0, gy0, gx1, gy1, clip_x0, clip_y0, clip_x1, clip_y1,
                      &ix0, &iy0, &ix1, &iy1)) {
    return;
  }
  if (!intersect_rect(ix0, iy0, ix1, iy1, 0, 0, ctx.width, ctx.height,
                      &ix0, &iy0, &ix1, &iy1)) {
    return;
  }

  int src_x = glyph.x0 + (ix0 - dst_x);
  int src_y = glyph.y0 + (iy0 - dst_y);
  int dx = ix0;
  int dy = iy0;
  src_w = ix1 - ix0;
  src_h = iy1 - iy0;
  if (src_w <= 0 || src_h <= 0) {
    return;
  }

  ColorComponents src_comp{
      static_cast<uint8_t>((color >> 24) & 0xFF),
      static_cast<uint8_t>((color >> 16) & 0xFF),
      static_cast<uint8_t>((color >> 8) & 0xFF),
      static_cast<uint8_t>(color & 0xFF),
  };

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  const uint32x4_t vcolor = vdupq_n_u32(color);
#endif

  for (int row = 0; row < src_h; ++row) {
    const uint8_t* src = atlas.pixels + (src_y + row) * atlas.width + src_x;
    uint32_t* dst = ctx.pixels + (dy + row) * ctx.stride + dx;

    if (src_comp.a == 255) {
      int col = 0;
      while (col < src_w) {
        const uint8_t a = src[col];
        if (a == 0) {
          ++col;
          continue;
        }
        if (a == 255) {
          int run = 1;
          while (col + run < src_w && src[col + run] == 255) {
            ++run;
          }
          uint32_t* d = dst + col;
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
          int n = run;
          while (n >= 4) {
            vst1q_u32(d, vcolor);
            d += 4;
            n -= 4;
          }
          while (n-- > 0) {
            *d++ = color;
          }
#else
          for (int i = 0; i < run; ++i) {
            d[i] = color;
          }
#endif
          col += run;
          continue;
        }
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
        if (src_w - col >= 8) {
          blend8_pixels_neon(dst + col, src + col, src_comp);
          col += 8;
          continue;
        }
#endif
        dst[col] = blend_pixel(dst[col], src_comp, a);
        ++col;
      }
    } else {
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
      int col = 0;
      for (; col + 8 <= src_w; col += 8) {
        blend8_pixels_neon(dst + col, src + col, src_comp);
      }
      for (; col < src_w; ++col) {
#else
      for (int col = 0; col < src_w; ++col) {
#endif
        const uint8_t a = src[col];
        if (a == 0) {
          continue;
        }
        dst[col] = blend_pixel(dst[col], src_comp, a);
      }
    }
  }
}

static void blit_glyph_slow_clipped(RenderContext& ctx,
                                    const FontAtlas& atlas,
                                    const GlyphInfo& glyph,
                                    int dst_x,
                                    int dst_y,
                                    const ColorComponents& src_comp,
                                    int clip_x0,
                                    int clip_y0,
                                    int clip_x1,
                                    int clip_y1) {
  if (!atlas.pixels) {
    return;
  }
  const int src_w = glyph.x1 - glyph.x0;
  const int src_h = glyph.y1 - glyph.y0;
  if (src_w <= 0 || src_h <= 0) {
    return;
  }

  for (int row = 0; row < src_h; ++row) {
    const int y = dst_y + row;
    if (y < clip_y0 || y >= clip_y1) {
      continue;
    }
    const uint8_t* src = atlas.pixels + (glyph.y0 + row) * atlas.width + glyph.x0;
    for (int col = 0; col < src_w; ++col) {
      const int x = dst_x + col;
      if (x < clip_x0 || x >= clip_x1) {
        continue;
      }
      const uint8_t a = src[col];
      if (a == 0) {
        continue;
      }
      blend_pixel_mapped(ctx, x, y, src_comp, a);
    }
  }
}

static void draw_text_atlas(MidrawContext& ctx,
                            const char* text,
                            int x,
                            int y,
                            uint32_t color) {
  if (!text || !ctx.atlas.valid) {
    return;
  }

  ColorComponents src_comp{
      static_cast<uint8_t>((color >> 24) & 0xFF),
      static_cast<uint8_t>((color >> 16) & 0xFF),
      static_cast<uint8_t>((color >> 8) & 0xFF),
      static_cast<uint8_t>(color & 0xFF),
  };

  int cursor_x = x;
  int cursor_y = y;
  const int first_char = ctx.atlas.first_char;
  const int last_char = ctx.atlas.first_char + ctx.atlas.num_chars - 1;

  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = x;
      cursor_y += (ctx.atlas.line_advance > 0 ? ctx.atlas.line_advance : 16);
      continue;
    }
    if (c < first_char || c > last_char) {
      cursor_x += 8;
      continue;
    }
    const GlyphInfo& glyph = ctx.atlas.glyphs[c - first_char];
    const int dst_x = cursor_x + glyph.xoff;
    const int dst_y = cursor_y + ctx.atlas.baseline + glyph.yoff;
    const int glyph_w = glyph.x1 - glyph.x0;
    const int glyph_h = glyph.y1 - glyph.y0;
    if (glyph_w > 0 && glyph_h > 0) {
      expand_dirty_rect(ctx.render, dst_x, dst_y, glyph_w, glyph_h);
    }

    if (ctx.render.rotation == 0 && ctx.render.pixels) {
      blit_glyph_fast(ctx.render, ctx.atlas, glyph, dst_x, dst_y, color);
    } else {
      blit_glyph_slow(ctx.render, ctx.atlas, glyph, dst_x, dst_y, src_comp);
    }
    cursor_x += glyph.xadvance;
  }
}

static void draw_text_atlas_clipped(MidrawContext& ctx,
                                    const char* text,
                                    int x,
                                    int y,
                                    int clip_x0,
                                    int clip_y0,
                                    int clip_x1,
                                    int clip_y1,
                                    uint32_t color) {
  if (!text || !ctx.atlas.valid) {
    return;
  }
  if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
    draw_text_atlas(ctx, text, x, y, color);
    return;
  }

  ColorComponents src_comp{
      static_cast<uint8_t>((color >> 24) & 0xFF),
      static_cast<uint8_t>((color >> 16) & 0xFF),
      static_cast<uint8_t>((color >> 8) & 0xFF),
      static_cast<uint8_t>(color & 0xFF),
  };

  int cursor_x = x;
  int cursor_y = y;
  const int first_char = ctx.atlas.first_char;
  const int last_char = ctx.atlas.first_char + ctx.atlas.num_chars - 1;

  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = x;
      cursor_y += (ctx.atlas.line_advance > 0 ? ctx.atlas.line_advance : 16);
      continue;
    }
    if (c < first_char || c > last_char) {
      cursor_x += 8;
      continue;
    }
    const GlyphInfo& glyph = ctx.atlas.glyphs[c - first_char];
    const int dst_x = cursor_x + glyph.xoff;
    const int dst_y = cursor_y + ctx.atlas.baseline + glyph.yoff;
    const int glyph_w = glyph.x1 - glyph.x0;
    const int glyph_h = glyph.y1 - glyph.y0;
    if (glyph_w > 0 && glyph_h > 0) {
      int ix0 = 0;
      int iy0 = 0;
      int ix1 = 0;
      int iy1 = 0;
      if (intersect_rect(dst_x, dst_y, dst_x + glyph_w, dst_y + glyph_h,
                         clip_x0, clip_y0, clip_x1, clip_y1,
                         &ix0, &iy0, &ix1, &iy1)) {
        expand_dirty_rect(ctx.render, ix0, iy0, ix1 - ix0, iy1 - iy0);
      }
    }

    if (ctx.render.rotation == 0 && ctx.render.pixels) {
      blit_glyph_fast_clipped(ctx.render, ctx.atlas, glyph, dst_x, dst_y, color,
                              clip_x0, clip_y0, clip_x1, clip_y1);
    } else {
      blit_glyph_slow_clipped(ctx.render, ctx.atlas, glyph, dst_x, dst_y, src_comp,
                              clip_x0, clip_y0, clip_x1, clip_y1);
    }
    cursor_x += glyph.xadvance;
  }
}

static void draw_image(RenderContext& ctx,
                       const uint32_t* src_pixels,
                       int src_w,
                       int src_h,
                       int dst_x,
                       int dst_y) {
  if (!ctx.pixels || !src_pixels || src_w <= 0 || src_h <= 0) {
    return;
  }
  expand_dirty_rect(ctx, dst_x, dst_y, src_w, src_h);

  if (ctx.rotation != 0) {
    for (int row = 0; row < src_h; ++row) {
      const int y = dst_y + row;
      for (int col = 0; col < src_w; ++col) {
        const int x = dst_x + col;
        const uint32_t src = src_pixels[row * src_w + col];
        const uint8_t a = static_cast<uint8_t>((src >> 24) & 0xFF);
        if (a == 0) {
          continue;
        }
        ColorComponents comp{
            a,
            static_cast<uint8_t>((src >> 16) & 0xFF),
            static_cast<uint8_t>((src >> 8) & 0xFF),
            static_cast<uint8_t>(src & 0xFF),
        };
        if (a == 255) {
          plot_pixel(ctx, x, y, src);
        } else {
          blend_pixel_mapped(ctx, x, y, comp, 255);
        }
      }
    }
    return;
  }

  int sx = 0;
  int sy = 0;
  int dx = dst_x;
  int dy = dst_y;
  int w = src_w;
  int h = src_h;

  if (dx < 0) {
    sx -= dx;
    w += dx;
    dx = 0;
  }
  if (dy < 0) {
    sy -= dy;
    h += dy;
    dy = 0;
  }
  if (dx + w > ctx.width) {
    w = ctx.width - dx;
  }
  if (dy + h > ctx.height) {
    h = ctx.height - dy;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  for (int row = 0; row < h; ++row) {
    const uint32_t* src_row = src_pixels + (sy + row) * src_w + sx;
    uint32_t* dst_row = ctx.pixels + (dy + row) * ctx.stride + dx;
    for (int col = 0; col < w; ++col) {
      const uint32_t src = src_row[col];
      const uint8_t a = static_cast<uint8_t>((src >> 24) & 0xFF);
      if (a == 0) {
        continue;
      }
      if (a == 255) {
        dst_row[col] = src;
        continue;
      }
      ColorComponents comp{
          a,
          static_cast<uint8_t>((src >> 16) & 0xFF),
          static_cast<uint8_t>((src >> 8) & 0xFF),
          static_cast<uint8_t>(src & 0xFF),
      };
      dst_row[col] = blend_pixel(dst_row[col], comp, 255);
    }
  }
}

static void wait_for_fence(int fence_fd) {
  if (fence_fd < 0) {
    return;
  }
  pollfd pfd{};
  pfd.fd = fence_fd;
  pfd.events = POLLIN;
  for (;;) {
    int res = poll(&pfd, 1, -1);
    if (res > 0 || (res < 0 && errno != EINTR)) {
      break;
    }
  }
  close(fence_fd);
}

static void release_staging_buffer(RenderContext& render);

static void release_ahb_buffer(MidrawContext& ctx) {
  if (ctx.render.ahb_buffer && ctx.symbols.AHardwareBuffer_release) {
    ctx.symbols.AHardwareBuffer_release(ctx.render.ahb_buffer);
  }
  ctx.render.ahb_buffer = nullptr;
  ctx.render.ahb_desc = {};
  release_staging_buffer(ctx.render);
}

static void release_staging_buffer(RenderContext& render) {
  if (render.staging_pixels) {
    free(render.staging_pixels);
  }
  render.staging_pixels = nullptr;
  render.staging_width = 0;
  render.staging_height = 0;
  render.staging_stride = 0;
  render.use_staging = false;
}

static bool ensure_staging_buffer(MidrawContext& ctx, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (ctx.render.staging_pixels && ctx.render.staging_width == width &&
      ctx.render.staging_height == height) {
    ctx.render.use_staging = true;
    return true;
  }
  release_staging_buffer(ctx.render);
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (count == 0 || count > (SIZE_MAX / sizeof(uint32_t))) {
    return false;
  }
  const size_t bytes = count * sizeof(uint32_t);
  void* buffer = malloc(bytes);
  if (!buffer) {
    return false;
  }
  memset(buffer, 0, bytes);
  ctx.render.staging_pixels = static_cast<uint32_t*>(buffer);
  ctx.render.staging_width = width;
  ctx.render.staging_height = height;
  ctx.render.staging_stride = width;
  ctx.render.use_staging = true;
  return true;
}

static void apply_surface_tx(MidrawContext& ctx, ASurfaceControl* surface, int width, int height) {
  if (!surface || !ctx.symbols.ASurfaceTransaction_create || !ctx.symbols.ASurfaceTransaction_apply) {
    return;
  }
  ASurfaceTransaction* tx = ctx.symbols.ASurfaceTransaction_create();
  if (!tx) {
    return;
  }
  if (ctx.symbols.ASurfaceTransaction_setVisibility) {
    ctx.symbols.ASurfaceTransaction_setVisibility(tx, surface, 1);
  }
  if (ctx.symbols.ASurfaceTransaction_setLayer) {
    ctx.symbols.ASurfaceTransaction_setLayer(tx, surface, INT_MAX);
  } else if (ctx.symbols.ASurfaceTransaction_setZOrder) {
    ctx.symbols.ASurfaceTransaction_setZOrder(tx, surface, INT_MAX);
  }
  if (ctx.symbols.ASurfaceTransaction_setBufferTransparency) {
    ctx.symbols.ASurfaceTransaction_setBufferTransparency(tx, surface,
                                                          kBufferTransparencyTranslucent);
  }
  if (ctx.symbols.ASurfaceTransaction_setBufferSize && width > 0 && height > 0) {
    ctx.symbols.ASurfaceTransaction_setBufferSize(tx, surface, width, height);
  } else if (ctx.symbols.ASurfaceTransaction_setGeometry && width > 0 && height > 0) {
    const ARect src{0, 0, width, height};
    const ARect dst{0, 0, width, height};
    ctx.symbols.ASurfaceTransaction_setGeometry(tx, surface, src, dst, 0);
  }
  ctx.symbols.ASurfaceTransaction_apply(tx);
  if (ctx.symbols.ASurfaceTransaction_release) {
    ctx.symbols.ASurfaceTransaction_release(tx);
  }
}

static bool ensure_ahb_surface(MidrawContext& ctx, int width, int height) {
  if (ctx.render.ahb_surface) {
    return true;
  }
  if (ctx.render.surface) {
    ctx.render.ahb_surface = ctx.render.surface;
    apply_surface_tx(ctx, ctx.render.ahb_surface, width, height);
    return true;
  }
  if (ctx.render.window && ctx.symbols.ASurfaceControl_createFromWindow) {
    ctx.render.ahb_surface =
        ctx.symbols.ASurfaceControl_createFromWindow(ctx.render.window, "midraw_ahb");
    if (ctx.render.ahb_surface) {
      apply_surface_tx(ctx, ctx.render.ahb_surface, width, height);
      return true;
    }
  }
  if (!ctx.symbols.ASurfaceControl_create) {
    return false;
  }
  ctx.render.ahb_surface = ctx.symbols.ASurfaceControl_create("midraw_ahb", nullptr);
  if (!ctx.render.ahb_surface) {
    fprintf(stderr, "midraw: ASurfaceControl_create failed for AHB surface\n");
    return false;
  }
  apply_surface_tx(ctx, ctx.render.ahb_surface, width, height);
  return true;
}

static bool setup_ahb_buffer(MidrawContext& ctx, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (!ctx.symbols.AHardwareBuffer_allocate || !ctx.symbols.AHardwareBuffer_describe ||
      !ctx.symbols.AHardwareBuffer_lock || !ctx.symbols.AHardwareBuffer_unlock ||
      !ctx.symbols.AHardwareBuffer_release) {
    return false;
  }
  if (!ensure_ahb_surface(ctx, width, height)) {
    return false;
  }
  release_ahb_buffer(ctx);
  memset(&ctx.render.ahb_desc, 0, sizeof(ctx.render.ahb_desc));
  ctx.render.ahb_desc.width = static_cast<uint32_t>(width);
  ctx.render.ahb_desc.height = static_cast<uint32_t>(height);
  ctx.render.ahb_desc.layers = 1;
  ctx.render.ahb_desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  ctx.render.ahb_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                              AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                              AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
  if (ctx.symbols.AHardwareBuffer_allocate(&ctx.render.ahb_desc, &ctx.render.ahb_buffer) != 0 ||
      !ctx.render.ahb_buffer) {
    fprintf(stderr, "AHardwareBuffer_allocate failed\n");
    ctx.render.ahb_buffer = nullptr;
    return false;
  }
  ctx.symbols.AHardwareBuffer_describe(ctx.render.ahb_buffer, &ctx.render.ahb_desc);
  ctx.render.use_ahb = true;
  ctx.render.use_surface_direct = false;
  return true;
}

static bool can_use_ahb(const MidrawContext& ctx) {
  if (!ctx.render.surface && !ctx.render.ahb_surface &&
      !(ctx.render.window && ctx.symbols.ASurfaceControl_createFromWindow) &&
      !ctx.symbols.ASurfaceControl_create) {
    return false;
  }
  return ctx.symbols.ASurfaceTransaction_create && ctx.symbols.ASurfaceTransaction_setBuffer &&
         ctx.symbols.ASurfaceTransaction_apply && ctx.symbols.ASurfaceTransaction_release &&
         ctx.symbols.AHardwareBuffer_allocate && ctx.symbols.AHardwareBuffer_lock &&
         ctx.symbols.AHardwareBuffer_unlock && ctx.symbols.AHardwareBuffer_release &&
         ctx.symbols.AHardwareBuffer_describe;
}

static bool can_use_surface_direct(const MidrawContext& ctx) {
  return ctx.render.surface_native && ctx.symbols.Surface_dequeueBuffer &&
         (ctx.symbols.Surface_queueBuffer4 || ctx.symbols.Surface_queueBuffer3) &&
         ctx.symbols.GraphicBuffer_from && ctx.symbols.GraphicBuffer_lock &&
         ctx.symbols.GraphicBuffer_unlock;
}

static void configure_fastpath(MidrawContext& ctx) {
  ctx.render.use_ahb = false;
  ctx.render.use_surface_direct = false;
  ctx.render.direct_buffer = nullptr;
  ctx.render.direct_graphic = nullptr;
  ctx.render.direct_prime_attempted = false;
  ctx.render.ahb_surface = nullptr;
  ctx.render.use_staging = false;
  ctx.ahb_only = env_int("MIDRAW_AHB_ONLY", 1) != 0;

  if (env_int("MIDRAW_FASTPATH", 1) == 0) {
    return;
  }

  const bool force_direct = env_truthy("MIDRAW_FORCE_DIRECT");
  const bool allow_direct =
      force_direct || env_truthy("MIDRAW_USE_DIRECT") || env_truthy("MIDRAW_ALLOW_DIRECT");
  const bool force_ahb = env_truthy("MIDRAW_USE_AHB");
  const bool require_ahb = ctx.ahb_only || force_ahb;
  const int sdk = read_sdk_version();

  int target_w = ctx.render.width > 0 ? ctx.render.width : ctx.requested_width;
  int target_h = ctx.render.height > 0 ? ctx.render.height : ctx.requested_height;
  if (target_w <= 0) {
    target_w = 1080;
  }
  if (target_h <= 0) {
    target_h = 1920;
  }

  if (force_direct) {
    if (can_use_surface_direct(ctx) && sdk >= 34) {
      ctx.render.use_surface_direct = true;
      fprintf(stderr, "midraw: using Surface direct buffer path (forced)\n");
      return;
    }
    fprintf(stderr,
            "midraw: Surface direct unavailable: dequeue=%p queue=%p from=%p lock=%p unlock=%p\n",
            reinterpret_cast<void*>(ctx.symbols.Surface_dequeueBuffer),
            reinterpret_cast<void*>(ctx.symbols.Surface_queueBuffer4
                                        ? reinterpret_cast<void*>(ctx.symbols.Surface_queueBuffer4)
                                        : reinterpret_cast<void*>(ctx.symbols.Surface_queueBuffer3)),
            reinterpret_cast<void*>(ctx.symbols.GraphicBuffer_from),
            reinterpret_cast<void*>(ctx.symbols.GraphicBuffer_lock),
            reinterpret_cast<void*>(ctx.symbols.GraphicBuffer_unlock));
  }

  if (require_ahb) {
    if (can_use_ahb(ctx) && setup_ahb_buffer(ctx, target_w, target_h)) {
      fprintf(stderr, "midraw: using AHB path (%s)\n", ctx.ahb_only ? "AHB-only" : "forced");
      return;
    }
    fprintf(stderr, "midraw: AHB path unavailable\n");
    if (ctx.ahb_only) {
      return;
    }
  }

  if (sdk >= 34) {
    if (can_use_ahb(ctx) && setup_ahb_buffer(ctx, target_w, target_h)) {
      fprintf(stderr, "midraw: using AHB path (API %d)\n", sdk);
      return;
    }
    if (allow_direct && can_use_surface_direct(ctx)) {
      ctx.render.use_surface_direct = true;
      fprintf(stderr, "midraw: using Surface direct buffer path (API %d)\n", sdk);
      return;
    }
  }
}

static bool lock_ahb_buffer(MidrawContext& ctx) {
  if (!ctx.render.ahb_buffer) {
    const int width = ctx.render.width > 0 ? ctx.render.width : ctx.requested_width;
    const int height = ctx.render.height > 0 ? ctx.render.height : ctx.requested_height;
    if (!setup_ahb_buffer(ctx, width, height)) {
      return false;
    }
  }
  const int width = static_cast<int>(ctx.render.ahb_desc.width);
  const int height = static_cast<int>(ctx.render.ahb_desc.height);
  if (ahb_staging_enabled()) {
    if (!ensure_staging_buffer(ctx, width, height)) {
      return false;
    }
    ctx.render.width = width;
    ctx.render.height = height;
    ctx.render.stride = ctx.render.staging_stride;
    ctx.render.pixels = ctx.render.staging_pixels;
    return ctx.render.pixels != nullptr;
  }
  ctx.render.use_staging = false;
  void* out = nullptr;
  ARect rect{0, 0, static_cast<int32_t>(ctx.render.ahb_desc.width),
             static_cast<int32_t>(ctx.render.ahb_desc.height)};
  const uint64_t usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                         AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
  if (ctx.symbols.AHardwareBuffer_lock(ctx.render.ahb_buffer, usage, -1, &rect, &out) != 0) {
    return false;
  }
  ctx.render.width = width;
  ctx.render.height = height;
  ctx.render.stride = static_cast<int>(ctx.render.ahb_desc.stride);
  if (ctx.render.stride == 0) {
    ctx.render.stride = ctx.render.width;
  }
  ctx.render.pixels = reinterpret_cast<uint32_t*>(out);
  return ctx.render.pixels != nullptr;
}

static bool lock_surface_direct_buffer(MidrawContext& ctx) {
  if (!can_use_surface_direct(ctx)) {
    return false;
  }
  auto close_fence = [](int fd) {
    if (fd >= 0) {
      close(fd);
    }
  };

  for (int attempt = 0; attempt < 2; ++attempt) {
    ctx.render.direct_buffer = nullptr;
    ctx.render.direct_graphic = nullptr;
    ANativeWindowBuffer* buffer = nullptr;
    int fence_fd = -1;
    int res = ctx.symbols.Surface_dequeueBuffer(ctx.render.surface_native, &buffer, &fence_fd);
    if (res != 0 || !buffer) {
      close_fence(fence_fd);
      if (attempt == 0 && !ctx.render.direct_prime_attempted && ctx.render.window &&
          ctx.symbols.ANativeWindow_lock && ctx.symbols.ANativeWindow_unlockAndPost) {
        ctx.render.direct_prime_attempted = true;
        ANativeWindow_Buffer probe{};
        if (ctx.symbols.ANativeWindow_lock(ctx.render.window, &probe, nullptr) == 0) {
          ctx.symbols.ANativeWindow_unlockAndPost(ctx.render.window);
        }
        continue;
      }
      return false;
    }
    wait_for_fence(fence_fd);
    void* graphic = ctx.symbols.GraphicBuffer_from(buffer);
    if (!graphic) {
      if (ctx.symbols.Surface_cancelBuffer) {
        ctx.symbols.Surface_cancelBuffer(ctx.render.surface_native, buffer, -1);
      }
      return false;
    }
    void* out = nullptr;
    int out_bpp = 0;
    int out_stride = 0;
    if (ctx.symbols.GraphicBuffer_lock(graphic, GRALLOC_USAGE_SW_WRITE_OFTEN,
                                       &out, &out_bpp, &out_stride) != 0 ||
        !out) {
      if (ctx.symbols.Surface_cancelBuffer) {
        ctx.symbols.Surface_cancelBuffer(ctx.render.surface_native, buffer, -1);
      }
      return false;
    }
    ctx.render.direct_buffer = buffer;
    ctx.render.direct_graphic = graphic;
    ctx.render.width = buffer->width;
    ctx.render.height = buffer->height;
    int stride_pixels = buffer->stride;
    if (out_stride > 0) {
      stride_pixels = out_stride / 4;
    }
    ctx.render.stride = stride_pixels > 0 ? stride_pixels : buffer->stride;
    ctx.render.pixels = reinterpret_cast<uint32_t*>(out);
    return ctx.render.pixels != nullptr;
  }
  return false;
}

static bool lock_buffer(MidrawContext& ctx) {
  if (ctx.ahb_only && !ctx.render.use_ahb) {
    if (lock_ahb_buffer(ctx)) {
      return true;
    }
    static bool warned = false;
    if (!warned) {
      fprintf(stderr, "midraw: AHB-only enabled, AHB lock failed\n");
      warned = true;
    }
    return false;
  }
  if (ctx.render.use_ahb) {
    if (lock_ahb_buffer(ctx)) {
      return true;
    }
    release_ahb_buffer(ctx);
    ctx.render.use_ahb = false;
    if (ctx.ahb_only) {
      static bool warned = false;
      if (!warned) {
        fprintf(stderr, "midraw: AHB-only enabled, AHB lock failed\n");
        warned = true;
      }
      return false;
    }
  }

  if (ctx.render.use_surface_direct) {
    if (lock_surface_direct_buffer(ctx)) {
      return true;
    }
    static bool warned = false;
    if (!warned) {
      fprintf(stderr, "midraw: surface direct failed, fallback to safe path\n");
      warned = true;
    }
    ctx.render.use_surface_direct = false;
    ctx.render.direct_buffer = nullptr;
    ctx.render.direct_graphic = nullptr;
    const int width = ctx.render.width > 0 ? ctx.render.width : ctx.requested_width;
    const int height = ctx.render.height > 0 ? ctx.render.height : ctx.requested_height;
    if (can_use_ahb(ctx) && setup_ahb_buffer(ctx, width, height)) {
      if (lock_ahb_buffer(ctx)) {
        return true;
      }
      release_ahb_buffer(ctx);
      ctx.render.use_ahb = false;
    }
  }

  if (!ctx.render.window) {
    return false;
  }
  ARect dirty_rect{};
  ARect* dirty_ptr = nullptr;
  if (rect_valid(ctx.render.present_min_x, ctx.render.present_min_y, ctx.render.present_max_x,
                 ctx.render.present_max_y)) {
    dirty_rect.left = ctx.render.present_min_x;
    dirty_rect.top = ctx.render.present_min_y;
    dirty_rect.right = ctx.render.present_max_x;
    dirty_rect.bottom = ctx.render.present_max_y;
    dirty_ptr = &dirty_rect;
  }
  if (ctx.symbols.ANativeWindow_lock(ctx.render.window, &ctx.render.buffer, dirty_ptr) != 0) {
    fprintf(stderr, "ANativeWindow_lock failed\n");
    return false;
  }
  ctx.render.width = ctx.render.buffer.width;
  ctx.render.height = ctx.render.buffer.height;
  ctx.render.stride = ctx.render.buffer.stride;
  ctx.render.pixels = reinterpret_cast<uint32_t*>(ctx.render.buffer.bits);
  return ctx.render.pixels != nullptr;
}

static void unlock_post(MidrawContext& ctx) {
  const bool timing_debug = debug_timing_enabled();
  if (ctx.render.use_ahb) {
    uint64_t unlock_ns = 0;
    uint64_t tx_create_ns = 0;
    uint64_t tx_set_ns = 0;
    uint64_t tx_apply_ns = 0;
    uint64_t tx_release_ns = 0;
    int fence = -1;
    if (ctx.render.use_staging && ctx.render.staging_pixels &&
        ctx.symbols.AHardwareBuffer_lock && ctx.symbols.AHardwareBuffer_unlock &&
        ctx.render.ahb_buffer) {
      void* out = nullptr;
      const int width = static_cast<int>(ctx.render.ahb_desc.width);
      const int height = static_cast<int>(ctx.render.ahb_desc.height);
      ARect rect{0, 0, width, height};
      const uint64_t usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
      if (ctx.symbols.AHardwareBuffer_lock(ctx.render.ahb_buffer, usage, -1, &rect, &out) == 0 &&
          out) {
        int copy_min_x = 0;
        int copy_min_y = 0;
        int copy_max_x = width;
        int copy_max_y = height;
        if (rect_valid(ctx.render.present_min_x, ctx.render.present_min_y,
                       ctx.render.present_max_x, ctx.render.present_max_y)) {
          copy_min_x = ctx.render.present_min_x;
          copy_min_y = ctx.render.present_min_y;
          copy_max_x = ctx.render.present_max_x;
          copy_max_y = ctx.render.present_max_y;
          if (copy_min_x < 0) {
            copy_min_x = 0;
          }
          if (copy_min_y < 0) {
            copy_min_y = 0;
          }
          if (copy_max_x > width) {
            copy_max_x = width;
          }
          if (copy_max_y > height) {
            copy_max_y = height;
          }
          if (copy_max_x <= copy_min_x || copy_max_y <= copy_min_y) {
            copy_min_x = 0;
            copy_min_y = 0;
            copy_max_x = width;
            copy_max_y = height;
          }
        }
        const int copy_w = copy_max_x - copy_min_x;
        const int copy_h = copy_max_y - copy_min_y;
        const int dst_stride = static_cast<int>(ctx.render.ahb_desc.stride);
        const int src_stride = ctx.render.staging_stride;
        uint32_t* dst_base = static_cast<uint32_t*>(out);
        const uint32_t* src_base = ctx.render.staging_pixels;
        for (int row = 0; row < copy_h; ++row) {
          const uint32_t* src_row = src_base + (copy_min_y + row) * src_stride + copy_min_x;
          uint32_t* dst_row = dst_base + (copy_min_y + row) * dst_stride + copy_min_x;
          memcpy(dst_row, src_row, static_cast<size_t>(copy_w) * sizeof(uint32_t));
        }
        uint64_t t0 = 0;
        if (timing_debug) {
          t0 = now_ns();
        }
        ctx.symbols.AHardwareBuffer_unlock(ctx.render.ahb_buffer, &fence);
        if (timing_debug) {
          unlock_ns = now_ns() - t0;
        }
        if (fence >= 0) {
          close(fence);
        }
      }
    } else if (ctx.symbols.AHardwareBuffer_unlock && ctx.render.ahb_buffer) {
      uint64_t t0 = 0;
      if (timing_debug) {
        t0 = now_ns();
      }
      ctx.symbols.AHardwareBuffer_unlock(ctx.render.ahb_buffer, &fence);
      if (timing_debug) {
        unlock_ns = now_ns() - t0;
      }
      if (fence >= 0) {
        close(fence);
      }
    }
    ASurfaceControl* target_surface = ctx.render.ahb_surface ? ctx.render.ahb_surface
                                                             : ctx.render.surface;
    if (target_surface && ctx.symbols.ASurfaceTransaction_create &&
        ctx.symbols.ASurfaceTransaction_apply && ctx.symbols.ASurfaceTransaction_setBuffer) {
      uint64_t t0 = 0;
      if (timing_debug) {
        t0 = now_ns();
      }
      ASurfaceTransaction* tx = ctx.symbols.ASurfaceTransaction_create();
      if (timing_debug) {
        tx_create_ns = now_ns() - t0;
      }
      if (tx) {
        if (timing_debug) {
          t0 = now_ns();
        }
        ctx.symbols.ASurfaceTransaction_setBuffer(tx, target_surface, ctx.render.ahb_buffer, -1);
        if (timing_debug) {
          tx_set_ns = now_ns() - t0;
          t0 = now_ns();
        }
        ctx.symbols.ASurfaceTransaction_apply(tx);
        if (timing_debug) {
          tx_apply_ns = now_ns() - t0;
        }
        if (ctx.symbols.ASurfaceTransaction_release) {
          if (timing_debug) {
            t0 = now_ns();
          }
          ctx.symbols.ASurfaceTransaction_release(tx);
          if (timing_debug) {
            tx_release_ns = now_ns() - t0;
          }
        }
      }
    }
    if (timing_debug) {
      static uint64_t last_log_ns = 0;
      const uint64_t now = now_ns();
      const bool slow = unlock_ns > 5000000ull || tx_create_ns > 5000000ull ||
                        tx_set_ns > 5000000ull || tx_apply_ns > 5000000ull ||
                        tx_release_ns > 5000000ull;
      if (slow || (now - last_log_ns) > 1000000000ull) {
        fprintf(stderr,
                "midraw timing: unlock=%.3f ms tx_create=%.3f ms tx_set=%.3f ms "
                "tx_apply=%.3f ms tx_release=%.3f ms fence=%d\n",
                unlock_ns / 1000000.0,
                tx_create_ns / 1000000.0,
                tx_set_ns / 1000000.0,
                tx_apply_ns / 1000000.0,
                tx_release_ns / 1000000.0,
                fence);
        last_log_ns = now;
      }
    }
    ctx.render.pixels = nullptr;
    return;
  }

  if (ctx.render.use_surface_direct) {
    if (ctx.render.direct_graphic && ctx.symbols.GraphicBuffer_unlock) {
      ctx.symbols.GraphicBuffer_unlock(ctx.render.direct_graphic);
    }
    if (ctx.render.surface_native && ctx.render.direct_buffer &&
        (ctx.symbols.Surface_queueBuffer4 || ctx.symbols.Surface_queueBuffer3)) {
      int res = -1;
      if (ctx.symbols.Surface_queueBuffer4) {
        res = ctx.symbols.Surface_queueBuffer4(ctx.render.surface_native,
                                               ctx.render.direct_buffer,
                                               -1,
                                               nullptr);
      } else if (ctx.symbols.Surface_queueBuffer3) {
        res = ctx.symbols.Surface_queueBuffer3(ctx.render.surface_native,
                                               ctx.render.direct_buffer,
                                               -1);
      }
      if (res != 0 && ctx.symbols.Surface_cancelBuffer) {
        ctx.symbols.Surface_cancelBuffer(ctx.render.surface_native, ctx.render.direct_buffer, -1);
      }
    } else if (ctx.render.surface_native && ctx.render.direct_buffer &&
               ctx.symbols.Surface_cancelBuffer) {
      ctx.symbols.Surface_cancelBuffer(ctx.render.surface_native, ctx.render.direct_buffer, -1);
    }
    ctx.render.direct_buffer = nullptr;
    ctx.render.direct_graphic = nullptr;
    ctx.render.pixels = nullptr;
    return;
  }

  if (ctx.render.window && ctx.symbols.ANativeWindow_unlockAndPost) {
    ctx.symbols.ANativeWindow_unlockAndPost(ctx.render.window);
  }
  ctx.render.pixels = nullptr;
}

static void clear_rect_physical(RenderContext& ctx, int min_x, int min_y, int max_x, int max_y) {
  if (!rect_valid(min_x, min_y, max_x, max_y)) {
    return;
  }
  fill_rect_physical(ctx, min_x, min_y, max_x - min_x, max_y - min_y, 0x00000000);
}

static void clear_previous_dirty(RenderContext& ctx) {
  if (!rect_valid(ctx.prev_dirty_min_x, ctx.prev_dirty_min_y, ctx.prev_dirty_max_x,
                  ctx.prev_dirty_max_y)) {
    return;
  }
  if (!rect_valid(ctx.dirty_min_x, ctx.dirty_min_y, ctx.dirty_max_x, ctx.dirty_max_y)) {
    clear_rect_physical(ctx, ctx.prev_dirty_min_x, ctx.prev_dirty_min_y, ctx.prev_dirty_max_x,
                        ctx.prev_dirty_max_y);
    return;
  }

  const int ix0 = ctx.prev_dirty_min_x > ctx.dirty_min_x ? ctx.prev_dirty_min_x
                                                         : ctx.dirty_min_x;
  const int iy0 = ctx.prev_dirty_min_y > ctx.dirty_min_y ? ctx.prev_dirty_min_y
                                                         : ctx.dirty_min_y;
  const int ix1 = ctx.prev_dirty_max_x < ctx.dirty_max_x ? ctx.prev_dirty_max_x
                                                         : ctx.dirty_max_x;
  const int iy1 = ctx.prev_dirty_max_y < ctx.dirty_max_y ? ctx.prev_dirty_max_y
                                                         : ctx.dirty_max_y;

  if (ix0 >= ix1 || iy0 >= iy1) {
    clear_rect_physical(ctx, ctx.prev_dirty_min_x, ctx.prev_dirty_min_y, ctx.prev_dirty_max_x,
                        ctx.prev_dirty_max_y);
    return;
  }

  if (ctx.prev_dirty_min_y < iy0) {
    clear_rect_physical(ctx, ctx.prev_dirty_min_x, ctx.prev_dirty_min_y, ctx.prev_dirty_max_x,
                        iy0);
  }
  if (iy1 < ctx.prev_dirty_max_y) {
    clear_rect_physical(ctx, ctx.prev_dirty_min_x, iy1, ctx.prev_dirty_max_x,
                        ctx.prev_dirty_max_y);
  }
  if (ctx.prev_dirty_min_x < ix0) {
    clear_rect_physical(ctx, ctx.prev_dirty_min_x, iy0, ix0, iy1);
  }
  if (ix1 < ctx.prev_dirty_max_x) {
    clear_rect_physical(ctx, ix1, iy0, ctx.prev_dirty_max_x, iy1);
  }
}

static inline void ensure_prev_dirty_initialized(RenderContext& ctx) {
  if (ctx.frame_index != 0) {
    return;
  }
  if (rect_valid(ctx.prev_dirty_min_x, ctx.prev_dirty_min_y, ctx.prev_dirty_max_x,
                 ctx.prev_dirty_max_y)) {
    return;
  }
  ctx.prev_dirty_min_x = 0;
  ctx.prev_dirty_min_y = 0;
  ctx.prev_dirty_max_x = ctx.width;
  ctx.prev_dirty_max_y = ctx.height;
}

static void replay_commands(MidrawContext& ctx) {
  if (ctx.command_count == 0) {
    return;
  }
  const bool timing_debug = debug_timing_enabled();
  uint64_t t_pixel = 0;
  uint64_t t_line = 0;
  uint64_t t_rect = 0;
  uint64_t t_circle = 0;
  uint64_t t_text = 0;
  uint64_t t_image = 0;
  size_t c_pixel = 0;
  size_t c_line = 0;
  size_t c_rect = 0;
  size_t c_circle = 0;
  size_t c_text = 0;
  size_t c_image = 0;
  for (size_t i = 0; i < ctx.command_count; ++i) {
    const size_t index = (ctx.command_head + i) % kCommandCapacity;
    const DrawCommand& cmd = ctx.command_buffer[index];
    uint64_t t0 = 0;
    if (timing_debug) {
      t0 = now_ns();
    }
    switch (cmd.type) {
      case DrawCommandType::Pixel:
        draw_pixel(ctx.render, cmd.a, cmd.b, cmd.color);
        if (timing_debug) {
          t_pixel += now_ns() - t0;
          ++c_pixel;
        }
        break;
      case DrawCommandType::Line:
        draw_line(ctx.render, cmd.a, cmd.b, cmd.c, cmd.d, cmd.color);
        if (timing_debug) {
          t_line += now_ns() - t0;
          ++c_line;
        }
        break;
      case DrawCommandType::Rect:
        draw_rect(ctx.render, cmd.a, cmd.b, cmd.c, cmd.d, cmd.e != 0, cmd.color);
        if (timing_debug) {
          t_rect += now_ns() - t0;
          ++c_rect;
        }
        break;
      case DrawCommandType::Circle:
        draw_circle(ctx.render, cmd.a, cmd.b, cmd.c, cmd.color);
        if (timing_debug) {
          t_circle += now_ns() - t0;
          ++c_circle;
        }
        break;
      case DrawCommandType::Text: {
        const char* text = &ctx.text_pool[cmd.e];
        const bool has_clip = (cmd.c > cmd.a && cmd.d > cmd.b);
        if (ctx.atlas.valid) {
          if (has_clip) {
            draw_text_atlas_clipped(ctx, text, cmd.a, cmd.b, cmd.a, cmd.b, cmd.c, cmd.d,
                                    cmd.color);
          } else {
            draw_text_atlas(ctx, text, cmd.a, cmd.b, cmd.color);
          }
        } else {
          if (has_clip) {
            draw_text_clipped(ctx.render, text, cmd.a, cmd.b, cmd.a, cmd.b, cmd.c, cmd.d,
                              cmd.color);
          } else {
            draw_text(ctx.render, text, cmd.a, cmd.b, cmd.color);
          }
        }
        if (timing_debug) {
          t_text += now_ns() - t0;
          ++c_text;
        }
      } break;
      case DrawCommandType::Image: {
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(cmd.ptr);
        draw_image(ctx.render, pixels, cmd.c, cmd.d, cmd.a, cmd.b);
        if (timing_debug) {
          t_image += now_ns() - t0;
          ++c_image;
        }
      } break;
      default:
        break;
    }
  }
  if (timing_debug) {
    static uint64_t last_log_ns = 0;
    const uint64_t now = now_ns();
    if ((now - last_log_ns) > 1000000000ull) {
      fprintf(stderr,
              "midraw timing: cmds=%zu px=%zu ln=%zu rc=%zu cc=%zu tx=%zu img=%zu "
              "t(px)=%.3f t(ln)=%.3f t(rc)=%.3f t(cc)=%.3f t(tx)=%.3f t(img)=%.3f ms\n",
              ctx.command_count,
              c_pixel,
              c_line,
              c_rect,
              c_circle,
              c_text,
              c_image,
              t_pixel / 1000000.0,
              t_line / 1000000.0,
              t_rect / 1000000.0,
              t_circle / 1000000.0,
              t_text / 1000000.0,
              t_image / 1000000.0);
      last_log_ns = now;
    }
  }
}

static void unlock_and_post_deferred(MidrawContext& ctx) {
  ctx.recording = false;
  const bool timing_debug = debug_timing_enabled();
  uint64_t lock_ns = 0;
  uint64_t clear_ns = 0;
  uint64_t replay_ns = 0;
  if (ctx.render.frame_index == 0 &&
      !rect_valid(ctx.render.prev_dirty_min_x, ctx.render.prev_dirty_min_y,
                  ctx.render.prev_dirty_max_x, ctx.render.prev_dirty_max_y)) {
    update_dimensions(ctx);
    if (ctx.render.width > 0 && ctx.render.height > 0) {
      ctx.render.prev_dirty_min_x = 0;
      ctx.render.prev_dirty_min_y = 0;
      ctx.render.prev_dirty_max_x = ctx.render.width;
      ctx.render.prev_dirty_max_y = ctx.render.height;
    }
  }
  const bool has_prev =
      rect_valid(ctx.render.prev_dirty_min_x, ctx.render.prev_dirty_min_y,
                 ctx.render.prev_dirty_max_x, ctx.render.prev_dirty_max_y);
  const bool has_curr = rect_valid(ctx.render.dirty_min_x, ctx.render.dirty_min_y,
                                   ctx.render.dirty_max_x, ctx.render.dirty_max_y);
  if (!has_prev && !has_curr) {
    reset_present(ctx.render);
    return;
  }

  if (has_prev || has_curr) {
    const int min_x = has_prev
                          ? (has_curr ? (ctx.render.prev_dirty_min_x < ctx.render.dirty_min_x
                                             ? ctx.render.prev_dirty_min_x
                                             : ctx.render.dirty_min_x)
                                      : ctx.render.prev_dirty_min_x)
                          : ctx.render.dirty_min_x;
    const int min_y = has_prev
                          ? (has_curr ? (ctx.render.prev_dirty_min_y < ctx.render.dirty_min_y
                                             ? ctx.render.prev_dirty_min_y
                                             : ctx.render.dirty_min_y)
                                      : ctx.render.prev_dirty_min_y)
                          : ctx.render.dirty_min_y;
    const int max_x = has_prev
                          ? (has_curr ? (ctx.render.prev_dirty_max_x > ctx.render.dirty_max_x
                                             ? ctx.render.prev_dirty_max_x
                                             : ctx.render.dirty_max_x)
                                      : ctx.render.prev_dirty_max_x)
                          : ctx.render.dirty_max_x;
    const int max_y = has_prev
                          ? (has_curr ? (ctx.render.prev_dirty_max_y > ctx.render.dirty_max_y
                                             ? ctx.render.prev_dirty_max_y
                                             : ctx.render.dirty_max_y)
                                      : ctx.render.prev_dirty_max_y)
                          : ctx.render.dirty_max_y;
    ctx.render.present_min_x = min_x;
    ctx.render.present_min_y = min_y;
    ctx.render.present_max_x = max_x;
    ctx.render.present_max_y = max_y;
  } else {
    reset_present(ctx.render);
  }

  if (ctx.command_overflow || ctx.text_overflow) {
    static bool warned = false;
    if (!warned) {
      fprintf(stderr, "midraw: command/text buffer overflow\n");
      warned = true;
    }
  }

  uint64_t t0 = 0;
  if (timing_debug) {
    t0 = now_ns();
  }
  if (!lock_buffer(ctx)) {
    fprintf(stderr, "ANativeWindow_lock failed\n");
    return;
  }
  if (timing_debug) {
    lock_ns = now_ns() - t0;
  }
  ensure_prev_dirty_initialized(ctx.render);
  if (timing_debug) {
    t0 = now_ns();
  }
  clear_previous_dirty(ctx.render);
  if (timing_debug) {
    clear_ns = now_ns() - t0;
    t0 = now_ns();
  }
  replay_commands(ctx);
  if (timing_debug) {
    replay_ns = now_ns() - t0;
    static uint64_t last_log_ns = 0;
    const uint64_t now = now_ns();
    const bool slow = lock_ns > 5000000ull || clear_ns > 5000000ull || replay_ns > 5000000ull;
    if (slow || (now - last_log_ns) > 1000000000ull) {
      fprintf(stderr,
              "midraw timing: lock_buffer=%.3f ms clear_prev=%.3f ms replay=%.3f ms\n",
              lock_ns / 1000000.0,
              clear_ns / 1000000.0,
              replay_ns / 1000000.0);
      last_log_ns = now;
    }
  }

  if (rect_valid(ctx.render.dirty_min_x, ctx.render.dirty_min_y, ctx.render.dirty_max_x,
                 ctx.render.dirty_max_y)) {
    ctx.render.prev_dirty_min_x = ctx.render.dirty_min_x;
    ctx.render.prev_dirty_min_y = ctx.render.dirty_min_y;
    ctx.render.prev_dirty_max_x = ctx.render.dirty_max_x;
    ctx.render.prev_dirty_max_y = ctx.render.dirty_max_y;
  } else {
    reset_prev_dirty(ctx.render);
  }
  ctx.render.frame_index++;
  unlock_post(ctx);
}

static void unlock_and_post(MidrawContext& ctx) {
  if (ctx.render.window) {
    clear_previous_dirty(ctx.render);

    if (rect_valid(ctx.render.prev_dirty_min_x, ctx.render.prev_dirty_min_y,
                   ctx.render.prev_dirty_max_x, ctx.render.prev_dirty_max_y) ||
        rect_valid(ctx.render.dirty_min_x, ctx.render.dirty_min_y, ctx.render.dirty_max_x,
                   ctx.render.dirty_max_y)) {
      const int min_x = (ctx.render.prev_dirty_min_x < ctx.render.dirty_min_x)
                            ? ctx.render.prev_dirty_min_x
                            : ctx.render.dirty_min_x;
      const int min_y = (ctx.render.prev_dirty_min_y < ctx.render.dirty_min_y)
                            ? ctx.render.prev_dirty_min_y
                            : ctx.render.dirty_min_y;
      const int max_x = (ctx.render.prev_dirty_max_x > ctx.render.dirty_max_x)
                            ? ctx.render.prev_dirty_max_x
                            : ctx.render.dirty_max_x;
      const int max_y = (ctx.render.prev_dirty_max_y > ctx.render.dirty_max_y)
                            ? ctx.render.prev_dirty_max_y
                            : ctx.render.dirty_max_y;
      ctx.render.present_min_x = min_x;
      ctx.render.present_min_y = min_y;
      ctx.render.present_max_x = max_x;
      ctx.render.present_max_y = max_y;
    } else {
      reset_present(ctx.render);
    }

    if (rect_valid(ctx.render.dirty_min_x, ctx.render.dirty_min_y, ctx.render.dirty_max_x,
                   ctx.render.dirty_max_y)) {
      ctx.render.prev_dirty_min_x = ctx.render.dirty_min_x;
      ctx.render.prev_dirty_min_y = ctx.render.dirty_min_y;
      ctx.render.prev_dirty_max_x = ctx.render.dirty_max_x;
      ctx.render.prev_dirty_max_y = ctx.render.dirty_max_y;
    } else {
      reset_prev_dirty(ctx.render);
    }

    ctx.render.frame_index++;
    unlock_post(ctx);
  }
}

int midraw_init(MidrawContext** out_ctx, const MidrawConfig* config) {
  if (!out_ctx) {
    return MIDRAW_EINVAL;
  }
  *out_ctx = nullptr;

  MidrawContext* ctx = static_cast<MidrawContext*>(calloc(1, sizeof(MidrawContext)));
  if (!ctx) {
    return MIDRAW_EFAILED;
  }

  ctx->defer_lock = should_defer_lock();

  if (!init_symbols(&ctx->symbols)) {
    free(ctx);
    return MIDRAW_EFAILED;
  }

  if (config) {
    ctx->render.rotation = config->rotation % 360;
    ctx->requested_width = config->width;
    ctx->requested_height = config->height;
  }
  if (ctx->render.rotation < 0) {
    ctx->render.rotation += 360;
  }
  const int lock_env = env_int("MIDRAW_LOCK_SIZE", -1);
  if (lock_env >= 0) {
    ctx->lock_size = (lock_env != 0);
  } else {
    ctx->lock_size = (ctx->requested_width > 0 && ctx->requested_height > 0);
  }

  if (!midraw_create_window(ctx->symbols, ctx->render, ctx->requested_width,
                            ctx->requested_height, config)) {
    midraw_shutdown(ctx);
    return MIDRAW_EFAILED;
  }

  update_dimensions(*ctx);
  configure_fastpath(*ctx);
  reset_dirty(ctx->render);
  reset_prev_dirty(ctx->render);
  reset_present(ctx->render);
  ctx->render.frame_index = 0;
  reset_recording_buffers(*ctx);

#if defined(MIDRAW_USE_STB_TRUETYPE)
  if (!init_font_atlas(*ctx, config)) {
    fprintf(stderr, "stb_truetype atlas init failed, fallback to 8x8 font\n");
  }
#endif

  *out_ctx = ctx;
  return MIDRAW_OK;
}

void midraw_shutdown(MidrawContext* ctx) {
  if (!ctx) {
    return;
  }
  release_font_atlas(ctx->atlas);
  if (ctx->render.use_surface_direct && ctx->render.direct_buffer &&
      ctx->symbols.Surface_cancelBuffer && ctx->render.surface_native) {
    ctx->symbols.Surface_cancelBuffer(ctx->render.surface_native,
                                      ctx->render.direct_buffer,
                                      -1);
  }
  ctx->render.direct_buffer = nullptr;
  ctx->render.direct_graphic = nullptr;
  ctx->render.use_surface_direct = false;
  release_ahb_buffer(*ctx);
  ctx->render.use_ahb = false;
  if (ctx->render.ahb_surface && ctx->render.ahb_surface != ctx->render.surface &&
      ctx->symbols.ASurfaceControl_release) {
    ctx->symbols.ASurfaceControl_release(ctx->render.ahb_surface);
  }
  ctx->render.ahb_surface = nullptr;
  if (ctx->render.uses_osimgui && ctx->render.window) {
    android::ANativeWindowCreator::Destroy(ctx->render.window);
    ctx->render.window = nullptr;
  } else {
    midraw_hide_surface_control(ctx->symbols, ctx->render);
    if (ctx->render.window && ctx->symbols.ANativeWindow_release) {
      ctx->symbols.ANativeWindow_release(ctx->render.window);
      ctx->render.window = nullptr;
    }
  }
  if (ctx->render.surface && ctx->symbols.ASurfaceControl_release) {
    ctx->symbols.ASurfaceControl_release(ctx->render.surface);
    ctx->render.surface = nullptr;
  }
  if (ctx->symbols.libui) {
    dlclose(ctx->symbols.libui);
    ctx->symbols.libui = nullptr;
  }
  if (ctx->symbols.libnativewindow) {
    dlclose(ctx->symbols.libnativewindow);
    ctx->symbols.libnativewindow = nullptr;
  }
  if (ctx->symbols.libgui) {
    dlclose(ctx->symbols.libgui);
    ctx->symbols.libgui = nullptr;
  }
  if (ctx->symbols.libandroid) {
    dlclose(ctx->symbols.libandroid);
    ctx->symbols.libandroid = nullptr;
  }
  free(ctx);
}

int midraw_lock(MidrawContext* ctx) {
  if (!ctx) {
    return MIDRAW_EINVAL;
  }
  refresh_display_state(*ctx);
  if (ctx->defer_lock) {
    ctx->recording = true;
    reset_recording_buffers(*ctx);
    update_dimensions(*ctx);
    if (ctx->render.frame_index == 0 &&
        !rect_valid(ctx->render.prev_dirty_min_x, ctx->render.prev_dirty_min_y,
                    ctx->render.prev_dirty_max_x, ctx->render.prev_dirty_max_y) &&
        ctx->render.width > 0 && ctx->render.height > 0) {
      ctx->render.prev_dirty_min_x = 0;
      ctx->render.prev_dirty_min_y = 0;
      ctx->render.prev_dirty_max_x = ctx->render.width;
      ctx->render.prev_dirty_max_y = ctx->render.height;
    }
    reset_dirty(ctx->render);
    return 0;
  }
  if (!lock_buffer(*ctx)) {
    return MIDRAW_EFAILED;
  }
  ensure_prev_dirty_initialized(ctx->render);
  reset_dirty(ctx->render);
  return MIDRAW_OK;
}

void midraw_unlock_post(MidrawContext* ctx) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock) {
    unlock_and_post_deferred(*ctx);
  } else {
    unlock_and_post(*ctx);
  }
}

int midraw_logical_width(const MidrawContext* ctx) {
  if (!ctx) {
    return 0;
  }
  return logical_width(ctx->render);
}

int midraw_logical_height(const MidrawContext* ctx) {
  if (!ctx) {
    return 0;
  }
  return logical_height(ctx->render);
}

int midraw_resize(MidrawContext* ctx, int width, int height) {
  if (!ctx) {
    return MIDRAW_EINVAL;
  }
  if (width <= 0 || height <= 0) {
    return MIDRAW_EINVAL;
  }
  ctx->requested_width = width;
  ctx->requested_height = height;
  if (width == ctx->render.width && height == ctx->render.height) {
    return MIDRAW_OK;
  }
  int result = midraw_resize_window(ctx->symbols, ctx->render, width, height);
  if (result == 0) {
    ctx->render.width = width;
    ctx->render.height = height;
    if (ctx->render.use_ahb) {
      setup_ahb_buffer(*ctx, width, height);
    }
    return MIDRAW_OK;
  }
  return MIDRAW_EFAILED;
}

void* midraw_get_native_window(MidrawContext* ctx) {
  if (!ctx) {
    return nullptr;
  }
  return reinterpret_cast<void*>(ctx->render.window);
}

int midraw_set_layer(MidrawContext* ctx, int32_t layer) {
  if (!ctx || !ctx->render.surface) {
    return MIDRAW_EINVAL;
  }
  if (!ctx->symbols.ASurfaceTransaction_create || !ctx->symbols.ASurfaceTransaction_release ||
      !ctx->symbols.ASurfaceTransaction_apply) {
    return MIDRAW_EFAILED;
  }
  if (!ctx->symbols.ASurfaceTransaction_setLayer && !ctx->symbols.ASurfaceTransaction_setZOrder) {
    return MIDRAW_EFAILED;
  }
  ASurfaceTransaction* tx = ctx->symbols.ASurfaceTransaction_create();
  if (!tx) {
    return MIDRAW_EFAILED;
  }
  if (ctx->symbols.ASurfaceTransaction_setLayer) {
    ctx->symbols.ASurfaceTransaction_setLayer(tx, ctx->render.surface, layer);
  } else if (ctx->symbols.ASurfaceTransaction_setZOrder) {
    ctx->symbols.ASurfaceTransaction_setZOrder(tx, ctx->render.surface, layer);
  }
  ctx->symbols.ASurfaceTransaction_apply(tx);
  ctx->symbols.ASurfaceTransaction_release(tx);
  return MIDRAW_OK;
}

int midraw_display_rotation(MidrawContext* ctx,
                            int* out_rotation,
                            int* out_width,
                            int* out_height) {
  if (!ctx) {
    return MIDRAW_EINVAL;
  }
  return midraw_query_display_rotation(ctx->symbols, out_rotation, out_width, out_height);
}

void midraw_draw_pixel(MidrawContext* ctx, int x, int y, uint32_t color) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Pixel;
    cmd.a = x;
    cmd.b = y;
    cmd.color = color;
    if (!push_command(*ctx, cmd)) {
      return;
    }
    expand_dirty_rect(ctx->render, x, y, 1, 1);
    return;
  }
  draw_pixel(ctx->render, x, y, color);
}

void midraw_draw_line(MidrawContext* ctx, int x1, int y1, int x2, int y2, uint32_t color) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Line;
    cmd.a = x1;
    cmd.b = y1;
    cmd.c = x2;
    cmd.d = y2;
    cmd.color = color;
    if (!push_command(*ctx, cmd)) {
      return;
    }
    const int min_x = x1 < x2 ? x1 : x2;
    const int max_x = x1 > x2 ? x1 : x2;
    const int min_y = y1 < y2 ? y1 : y2;
    const int max_y = y1 > y2 ? y1 : y2;
    const int pad = aa_lines_enabled() ? 1 : 0;
    expand_dirty_rect(ctx->render, min_x - pad, min_y - pad,
                      (max_x - min_x + 1) + pad * 2,
                      (max_y - min_y + 1) + pad * 2);
    return;
  }
  draw_line(ctx->render, x1, y1, x2, y2, color);
}

void midraw_draw_rect(MidrawContext* ctx, int x, int y, int w, int h, int filled,
                      uint32_t color) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Rect;
    cmd.a = x;
    cmd.b = y;
    cmd.c = w;
    cmd.d = h;
    cmd.e = filled != 0 ? 1 : 0;
    cmd.color = color;
    if (!push_command(*ctx, cmd)) {
      return;
    }
    expand_dirty_rect(ctx->render, x, y, w, h);
    return;
  }
  draw_rect(ctx->render, x, y, w, h, filled != 0, color);
}

void midraw_draw_circle(MidrawContext* ctx, int cx, int cy, int radius, uint32_t color) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Circle;
    cmd.a = cx;
    cmd.b = cy;
    cmd.c = radius;
    cmd.color = color;
    if (!push_command(*ctx, cmd)) {
      return;
    }
    const int pad = aa_circles_enabled() ? 1 : 0;
    expand_dirty_rect(ctx->render, cx - radius - pad, cy - radius - pad,
                      radius * 2 + 1 + pad * 2,
                      radius * 2 + 1 + pad * 2);
    return;
  }
  draw_circle(ctx->render, cx, cy, radius, color);
}

void midraw_draw_text(MidrawContext* ctx, const char* text, int x, int y, uint32_t color) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    record_text(*ctx, text, x, y, color);
    return;
  }
  if (ctx->atlas.valid) {
    draw_text_atlas(*ctx, text, x, y, color);
  } else {
    draw_text(ctx->render, text, x, y, color);
  }
}

void midraw_draw_text_rect(MidrawContext* ctx,
                           const char* text,
                           int x0,
                           int y0,
                           int x1,
                           int y1,
                           uint32_t color) {
  if (!ctx) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    record_text_rect(*ctx, text, x0, y0, x1, y1, color);
    return;
  }
  if (x1 <= x0 || y1 <= y0) {
    midraw_draw_text(ctx, text, x0, y0, color);
    return;
  }
  if (ctx->atlas.valid) {
    draw_text_atlas_clipped(*ctx, text, x0, y0, x0, y0, x1, y1, color);
  } else {
    draw_text_clipped(ctx->render, text, x0, y0, x0, y0, x1, y1, color);
  }
}

void midraw_draw_image(MidrawContext* ctx,
                       const uint32_t* pixels,
                       int img_w,
                       int img_h,
                       int x,
                       int y) {
  if (!ctx || !pixels || img_w <= 0 || img_h <= 0) {
    return;
  }
  if (ctx->defer_lock && ctx->recording) {
    DrawCommand cmd;
    cmd.type = DrawCommandType::Image;
    cmd.a = x;
    cmd.b = y;
    cmd.c = img_w;
    cmd.d = img_h;
    cmd.ptr = reinterpret_cast<uintptr_t>(pixels);
    if (!push_command(*ctx, cmd)) {
      return;
    }
    expand_dirty_rect(ctx->render, x, y, img_w, img_h);
    return;
  }
  draw_image(ctx->render, pixels, img_w, img_h, x, y);
}
