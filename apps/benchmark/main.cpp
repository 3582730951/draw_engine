#include "midraw.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

static int env_int(const char* name, int default_value) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return default_value;
  }
  return atoi(value);
}

static bool env_flag(const char* name) {
  const char* value = getenv(name);
  if (!value) {
    return false;
  }
  return value[0] != '0';
}

static void busy_wait_ns(uint64_t ns) {
  const uint64_t start = now_ns();
  volatile uint32_t spin = 0;
  while (now_ns() - start < ns) {
    spin = spin * 1664525u + 1013904223u;
  }
  (void)spin;
}

static bool read_proc_times(uint64_t* utime, uint64_t* stime) {
  if (!utime || !stime) {
    return false;
  }
  FILE* fp = fopen("/proc/self/stat", "r");
  if (!fp) {
    return false;
  }
  char buf[2048];
  if (!fgets(buf, sizeof(buf), fp)) {
    fclose(fp);
    return false;
  }
  fclose(fp);
  char* right = strrchr(buf, ')');
  if (!right) {
    return false;
  }
  char* p = right + 2; // skip ") "
  int field = 3;
  char* saveptr = nullptr;
  char* token = strtok_r(p, " ", &saveptr);
  uint64_t u = 0;
  uint64_t s = 0;
  while (token) {
    if (field == 14) {
      u = static_cast<uint64_t>(strtoull(token, nullptr, 10));
    } else if (field == 15) {
      s = static_cast<uint64_t>(strtoull(token, nullptr, 10));
      break;
    }
    ++field;
    token = strtok_r(nullptr, " ", &saveptr);
  }
  if (u == 0 && s == 0) {
    return false;
  }
  *utime = u;
  *stime = s;
  return true;
}

static inline uint32_t lcg(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

static void draw_stress_shapes(MidrawContext* ctx,
                               int w,
                               int h,
                               int region_x,
                               int region_y,
                               int region_w,
                               int region_h,
                               uint64_t frame,
                               int shape_count) {
  if (!ctx || w <= 0 || h <= 0 || shape_count <= 0) {
    return;
  }
  int rx = region_x;
  int ry = region_y;
  int rw = (region_w > 0) ? region_w : w;
  int rh = (region_h > 0) ? region_h : h;
  if (rx < 0) {
    rx = 0;
  }
  if (ry < 0) {
    ry = 0;
  }
  if (rx >= w) {
    rx = 0;
  }
  if (ry >= h) {
    ry = 0;
  }
  if (rw > w - rx) {
    rw = w - rx;
  }
  if (rh > h - ry) {
    rh = h - ry;
  }
  if (rw <= 0 || rh <= 0) {
    return;
  }
  const int max_w = rw;
  const int max_h = rh;
  static int shape_min = -1;
  static int shape_max = -1;
  if (shape_min < 0 || shape_max < 0) {
    shape_min = env_int("MIDRAW_SHAPE_MIN", 6);
    shape_max = env_int("MIDRAW_SHAPE_MAX", 96);
    if (shape_min < 1) {
      shape_min = 1;
    }
    if (shape_max < shape_min) {
      shape_max = shape_min;
    }
  }
  const int max_dim = (max_w < max_h) ? max_w : max_h;
  int max_size = shape_max;
  if (max_size > max_dim) {
    max_size = max_dim;
  }
  if (max_size < shape_min) {
    max_size = shape_min;
  }

  uint32_t seed = 0x5a5a5a5au ^ static_cast<uint32_t>(frame);
  for (int i = 0; i < shape_count; ++i) {
    uint32_t r = lcg(seed);
    const int type = static_cast<int>(r % 3u);
    if (type == 0) {
      int rw = shape_min + static_cast<int>(lcg(seed) % (static_cast<uint32_t>(max_size - shape_min + 1)));
      int rh = shape_min + static_cast<int>(lcg(seed) % (static_cast<uint32_t>(max_size - shape_min + 1)));
      if (rw > max_w) {
        rw = max_w;
      }
      if (rh > max_h) {
        rh = max_h;
      }
      const int x = rx + ((max_w > rw) ? static_cast<int>(lcg(seed) % (max_w - rw)) : 0);
      const int y = ry + ((max_h > rh) ? static_cast<int>(lcg(seed) % (max_h - rh)) : 0);
      const uint32_t color = 0xFF000000u | (lcg(seed) & 0x00FFFFFFu);
      midraw_draw_rect(ctx, x, y, rw, rh, (i & 1), color);
    } else if (type == 1) {
      const int x1 = rx + static_cast<int>(lcg(seed) % max_w);
      const int y1 = ry + static_cast<int>(lcg(seed) % max_h);
      const int x2 = rx + static_cast<int>(lcg(seed) % max_w);
      const int y2 = ry + static_cast<int>(lcg(seed) % max_h);
      const uint32_t color = 0xFF000000u | (lcg(seed) & 0x00FFFFFFu);
      midraw_draw_line(ctx, x1, y1, x2, y2, color);
    } else {
      int max_r = max_size / 2;
      if (max_r < 2) {
        max_r = 2;
      }
      int radius = (shape_min / 2) + static_cast<int>(lcg(seed) % static_cast<uint32_t>(max_r));
      if (radius < 2) {
        radius = 2;
      }
      const int cx = rx + static_cast<int>(lcg(seed) % max_w);
      const int cy = ry + static_cast<int>(lcg(seed) % max_h);
      const uint32_t color = 0xFF000000u | (lcg(seed) & 0x00FFFFFFu);
      midraw_draw_circle(ctx, cx, cy, radius, color);
    }
  }
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

  int target_fps = env_int("MIDRAW_FPS", 120);
  if (target_fps <= 0) {
    target_fps = 120;
  }
  int active_ms = env_int("MIDRAW_ACTIVE_MS", 4);
  if (active_ms < 0) {
    active_ms = 0;
  }
  const uint64_t frame_ns = static_cast<uint64_t>(1000000000ull / target_fps);
  const uint64_t active_ns = static_cast<uint64_t>(active_ms) * 1000000ull;
  const int log_interval = env_int("MIDRAW_LOG_INTERVAL", 120);
  const bool stress_mode = env_flag("MIDRAW_STRESS");
  int shape_count = env_int("MIDRAW_SHAPES", 0);
  if (stress_mode && shape_count <= 0) {
    shape_count = 300;
  }
  const int region_x = env_int("MIDRAW_REGION_X", 0);
  const int region_y = env_int("MIDRAW_REGION_Y", 0);
  const int region_w = env_int("MIDRAW_REGION_W", 0);
  const int region_h = env_int("MIDRAW_REGION_H", 0);

  uint64_t frame = 0;
  uint64_t last_log_time = now_ns();
  uint64_t last_utime = 0;
  uint64_t last_stime = 0;
  const long ticks_per_sec = sysconf(_SC_CLK_TCK);
  read_proc_times(&last_utime, &last_stime);

  while (true) {
    const uint64_t frame_start = now_ns();

    if (midraw_lock(ctx) != 0) {
      usleep(1000);
      continue;
    }

    const int w = midraw_logical_width(ctx);
    const int h = midraw_logical_height(ctx);

    if (shape_count > 0) {
      draw_stress_shapes(ctx, w, h, region_x, region_y, region_w, region_h, frame,
                         shape_count);
    } else {
      const int rect_w = w / 4;
      const int rect_h = h / 8;
      const int x = (rect_w > 0) ? static_cast<int>((frame * 4) % (w - rect_w + 1)) : 0;
      const int y = (rect_h > 0) ? static_cast<int>((frame * 2) % (h - rect_h + 1)) : 0;
      midraw_draw_rect(ctx, x, y, rect_w, rect_h, 1, 0xFF00FF00);
      midraw_draw_rect(ctx, x, y, rect_w, rect_h, 0, 0xFFFFFFFF);

      const int radius = (h < w ? h : w) / 6;
      midraw_draw_circle(ctx, w / 2, h / 2, radius, 0xFFFF0000);
    }

    int text_x = 16;
    int text_y = 16;
    if (region_w > 0 || region_h > 0) {
      int rx = region_x;
      int ry = region_y;
      if (rx < 0) {
        rx = 0;
      }
      if (ry < 0) {
        ry = 0;
      }
      text_x = rx + 16;
      text_y = ry + 16;
    }

    char text[96];
    snprintf(text, sizeof(text), "FRAME:%" PRIu64 " SHAPES:%d", frame, shape_count);
    midraw_draw_text(ctx, text, text_x, text_y, 0xFFFFFFFF);

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

    if (log_interval > 0 && (frame % static_cast<uint64_t>(log_interval)) == 0) {
      double cpu_pct = 0.0;
      uint64_t now = now_ns();
      uint64_t cur_utime = 0;
      uint64_t cur_stime = 0;
      if (ticks_per_sec > 0 && read_proc_times(&cur_utime, &cur_stime)) {
        const uint64_t delta_ticks = (cur_utime - last_utime) + (cur_stime - last_stime);
        const double cpu_ns = (static_cast<double>(delta_ticks) * 1000000000.0) /
                              static_cast<double>(ticks_per_sec);
        const double wall_ns = static_cast<double>(now - last_log_time);
        if (wall_ns > 0.0) {
          cpu_pct = (cpu_ns / wall_ns) * 100.0;
        }
        last_utime = cur_utime;
        last_stime = cur_stime;
        last_log_time = now;
      }
      fprintf(stderr,
              "DrawTime: %.3fms BusyWait: %.3fms CPU: %.1f%% Shapes:%d\n",
              draw_ns / 1000000.0,
              busy_ns / 1000000.0,
              cpu_pct,
              shape_count);
    }

    ++frame;
  }

  midraw_shutdown(ctx);
  return 0;
}
