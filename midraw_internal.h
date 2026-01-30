#pragma once

#include <android/native_window.h>
#include <stdint.h>

// Forward declarations to avoid compile-time linkage to SurfaceControl headers.
struct ASurfaceControl;
struct ASurfaceTransaction;

// Function pointer types for dynamic symbol resolution.
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
using PFN_ANativeWindow_fromSurfaceControl = ANativeWindow* (*)(ASurfaceControl* control);
using PFN_ANativeWindow_lock = int32_t (*)(ANativeWindow* window,
                                          ANativeWindow_Buffer* outBuffer,
                                          ARect* inOutDirtyBounds);
using PFN_ANativeWindow_unlockAndPost = int32_t (*)(ANativeWindow* window);
using PFN_ANativeWindow_release = void (*)(ANativeWindow* window);
using PFN_ANativeWindow_getWidth = int32_t (*)(ANativeWindow* window);
using PFN_ANativeWindow_getHeight = int32_t (*)(ANativeWindow* window);

struct AndroidSymbols {
  void* libandroid = nullptr;
  void* libgui = nullptr;

  PFN_ASurfaceControl_create ASurfaceControl_create = nullptr;
  PFN_ASurfaceControl_release ASurfaceControl_release = nullptr;
  PFN_ASurfaceTransaction_create ASurfaceTransaction_create = nullptr;
  PFN_ASurfaceTransaction_release ASurfaceTransaction_release = nullptr;
  PFN_ASurfaceTransaction_setBufferSize ASurfaceTransaction_setBufferSize = nullptr;
  PFN_ASurfaceTransaction_setVisibility ASurfaceTransaction_setVisibility = nullptr;
  PFN_ASurfaceTransaction_setLayer ASurfaceTransaction_setLayer = nullptr;
  PFN_ASurfaceTransaction_apply ASurfaceTransaction_apply = nullptr;
  PFN_ANativeWindow_fromSurfaceControl ANativeWindow_fromSurfaceControl = nullptr;
  PFN_ANativeWindow_lock ANativeWindow_lock = nullptr;
  PFN_ANativeWindow_unlockAndPost ANativeWindow_unlockAndPost = nullptr;
  PFN_ANativeWindow_release ANativeWindow_release = nullptr;
  PFN_ANativeWindow_getWidth ANativeWindow_getWidth = nullptr;
  PFN_ANativeWindow_getHeight ANativeWindow_getHeight = nullptr;
};

struct RenderContext {
  ANativeWindow* window = nullptr;
  ASurfaceControl* surface = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
  int rotation = 0;
  uint32_t* pixels = nullptr;
  ANativeWindow_Buffer buffer{};
  int dirty_min_x = 0;
  int dirty_min_y = 0;
  int dirty_max_x = 0;
  int dirty_max_y = 0;
  int prev_dirty_min_x = 0;
  int prev_dirty_min_y = 0;
  int prev_dirty_max_x = 0;
  int prev_dirty_max_y = 0;
  int present_min_x = 0;
  int present_min_y = 0;
  int present_max_x = 0;
  int present_max_y = 0;
  uint64_t frame_index = 0;
};

struct MidrawConfig;

bool midraw_create_window(AndroidSymbols& symbols,
                          RenderContext& render,
                          int32_t requested_width,
                          int32_t requested_height,
                          const MidrawConfig* config);
