#include "include/draw_engine.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static const unsigned char kDemoPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x60, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x01, 0xE5, 0x27, 0xD4, 0xA2, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

int main() {
  if (init_draw_engine(0) != 0) {
    fprintf(stderr, "init_draw_engine failed\n");
    return 1;
  }
  if (init_draw_windows("SystemProfilerDemo", 1) != 0) {
    fprintf(stderr, "init_draw_windows failed\n");
    return 1;
  }

  DrawImage* img = draw_load_image_from_memory(kDemoPng, sizeof(kDemoPng));
  uint64_t frame = 0;

  while (true) {
    if (draw_begin_frame() != 0) {
      usleep(1000);
      continue;
    }

    const int w = draw_screen_width();
    const int h = draw_screen_height();

    const int rect_w = (w > 0) ? w / 6 : 0;
    const int rect_h = (h > 0) ? h / 10 : 0;
    const int rect_x = (rect_w > 0) ? static_cast<int>((frame * 4) % (w - rect_w + 1)) : 0;
    const int rect_y = (rect_h > 0) ? static_cast<int>((frame * 2) % (h - rect_h + 1)) : 0;
    draw_rect(rect_x, rect_y, rect_w, rect_h, 1, 0xFF00FF00);
    draw_rect(rect_x, rect_y, rect_w, rect_h, 0, 0xFFFFFFFF);

    const int cx = w / 2;
    const int cy = h / 2;
    const int radius = (h < w ? h : w) / 8;
    draw_circle(cx, cy, radius, 0xFF0000FF);

    draw_line(16, h - 32, w - 16, h - 32, 0xFFFF0000);

    char text[96];
    snprintf(text, sizeof(text), "DEMO FRAME:%" PRIu64, frame);
    draw_text(text, 16, 16, w - 16, 64, 0xFFFFFFFF);

    if (img) {
      int ix = (w > 64) ? (w - 64) : 0;
      int iy = 64;
      draw_image(img, ix, iy);
    }

    draw_end_frame();
    ++frame;
    usleep(8333);
  }

  draw_free_image(img);
  shutdown_draw_engine();
  return 0;
}
