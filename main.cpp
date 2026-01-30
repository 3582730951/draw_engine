#include "include/midraw.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

static void busy_wait_ns(uint64_t ns) {
  const uint64_t start = now_ns();
  volatile uint32_t spin = 0;
  while (now_ns() - start < ns) {
    spin = spin * 1664525u + 1013904223u;
  }
  (void)spin;
}

int main(int argc, char** argv) {
  int rotation = 0;
  int32_t width = 0;
  int32_t height = 0;
  const char* font_path = nullptr;
  int font_size = 0;
  if (argc > 1) {
    rotation = atoi(argv[1]);
  }
  if (argc > 2) {
    width = atoi(argv[2]);
  }
  if (argc > 3) {
    height = atoi(argv[3]);
  }
  if (argc > 4) {
    font_path = argv[4];
  }
  if (argc > 5) {
    font_size = atoi(argv[5]);
  }
  if (width <= 0 || height <= 0) {
    width = 0;
    height = 0;
  }

  MidrawConfig config{};
  config.surface_name = "Benchmark_Layer";
  config.rotation = rotation;
  config.width = width;
  config.height = height;
  config.font_path = font_path;
  config.font_size = font_size;
  config.atlas_size = 512;

  MidrawContext* ctx = nullptr;
  if (midraw_init(&ctx, &config) != 0) {
    fprintf(stderr, "midraw_init failed\n");
    return 1;
  }

  const uint64_t frame_ns = 8333333ull;  // ~120 FPS
  const uint64_t active_ns = 4000000ull; // target active time (~4ms)

  uint64_t frame = 0;
  while (true) {
    const uint64_t frame_start = now_ns();

    if (midraw_lock(ctx) != 0) {
      usleep(1000);
      continue;
    }

    const int w = midraw_logical_width(ctx);
    const int h = midraw_logical_height(ctx);

    const int rect_w = w / 4;
    const int rect_h = h / 8;
    const int x = (rect_w > 0) ? static_cast<int>((frame * 4) % (w - rect_w + 1)) : 0;
    const int y = (rect_h > 0) ? static_cast<int>((frame * 2) % (h - rect_h + 1)) : 0;
    midraw_draw_rect(ctx, x, y, rect_w, rect_h, 1, 0xFF00FF00);
    midraw_draw_rect(ctx, x, y, rect_w, rect_h, 0, 0xFFFFFFFF);

    const int radius = (h < w ? h : w) / 6;
    midraw_draw_circle(ctx, w / 2, h / 2, radius, 0xFFFF0000);

    char text[64];
    snprintf(text, sizeof(text), "FRAME:%" PRIu64, frame);
    midraw_draw_text(ctx, text, 16, 16, 0xFFFFFFFF);

    midraw_unlock_post(ctx);

    const uint64_t draw_end = now_ns();
    const uint64_t draw_ns = draw_end - frame_start;
    uint64_t busy_ns = 0;
    if (draw_ns < active_ns) {
      busy_ns = active_ns - draw_ns;
      busy_wait_ns(busy_ns);
    }

    const uint64_t after_busy = now_ns();
    const uint64_t elapsed = after_busy - frame_start;
    if (elapsed < frame_ns) {
      usleep(static_cast<useconds_t>((frame_ns - elapsed) / 1000ull));
    }

    if ((frame % 120) == 0) {
      fprintf(stderr, "DrawTime: %.3fms BusyWait: %.3fms\n", draw_ns / 1000000.0,
              busy_ns / 1000000.0);
    }

    ++frame;
  }

  midraw_shutdown(ctx);
  return 0;
}
