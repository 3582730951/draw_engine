#pragma once

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define MIDRAW_API __attribute__((visibility("default")))
#else
#define MIDRAW_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MidrawContext MidrawContext;

typedef struct MidrawConfig {
  const char* surface_name;
  int rotation;
  int32_t width;
  int32_t height;
  const char* font_path;
  int font_size;
  int atlas_size;
} MidrawConfig;

MIDRAW_API int midraw_init(MidrawContext** out_ctx, const MidrawConfig* config);
MIDRAW_API void midraw_shutdown(MidrawContext* ctx);

MIDRAW_API int midraw_lock(MidrawContext* ctx);
MIDRAW_API void midraw_unlock_post(MidrawContext* ctx);

MIDRAW_API int midraw_logical_width(const MidrawContext* ctx);
MIDRAW_API int midraw_logical_height(const MidrawContext* ctx);

MIDRAW_API void midraw_draw_pixel(MidrawContext* ctx, int x, int y, uint32_t color);
MIDRAW_API void midraw_draw_line(MidrawContext* ctx, int x1, int y1, int x2, int y2,
                                 uint32_t color);
MIDRAW_API void midraw_draw_rect(MidrawContext* ctx, int x, int y, int w, int h, int filled,
                                 uint32_t color);
MIDRAW_API void midraw_draw_circle(MidrawContext* ctx, int cx, int cy, int radius,
                                   uint32_t color);
MIDRAW_API void midraw_draw_text(MidrawContext* ctx, const char* text, int x, int y,
                                 uint32_t color);
MIDRAW_API void midraw_draw_text_rect(MidrawContext* ctx,
                                      const char* text,
                                      int x0,
                                      int y0,
                                      int x1,
                                      int y1,
                                      uint32_t color);
MIDRAW_API void midraw_draw_image(MidrawContext* ctx,
                                  const uint32_t* pixels,
                                  int img_w,
                                  int img_h,
                                  int x,
                                  int y);

#ifdef __cplusplus
}
#endif
