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
  SymbolNameList SurfaceComposerClient_getDefault;
  SymbolNameList SurfaceComposerClient_openGlobalTransaction;
  SymbolNameList SurfaceComposerClient_closeGlobalTransaction;
  SymbolNameList ASurfaceTransaction_setAlpha;
  SymbolNameList ASurfaceTransaction_setOpaque;
  SymbolNameList ASurfaceTransaction_setVisibility;
  SymbolNameList ASurfaceTransaction_setLayer;
  SymbolNameList ASurfaceControl_create;
  SymbolNameList ASurfaceControl_release;
  SymbolNameList ANativeWindow_setBuffersGeometry;
  SymbolNameList SurfaceComposerClient_createSurface;
  SymbolNameList SurfaceComposerClient_createSurfaceChecked;
  SymbolNameList SurfaceComposerClient_ctor;
  SymbolNameList SurfaceComposerClient_dtor;
  SymbolNameList SurfaceControl_getSurface;
  SymbolNameList SurfaceControl_setLayer;
  SymbolNameList SurfaceControl_setPosition;
  SymbolNameList SurfaceControl_setSize;
  SymbolNameList SurfaceControl_setAlpha;
  SymbolNameList SurfaceControl_setFlags;
  SymbolNameList SurfaceControl_setBufferSize;
};

static const char* kNames_String8_ctor[] = {"_ZN7android7String8C1EPKc", "_ZN7android7String8C2EPKc"};
static const char* kNames_String8_dtor[] = {"_ZN7android7String8D1Ev", "_ZN7android7String8D2Ev"};
static const char* kNames_SurfaceComposerClient_getDefault[] = {"_ZN7android21SurfaceComposerClient10getDefaultEv"};
static const char* kNames_SurfaceComposerClient_openGlobalTransaction[] = {
    "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv"};
static const char* kNames_SurfaceComposerClient_closeGlobalTransaction[] = {
    "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEv"};
static const char* kNames_ASurfaceTransaction_setAlpha[] = {"ASurfaceTransaction_setAlpha"};
static const char* kNames_ASurfaceTransaction_setOpaque[] = {"ASurfaceTransaction_setOpaque"};
static const char* kNames_ASurfaceTransaction_setVisibility[] = {"ASurfaceTransaction_setVisibility"};
static const char* kNames_ASurfaceTransaction_setLayer[] = {"ASurfaceTransaction_setLayer"};
static const char* kNames_ASurfaceControl_create[] = {"ASurfaceControl_create"};
static const char* kNames_ASurfaceControl_release[] = {"ASurfaceControl_release"};
static const char* kNames_ANativeWindow_setBuffersGeometry[] = {"ANativeWindow_setBuffersGeometry"};

static const SymbolVersion kSymbolVersions[] = {
    {0,
     {kNames_String8_ctor, sizeof(kNames_String8_ctor) / sizeof(kNames_String8_ctor[0])},
     {kNames_String8_dtor, sizeof(kNames_String8_dtor) / sizeof(kNames_String8_dtor[0])},
     {kNames_SurfaceComposerClient_getDefault,
      sizeof(kNames_SurfaceComposerClient_getDefault) /
          sizeof(kNames_SurfaceComposerClient_getDefault[0])},
     {kNames_SurfaceComposerClient_openGlobalTransaction,
      sizeof(kNames_SurfaceComposerClient_openGlobalTransaction) /
          sizeof(kNames_SurfaceComposerClient_openGlobalTransaction[0])},
     {kNames_SurfaceComposerClient_closeGlobalTransaction,
      sizeof(kNames_SurfaceComposerClient_closeGlobalTransaction) /
          sizeof(kNames_SurfaceComposerClient_closeGlobalTransaction[0])},
     {kNames_ASurfaceTransaction_setAlpha,
      sizeof(kNames_ASurfaceTransaction_setAlpha) /
          sizeof(kNames_ASurfaceTransaction_setAlpha[0])},
     {kNames_ASurfaceTransaction_setOpaque,
      sizeof(kNames_ASurfaceTransaction_setOpaque) /
          sizeof(kNames_ASurfaceTransaction_setOpaque[0])},
     {kNames_ASurfaceTransaction_setVisibility,
      sizeof(kNames_ASurfaceTransaction_setVisibility) /
          sizeof(kNames_ASurfaceTransaction_setVisibility[0])},
     {kNames_ASurfaceTransaction_setLayer,
      sizeof(kNames_ASurfaceTransaction_setLayer) /
          sizeof(kNames_ASurfaceTransaction_setLayer[0])},
     {kNames_ASurfaceControl_create,
      sizeof(kNames_ASurfaceControl_create) / sizeof(kNames_ASurfaceControl_create[0])},
     {kNames_ASurfaceControl_release,
      sizeof(kNames_ASurfaceControl_release) / sizeof(kNames_ASurfaceControl_release[0])},
     {kNames_ANativeWindow_setBuffersGeometry,
      sizeof(kNames_ANativeWindow_setBuffersGeometry) /
          sizeof(kNames_ANativeWindow_setBuffersGeometry[0])},
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
