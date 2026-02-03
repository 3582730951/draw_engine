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

#define DRAW_COLOR_TRANSPARENT 0x00000000u
#define DRAW_COLOR_BLACK 0xFF000000u
#define DRAW_COLOR_WHITE 0xFFFFFFFFu
#define DRAW_COLOR_GRAY 0xFF808080u
#define DRAW_COLOR_LIGHT_GRAY 0xFFC0C0C0u
#define DRAW_COLOR_DARK_GRAY 0xFF404040u
#define DRAW_COLOR_RED 0xFFFF0000u
#define DRAW_COLOR_GREEN 0xFF00FF00u
#define DRAW_COLOR_BLUE 0xFF0000FFu
#define DRAW_COLOR_YELLOW 0xFFFFFF00u
#define DRAW_COLOR_CYAN 0xFF00FFFFu
#define DRAW_COLOR_MAGENTA 0xFFFF00FFu
#define DRAW_COLOR_ORANGE 0xFFFFA500u
#define DRAW_COLOR_PURPLE 0xFF800080u
#define DRAW_COLOR_PINK 0xFFFFC0CBu
#define DRAW_COLOR_BROWN 0xFFA52A2Au
#define DRAW_COLOR_LIME 0xFF32CD32u
#define DRAW_COLOR_NAVY 0xFF000080u
#define DRAW_COLOR_TEAL 0xFF008080u
#define DRAW_COLOR_OLIVE 0xFF808000u
#define DRAW_COLOR_MAROON 0xFF800000u
#define DRAW_COLOR_SILVER 0xFFC0C0C0u
#define DRAW_COLOR_GOLD 0xFFFFD700u
#define DRAW_COLOR_SKY 0xFF87CEEBu
#define DRAW_COLOR_SALMON 0xFFFA8072u
#define DRAW_COLOR_KHAKI 0xFFF0E68Cu
#define DRAW_COLOR_CORAL 0xFFFF7F50u
#define DRAW_COLOR_IVORY 0xFFFFFFF0u
#define DRAW_COLOR_BEIGE 0xFFF5F5DCu
#define DRAW_COLOR_MINT 0xFF98FF98u
#define DRAW_COLOR_LAVENDER 0xFFE6E6FAu
#define DRAW_COLOR_CHOCOLATE 0xFFD2691Eu
#define DRAW_COLOR_ALICE_BLUE 0xFFF0F8FFu
#define DRAW_COLOR_ANTIQUE_WHITE 0xFFFAEBD7u
#define DRAW_COLOR_AQUA 0xFF00FFFFu
#define DRAW_COLOR_AQUAMARINE 0xFF7FFFD4u
#define DRAW_COLOR_AZURE 0xFFF0FFFFu
#define DRAW_COLOR_BISQUE 0xFFFFE4C4u
#define DRAW_COLOR_BLANCHED_ALMOND 0xFFFFEBCDu
#define DRAW_COLOR_BLUE_VIOLET 0xFF8A2BE2u
#define DRAW_COLOR_BURLY_WOOD 0xFFDEB887u
#define DRAW_COLOR_CADET_BLUE 0xFF5F9EA0u
#define DRAW_COLOR_CHARTREUSE 0xFF7FFF00u
#define DRAW_COLOR_CORNFLOWER_BLUE 0xFF6495EDu
#define DRAW_COLOR_CORNSILK 0xFFFFF8DCu
#define DRAW_COLOR_CRIMSON 0xFFDC143Cu
#define DRAW_COLOR_DARK_BLUE 0xFF00008Bu
#define DRAW_COLOR_DARK_CYAN 0xFF008B8Bu
#define DRAW_COLOR_DARK_GOLDENROD 0xFFB8860Bu
#define DRAW_COLOR_DARK_GRAY_HTML 0xFFA9A9A9u
#define DRAW_COLOR_DARK_GREEN 0xFF006400u
#define DRAW_COLOR_DARK_KHAKI 0xFFBDB76Bu
#define DRAW_COLOR_DARK_MAGENTA 0xFF8B008Bu
#define DRAW_COLOR_DARK_OLIVE_GREEN 0xFF556B2Fu
#define DRAW_COLOR_DARK_ORANGE 0xFFFF8C00u
#define DRAW_COLOR_DARK_ORCHID 0xFF9932CCu
#define DRAW_COLOR_DARK_RED 0xFF8B0000u
#define DRAW_COLOR_DARK_SALMON 0xFFE9967Au
#define DRAW_COLOR_DARK_SEA_GREEN 0xFF8FBC8Fu
#define DRAW_COLOR_DARK_SLATE_BLUE 0xFF483D8Bu
#define DRAW_COLOR_DARK_SLATE_GRAY 0xFF2F4F4Fu
#define DRAW_COLOR_DARK_TURQUOISE 0xFF00CED1u
#define DRAW_COLOR_DARK_VIOLET 0xFF9400D3u
#define DRAW_COLOR_DEEP_PINK 0xFFFF1493u
#define DRAW_COLOR_DEEP_SKY_BLUE 0xFF00BFFFu
#define DRAW_COLOR_DIM_GRAY 0xFF696969u
#define DRAW_COLOR_DODGER_BLUE 0xFF1E90FFu
#define DRAW_COLOR_FIRE_BRICK 0xFFB22222u
#define DRAW_COLOR_FLORAL_WHITE 0xFFFFFAF0u
#define DRAW_COLOR_FOREST_GREEN 0xFF228B22u
#define DRAW_COLOR_FUCHSIA 0xFFFF00FFu
#define DRAW_COLOR_GAINSBORO 0xFFDCDCDCu
#define DRAW_COLOR_GHOST_WHITE 0xFFF8F8FFu
#define DRAW_COLOR_HONEYDEW 0xFFF0FFF0u
#define DRAW_COLOR_HOT_PINK 0xFFFF69B4u
#define DRAW_COLOR_INDIAN_RED 0xFFCD5C5Cu
#define DRAW_COLOR_INDIGO 0xFF4B0082u
#define DRAW_COLOR_LAVENDER_BLUSH 0xFFFFF0F5u
#define DRAW_COLOR_LAWN_GREEN 0xFF7CFC00u
#define DRAW_COLOR_LEMON_CHIFFON 0xFFFFFACDu
#define DRAW_COLOR_LIGHT_BLUE 0xFFADD8E6u
#define DRAW_COLOR_LIGHT_CORAL 0xFFF08080u
#define DRAW_COLOR_LIGHT_CYAN 0xFFE0FFFFu
#define DRAW_COLOR_LIGHT_GOLDENROD 0xFFFAFAD2u
#define DRAW_COLOR_LIGHT_GREEN 0xFF90EE90u
#define DRAW_COLOR_LIGHT_PINK 0xFFFFB6C1u
#define DRAW_COLOR_LIGHT_SALMON 0xFFFFA07Au
#define DRAW_COLOR_LIGHT_SEA_GREEN 0xFF20B2AAu
#define DRAW_COLOR_LIGHT_SKY_BLUE 0xFF87CEFAu
#define DRAW_COLOR_LIGHT_SLATE_GRAY 0xFF778899u
#define DRAW_COLOR_LIGHT_STEEL_BLUE 0xFFB0C4DEu
#define DRAW_COLOR_LIGHT_YELLOW 0xFFFFFFE0u
#define DRAW_COLOR_LIME_GREEN 0xFF32CD32u
#define DRAW_COLOR_LINEN 0xFFFAF0E6u
#define DRAW_COLOR_MEDIUM_AQUAMARINE 0xFF66CDAAu
#define DRAW_COLOR_MEDIUM_BLUE 0xFF0000CDu
#define DRAW_COLOR_MEDIUM_ORCHID 0xFFBA55D3u
#define DRAW_COLOR_MEDIUM_PURPLE 0xFF9370DBu
#define DRAW_COLOR_MEDIUM_SEA_GREEN 0xFF3CB371u
#define DRAW_COLOR_MEDIUM_SLATE_BLUE 0xFF7B68EEu
#define DRAW_COLOR_MEDIUM_SPRING_GREEN 0xFF00FA9Au
#define DRAW_COLOR_MEDIUM_TURQUOISE 0xFF48D1CCu
#define DRAW_COLOR_MEDIUM_VIOLET_RED 0xFFC71585u
#define DRAW_COLOR_MIDNIGHT_BLUE 0xFF191970u
#define DRAW_COLOR_MISTY_ROSE 0xFFFFE4E1u
#define DRAW_COLOR_MOCCASIN 0xFFFFE4B5u
#define DRAW_COLOR_NAVAJO_WHITE 0xFFFFDEADu
#define DRAW_COLOR_OLD_LACE 0xFFFDF5E6u
#define DRAW_COLOR_OLIVE_DRAB 0xFF6B8E23u
#define DRAW_COLOR_ORANGE_RED 0xFFFF4500u
#define DRAW_COLOR_ORCHID 0xFFDA70D6u
#define DRAW_COLOR_PALE_GOLDENROD 0xFFEEE8AAu
#define DRAW_COLOR_PALE_GREEN 0xFF98FB98u
#define DRAW_COLOR_PALE_TURQUOISE 0xFFAFEEEEu
#define DRAW_COLOR_PALE_VIOLET_RED 0xFFDB7093u
#define DRAW_COLOR_PAPAYA_WHIP 0xFFFFEFD5u
#define DRAW_COLOR_PEACH_PUFF 0xFFFFDAB9u
#define DRAW_COLOR_PERU 0xFFCD853Fu
#define DRAW_COLOR_PLUM 0xFFDDA0DDu
#define DRAW_COLOR_POWDER_BLUE 0xFFB0E0E6u
#define DRAW_COLOR_ROSY_BROWN 0xFFBC8F8Fu
#define DRAW_COLOR_ROYAL_BLUE 0xFF4169E1u
#define DRAW_COLOR_SADDLE_BROWN 0xFF8B4513u
#define DRAW_COLOR_SANDY_BROWN 0xFFF4A460u
#define DRAW_COLOR_SEA_GREEN 0xFF2E8B57u
#define DRAW_COLOR_SEASHELL 0xFFFFF5EEu
#define DRAW_COLOR_SIENNA 0xFFA0522Du
#define DRAW_COLOR_SKY_BLUE 0xFF87CEEBu
#define DRAW_COLOR_SLATE_BLUE 0xFF6A5ACDu
#define DRAW_COLOR_SLATE_GRAY 0xFF708090u
#define DRAW_COLOR_SNOW 0xFFFFFAFAu
#define DRAW_COLOR_SPRING_GREEN 0xFF00FF7Fu
#define DRAW_COLOR_STEEL_BLUE 0xFF4682B4u
#define DRAW_COLOR_TAN 0xFFD2B48Cu
#define DRAW_COLOR_THISTLE 0xFFD8BFD8u
#define DRAW_COLOR_TOMATO 0xFFFF6347u
#define DRAW_COLOR_TURQUOISE 0xFF40E0D0u
#define DRAW_COLOR_VIOLET 0xFFEE82EEu
#define DRAW_COLOR_WHEAT 0xFFF5DEB3u
#define DRAW_COLOR_WHITE_SMOKE 0xFFF5F5F5u
#define DRAW_COLOR_YELLOW_GREEN 0xFF9ACD32u

enum {
  DRAW_UI_WINDOW_MOVABLE = 1 << 0,
  DRAW_UI_WINDOW_NO_TITLE = 1 << 1,
  DRAW_UI_WINDOW_NO_BORDER = 1 << 2,
  DRAW_UI_WINDOW_NO_BG = 1 << 3
};

enum {
  DRAW_UI_KEY_BACKSPACE = 1,
  DRAW_UI_KEY_ENTER = 2,
  DRAW_UI_KEY_LEFT = 3,
  DRAW_UI_KEY_RIGHT = 4,
  DRAW_UI_KEY_HOME = 5,
  DRAW_UI_KEY_END = 6,
  DRAW_UI_KEY_DELETE = 7,
  DRAW_UI_KEY_TAB = 8,
  DRAW_UI_KEY_ESCAPE = 9,
  DRAW_UI_KEY_MAX = 9
};

typedef struct DrawUiStyle {
  uint32_t window_bg;
  uint32_t title_bg;
  uint32_t border;
  uint32_t text;
  uint32_t button_bg;
  uint32_t button_hover;
  uint32_t button_active;
  int title_height;
  int padding;
  int border_size;
} DrawUiStyle;

typedef struct DrawUiImeConfig {
  void* java_vm;
  void* jni_env;
  void* context;
  void* view;
} DrawUiImeConfig;

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

DRAW_ENGINE_API uint32_t draw_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

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

DRAW_ENGINE_API void draw_ui_set_style(const DrawUiStyle* style);
DRAW_ENGINE_API void draw_ui_get_style(DrawUiStyle* out_style);
DRAW_ENGINE_API void draw_ui_set_touch(int down, float x, float y);
DRAW_ENGINE_API void draw_ui_new_frame(void);
DRAW_ENGINE_API int draw_ui_begin_window(const char* title,
                                         int* x,
                                         int* y,
                                         int w,
                                         int h,
                                         int flags);
DRAW_ENGINE_API void draw_ui_end_window(void);
DRAW_ENGINE_API int draw_ui_button(const char* label, int x, int y, int w, int h);
DRAW_ENGINE_API void draw_ui_text(const char* text, int x, int y, uint32_t color);
DRAW_ENGINE_API int draw_ui_radio(const char* label, int x, int y, int value, int* current);
DRAW_ENGINE_API int draw_ui_listbox(const char* label,
                                    int x,
                                    int y,
                                    int w,
                                    int h,
                                    const char* const* items,
                                    int item_count,
                                    int* current);
DRAW_ENGINE_API int draw_ui_listbox_multi(const char* label,
                                          int x,
                                          int y,
                                          int w,
                                          int h,
                                          const char* const* items,
                                          int item_count,
                                          uint32_t* mask);
DRAW_ENGINE_API int draw_ui_combo(const char* label,
                                  int x,
                                  int y,
                                  int w,
                                  const char* const* items,
                                  int item_count,
                                  int* current);
DRAW_ENGINE_API int draw_ui_tabs(int x,
                                 int y,
                                 int w,
                                 int h,
                                 const char* const* labels,
                                 int label_count,
                                 int* current);
DRAW_ENGINE_API int draw_ui_scrollbar(const char* label,
                                      int x,
                                      int y,
                                      int h,
                                      int content_h,
                                      int* scroll_y);
DRAW_ENGINE_API int draw_ui_tree_node(const char* label, int x, int y, int* open);
DRAW_ENGINE_API int draw_ui_color_picker_rgba(const char* label, int x, int y, uint32_t* color);
DRAW_ENGINE_API int draw_ui_checkbox(const char* label, int x, int y, int* value);
DRAW_ENGINE_API int draw_ui_slider_int(const char* label,
                                       int x,
                                       int y,
                                       int w,
                                       int min_value,
                                       int max_value,
                                       int* value);
DRAW_ENGINE_API int draw_ui_slider_float(const char* label,
                                         int x,
                                         int y,
                                         int w,
                                         float min_value,
                                         float max_value,
                                         float* value);
DRAW_ENGINE_API int draw_ui_toggle(const char* label, int x, int y, int* value);
DRAW_ENGINE_API void draw_ui_progress(const char* label, int x, int y, int w, float value);
DRAW_ENGINE_API void draw_ui_separator(int x, int y, int w);
DRAW_ENGINE_API void draw_ui_input_char(uint32_t codepoint);
DRAW_ENGINE_API void draw_ui_input_key(int key, int down);
DRAW_ENGINE_API int draw_ui_input_text(const char* label,
                                       int x,
                                       int y,
                                       int w,
                                       char* buffer,
                                       int buffer_size);
DRAW_ENGINE_API int draw_ui_ime_set_context(const DrawUiImeConfig* config);
DRAW_ENGINE_API int draw_ui_ime_show(int show);

DRAW_ENGINE_API int draw_ui_touch_open(void);
DRAW_ENGINE_API void draw_ui_touch_close(void);
DRAW_ENGINE_API int draw_ui_touch_poll(void);

#ifdef __cplusplus
}
#endif
