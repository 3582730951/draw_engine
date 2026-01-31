#pragma once

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define DRAW_ENGINE_API __attribute__((visibility("default")))
#else
#define DRAW_ENGINE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DrawImage DrawImage;

DRAW_ENGINE_API int init_draw_engine(int mode);
DRAW_ENGINE_API int init_draw_windows(const char* name, int randomize_name);
DRAW_ENGINE_API void shutdown_draw_engine(void);

DRAW_ENGINE_API int draw_begin_frame(void);
DRAW_ENGINE_API void draw_end_frame(void);
DRAW_ENGINE_API int draw_screen_width(void);
DRAW_ENGINE_API int draw_screen_height(void);

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
