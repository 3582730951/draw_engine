#include "include/midraw.h"
#include "midraw_internal.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

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

enum class DrawCommandType : uint8_t { Pixel, Line, Rect, Circle, Text };

struct DrawCommand {
  DrawCommandType type = DrawCommandType::Pixel;
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  int e = 0;
  int f = 0;
  uint32_t color = 0;
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

  symbols->ASurfaceControl_create = reinterpret_cast<PFN_ASurfaceControl_create>(
      load_symbol_candidates(symbols->libgui, "ASurfaceControl_create",
                             kCandidates_ASurfaceControl_create,
                             sizeof(kCandidates_ASurfaceControl_create) /
                                 sizeof(kCandidates_ASurfaceControl_create[0])));
  symbols->ASurfaceControl_release = reinterpret_cast<PFN_ASurfaceControl_release>(
      load_symbol_candidates(symbols->libgui, "ASurfaceControl_release",
                             kCandidates_ASurfaceControl_release,
                             sizeof(kCandidates_ASurfaceControl_release) /
                                 sizeof(kCandidates_ASurfaceControl_release[0])));
  symbols->ASurfaceTransaction_create = reinterpret_cast<PFN_ASurfaceTransaction_create>(
      load_symbol_candidates(symbols->libgui, "ASurfaceTransaction_create",
                             kCandidates_ASurfaceTransaction_create,
                             sizeof(kCandidates_ASurfaceTransaction_create) /
                                 sizeof(kCandidates_ASurfaceTransaction_create[0])));
  symbols->ASurfaceTransaction_release = reinterpret_cast<PFN_ASurfaceTransaction_release>(
      load_symbol_candidates(symbols->libgui, "ASurfaceTransaction_release",
                             kCandidates_ASurfaceTransaction_release,
                             sizeof(kCandidates_ASurfaceTransaction_release) /
                                 sizeof(kCandidates_ASurfaceTransaction_release[0])));
  symbols->ASurfaceTransaction_setBufferSize =
      reinterpret_cast<PFN_ASurfaceTransaction_setBufferSize>(load_symbol_candidates(
          symbols->libgui, "ASurfaceTransaction_setBufferSize",
          kCandidates_ASurfaceTransaction_setBufferSize,
          sizeof(kCandidates_ASurfaceTransaction_setBufferSize) /
              sizeof(kCandidates_ASurfaceTransaction_setBufferSize[0])));
  symbols->ASurfaceTransaction_setVisibility =
      reinterpret_cast<PFN_ASurfaceTransaction_setVisibility>(load_symbol_candidates(
          symbols->libgui, "ASurfaceTransaction_setVisibility",
          kCandidates_ASurfaceTransaction_setVisibility,
          sizeof(kCandidates_ASurfaceTransaction_setVisibility) /
              sizeof(kCandidates_ASurfaceTransaction_setVisibility[0])));
  symbols->ASurfaceTransaction_setLayer =
      reinterpret_cast<PFN_ASurfaceTransaction_setLayer>(load_symbol_candidates(
          symbols->libgui, "ASurfaceTransaction_setLayer",
          kCandidates_ASurfaceTransaction_setLayer,
          sizeof(kCandidates_ASurfaceTransaction_setLayer) /
              sizeof(kCandidates_ASurfaceTransaction_setLayer[0])));
  symbols->ASurfaceTransaction_apply = reinterpret_cast<PFN_ASurfaceTransaction_apply>(
      load_symbol_candidates(symbols->libgui, "ASurfaceTransaction_apply",
                             kCandidates_ASurfaceTransaction_apply,
                             sizeof(kCandidates_ASurfaceTransaction_apply) /
                                 sizeof(kCandidates_ASurfaceTransaction_apply[0])));

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

  const bool ok = symbols->ASurfaceControl_create && symbols->ASurfaceControl_release &&
                  symbols->ASurfaceTransaction_create && symbols->ASurfaceTransaction_release &&
                  symbols->ASurfaceTransaction_setBufferSize &&
                  symbols->ASurfaceTransaction_setVisibility &&
                  symbols->ASurfaceTransaction_setLayer && symbols->ASurfaceTransaction_apply &&
                  symbols->ANativeWindow_fromSurfaceControl && symbols->ANativeWindow_lock &&
                  symbols->ANativeWindow_unlockAndPost && symbols->ANativeWindow_release;

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
      *out_x = ctx.height - 1 - y;
      *out_y = x;
      break;
    case 180:
      *out_x = ctx.width - 1 - x;
      *out_y = ctx.height - 1 - y;
      break;
    case 270:
      *out_x = y;
      *out_y = ctx.width - 1 - x;
      break;
    default:
      *out_x = x;
      *out_y = y;
      break;
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
      px = ctx.height - (y + h);
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
      py = ctx.width - (x + w);
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

static inline void plot_pixel(RenderContext& ctx, int x, int y, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const int w = logical_width(ctx);
  const int h = logical_height(ctx);
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return;
  }
  int px = 0;
  int py = 0;
  map_coords(ctx, x, y, &px, &py);
  if (px < 0 || py < 0 || px >= ctx.width || py >= ctx.height) {
    return;
  }
  ctx.pixels[py * ctx.stride + px] = color;
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

static void draw_line(RenderContext& ctx, int x1, int y1, int x2, int y2, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  const int min_x = x1 < x2 ? x1 : x2;
  const int max_x = x1 > x2 ? x1 : x2;
  const int min_y = y1 < y2 ? y1 : y2;
  const int max_y = y1 > y2 ? y1 : y2;
  expand_dirty_rect(ctx, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
  plot_line(ctx, x1, y1, x2, y2, color);
}

static void draw_circle(RenderContext& ctx, int cx, int cy, int radius, uint32_t color) {
  if (!ctx.pixels) {
    return;
  }
  expand_dirty_rect(ctx, cx - radius, cy - radius, radius * 2 + 1, radius * 2 + 1);

  int x = radius;
  int y = 0;
  int err = 1 - x;

  while (x >= y) {
    plot_pixel(ctx, cx + x, cy + y, color);
    plot_pixel(ctx, cx + y, cy + x, color);
    plot_pixel(ctx, cx - y, cy + x, color);
    plot_pixel(ctx, cx - x, cy + y, color);
    plot_pixel(ctx, cx - x, cy - y, color);
    plot_pixel(ctx, cx - y, cy - x, color);
    plot_pixel(ctx, cx + y, cy - x, color);
    plot_pixel(ctx, cx + x, cy - y, color);

    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x + 1);
    }
  }
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
    plot_line(ctx, x, y, x + w - 1, y, color);
    plot_line(ctx, x, y + h - 1, x + w - 1, y + h - 1, color);
    plot_line(ctx, x, y, x, y + h - 1, color);
    plot_line(ctx, x + w - 1, y, x + w - 1, y + h - 1, color);
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

static inline void record_text(MidrawContext& ctx, const char* text, int x, int y,
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

static bool lock_buffer(MidrawContext& ctx) {
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
  for (size_t i = 0; i < ctx.command_count; ++i) {
    const size_t index = (ctx.command_head + i) % kCommandCapacity;
    const DrawCommand& cmd = ctx.command_buffer[index];
    switch (cmd.type) {
      case DrawCommandType::Pixel:
        draw_pixel(ctx.render, cmd.a, cmd.b, cmd.color);
        break;
      case DrawCommandType::Line:
        draw_line(ctx.render, cmd.a, cmd.b, cmd.c, cmd.d, cmd.color);
        break;
      case DrawCommandType::Rect:
        draw_rect(ctx.render, cmd.a, cmd.b, cmd.c, cmd.d, cmd.e != 0, cmd.color);
        break;
      case DrawCommandType::Circle:
        draw_circle(ctx.render, cmd.a, cmd.b, cmd.c, cmd.color);
        break;
      case DrawCommandType::Text: {
        const char* text = &ctx.text_pool[cmd.e];
        if (ctx.atlas.valid) {
          draw_text_atlas(ctx, text, cmd.a, cmd.b, cmd.color);
        } else {
          draw_text(ctx.render, text, cmd.a, cmd.b, cmd.color);
        }
      } break;
      default:
        break;
    }
  }
}

static void unlock_and_post_deferred(MidrawContext& ctx) {
  ctx.recording = false;
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

  if (!lock_buffer(ctx)) {
    fprintf(stderr, "ANativeWindow_lock failed\n");
    return;
  }
  ensure_prev_dirty_initialized(ctx.render);
  clear_previous_dirty(ctx.render);
  replay_commands(ctx);

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
  ctx.symbols.ANativeWindow_unlockAndPost(ctx.render.window);
  ctx.render.pixels = nullptr;
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
    ctx.symbols.ANativeWindow_unlockAndPost(ctx.render.window);
  }
  ctx.render.pixels = nullptr;
}

int midraw_init(MidrawContext** out_ctx, const MidrawConfig* config) {
  if (!out_ctx) {
    return -1;
  }
  *out_ctx = nullptr;

  MidrawContext* ctx = static_cast<MidrawContext*>(calloc(1, sizeof(MidrawContext)));
  if (!ctx) {
    return -1;
  }

  ctx->defer_lock = should_defer_lock();

  if (!init_symbols(&ctx->symbols)) {
    free(ctx);
    return -1;
  }

  if (config) {
    ctx->render.rotation = config->rotation % 360;
    ctx->requested_width = config->width;
    ctx->requested_height = config->height;
  }
  if (ctx->render.rotation < 0) {
    ctx->render.rotation += 360;
  }

  if (!midraw_create_window(ctx->symbols, ctx->render, ctx->requested_width,
                            ctx->requested_height, config)) {
    midraw_shutdown(ctx);
    return -1;
  }

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
  return 0;
}

void midraw_shutdown(MidrawContext* ctx) {
  if (!ctx) {
    return;
  }
  release_font_atlas(ctx->atlas);
  if (ctx->render.window && ctx->symbols.ANativeWindow_release) {
    ctx->symbols.ANativeWindow_release(ctx->render.window);
    ctx->render.window = nullptr;
  }
  if (ctx->render.surface && ctx->symbols.ASurfaceControl_release) {
    ctx->symbols.ASurfaceControl_release(ctx->render.surface);
    ctx->render.surface = nullptr;
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
    return -1;
  }
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
    return -1;
  }
  ensure_prev_dirty_initialized(ctx->render);
  reset_dirty(ctx->render);
  return 0;
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
    expand_dirty_rect(ctx->render, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
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
    expand_dirty_rect(ctx->render, cx - radius, cy - radius, radius * 2 + 1, radius * 2 + 1);
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
