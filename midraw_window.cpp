#include "midraw_internal.h"

#include "generated/generated_symbols.h"
#include "include/midraw.h"

#include <android/native_window.h>
#include <cstddef>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <vector>

using PFN_ASurfaceTransaction_setAlpha = void (*)(ASurfaceTransaction* transaction,
                                                  ASurfaceControl* control,
                                                  float alpha);
using PFN_ASurfaceTransaction_setOpaque = void (*)(ASurfaceTransaction* transaction,
                                                   ASurfaceControl* control,
                                                   int32_t opaque);
using PFN_ANativeWindow_setBuffersGeometry = int32_t (*)(ANativeWindow* window,
                                                        int32_t width,
                                                        int32_t height,
                                                        int32_t format);

struct SpObject {
  void* ptr;
  SpObject() : ptr(nullptr) {}
  ~SpObject() {}
};

namespace android {
class IBinder;

template <typename T>
struct sp {
  T* ptr;
  sp() : ptr(nullptr) {}
};

struct PhysicalDisplayId {
  uint64_t value;
};

namespace ui {
struct LayerStack {
  uint32_t id;
};

struct Size {
  int32_t width;
  int32_t height;
};

enum class Rotation : uint32_t {
  Rotation0 = 0,
  Rotation90 = 1,
  Rotation180 = 2,
  Rotation270 = 3,
};

struct DisplayState {
  LayerStack layerStack;
  Rotation orientation;
  Size layerStackSpaceRect;
};
}  // namespace ui
}  // namespace android

static ANativeWindow* surface_to_window(void* surface_ptr) {
  if (!surface_ptr) {
    return nullptr;
  }
  const size_t offset = sizeof(std::max_align_t) / 2;
  return reinterpret_cast<ANativeWindow*>(reinterpret_cast<uintptr_t>(surface_ptr) + offset);
}

struct String8Storage {
  alignas(void*) unsigned char data[64];
};

using PFN_String8_ctor = void (*)(void* self, const char* str);
using PFN_String8_dtor = void (*)(void* self);
using PFN_LayerMetadata_ctor = void (*)(void* self);
using PFN_SurfaceComposerClient_getDefault = SpObject (*)();
using PFN_SurfaceComposerClient_getPhysicalDisplayToken =
    SpObject (*)(android::PhysicalDisplayId display_id);
using PFN_SurfaceComposerClient_getPhysicalDisplayIds =
    std::vector<android::PhysicalDisplayId> (*)();
using PFN_SurfaceComposerClient_getDisplayState =
    int32_t (*)(const android::sp<android::IBinder>& display,
                android::ui::DisplayState* out_state);
using PFN_SurfaceComposerClient_openGlobalTransaction = void (*)();
using PFN_SurfaceComposerClient_closeGlobalTransaction = void (*)();
using PFN_SurfaceComposerClient_createSurface =
    SpObject (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                uint32_t flags,
                const android::sp<android::IBinder>& parent,
                const void* layer_metadata,
                uint32_t* out_transform_hint);
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
using PFN_SurfaceComposerClient_Transaction_ctor = void (*)(void* tx);
using PFN_SurfaceComposerClient_Transaction_setLayer =
    void* (*)(void* tx, void* surface_control, int32_t z);
using PFN_SurfaceComposerClient_Transaction_setPosition =
    void* (*)(void* tx, void* surface_control, float x, float y);
using PFN_SurfaceComposerClient_Transaction_setLayerStack =
    void* (*)(void* tx, void* surface_control, android::ui::LayerStack layer_stack);
using PFN_SurfaceComposerClient_Transaction_show =
    void* (*)(void* tx, void* surface_control);
using PFN_SurfaceComposerClient_Transaction_setTrustedOverlay =
    void* (*)(void* tx, void* surface_control, bool is_trusted);
using PFN_SurfaceComposerClient_Transaction_apply2 =
    int32_t (*)(void* tx, bool synchronous, bool one_way);
using PFN_SurfaceComposerClient_Transaction_apply1 =
    int32_t (*)(void* tx, bool synchronous);

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

static bool env_truthy(const char* name) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return false;
  }
  return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
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

static void* load_symbol_list(void* handle,
                              const SymbolNameList& list,
                              const char* fallback) {
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

static bool create_window_asurface(AndroidSymbols& symbols,
                                   RenderContext& render,
                                   int32_t requested_width,
                                   int32_t requested_height,
                                   const MidrawConfig* config) {
  const char* name = (config && config->surface_name) ? config->surface_name : "Benchmark_Layer";
  render.surface = symbols.ASurfaceControl_create(name, nullptr);
  if (!render.surface) {
    fprintf(stderr, "ASurfaceControl_create failed\n");
    return false;
  }

  const SymbolVersion* version = pick_symbol_version(read_sdk_version());
  const SymbolNameList empty_list{nullptr, 0};
  const SymbolNameList alpha_list =
      version ? version->ASurfaceTransaction_setAlpha : empty_list;
  const SymbolNameList opaque_list =
      version ? version->ASurfaceTransaction_setOpaque : empty_list;
  const SymbolNameList geometry_list =
      version ? version->ANativeWindow_setBuffersGeometry : empty_list;

  PFN_ASurfaceTransaction_setAlpha set_alpha =
      reinterpret_cast<PFN_ASurfaceTransaction_setAlpha>(
          load_symbol_list(symbols.libgui, alpha_list, "ASurfaceTransaction_setAlpha"));
  PFN_ASurfaceTransaction_setOpaque set_opaque =
      reinterpret_cast<PFN_ASurfaceTransaction_setOpaque>(
          load_symbol_list(symbols.libgui, opaque_list, "ASurfaceTransaction_setOpaque"));

  ASurfaceTransaction* transaction = symbols.ASurfaceTransaction_create();
  if (!transaction) {
    fprintf(stderr, "ASurfaceTransaction_create failed\n");
    return false;
  }

  symbols.ASurfaceTransaction_setVisibility(transaction, render.surface, 1);
  symbols.ASurfaceTransaction_setLayer(transaction, render.surface, 0x7fffffff);
  if (set_alpha) {
    set_alpha(transaction, render.surface, 1.0f);
  }
  if (set_opaque) {
    set_opaque(transaction, render.surface, 0);
  }
  if (requested_width > 0 && requested_height > 0) {
    symbols.ASurfaceTransaction_setBufferSize(transaction, render.surface, requested_width,
                                              requested_height);
  }
  symbols.ASurfaceTransaction_apply(transaction);
  symbols.ASurfaceTransaction_release(transaction);

  render.window = symbols.ANativeWindow_fromSurfaceControl(render.surface);
  if (!render.window) {
    fprintf(stderr, "ANativeWindow_fromSurfaceControl failed\n");
    return false;
  }

  int32_t target_width = requested_width;
  int32_t target_height = requested_height;
  if (target_width <= 0 || target_height <= 0) {
    if (symbols.ANativeWindow_getWidth && symbols.ANativeWindow_getHeight) {
      const int32_t w = symbols.ANativeWindow_getWidth(render.window);
      const int32_t h = symbols.ANativeWindow_getHeight(render.window);
      if (w > 0 && h > 0) {
        target_width = w;
        target_height = h;
      }
    }
  }
  if (target_width <= 0 || target_height <= 0) {
    ANativeWindow_Buffer probe{};
    if (symbols.ANativeWindow_lock(render.window, &probe, nullptr) == 0) {
      if (probe.width > 0 && probe.height > 0) {
        target_width = probe.width;
        target_height = probe.height;
      }
      symbols.ANativeWindow_unlockAndPost(render.window);
    }
  }
  if (target_width <= 0 || target_height <= 0) {
    target_width = 1080;
    target_height = 1920;
  }

  if (requested_width <= 0 || requested_height <= 0) {
    ASurfaceTransaction* resize_tx = symbols.ASurfaceTransaction_create();
    if (resize_tx) {
      symbols.ASurfaceTransaction_setBufferSize(resize_tx, render.surface, target_width,
                                                target_height);
      symbols.ASurfaceTransaction_setVisibility(resize_tx, render.surface, 1);
      symbols.ASurfaceTransaction_setLayer(resize_tx, render.surface, 0x7fffffff);
      if (set_alpha) {
        set_alpha(resize_tx, render.surface, 1.0f);
      }
      if (set_opaque) {
        set_opaque(resize_tx, render.surface, 0);
      }
      symbols.ASurfaceTransaction_apply(resize_tx);
      symbols.ASurfaceTransaction_release(resize_tx);
    }
  }

  PFN_ANativeWindow_setBuffersGeometry set_geometry =
      reinterpret_cast<PFN_ANativeWindow_setBuffersGeometry>(load_symbol_list(
          symbols.libandroid, geometry_list, "ANativeWindow_setBuffersGeometry"));
  if (set_geometry) {
    set_geometry(render.window, target_width, target_height, WINDOW_FORMAT_RGBA_8888);
  }

  return true;
}

struct LayerMetadataStorage {
  alignas(void*) unsigned char data[1024];
};

struct TransactionStorage {
  alignas(void*) unsigned char data[1024];
};

static void init_layer_metadata(PFN_LayerMetadata_ctor ctor, LayerMetadataStorage& storage) {
  memset(storage.data, 0, sizeof(storage.data));
  if (ctor) {
    ctor(storage.data);
  }
}

static bool create_window_osimgui(AndroidSymbols& symbols,
                                  RenderContext& render,
                                  int32_t requested_width,
                                  int32_t requested_height,
                                  const MidrawConfig* config) {
  if (!symbols.libgui) {
    return false;
  }
  void* libutils = dlopen("libutils.so", RTLD_NOW);
  if (!libutils) {
    fprintf(stderr, "dlopen libutils.so failed\n");
    return false;
  }

  const char* name = (config && config->surface_name) ? config->surface_name : "Benchmark_Layer";

  PFN_String8_ctor string8_ctor = reinterpret_cast<PFN_String8_ctor>(
      load_symbol_list(libutils, SymbolNameList{nullptr, 0}, "_ZN7android7String8C1EPKc"));
  if (!string8_ctor) {
    string8_ctor = reinterpret_cast<PFN_String8_ctor>(
        load_symbol_list(libutils, SymbolNameList{nullptr, 0}, "_ZN7android7String8C2EPKc"));
  }
  PFN_String8_dtor string8_dtor = reinterpret_cast<PFN_String8_dtor>(
      load_symbol_list(libutils, SymbolNameList{nullptr, 0}, "_ZN7android7String8D1Ev"));
  if (!string8_dtor) {
    string8_dtor = reinterpret_cast<PFN_String8_dtor>(
        load_symbol_list(libutils, SymbolNameList{nullptr, 0}, "_ZN7android7String8D2Ev"));
  }

  static const char* kLayerMetadataCtorNames[] = {
      "_ZN7android3gui13LayerMetadataC2Ev",
      "_ZN7android13LayerMetadataC2Ev",
  };
  const SymbolNameList layer_ctor_list{
      kLayerMetadataCtorNames,
      sizeof(kLayerMetadataCtorNames) / sizeof(kLayerMetadataCtorNames[0])};
  PFN_LayerMetadata_ctor layer_ctor = reinterpret_cast<PFN_LayerMetadata_ctor>(
      load_symbol_list(symbols.libgui, layer_ctor_list, nullptr));

  static const char* kSurfaceComposerCreateSurfaceNames[] = {
      "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj",
      "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj",
      "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataEPj",
      "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataE",
      "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEii",
  };
  const SymbolNameList create_surface_list{
      kSurfaceComposerCreateSurfaceNames,
      sizeof(kSurfaceComposerCreateSurfaceNames) /
          sizeof(kSurfaceComposerCreateSurfaceNames[0])};
  PFN_SurfaceComposerClient_createSurface create_surface =
      reinterpret_cast<PFN_SurfaceComposerClient_createSurface>(
          load_symbol_list(symbols.libgui, create_surface_list, nullptr));

  PFN_SurfaceComposerClient_getDefault get_default =
      reinterpret_cast<PFN_SurfaceComposerClient_getDefault>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient10getDefaultEv"));

  static const char* kSurfaceControlGetSurfaceNames[] = {
      "_ZN7android14SurfaceControl10getSurfaceEv",
      "_ZNK7android14SurfaceControl10getSurfaceEv",
  };
  const SymbolNameList get_surface_list{
      kSurfaceControlGetSurfaceNames,
      sizeof(kSurfaceControlGetSurfaceNames) / sizeof(kSurfaceControlGetSurfaceNames[0])};
  PFN_SurfaceControl_getSurface get_surface =
      reinterpret_cast<PFN_SurfaceControl_getSurface>(
          load_symbol_list(symbols.libgui, get_surface_list, nullptr));

  PFN_SurfaceComposerClient_getPhysicalDisplayToken get_display_token =
      reinterpret_cast<PFN_SurfaceComposerClient_getPhysicalDisplayToken>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE"));
  PFN_SurfaceComposerClient_getPhysicalDisplayIds get_display_ids =
      reinterpret_cast<PFN_SurfaceComposerClient_getPhysicalDisplayIds>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv"));
  PFN_SurfaceComposerClient_getDisplayState get_display_state =
      reinterpret_cast<PFN_SurfaceComposerClient_getDisplayState>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE"));

  PFN_SurfaceComposerClient_openGlobalTransaction open_tx =
      reinterpret_cast<PFN_SurfaceComposerClient_openGlobalTransaction>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv"));
  PFN_SurfaceComposerClient_closeGlobalTransaction close_tx =
      reinterpret_cast<PFN_SurfaceComposerClient_closeGlobalTransaction>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEv"));
  PFN_SurfaceComposerClient_Transaction_ctor tx_ctor =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_ctor>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11TransactionC2Ev"));
  PFN_SurfaceComposerClient_Transaction_setLayer tx_set_layer =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_setLayer>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi"));
  PFN_SurfaceComposerClient_Transaction_setPosition tx_set_pos =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_setPosition>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff"));
  PFN_SurfaceComposerClient_Transaction_setLayerStack tx_set_layer_stack =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_setLayerStack>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE"));
  PFN_SurfaceComposerClient_Transaction_show tx_show =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_show>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE"));
  PFN_SurfaceComposerClient_Transaction_setTrustedOverlay tx_set_trusted =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_setTrustedOverlay>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb"));
  PFN_SurfaceComposerClient_Transaction_apply2 tx_apply2 =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_apply2>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction5applyEbb"));
  PFN_SurfaceComposerClient_Transaction_apply1 tx_apply1 =
      reinterpret_cast<PFN_SurfaceComposerClient_Transaction_apply1>(
          load_symbol_list(symbols.libgui,
                           SymbolNameList{nullptr, 0},
                           "_ZN7android21SurfaceComposerClient11Transaction5applyEb"));

  if (!string8_ctor || !string8_dtor || !get_default || !create_surface || !get_surface) {
    dlclose(libutils);
    return false;
  }

  const int sdk = read_sdk_version();
  SpObject client_sp = get_default();
  if (!client_sp.ptr) {
    dlclose(libutils);
    return false;
  }

  String8Storage name_storage{};
  string8_ctor(name_storage.data, name);

  LayerMetadataStorage metadata_storage{};
  init_layer_metadata(layer_ctor, metadata_storage);

  android::sp<android::IBinder> display_token{};
  android::ui::LayerStack display_layer_stack{0xFFFFFFFFu};
  int display_width = 0;
  int display_height = 0;
  if (get_display_token) {
    android::PhysicalDisplayId display_id{0};
    SpObject token_sp = get_display_token(display_id);
    display_token.ptr = reinterpret_cast<android::IBinder*>(token_sp.ptr);
    if (!display_token.ptr && get_display_ids) {
      std::vector<android::PhysicalDisplayId> ids = get_display_ids();
      if (!ids.empty()) {
        SpObject token2 = get_display_token(ids.front());
        display_token.ptr = reinterpret_cast<android::IBinder*>(token2.ptr);
      }
    }
  }
  if (display_token.ptr && get_display_state) {
    android::ui::DisplayState display_state{};
    if (get_display_state(display_token, &display_state) == 0) {
      display_layer_stack = display_state.layerStack;
      display_width = display_state.layerStackSpaceRect.width;
      display_height = display_state.layerStackSpaceRect.height;
    }
  }
  const char* layer_env = getenv("MIDRAW_LAYERSTACK");
  if (layer_env && layer_env[0]) {
    char* endptr = nullptr;
    unsigned long val = strtoul(layer_env, &endptr, 0);
    if (endptr != layer_env) {
      display_layer_stack.id = static_cast<uint32_t>(val);
    }
  }

  bool use_display_parent = sdk < 34;
  const char* parent_env = getenv("MIDRAW_PARENT_DISPLAY");
  if (parent_env) {
    use_display_parent = parent_env[0] != '0';
  }
  android::sp<android::IBinder> parent_handle{};
  if (use_display_parent) {
    parent_handle = display_token;
  }

  bool use_display_size = sdk >= 34;
  const char* size_env = getenv("MIDRAW_USE_DISPLAY_SIZE");
  if (size_env) {
    use_display_size = size_env[0] != '0';
  }
  int request_width = requested_width;
  int request_height = requested_height;
  if ((request_width <= 0 || request_height <= 0) && use_display_size &&
      display_width > 0 && display_height > 0) {
    request_width = display_width;
    request_height = display_height;
  }
  if (request_width <= 0 || request_height <= 0) {
    request_width = 1080;
    request_height = 1920;
  }

  SpObject control_sp = create_surface(client_sp.ptr,
                                       name_storage.data,
                                       static_cast<uint32_t>(request_width),
                                       static_cast<uint32_t>(request_height),
                                       WINDOW_FORMAT_RGBA_8888,
                                       0,
                                       parent_handle,
                                       metadata_storage.data,
                                       nullptr);
  string8_dtor(name_storage.data);
  dlclose(libutils);

  if (!control_sp.ptr) {
    return false;
  }

  if (open_tx && close_tx) {
    open_tx();
  }
  if (tx_ctor) {
    TransactionStorage tx_storage{};
    tx_ctor(tx_storage.data);
    if (tx_set_layer) {
      tx_set_layer(tx_storage.data, &control_sp, INT_MAX);
    }
    if (tx_set_pos) {
      tx_set_pos(tx_storage.data, &control_sp, 0.0f, 0.0f);
    }
    if (tx_set_layer_stack) {
      tx_set_layer_stack(tx_storage.data, &control_sp, display_layer_stack);
    }
    if (tx_show) {
      tx_show(tx_storage.data, &control_sp);
    }
    if (sdk >= 31 && tx_set_trusted) {
      tx_set_trusted(tx_storage.data, &control_sp, true);
    }
    if (tx_apply2) {
      tx_apply2(tx_storage.data, false, true);
    } else if (tx_apply1) {
      tx_apply1(tx_storage.data, false);
    }
  }
  if (open_tx && close_tx) {
    close_tx();
  }

  SpObject surface_sp = get_surface(control_sp.ptr);
  if (!surface_sp.ptr) {
    return false;
  }

  render.window = surface_to_window(surface_sp.ptr);
  if (!render.window) {
    render.window = reinterpret_cast<ANativeWindow*>(surface_sp.ptr);
  }
  render.surface = nullptr;

  PFN_ANativeWindow_setBuffersGeometry set_geometry =
      reinterpret_cast<PFN_ANativeWindow_setBuffersGeometry>(
          load_symbol_list(symbols.libandroid,
                           SymbolNameList{nullptr, 0},
                           "ANativeWindow_setBuffersGeometry"));
  if (set_geometry) {
    set_geometry(render.window, request_width, request_height, WINDOW_FORMAT_RGBA_8888);
  }

  return true;
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

static bool create_window_legacy(AndroidSymbols& symbols,
                                 RenderContext& render,
                                 int32_t requested_width,
                                 int32_t requested_height,
                                 const MidrawConfig* config) {
  const SymbolVersion* version = pick_symbol_version(read_sdk_version());
  if (!version || !symbols.libgui) {
    return false;
  }

  void* libutils = dlopen("libutils.so", RTLD_NOW);
  if (!libutils) {
    fprintf(stderr, "dlopen libutils.so failed\n");
    return false;
  }

  const SymbolNameList empty_list{nullptr, 0};
  const SymbolNameList ctor_list = version ? version->String8_ctor : empty_list;
  const SymbolNameList dtor_list = version ? version->String8_dtor : empty_list;
  const SymbolNameList get_default_list =
      version ? version->SurfaceComposerClient_getDefault : empty_list;
  const SymbolNameList open_tx_list =
      version ? version->SurfaceComposerClient_openGlobalTransaction : empty_list;
  const SymbolNameList close_tx_list =
      version ? version->SurfaceComposerClient_closeGlobalTransaction : empty_list;
  const SymbolNameList create_checked_list =
      version ? version->SurfaceComposerClient_createSurfaceChecked : empty_list;
  const SymbolNameList get_surface_list =
      version ? version->SurfaceControl_getSurface : empty_list;
  const SymbolNameList set_layer_list =
      version ? version->SurfaceControl_setLayer : empty_list;
  const SymbolNameList set_pos_list =
      version ? version->SurfaceControl_setPosition : empty_list;
  const SymbolNameList set_size_list =
      version ? version->SurfaceControl_setSize : empty_list;
  const SymbolNameList set_alpha_list =
      version ? version->SurfaceControl_setAlpha : empty_list;
  const SymbolNameList set_flags_list =
      version ? version->SurfaceControl_setFlags : empty_list;
  const SymbolNameList geometry_list =
      version ? version->ANativeWindow_setBuffersGeometry : empty_list;

  PFN_String8_ctor string8_ctor = reinterpret_cast<PFN_String8_ctor>(
      load_symbol_list(libutils, ctor_list, "_ZN7android7String8C1EPKc"));
  if (!string8_ctor) {
    string8_ctor = reinterpret_cast<PFN_String8_ctor>(
        load_symbol_list(libutils, ctor_list, "_ZN7android7String8C2EPKc"));
  }
  PFN_String8_dtor string8_dtor = reinterpret_cast<PFN_String8_dtor>(
      load_symbol_list(libutils, dtor_list, "_ZN7android7String8D1Ev"));
  if (!string8_dtor) {
    string8_dtor = reinterpret_cast<PFN_String8_dtor>(
        load_symbol_list(libutils, dtor_list, "_ZN7android7String8D2Ev"));
  }

  PFN_SurfaceComposerClient_getDefault get_default =
      reinterpret_cast<PFN_SurfaceComposerClient_getDefault>(
          load_symbol_list(symbols.libgui, get_default_list,
                           "_ZN7android21SurfaceComposerClient10getDefaultEv"));
  PFN_SurfaceComposerClient_openGlobalTransaction open_tx =
      reinterpret_cast<PFN_SurfaceComposerClient_openGlobalTransaction>(
          load_symbol_list(symbols.libgui, open_tx_list,
                           "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv"));
  PFN_SurfaceComposerClient_closeGlobalTransaction close_tx =
      reinterpret_cast<PFN_SurfaceComposerClient_closeGlobalTransaction>(
          load_symbol_list(symbols.libgui, close_tx_list,
                           "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEv"));
  PFN_SurfaceComposerClient_createSurfaceChecked_v1 create_checked =
      reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v1>(
          load_symbol_list_filtered(symbols.libgui, create_checked_list,
                                    legacy_name_is_v1));

  PFN_SurfaceControl_getSurface get_surface =
      reinterpret_cast<PFN_SurfaceControl_getSurface>(
          load_symbol_list(symbols.libgui, get_surface_list, nullptr));
  PFN_SurfaceControl_setLayer set_layer =
      reinterpret_cast<PFN_SurfaceControl_setLayer>(
          load_symbol_list(symbols.libgui, set_layer_list, nullptr));
  PFN_SurfaceControl_setPosition set_position =
      reinterpret_cast<PFN_SurfaceControl_setPosition>(
          load_symbol_list(symbols.libgui, set_pos_list, nullptr));
  PFN_SurfaceControl_setSize set_size =
      reinterpret_cast<PFN_SurfaceControl_setSize>(
          load_symbol_list(symbols.libgui, set_size_list, nullptr));
  PFN_SurfaceControl_setAlpha set_alpha =
      reinterpret_cast<PFN_SurfaceControl_setAlpha>(
          load_symbol_list(symbols.libgui, set_alpha_list, nullptr));
  PFN_SurfaceControl_setFlags set_flags =
      reinterpret_cast<PFN_SurfaceControl_setFlags>(
          load_symbol_list(symbols.libgui, set_flags_list, nullptr));

  if (!string8_ctor || !string8_dtor || !get_default || !create_checked || !get_surface) {
    dlclose(libutils);
    return false;
  }

  SpObject client_sp = get_default();
  if (!client_sp.ptr) {
    dlclose(libutils);
    return false;
  }

  const char* name = (config && config->surface_name) ? config->surface_name : "Benchmark_Layer";
  String8Storage name_storage{};
  string8_ctor(name_storage.data, name);

  const int32_t width = (requested_width > 0) ? requested_width : 1080;
  const int32_t height = (requested_height > 0) ? requested_height : 1920;
  SpObject control_sp{};
  int32_t status = create_checked(client_sp.ptr, name_storage.data, width, height,
                                  WINDOW_FORMAT_RGBA_8888, &control_sp, 0, nullptr, -1, -1);
  string8_dtor(name_storage.data);
  dlclose(libutils);

  if (status != 0 || !control_sp.ptr) {
    return false;
  }

  if (open_tx && close_tx) {
    open_tx();
  }
  if (set_layer) {
    set_layer(control_sp.ptr, 0x7fffffff);
  }
  if (set_position) {
    set_position(control_sp.ptr, 0.0f, 0.0f);
  }
  if (set_size) {
    set_size(control_sp.ptr, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  }
  if (set_alpha) {
    set_alpha(control_sp.ptr, 1.0f);
  }
  if (set_flags) {
    const uint32_t kOpaqueMask = 0x00000400;
    set_flags(control_sp.ptr, 0, kOpaqueMask);
  }
  if (open_tx && close_tx) {
    close_tx();
  }

  SpObject surface_sp = get_surface(control_sp.ptr);
  if (!surface_sp.ptr) {
    return false;
  }

  render.window = reinterpret_cast<ANativeWindow*>(surface_sp.ptr);
  render.surface = nullptr;

  PFN_ANativeWindow_setBuffersGeometry set_geometry =
      reinterpret_cast<PFN_ANativeWindow_setBuffersGeometry>(load_symbol_list(
          symbols.libandroid, geometry_list, "ANativeWindow_setBuffersGeometry"));
  if (set_geometry) {
    set_geometry(render.window, width, height, WINDOW_FORMAT_RGBA_8888);
  }

  return true;
}

bool midraw_create_window(AndroidSymbols& symbols,
                          RenderContext& render,
                          int32_t requested_width,
                          int32_t requested_height,
                          const MidrawConfig* config) {
  const bool force_osimgui = env_truthy("MIDRAW_USE_OSIMGUI");
  const bool force_legacy = env_truthy("MIDRAW_USE_LEGACY_SCC");
  const int sdk = read_sdk_version();
  const bool prefer_osimgui = sdk >= 34;

  if (force_osimgui) {
    if (create_window_osimgui(symbols, render, requested_width, requested_height, config)) {
      return true;
    }
  }

  if (!force_legacy && !prefer_osimgui && symbols.ASurfaceControl_create) {
    if (create_window_asurface(symbols, render, requested_width, requested_height, config)) {
      return true;
    }
  }

  if (!force_legacy && prefer_osimgui) {
    if (create_window_osimgui(symbols, render, requested_width, requested_height, config)) {
      return true;
    }
  }

  if (force_legacy) {
    if (create_window_legacy(symbols, render, requested_width, requested_height, config)) {
      return true;
    }
  }

  if (symbols.ASurfaceControl_create) {
    if (create_window_asurface(symbols, render, requested_width, requested_height, config)) {
      return true;
    }
  }

  if (create_window_osimgui(symbols, render, requested_width, requested_height, config)) {
    return true;
  }

  return create_window_legacy(symbols, render, requested_width, requested_height, config);
}
