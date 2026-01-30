#pragma once

#include <stddef.h>

struct SymbolNameList {
  const char* const* names;
  size_t count;
};

struct SymbolVersion {
  int sdk;
  SymbolNameList String8_ctor;
  SymbolNameList String8_dtor;
  SymbolNameList SurfaceComposerClient_ctor;
  SymbolNameList SurfaceComposerClient_dtor;
  SymbolNameList SurfaceComposerClient_getDefault;
  SymbolNameList SurfaceComposerClient_openGlobalTransaction;
  SymbolNameList SurfaceComposerClient_closeGlobalTransaction;
  SymbolNameList SurfaceComposerClient_createSurface;
  SymbolNameList SurfaceComposerClient_createSurfaceChecked;
  SymbolNameList SurfaceComposerClient_Transaction_apply;
  SymbolNameList SurfaceControl_getSurface;
  SymbolNameList SurfaceControl_setLayer;
  SymbolNameList SurfaceControl_setPosition;
  SymbolNameList SurfaceControl_setSize;
  SymbolNameList SurfaceControl_setAlpha;
  SymbolNameList SurfaceControl_setFlags;
  SymbolNameList SurfaceControl_setBufferSize;
  SymbolNameList ASurfaceControl_create;
  SymbolNameList ASurfaceControl_release;
  SymbolNameList ASurfaceTransaction_create;
  SymbolNameList ASurfaceTransaction_release;
  SymbolNameList ASurfaceTransaction_setBufferSize;
  SymbolNameList ASurfaceTransaction_apply;
  SymbolNameList ASurfaceTransaction_setAlpha;
  SymbolNameList ASurfaceTransaction_setOpaque;
  SymbolNameList ASurfaceTransaction_setVisibility;
  SymbolNameList ASurfaceTransaction_setLayer;
  SymbolNameList ANativeWindow_setBuffersGeometry;
};

static const SymbolVersion kSymbolVersions[] = {
    {0,
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0},
     {nullptr, 0}}};

static const size_t kSymbolVersionCount =
    sizeof(kSymbolVersions) / sizeof(kSymbolVersions[0]);
