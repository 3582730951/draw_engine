#pragma once

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <stdint.h>

// Forward declarations to avoid compile-time linkage to SurfaceControl headers.
struct ASurfaceControl;
struct ASurfaceTransaction;
struct ANativeWindowBuffer;

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
using PFN_ASurfaceControl_createFromWindow = ASurfaceControl* (*)(ANativeWindow* parent,
                                                                  const char* debug_name);
using PFN_ASurfaceTransaction_setBuffer = void (*)(ASurfaceTransaction* transaction,
                                                   ASurfaceControl* control,
                                                   AHardwareBuffer* buffer,
                                                   int acquire_fence_fd);
using PFN_ASurfaceTransaction_setBufferTransparency = void (*)(ASurfaceTransaction* transaction,
                                                               ASurfaceControl* control,
                                                               int8_t transparency);
using PFN_ASurfaceTransaction_setGeometry = void (*)(ASurfaceTransaction* transaction,
                                                    ASurfaceControl* control,
                                                    const ARect& source,
                                                    const ARect& destination,
                                                    int32_t transform);
using PFN_ASurfaceTransaction_setZOrder = void (*)(ASurfaceTransaction* transaction,
                                                  ASurfaceControl* control,
                                                  int32_t z_order);
using PFN_ANativeWindow_setBuffersGeometry = int32_t (*)(ANativeWindow* window,
                                                        int32_t width,
                                                        int32_t height,
                                                        int32_t format);
using PFN_AHardwareBuffer_allocate = int (*)(const AHardwareBuffer_Desc* desc,
                                             AHardwareBuffer** outBuffer);
using PFN_AHardwareBuffer_describe = void (*)(const AHardwareBuffer* buffer,
                                              AHardwareBuffer_Desc* outDesc);
using PFN_AHardwareBuffer_release = void (*)(AHardwareBuffer* buffer);
using PFN_AHardwareBuffer_lock = int (*)(AHardwareBuffer* buffer,
                                         uint64_t usage,
                                         int32_t fence,
                                         const ARect* rect,
                                         void** out);
using PFN_AHardwareBuffer_unlock = int (*)(AHardwareBuffer* buffer, int32_t* fence);
using PFN_Surface_dequeueBuffer =
    int32_t (*)(void* surface, ANativeWindowBuffer** outBuffer, int* fenceFd);
using PFN_Surface_queueBuffer4 =
    int32_t (*)(void* surface,
                ANativeWindowBuffer* buffer,
                int fenceFd,
                void* queueOutput);
using PFN_Surface_queueBuffer3 =
    int32_t (*)(void* surface, ANativeWindowBuffer* buffer, int fenceFd);
using PFN_Surface_cancelBuffer =
    int32_t (*)(void* surface, ANativeWindowBuffer* buffer, int fenceFd);
using PFN_GraphicBuffer_from = void* (*)(ANativeWindowBuffer* buffer);
using PFN_GraphicBuffer_lock =
    int32_t (*)(void* graphicBuffer, uint32_t usage, void** vaddr, int* outBpp, int* outStride);
using PFN_GraphicBuffer_unlock = void (*)(void* graphicBuffer);

struct AndroidSymbols {
  void* libandroid = nullptr;
  void* libgui = nullptr;
  void* libui = nullptr;
  void* libnativewindow = nullptr;

  PFN_ASurfaceControl_create ASurfaceControl_create = nullptr;
  PFN_ASurfaceControl_createFromWindow ASurfaceControl_createFromWindow = nullptr;
  PFN_ASurfaceControl_release ASurfaceControl_release = nullptr;
  PFN_ASurfaceTransaction_create ASurfaceTransaction_create = nullptr;
  PFN_ASurfaceTransaction_release ASurfaceTransaction_release = nullptr;
  PFN_ASurfaceTransaction_setBufferSize ASurfaceTransaction_setBufferSize = nullptr;
  PFN_ASurfaceTransaction_setGeometry ASurfaceTransaction_setGeometry = nullptr;
  PFN_ASurfaceTransaction_setVisibility ASurfaceTransaction_setVisibility = nullptr;
  PFN_ASurfaceTransaction_setLayer ASurfaceTransaction_setLayer = nullptr;
  PFN_ASurfaceTransaction_setZOrder ASurfaceTransaction_setZOrder = nullptr;
  PFN_ASurfaceTransaction_apply ASurfaceTransaction_apply = nullptr;
  PFN_ASurfaceTransaction_setBuffer ASurfaceTransaction_setBuffer = nullptr;
  PFN_ASurfaceTransaction_setBufferTransparency ASurfaceTransaction_setBufferTransparency =
      nullptr;
  PFN_ANativeWindow_fromSurfaceControl ANativeWindow_fromSurfaceControl = nullptr;
  PFN_ANativeWindow_lock ANativeWindow_lock = nullptr;
  PFN_ANativeWindow_unlockAndPost ANativeWindow_unlockAndPost = nullptr;
  PFN_ANativeWindow_release ANativeWindow_release = nullptr;
  PFN_ANativeWindow_getWidth ANativeWindow_getWidth = nullptr;
  PFN_ANativeWindow_getHeight ANativeWindow_getHeight = nullptr;
  PFN_ANativeWindow_setBuffersGeometry ANativeWindow_setBuffersGeometry = nullptr;

  PFN_AHardwareBuffer_allocate AHardwareBuffer_allocate = nullptr;
  PFN_AHardwareBuffer_describe AHardwareBuffer_describe = nullptr;
  PFN_AHardwareBuffer_release AHardwareBuffer_release = nullptr;
  PFN_AHardwareBuffer_lock AHardwareBuffer_lock = nullptr;
  PFN_AHardwareBuffer_unlock AHardwareBuffer_unlock = nullptr;

  PFN_Surface_dequeueBuffer Surface_dequeueBuffer = nullptr;
  PFN_Surface_queueBuffer4 Surface_queueBuffer4 = nullptr;
  PFN_Surface_queueBuffer3 Surface_queueBuffer3 = nullptr;
  PFN_Surface_cancelBuffer Surface_cancelBuffer = nullptr;
  PFN_GraphicBuffer_from GraphicBuffer_from = nullptr;
  PFN_GraphicBuffer_lock GraphicBuffer_lock = nullptr;
  PFN_GraphicBuffer_unlock GraphicBuffer_unlock = nullptr;
};

struct RenderContext {
  ANativeWindow* window = nullptr;
  ASurfaceControl* surface = nullptr;
  void* surface_control = nullptr;
  bool uses_osimgui = false;
  ASurfaceControl* ahb_surface = nullptr;
  void* surface_native = nullptr;
  AHardwareBuffer* ahb_buffer = nullptr;
  AHardwareBuffer_Desc ahb_desc{};
  ANativeWindowBuffer* direct_buffer = nullptr;
  void* direct_graphic = nullptr;
  bool use_ahb = false;
  bool use_surface_direct = false;
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
int midraw_resize_window(AndroidSymbols& symbols,
                         RenderContext& render,
                         int32_t width,
                         int32_t height);
void midraw_hide_surface_control(AndroidSymbols& symbols, RenderContext& render);

bool midraw_create_window(AndroidSymbols& symbols,
                          RenderContext& render,
                          int32_t requested_width,
                          int32_t requested_height,
                          const MidrawConfig* config);

int midraw_query_display_rotation(AndroidSymbols& symbols,
                                  int* out_rotation,
                                  int* out_width,
                                  int* out_height);
