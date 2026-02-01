#include "include/draw_engine.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern "C" void draw_engine_demo_scene(uint64_t frame);

static const unsigned char kDemoPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x60, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x01, 0xE5, 0x27, 0xD4, 0xA2, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

static uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

static uint32_t lcg(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

static bool read_int64(const char* path, int64_t* out_value) {
  if (!path || !out_value) {
    return false;
  }
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return false;
  }
  char buf[64];
  const ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) {
    return false;
  }
  buf[n] = '\0';
  *out_value = atoll(buf);
  return true;
}

static bool read_battery_power_mw(float* out_mw) {
  if (!out_mw) {
    return false;
  }
  const char* current_paths[] = {
      "/sys/class/power_supply/battery/current_now",
      "/sys/class/power_supply/battery/current_avg",
      "/sys/class/power_supply/battery/instantaneous_current",
      nullptr
  };
  const char* voltage_paths[] = {
      "/sys/class/power_supply/battery/voltage_now",
      "/sys/class/power_supply/battery/voltage_avg",
      nullptr
  };
  int64_t current_ua = 0;
  int64_t voltage_uv = 0;
  bool got_current = false;
  bool got_voltage = false;
  for (int i = 0; current_paths[i]; ++i) {
    if (read_int64(current_paths[i], &current_ua)) {
      got_current = true;
      break;
    }
  }
  for (int i = 0; voltage_paths[i]; ++i) {
    if (read_int64(voltage_paths[i], &voltage_uv)) {
      got_voltage = true;
      break;
    }
  }
  if (!got_current || !got_voltage) {
    return false;
  }
  double power_mw =
      (static_cast<double>(current_ua) * static_cast<double>(voltage_uv)) / 1e9;
  if (power_mw < 0.0) {
    power_mw = -power_mw;
  }
  *out_mw = static_cast<float>(power_mw);
  return true;
}

int main() {
  int mode = DRAW_ENGINE_MODE_CPU;
  const char* env_mode = getenv("DRAW_ENGINE_MODE");
  if (env_mode && env_mode[0]) {
    mode = atoi(env_mode);
  }
  int fps = 120;
  const char* env_fps = getenv("DRAW_ENGINE_FPS");
  if (env_fps && env_fps[0]) {
    fps = atoi(env_fps);
  }
  int stress = 0;
  const char* env_stress = getenv("DRAW_ENGINE_STRESS");
  if (env_stress && env_stress[0]) {
    stress = atoi(env_stress);
  }
  int enable_3d = 1;
  const char* env_3d = getenv("DRAW_ENGINE_3D");
  if (env_3d && env_3d[0]) {
    enable_3d = atoi(env_3d) != 0;
  }
  int stress_count = 0;
  if (stress > 0) {
    stress_count = (stress > 1) ? stress : 300;
  }
  int stress_seconds = 0;
  const char* env_stress_seconds = getenv("DRAW_ENGINE_STRESS_DURATION");
  if (env_stress_seconds && env_stress_seconds[0]) {
    stress_seconds = atoi(env_stress_seconds);
  }
  if (stress_seconds <= 0 && stress_count > 0) {
    stress_seconds = 60;
  }
  if (init_draw_engine(mode) != 0) {
    fprintf(stderr, "init_draw_engine failed\n");
    return 1;
  }
  draw_engine_set_fps(fps);
  if (init_draw_windows("SystemProfilerDemo", 1) != 0) {
    fprintf(stderr, "init_draw_windows failed\n");
    return 1;
  }

  DrawImage* img = draw_load_image_from_memory(kDemoPng, sizeof(kDemoPng));
  const uint64_t start_ns = now_ns();
  uint64_t frame = 0;
  uint64_t last_log_ns = start_ns;
  uint64_t acc_draw_ns = 0;
  uint64_t acc_frame_ns = 0;
  uint64_t acc_frames = 0;
  while (true) {
    const uint64_t frame_start_ns = now_ns();
    if (draw_begin_frame() != 0) {
      usleep(1000);
      continue;
    }
    const uint64_t draw_start_ns = now_ns();

    const int w = draw_screen_width();
    const int h = draw_screen_height();

    if (w > 0 && h > 0) {
      draw_rect(0, 0, w, 72, 1, 0x60000000);
      draw_text("GPU 2D HUD", 26, 26, w - 16, 72, 0xFF000000);
      draw_text("GPU 2D HUD", 24, 24, w - 16, 72, 0xFFFFFFFF);
      draw_text("TEXT OK", 24, 48, w - 16, 72, 0xFFFFFFFF);
    }

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
    if (mode == DRAW_ENGINE_MODE_HYBRID) {
      draw_engine_set_sensitive(1);
      draw_text("SENSITIVE", 16, 96, w - 16, 128, 0xFFFFAA00);
      draw_engine_set_sensitive(0);
    }
    draw_text(text, 16, 16, w - 16, 64, 0xFFFFFFFF);

    if (img) {
      int ix = (w > 64) ? (w - 64) : 0;
      int iy = 64;
      draw_image(img, ix, iy);
    }

    if (stress_count > 0) {
      const int safe_w = (w > 1) ? w : 1;
      const int safe_h = (h > 1) ? h : 1;
      uint32_t rng = static_cast<uint32_t>(frame * 1664525u + 1013904223u);
      for (int i = 0; i < stress_count; ++i) {
        const int size = 8 + (lcg(rng) % 24);
        int max_x = safe_w - size;
        int max_y = safe_h - size;
        if (max_x < 1) {
          max_x = 1;
        }
        if (max_y < 1) {
          max_y = 1;
        }
        const int x = static_cast<int>(lcg(rng) % static_cast<uint32_t>(max_x));
        const int y = static_cast<int>(lcg(rng) % static_cast<uint32_t>(max_y));
        const uint8_t b = static_cast<uint8_t>(lcg(rng) & 0xFF);
        const uint8_t g = static_cast<uint8_t>(lcg(rng) & 0xFF);
        const uint8_t r = static_cast<uint8_t>(lcg(rng) & 0xFF);
        const uint32_t color =
            0xFF000000u | (static_cast<uint32_t>(b) << 16) |
            (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);

        switch (i % 6) {
          case 0:
            draw_rect(x, y, size, size, 1, color);
            break;
          case 1:
            draw_rect(x, y, size, size, 0, color);
            break;
          case 2:
            draw_circle(x + size / 2, y + size / 2, size / 2, color);
            break;
          case 3:
            draw_line(x, y, x + size, y + size, color);
            break;
          case 4: {
            char tiny[8];
            snprintf(tiny, sizeof(tiny), "T%d", i % 10);
            draw_text(tiny, x, y, x + 32, y + 16, 0xFFFFFFFF);
            break;
          }
          default:
            if (img) {
              draw_image(img, x, y);
            } else {
              draw_rect(x, y, size, size, 1, color);
            }
            break;
        }
      }
    }

    if (enable_3d) {
      draw_engine_demo_scene(frame);
    }

    const uint64_t draw_cpu_end_ns = now_ns();
    draw_end_frame();
    const uint64_t frame_end_ns = now_ns();
    const uint64_t draw_ns = draw_cpu_end_ns - draw_start_ns;

    ++frame;

    acc_draw_ns += draw_ns;
    acc_frame_ns += (frame_end_ns - frame_start_ns);
    acc_frames += 1;

    if (frame_end_ns - last_log_ns >= 1000000000ull) {
      const double avg_draw_ms =
          acc_frames ? (acc_draw_ns / 1000000.0) / static_cast<double>(acc_frames) : 0.0;
      const double avg_frame_ms =
          acc_frames ? (acc_frame_ns / 1000000.0) / static_cast<double>(acc_frames) : 0.0;
      const double fps = (avg_frame_ms > 0.0) ? (1000.0 / avg_frame_ms) : 0.0;
      float power_mw = 0.0f;
      const bool has_power = read_battery_power_mw(&power_mw);
      if (has_power) {
        fprintf(stderr,
                "HUD stat: avg_draw=%.3f ms avg_frame=%.3f ms fps=%.1f power=%.1f mW\n",
                avg_draw_ms, avg_frame_ms, fps, power_mw);
      } else {
        fprintf(stderr,
                "HUD stat: avg_draw=%.3f ms avg_frame=%.3f ms fps=%.1f power=NA\n",
                avg_draw_ms, avg_frame_ms, fps);
      }
      acc_draw_ns = 0;
      acc_frame_ns = 0;
      acc_frames = 0;
      last_log_ns = frame_end_ns;
    }

    if (stress_seconds > 0) {
      const uint64_t elapsed_ns = frame_end_ns - start_ns;
      if (elapsed_ns >= static_cast<uint64_t>(stress_seconds) * 1000000000ull) {
        fprintf(stderr, "HUD stress done: %d seconds\n", stress_seconds);
        break;
      }
    }
  }

  draw_free_image(img);
  shutdown_draw_engine();
  return 0;
}
