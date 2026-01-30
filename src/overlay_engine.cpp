#include "generated_symbols.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

// Forward declarations to avoid compile-time linkage to SurfaceControl headers.
struct ASurfaceControl;
struct ASurfaceTransaction;

using PFN_ASurfaceControl_create = ASurfaceControl* (*)(const char* name, ASurfaceControl* parent);
using PFN_ASurfaceControl_release = void (*)(ASurfaceControl* control);
using PFN_ASurfaceTransaction_create = ASurfaceTransaction* (*)();
using PFN_ASurfaceTransaction_release = void (*)(ASurfaceTransaction* transaction);
using PFN_ASurfaceTransaction_setBufferSize = void (*)(ASurfaceTransaction* transaction,
                                                     ASurfaceControl* control,
                                                     int32_t width,
                                                     int32_t height);
using PFN_ASurfaceTransaction_setVisibility = void (*)(ASurfaceTransaction* transaction,
                                                      ASurfaceControl* control,
                                                      int32_t visibility);
using PFN_ASurfaceTransaction_setLayer = void (*)(ASurfaceTransaction* transaction,
                                                 ASurfaceControl* control,
                                                 int32_t layer);
using PFN_ASurfaceTransaction_apply = void (*)(ASurfaceTransaction* transaction);
using PFN_ASurfaceTransaction_setAlpha = void (*)(ASurfaceTransaction* transaction,
                                                  ASurfaceControl* control,
                                                  float alpha);
using PFN_ASurfaceTransaction_setOpaque = void (*)(ASurfaceTransaction* transaction,
                                                   ASurfaceControl* control,
                                                   int32_t opaque);
using PFN_ANativeWindow_fromSurfaceControl = ANativeWindow* (*)(ASurfaceControl* control);
using PFN_ANativeWindow_lock = int32_t (*)(ANativeWindow* window,
                                          ANativeWindow_Buffer* outBuffer,
                                          ARect* inOutDirtyBounds);
using PFN_ANativeWindow_unlockAndPost = int32_t (*)(ANativeWindow* window);
using PFN_ANativeWindow_release = void (*)(ANativeWindow* window);
using PFN_ANativeWindow_setBuffersGeometry = int32_t (*)(ANativeWindow* window,
                                                        int32_t width,
                                                        int32_t height,
                                                        int32_t format);

struct SpObject {
  void* ptr;
};

struct String8Storage {
  alignas(void*) unsigned char data[64];
};

using PFN_String8_ctor = void (*)(void* self, const char* str);
using PFN_String8_dtor = void (*)(void* self);
using PFN_SurfaceComposerClient_getDefault = SpObject (*)();
using PFN_SurfaceComposerClient_openGlobalTransaction = void (*)();
using PFN_SurfaceComposerClient_closeGlobalTransaction = void (*)();
using PFN_SurfaceComposerClient_createSurfaceChecked_v1 =
    int32_t (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                SpObject* out_surface,
                uint32_t flags,
                void* parent,
                int32_t window_type,
                int32_t owner_uid);
using PFN_SurfaceControl_getSurface = SpObject (*)(void* control);
using PFN_SurfaceControl_setLayer = int32_t (*)(void* control, int32_t layer);
using PFN_SurfaceControl_setPosition = int32_t (*)(void* control, float x, float y);
using PFN_SurfaceControl_setSize = int32_t (*)(void* control, uint32_t w, uint32_t h);
using PFN_SurfaceControl_setAlpha = int32_t (*)(void* control, float alpha);
using PFN_SurfaceControl_setFlags = int32_t (*)(void* control, uint32_t flags, uint32_t mask);

struct EngineSymbols {
  void* libgui = nullptr;
  void* libandroid = nullptr;
  void* libutils = nullptr;

  PFN_ASurfaceControl_create ASurfaceControl_create = nullptr;
  PFN_ASurfaceControl_release ASurfaceControl_release = nullptr;
  PFN_ASurfaceTransaction_create ASurfaceTransaction_create = nullptr;
  PFN_ASurfaceTransaction_release ASurfaceTransaction_release = nullptr;
  PFN_ASurfaceTransaction_setBufferSize ASurfaceTransaction_setBufferSize = nullptr;
  PFN_ASurfaceTransaction_setVisibility ASurfaceTransaction_setVisibility = nullptr;
  PFN_ASurfaceTransaction_setLayer ASurfaceTransaction_setLayer = nullptr;
  PFN_ASurfaceTransaction_apply ASurfaceTransaction_apply = nullptr;
  PFN_ASurfaceTransaction_setAlpha ASurfaceTransaction_setAlpha = nullptr;
  PFN_ASurfaceTransaction_setOpaque ASurfaceTransaction_setOpaque = nullptr;
  PFN_ANativeWindow_fromSurfaceControl ANativeWindow_fromSurfaceControl = nullptr;
  PFN_ANativeWindow_lock ANativeWindow_lock = nullptr;
  PFN_ANativeWindow_unlockAndPost ANativeWindow_unlockAndPost = nullptr;
  PFN_ANativeWindow_release ANativeWindow_release = nullptr;
  PFN_ANativeWindow_setBuffersGeometry ANativeWindow_setBuffersGeometry = nullptr;

  PFN_String8_ctor String8_ctor = nullptr;
  PFN_String8_dtor String8_dtor = nullptr;
  PFN_SurfaceComposerClient_getDefault SurfaceComposerClient_getDefault = nullptr;
  PFN_SurfaceComposerClient_openGlobalTransaction SurfaceComposerClient_openGlobalTransaction =
      nullptr;
  PFN_SurfaceComposerClient_closeGlobalTransaction SurfaceComposerClient_closeGlobalTransaction =
      nullptr;
  PFN_SurfaceComposerClient_createSurfaceChecked_v1 SurfaceComposerClient_createSurfaceChecked =
      nullptr;
  PFN_SurfaceControl_getSurface SurfaceControl_getSurface = nullptr;
  PFN_SurfaceControl_setLayer SurfaceControl_setLayer = nullptr;
  PFN_SurfaceControl_setPosition SurfaceControl_setPosition = nullptr;
  PFN_SurfaceControl_setSize SurfaceControl_setSize = nullptr;
  PFN_SurfaceControl_setAlpha SurfaceControl_setAlpha = nullptr;
  PFN_SurfaceControl_setFlags SurfaceControl_setFlags = nullptr;
};

struct EngineState {
  EngineSymbols symbols;
  ANativeWindow* window = nullptr;
  ASurfaceControl* surface = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
  uint32_t* pixels = nullptr;
  ANativeWindow_Buffer buffer{};
};

static int read_sdk_version() {
  char value[PROP_VALUE_MAX] = {};
  int len = __system_property_get("ro.build.version.sdk", value);
  if (len > 0) {
    return atoi(value);
  }
  return 0;
}

static const SymbolVersion* pick_symbol_version(int sdk) {
  if (kSymbolVersionCount == 0) {
    return nullptr;
  }
  const SymbolVersion* best = nullptr;
  for (size_t i = 0; i < kSymbolVersionCount; ++i) {
    const SymbolVersion* cur = &kSymbolVersions[i];
    if (cur->sdk == sdk) {
      return cur;
    }
    if (sdk > 0 && cur->sdk > 0 && cur->sdk <= sdk) {
      if (!best || cur->sdk > best->sdk) {
        best = cur;
      }
    }
  }
  return best ? best : &kSymbolVersions[0];
}

static void* load_symbol_list(void* handle, const SymbolNameList& list, const char* fallback) {
  if (!handle) {
    return nullptr;
  }
  if (list.names && list.count > 0) {
    for (size_t i = 0; i < list.count; ++i) {
      const char* name = list.names[i];
      if (!name || !name[0]) {
        continue;
      }
      dlerror();
      void* sym = dlsym(handle, name);
      if (!dlerror() && sym) {
        return sym;
      }
    }
  }
  if (fallback && fallback[0]) {
    dlerror();
    void* sym = dlsym(handle, fallback);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  return nullptr;
}

static bool legacy_name_is_v1(const char* name) {
  if (!name) {
    return false;
  }
  if (strstr(name, "LayerMetadata") != nullptr) {
    return false;
  }
  if (strstr(name, "IBinder") != nullptr) {
    return false;
  }
  return strstr(name, "SurfaceControl") != nullptr;
}

static void* load_symbol_list_filtered(void* handle,
                                       const SymbolNameList& list,
                                       bool (*filter)(const char*)) {
  if (!handle || !list.names) {
    return nullptr;
  }
  for (size_t i = 0; i < list.count; ++i) {
    const char* name = list.names[i];
    if (!name || !name[0]) {
      continue;
    }
    if (filter && !filter(name)) {
      continue;
    }
    dlerror();
    void* sym = dlsym(handle, name);
    if (!dlerror() && sym) {
      return sym;
    }
  }
  return nullptr;
}

static bool load_symbols(EngineSymbols& s) {
  s.libgui = dlopen("libgui.so", RTLD_NOW);
  if (!s.libgui) {
    fprintf(stderr, "dlopen libgui.so failed: %s\n", dlerror());
    return false;
  }
  s.libandroid = dlopen("libandroid.so", RTLD_NOW);
  if (!s.libandroid) {
    fprintf(stderr, "dlopen libandroid.so failed: %s\n", dlerror());
    return false;
  }
  s.libutils = dlopen("libutils.so", RTLD_NOW);

  const SymbolVersion* version = pick_symbol_version(read_sdk_version());
  const SymbolNameList empty_list{nullptr, 0};

  s.ASurfaceControl_create = reinterpret_cast<PFN_ASurfaceControl_create>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceControl_create : empty_list,
                       "ASurfaceControl_create"));
  s.ASurfaceControl_release = reinterpret_cast<PFN_ASurfaceControl_release>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceControl_release : empty_list,
                       "ASurfaceControl_release"));
  s.ASurfaceTransaction_create = reinterpret_cast<PFN_ASurfaceTransaction_create>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_create : empty_list,
                       "ASurfaceTransaction_create"));
  s.ASurfaceTransaction_release = reinterpret_cast<PFN_ASurfaceTransaction_release>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_release : empty_list,
                       "ASurfaceTransaction_release"));
  s.ASurfaceTransaction_setBufferSize = reinterpret_cast<PFN_ASurfaceTransaction_setBufferSize>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_setBufferSize : empty_list,
                       "ASurfaceTransaction_setBufferSize"));
  s.ASurfaceTransaction_setVisibility =
      reinterpret_cast<PFN_ASurfaceTransaction_setVisibility>(
          load_symbol_list(s.libgui,
                           version ? version->ASurfaceTransaction_setVisibility : empty_list,
                           "ASurfaceTransaction_setVisibility"));
  s.ASurfaceTransaction_setLayer = reinterpret_cast<PFN_ASurfaceTransaction_setLayer>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_setLayer : empty_list,
                       "ASurfaceTransaction_setLayer"));
  s.ASurfaceTransaction_apply = reinterpret_cast<PFN_ASurfaceTransaction_apply>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_apply : empty_list,
                       "ASurfaceTransaction_apply"));
  s.ASurfaceTransaction_setAlpha = reinterpret_cast<PFN_ASurfaceTransaction_setAlpha>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_setAlpha : empty_list,
                       "ASurfaceTransaction_setAlpha"));
  s.ASurfaceTransaction_setOpaque = reinterpret_cast<PFN_ASurfaceTransaction_setOpaque>(
      load_symbol_list(s.libgui,
                       version ? version->ASurfaceTransaction_setOpaque : empty_list,
                       "ASurfaceTransaction_setOpaque"));
  s.ANativeWindow_fromSurfaceControl = reinterpret_cast<PFN_ANativeWindow_fromSurfaceControl>(
      load_symbol_list(s.libgui, empty_list, "ANativeWindow_fromSurfaceControl"));

  s.ANativeWindow_lock = reinterpret_cast<PFN_ANativeWindow_lock>(
      load_symbol_list(s.libandroid, empty_list, "ANativeWindow_lock"));
  s.ANativeWindow_unlockAndPost = reinterpret_cast<PFN_ANativeWindow_unlockAndPost>(
      load_symbol_list(s.libandroid, empty_list, "ANativeWindow_unlockAndPost"));
  s.ANativeWindow_release = reinterpret_cast<PFN_ANativeWindow_release>(
      load_symbol_list(s.libandroid, empty_list, "ANativeWindow_release"));
  s.ANativeWindow_setBuffersGeometry = reinterpret_cast<PFN_ANativeWindow_setBuffersGeometry>(
      load_symbol_list(s.libandroid,
                       version ? version->ANativeWindow_setBuffersGeometry : empty_list,
                       "ANativeWindow_setBuffersGeometry"));

  if (s.libutils) {
    s.String8_ctor = reinterpret_cast<PFN_String8_ctor>(
        load_symbol_list(s.libutils,
                         version ? version->String8_ctor : empty_list,
                         "_ZN7android7String8C1EPKc"));
    if (!s.String8_ctor) {
      s.String8_ctor = reinterpret_cast<PFN_String8_ctor>(
          load_symbol_list(s.libutils,
                           version ? version->String8_ctor : empty_list,
                           "_ZN7android7String8C2EPKc"));
    }
    s.String8_dtor = reinterpret_cast<PFN_String8_dtor>(
        load_symbol_list(s.libutils,
                         version ? version->String8_dtor : empty_list,
                         "_ZN7android7String8D1Ev"));
    if (!s.String8_dtor) {
      s.String8_dtor = reinterpret_cast<PFN_String8_dtor>(
          load_symbol_list(s.libutils,
                           version ? version->String8_dtor : empty_list,
                           "_ZN7android7String8D2Ev"));
    }
  }

  s.SurfaceComposerClient_getDefault =
      reinterpret_cast<PFN_SurfaceComposerClient_getDefault>(
          load_symbol_list(s.libgui,
                           version ? version->SurfaceComposerClient_getDefault : empty_list,
                           "_ZN7android21SurfaceComposerClient10getDefaultEv"));
  s.SurfaceComposerClient_openGlobalTransaction =
      reinterpret_cast<PFN_SurfaceComposerClient_openGlobalTransaction>(
          load_symbol_list(s.libgui,
                           version ? version->SurfaceComposerClient_openGlobalTransaction
                                   : empty_list,
                           "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv"));
  s.SurfaceComposerClient_closeGlobalTransaction =
      reinterpret_cast<PFN_SurfaceComposerClient_closeGlobalTransaction>(
          load_symbol_list(s.libgui,
                           version ? version->SurfaceComposerClient_closeGlobalTransaction
                                   : empty_list,
                           "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEv"));
  s.SurfaceComposerClient_createSurfaceChecked =
      reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v1>(
          load_symbol_list_filtered(s.libgui,
                                    version ? version->SurfaceComposerClient_createSurfaceChecked
                                            : empty_list,
                                    legacy_name_is_v1));

  s.SurfaceControl_getSurface = reinterpret_cast<PFN_SurfaceControl_getSurface>(
      load_symbol_list(s.libgui,
                       version ? version->SurfaceControl_getSurface : empty_list,
                       nullptr));
  s.SurfaceControl_setLayer = reinterpret_cast<PFN_SurfaceControl_setLayer>(
      load_symbol_list(s.libgui,
                       version ? version->SurfaceControl_setLayer : empty_list,
                       nullptr));
  s.SurfaceControl_setPosition = reinterpret_cast<PFN_SurfaceControl_setPosition>(
      load_symbol_list(s.libgui,
                       version ? version->SurfaceControl_setPosition : empty_list,
                       nullptr));
  s.SurfaceControl_setSize = reinterpret_cast<PFN_SurfaceControl_setSize>(
      load_symbol_list(s.libgui,
                       version ? version->SurfaceControl_setSize : empty_list,
                       nullptr));
  s.SurfaceControl_setAlpha = reinterpret_cast<PFN_SurfaceControl_setAlpha>(
      load_symbol_list(s.libgui,
                       version ? version->SurfaceControl_setAlpha : empty_list,
                       nullptr));
  s.SurfaceControl_setFlags = reinterpret_cast<PFN_SurfaceControl_setFlags>(
      load_symbol_list(s.libgui,
                       version ? version->SurfaceControl_setFlags : empty_list,
                       nullptr));

  return true;
}

static bool create_surface_asurface(EngineState& state, int width, int height) {
  EngineSymbols& s = state.symbols;
  if (!s.ASurfaceControl_create || !s.ASurfaceTransaction_create || !s.ANativeWindow_fromSurfaceControl) {
    return false;
  }

  state.surface = s.ASurfaceControl_create("SystemProfiler", nullptr);
  if (!state.surface) {
    return false;
  }

  ASurfaceTransaction* tx = s.ASurfaceTransaction_create();
  if (!tx) {
    return false;
  }

  s.ASurfaceTransaction_setVisibility(tx, state.surface, 1);
  s.ASurfaceTransaction_setLayer(tx, state.surface, INT_MAX);
  if (s.ASurfaceTransaction_setAlpha) {
    s.ASurfaceTransaction_setAlpha(tx, state.surface, 1.0f);
  }
  if (s.ASurfaceTransaction_setOpaque) {
    s.ASurfaceTransaction_setOpaque(tx, state.surface, 0);
  }
  if (s.ASurfaceTransaction_setBufferSize && width > 0 && height > 0) {
    s.ASurfaceTransaction_setBufferSize(tx, state.surface, width, height);
  }
  s.ASurfaceTransaction_apply(tx);
  s.ASurfaceTransaction_release(tx);

  state.window = s.ANativeWindow_fromSurfaceControl(state.surface);
  if (!state.window) {
    return false;
  }

  if (s.ANativeWindow_setBuffersGeometry) {
    s.ANativeWindow_setBuffersGeometry(state.window, width, height, WINDOW_FORMAT_RGBA_8888);
  }
  return true;
}

static bool create_surface_legacy(EngineState& state, int width, int height) {
  EngineSymbols& s = state.symbols;
  if (!s.SurfaceComposerClient_getDefault || !s.SurfaceComposerClient_createSurfaceChecked ||
      !s.SurfaceControl_getSurface || !s.String8_ctor || !s.String8_dtor) {
    return false;
  }

  SpObject client_sp = s.SurfaceComposerClient_getDefault();
  if (!client_sp.ptr) {
    return false;
  }

  String8Storage name_storage{};
  s.String8_ctor(name_storage.data, "SystemProfiler");

  SpObject control_sp{};
  int32_t status = s.SurfaceComposerClient_createSurfaceChecked(
      client_sp.ptr,
      name_storage.data,
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
      WINDOW_FORMAT_RGBA_8888,
      &control_sp,
      0,
      nullptr,
      -1,
      -1);
  s.String8_dtor(name_storage.data);

  if (status != 0 || !control_sp.ptr) {
    return false;
  }

  if (s.SurfaceComposerClient_openGlobalTransaction &&
      s.SurfaceComposerClient_closeGlobalTransaction) {
    s.SurfaceComposerClient_openGlobalTransaction();
  }
  if (s.SurfaceControl_setLayer) {
    s.SurfaceControl_setLayer(control_sp.ptr, INT_MAX);
  }
  if (s.SurfaceControl_setPosition) {
    s.SurfaceControl_setPosition(control_sp.ptr, 0.0f, 0.0f);
  }
  if (s.SurfaceControl_setSize) {
    s.SurfaceControl_setSize(control_sp.ptr, static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height));
  }
  if (s.SurfaceControl_setAlpha) {
    s.SurfaceControl_setAlpha(control_sp.ptr, 1.0f);
  }
  if (s.SurfaceControl_setFlags) {
    const uint32_t kOpaqueMask = 0x00000400u;
    s.SurfaceControl_setFlags(control_sp.ptr, 0, kOpaqueMask);
  }
  if (s.SurfaceComposerClient_openGlobalTransaction &&
      s.SurfaceComposerClient_closeGlobalTransaction) {
    s.SurfaceComposerClient_closeGlobalTransaction();
  }

  SpObject surface_sp = s.SurfaceControl_getSurface(control_sp.ptr);
  if (!surface_sp.ptr) {
    return false;
  }

  state.window = reinterpret_cast<ANativeWindow*>(surface_sp.ptr);
  if (s.ANativeWindow_setBuffersGeometry) {
    s.ANativeWindow_setBuffersGeometry(state.window, width, height, WINDOW_FORMAT_RGBA_8888);
  }
  return true;
}

static bool lock_buffer(EngineState& state) {
  if (!state.window || !state.symbols.ANativeWindow_lock) {
    return false;
  }
  if (state.symbols.ANativeWindow_lock(state.window, &state.buffer, nullptr) != 0) {
    return false;
  }
  state.width = state.buffer.width;
  state.height = state.buffer.height;
  state.stride = state.buffer.stride;
  state.pixels = reinterpret_cast<uint32_t*>(state.buffer.bits);
  return state.pixels != nullptr;
}

static void unlock_post(EngineState& state) {
  if (state.window && state.symbols.ANativeWindow_unlockAndPost) {
    state.symbols.ANativeWindow_unlockAndPost(state.window);
  }
  state.pixels = nullptr;
}

static void clear_rect(uint32_t* pixels, int stride, int x, int y, int w, int h, uint32_t color) {
  for (int yy = 0; yy < h; ++yy) {
    uint32_t* row = pixels + (y + yy) * stride + x;
    for (int xx = 0; xx < w; ++xx) {
      row[xx] = color;
    }
  }
}

static void draw_point(uint32_t* pixels, int stride, int x, int y, uint32_t color) {
  pixels[y * stride + x] = color;
}

static void draw_line(uint32_t* pixels,
                      int stride,
                      int x1,
                      int y1,
                      int x2,
                      int y2,
                      uint32_t color) {
  int dx = abs(x2 - x1);
  int sx = x1 < x2 ? 1 : -1;
  int dy = -abs(y2 - y1);
  int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    draw_point(pixels, stride, x1, y1, color);
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

int main(int argc, char** argv) {
  int width = 1080;
  int height = 2400;
  if (argc > 1) {
    width = atoi(argv[1]);
  }
  if (argc > 2) {
    height = atoi(argv[2]);
  }

  EngineState state{};
  if (!load_symbols(state.symbols)) {
    fprintf(stderr, "load_symbols failed\n");
    return 1;
  }

  if (!create_surface_asurface(state, width, height)) {
    if (!create_surface_legacy(state, width, height)) {
      fprintf(stderr, "create_surface failed\n");
      return 1;
    }
  }

  const int graph_height = height / 6;
  const int graph_width = 240;
  const int graph_x = 16;
  const int graph_y = 16;
  uint16_t samples[256] = {};
  int sample_index = 0;
  uint64_t frame = 0;

  while (true) {
    if (!lock_buffer(state)) {
      usleep(1000);
      continue;
    }

    clear_rect(state.pixels, state.stride, graph_x, graph_y, graph_width, graph_height,
               0x00000000);

    int value = static_cast<int>((frame * 3) % graph_height);
    samples[sample_index] = static_cast<uint16_t>(value);
    sample_index = (sample_index + 1) % 256;

    for (int x = 0; x < graph_width; ++x) {
      int idx = (sample_index + x) % 256;
      int y = graph_y + (graph_height - 1 - samples[idx]);
      if (x + graph_x >= 0 && x + graph_x < state.width && y >= 0 && y < state.height) {
        draw_point(state.pixels, state.stride, graph_x + x, y, 0xFFFFFFFF);
      }
    }

    draw_line(state.pixels, state.stride, graph_x, graph_y + graph_height - 1,
              graph_x + graph_width - 1, graph_y + graph_height - 1, 0x80FFFFFF);

    unlock_post(state);

    ++frame;
    usleep(8333);
  }

  return 0;
}
