#include "generated_symbols.h"

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <time.h>

// Forward declarations to avoid compile-time linkage to SurfaceControl headers.
struct ASurfaceControl;
struct ASurfaceTransaction;

namespace android {
using status_t = int32_t;

class Parcel;
class IBinder;

class Parcelable {
 public:
  virtual ~Parcelable() = default;
  virtual status_t writeToParcel(Parcel* parcel) const = 0;
  virtual status_t readFromParcel(const Parcel* parcel) = 0;
};

class Parcel {};

template <typename T>
struct sp {
  T* ptr;
  sp() : ptr(nullptr) {}
};

struct LayerMetadata : public Parcelable {
  std::unordered_map<uint32_t, std::vector<uint8_t>> mMap;
  LayerMetadata() = default;
  LayerMetadata(const LayerMetadata&) = default;
  LayerMetadata(LayerMetadata&&) = default;
  LayerMetadata& operator=(const LayerMetadata&) = default;
  LayerMetadata& operator=(LayerMetadata&&) = default;
  ~LayerMetadata() override = default;

  status_t writeToParcel(Parcel*) const override { return 0; }
  status_t readFromParcel(const Parcel*) override { return 0; }
};
}  // namespace android

using PFN_ASurfaceControl_create = ASurfaceControl* (*)(ASurfaceControl* parent,
                                                        const char* debug_name);
using PFN_ASurfaceControl_createFromWindow = ASurfaceControl* (*)(ANativeWindow* parent,
                                                                  const char* debug_name);
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
using PFN_ASurfaceTransaction_setBuffer = void (*)(ASurfaceTransaction* transaction,
                                                   ASurfaceControl* control,
                                                   AHardwareBuffer* buffer,
                                                   int acquire_fence_fd);
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

struct SpObject {
  void* ptr;
};

struct String8Storage {
  alignas(void*) unsigned char data[64];
};

using PFN_String8_ctor = void (*)(void* self, const char* str);
using PFN_String8_dtor = void (*)(void* self);
using PFN_SurfaceComposerClient_getDefault = void (*)(SpObject* out);
using PFN_SurfaceComposerClient_openGlobalTransaction = void (*)();
using PFN_SurfaceComposerClient_closeGlobalTransaction = void (*)();
using PFN_SurfaceComposerClient_createSurfaceChecked_v1 =
    int32_t (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                void* out_surface,
                uint32_t flags,
                void* parent,
                int32_t window_type,
                int32_t owner_uid);
using PFN_SurfaceComposerClient_createSurfaceChecked_v2_parent =
    int32_t (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                void* out_surface,
                int32_t flags,
                void* parent,
                android::LayerMetadata metadata);
using PFN_SurfaceComposerClient_createSurfaceChecked_v2_parent_hint =
    int32_t (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                void* out_surface,
                int32_t flags,
                void* parent,
                android::LayerMetadata metadata,
                uint32_t* out_transform_hint);
using PFN_SurfaceComposerClient_createSurfaceChecked_v2_handle =
    int32_t (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                void* out_surface,
                int32_t flags,
                const android::sp<android::IBinder>& parent_handle,
                android::LayerMetadata metadata);
using PFN_SurfaceComposerClient_createSurfaceChecked_v2_handle_hint =
    int32_t (*)(void* client,
                const void* name,
                uint32_t width,
                uint32_t height,
                int32_t format,
                void* out_surface,
                int32_t flags,
                const android::sp<android::IBinder>& parent_handle,
                android::LayerMetadata metadata,
                uint32_t* out_transform_hint);
using PFN_SurfaceControl_getSurface = void (*)(SpObject* out, void* control);
using PFN_SurfaceControl_setLayer = int32_t (*)(void* control, int32_t layer);
using PFN_SurfaceControl_setPosition = int32_t (*)(void* control, float x, float y);
using PFN_SurfaceControl_setSize = int32_t (*)(void* control, uint32_t w, uint32_t h);
using PFN_SurfaceControl_setAlpha = int32_t (*)(void* control, float alpha);
using PFN_SurfaceControl_setFlags = int32_t (*)(void* control, uint32_t flags, uint32_t mask);

struct EngineSymbols {
  void* libgui = nullptr;
  void* libandroid = nullptr;
  void* libutils = nullptr;
  void* libnativewindow = nullptr;

  PFN_ASurfaceControl_create ASurfaceControl_create = nullptr;
  PFN_ASurfaceControl_createFromWindow ASurfaceControl_createFromWindow = nullptr;
  PFN_ASurfaceControl_release ASurfaceControl_release = nullptr;
  PFN_ASurfaceTransaction_create ASurfaceTransaction_create = nullptr;
  PFN_ASurfaceTransaction_release ASurfaceTransaction_release = nullptr;
  PFN_ASurfaceTransaction_setBufferSize ASurfaceTransaction_setBufferSize = nullptr;
  PFN_ASurfaceTransaction_setVisibility ASurfaceTransaction_setVisibility = nullptr;
  PFN_ASurfaceTransaction_setLayer ASurfaceTransaction_setLayer = nullptr;
  PFN_ASurfaceTransaction_apply ASurfaceTransaction_apply = nullptr;
  PFN_ASurfaceTransaction_setBuffer ASurfaceTransaction_setBuffer = nullptr;
  PFN_ASurfaceTransaction_setAlpha ASurfaceTransaction_setAlpha = nullptr;
  PFN_ASurfaceTransaction_setOpaque ASurfaceTransaction_setOpaque = nullptr;
  PFN_ANativeWindow_fromSurfaceControl ANativeWindow_fromSurfaceControl = nullptr;
  PFN_ANativeWindow_lock ANativeWindow_lock = nullptr;
  PFN_ANativeWindow_unlockAndPost ANativeWindow_unlockAndPost = nullptr;
  PFN_ANativeWindow_release ANativeWindow_release = nullptr;
  PFN_ANativeWindow_setBuffersGeometry ANativeWindow_setBuffersGeometry = nullptr;
  PFN_AHardwareBuffer_allocate AHardwareBuffer_allocate = nullptr;
  PFN_AHardwareBuffer_describe AHardwareBuffer_describe = nullptr;
  PFN_AHardwareBuffer_release AHardwareBuffer_release = nullptr;
  PFN_AHardwareBuffer_lock AHardwareBuffer_lock = nullptr;
  PFN_AHardwareBuffer_unlock AHardwareBuffer_unlock = nullptr;

  PFN_String8_ctor String8_ctor = nullptr;
  PFN_String8_dtor String8_dtor = nullptr;
  PFN_SurfaceComposerClient_getDefault SurfaceComposerClient_getDefault = nullptr;
  PFN_SurfaceComposerClient_openGlobalTransaction SurfaceComposerClient_openGlobalTransaction =
      nullptr;
  PFN_SurfaceComposerClient_closeGlobalTransaction SurfaceComposerClient_closeGlobalTransaction =
      nullptr;
  PFN_SurfaceComposerClient_createSurfaceChecked_v1 SurfaceComposerClient_createSurfaceChecked_v1 =
      nullptr;
  PFN_SurfaceComposerClient_createSurfaceChecked_v2_parent
      SurfaceComposerClient_createSurfaceChecked_v2_parent = nullptr;
  PFN_SurfaceComposerClient_createSurfaceChecked_v2_parent_hint
      SurfaceComposerClient_createSurfaceChecked_v2_parent_hint = nullptr;
  PFN_SurfaceComposerClient_createSurfaceChecked_v2_handle
      SurfaceComposerClient_createSurfaceChecked_v2_handle = nullptr;
  PFN_SurfaceComposerClient_createSurfaceChecked_v2_handle_hint
      SurfaceComposerClient_createSurfaceChecked_v2_handle_hint = nullptr;
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
  AHardwareBuffer* buffer = nullptr;
  AHardwareBuffer_Desc buffer_desc{};
  bool use_ahb = false;
  int width = 0;
  int height = 0;
  int stride = 0;
  uint32_t* pixels = nullptr;
  ANativeWindow_Buffer window_buffer{};
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

static void* load_symbol_multi(void* h1,
                               void* h2,
                               void* h3,
                               const SymbolNameList& list,
                               const char* fallback) {
  void* sym = load_symbol_list(h1, list, fallback);
  if (!sym && h2) {
    sym = load_symbol_list(h2, list, fallback);
  }
  if (!sym && h3) {
    sym = load_symbol_list(h3, list, fallback);
  }
  return sym;
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

static bool read_section(FILE* file, long offset, void* out, size_t size) {
  if (!file) {
    return false;
  }
  if (fseek(file, offset, SEEK_SET) != 0) {
    return false;
  }
  return fread(out, 1, size, file) == size;
}

static bool find_lib_path(const char* soname, char* out, size_t out_len) {
  if (!soname || !out || out_len == 0) {
    return false;
  }
  FILE* maps = fopen("/proc/self/maps", "r");
  if (!maps) {
    return false;
  }
  char line[512];
  while (fgets(line, sizeof(line), maps)) {
    if (!strstr(line, soname)) {
      continue;
    }
    const char* path = strchr(line, '/');
    if (!path) {
      continue;
    }
    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
      --len;
    }
    if (len + 1 > out_len) {
      continue;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    fclose(maps);
    return true;
  }
  fclose(maps);
  return false;
}

static bool collect_dynsym_names(const char* path,
                                 const char* needle,
                                 std::vector<std::string>& out) {
  if (!path || !needle) {
    return false;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  Elf64_Ehdr ehdr{};
  if (!read_section(file, 0, &ehdr, sizeof(ehdr))) {
    fclose(file);
    return false;
  }
  if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
    fclose(file);
    return false;
  }
  if (ehdr.e_shoff == 0 || ehdr.e_shentsize == 0 || ehdr.e_shnum == 0) {
    fclose(file);
    return false;
  }
  std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
  if (!read_section(file, static_cast<long>(ehdr.e_shoff), shdrs.data(),
                    shdrs.size() * sizeof(Elf64_Shdr))) {
    fclose(file);
    return false;
  }
  if (ehdr.e_shstrndx >= shdrs.size()) {
    fclose(file);
    return false;
  }
  const Elf64_Shdr& shstr = shdrs[ehdr.e_shstrndx];
  std::vector<char> shstrtab(shstr.sh_size);
  if (!read_section(file, static_cast<long>(shstr.sh_offset), shstrtab.data(),
                    shstrtab.size())) {
    fclose(file);
    return false;
  }
  const Elf64_Shdr* dynsym = nullptr;
  const Elf64_Shdr* dynstr = nullptr;
  for (const auto& sh : shdrs) {
    if (sh.sh_name >= shstrtab.size()) {
      continue;
    }
    const char* name = shstrtab.data() + sh.sh_name;
    if (strcmp(name, ".dynsym") == 0) {
      dynsym = &sh;
    } else if (strcmp(name, ".dynstr") == 0) {
      dynstr = &sh;
    }
  }
  if (!dynsym || !dynstr || dynsym->sh_entsize == 0) {
    fclose(file);
    return false;
  }
  std::vector<char> dynstrtab(dynstr->sh_size);
  if (!read_section(file, static_cast<long>(dynstr->sh_offset), dynstrtab.data(),
                    dynstrtab.size())) {
    fclose(file);
    return false;
  }
  const size_t sym_count = dynsym->sh_size / dynsym->sh_entsize;
  std::vector<Elf64_Sym> syms(sym_count);
  if (!read_section(file, static_cast<long>(dynsym->sh_offset), syms.data(),
                    syms.size() * sizeof(Elf64_Sym))) {
    fclose(file);
    return false;
  }
  for (const auto& sym : syms) {
    if (sym.st_name >= dynstrtab.size()) {
      continue;
    }
    const char* sym_name = dynstrtab.data() + sym.st_name;
    if (!sym_name || !sym_name[0]) {
      continue;
    }
    if (strstr(sym_name, needle)) {
      out.emplace_back(sym_name);
    }
  }
  fclose(file);
  return !out.empty();
}

enum CreateSurfaceCheckedKind {
  kCSC_None = 0,
  kCSC_V1,
  kCSC_V2_PARENT,
  kCSC_V2_PARENT_HINT,
  kCSC_V2_HANDLE,
  kCSC_V2_HANDLE_HINT,
};

static CreateSurfaceCheckedKind classify_create_surface_checked(const char* demangled) {
  if (!demangled) {
    return kCSC_None;
  }
  if (!strstr(demangled, "SurfaceComposerClient::createSurfaceChecked")) {
    return kCSC_None;
  }
  const bool has_layer_metadata = strstr(demangled, "LayerMetadata") != nullptr;
  const bool has_ibinder = strstr(demangled, "IBinder") != nullptr;
  const bool has_surface_control = strstr(demangled, "SurfaceControl*") != nullptr;
  const bool has_out_hint = strstr(demangled, "uint32_t*") != nullptr ||
                            strstr(demangled, "uint32_t *") != nullptr ||
                            strstr(demangled, "unsigned int*") != nullptr ||
                            strstr(demangled, "unsigned int *") != nullptr;

  if (!has_layer_metadata && has_surface_control) {
    return kCSC_V1;
  }
  if (has_layer_metadata) {
    if (has_ibinder) {
      return has_out_hint ? kCSC_V2_HANDLE_HINT : kCSC_V2_HANDLE;
    }
    if (has_surface_control) {
      return has_out_hint ? kCSC_V2_PARENT_HINT : kCSC_V2_PARENT;
    }
  }
  return kCSC_None;
}

static void resolve_create_surface_checked_dynamic(EngineSymbols& s) {
  if (!s.libgui) {
    return;
  }
  if (s.SurfaceComposerClient_createSurfaceChecked_v1 ||
      s.SurfaceComposerClient_createSurfaceChecked_v2_parent ||
      s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint ||
      s.SurfaceComposerClient_createSurfaceChecked_v2_handle ||
      s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint) {
    return;
  }
  char lib_path[256] = {};
  if (!find_lib_path("libgui.so", lib_path, sizeof(lib_path))) {
    return;
  }
  std::vector<std::string> symbols;
  if (!collect_dynsym_names(lib_path, "createSurfaceChecked", symbols)) {
    return;
  }
  for (const auto& name : symbols) {
    void* sym = dlsym(s.libgui, name.c_str());
    if (!sym) {
      continue;
    }
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    const char* demangled_view = (status == 0 && demangled) ? demangled : name.c_str();
    CreateSurfaceCheckedKind kind = classify_create_surface_checked(demangled_view);
    free(demangled);
    switch (kind) {
      case kCSC_V1:
        if (!s.SurfaceComposerClient_createSurfaceChecked_v1) {
          s.SurfaceComposerClient_createSurfaceChecked_v1 =
              reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v1>(sym);
        }
        break;
      case kCSC_V2_PARENT:
        if (!s.SurfaceComposerClient_createSurfaceChecked_v2_parent) {
          s.SurfaceComposerClient_createSurfaceChecked_v2_parent =
              reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v2_parent>(sym);
        }
        break;
      case kCSC_V2_PARENT_HINT:
        if (!s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint) {
          s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint =
              reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v2_parent_hint>(sym);
        }
        break;
      case kCSC_V2_HANDLE:
        if (!s.SurfaceComposerClient_createSurfaceChecked_v2_handle) {
          s.SurfaceComposerClient_createSurfaceChecked_v2_handle =
              reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v2_handle>(sym);
        }
        break;
      case kCSC_V2_HANDLE_HINT:
        if (!s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint) {
          s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint =
              reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v2_handle_hint>(sym);
        }
        break;
      default:
        break;
    }
  }
}

enum SurfaceControlSymbolKind {
  kSC_None = 0,
  kSC_GetSurface,
  kSC_SetLayer,
  kSC_SetPosition,
  kSC_SetSize,
  kSC_SetAlpha,
  kSC_SetFlags,
};

static SurfaceControlSymbolKind classify_surfacecontrol_symbol(const char* demangled) {
  if (!demangled) {
    return kSC_None;
  }
  if (!strstr(demangled, "SurfaceControl::")) {
    return kSC_None;
  }
  if (strstr(demangled, "SurfaceControl::getSurface")) {
    return kSC_GetSurface;
  }
  if (strstr(demangled, "SurfaceControl::setLayer")) {
    return kSC_SetLayer;
  }
  if (strstr(demangled, "SurfaceControl::setPosition")) {
    return kSC_SetPosition;
  }
  if (strstr(demangled, "SurfaceControl::setSize")) {
    return kSC_SetSize;
  }
  if (strstr(demangled, "SurfaceControl::setAlpha")) {
    return kSC_SetAlpha;
  }
  if (strstr(demangled, "SurfaceControl::setFlags")) {
    return kSC_SetFlags;
  }
  return kSC_None;
}

static void resolve_surfacecontrol_symbols_dynamic(EngineSymbols& s) {
  if (!s.libgui) {
    return;
  }
  if (s.SurfaceControl_getSurface && s.SurfaceControl_setLayer && s.SurfaceControl_setPosition &&
      s.SurfaceControl_setSize && s.SurfaceControl_setAlpha && s.SurfaceControl_setFlags) {
    return;
  }
  char lib_path[256] = {};
  if (!find_lib_path("libgui.so", lib_path, sizeof(lib_path))) {
    return;
  }
  std::vector<std::string> symbols;
  if (!collect_dynsym_names(lib_path, "SurfaceControl", symbols)) {
    return;
  }
  for (const auto& name : symbols) {
    void* sym = dlsym(s.libgui, name.c_str());
    if (!sym) {
      continue;
    }
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    const char* demangled_view = (status == 0 && demangled) ? demangled : name.c_str();
    SurfaceControlSymbolKind kind = classify_surfacecontrol_symbol(demangled_view);
    free(demangled);
    switch (kind) {
      case kSC_GetSurface:
        if (!s.SurfaceControl_getSurface) {
          s.SurfaceControl_getSurface =
              reinterpret_cast<PFN_SurfaceControl_getSurface>(sym);
        }
        break;
      case kSC_SetLayer:
        if (!s.SurfaceControl_setLayer) {
          s.SurfaceControl_setLayer =
              reinterpret_cast<PFN_SurfaceControl_setLayer>(sym);
        }
        break;
      case kSC_SetPosition:
        if (!s.SurfaceControl_setPosition) {
          s.SurfaceControl_setPosition =
              reinterpret_cast<PFN_SurfaceControl_setPosition>(sym);
        }
        break;
      case kSC_SetSize:
        if (!s.SurfaceControl_setSize) {
          s.SurfaceControl_setSize =
              reinterpret_cast<PFN_SurfaceControl_setSize>(sym);
        }
        break;
      case kSC_SetAlpha:
        if (!s.SurfaceControl_setAlpha) {
          s.SurfaceControl_setAlpha =
              reinterpret_cast<PFN_SurfaceControl_setAlpha>(sym);
        }
        break;
      case kSC_SetFlags:
        if (!s.SurfaceControl_setFlags) {
          s.SurfaceControl_setFlags =
              reinterpret_cast<PFN_SurfaceControl_setFlags>(sym);
        }
        break;
      default:
        break;
    }
  }
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
  s.libnativewindow = dlopen("libnativewindow.so", RTLD_NOW);

  const SymbolVersion* version = pick_symbol_version(read_sdk_version());
  const SymbolNameList empty_list{nullptr, 0};

  s.ASurfaceControl_create = reinterpret_cast<PFN_ASurfaceControl_create>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceControl_create : empty_list,
                        "ASurfaceControl_create"));
  s.ASurfaceControl_createFromWindow = reinterpret_cast<PFN_ASurfaceControl_createFromWindow>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        empty_list,
                        "ASurfaceControl_createFromWindow"));
  s.ASurfaceControl_release = reinterpret_cast<PFN_ASurfaceControl_release>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceControl_release : empty_list,
                        "ASurfaceControl_release"));
  s.ASurfaceTransaction_create = reinterpret_cast<PFN_ASurfaceTransaction_create>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_create : empty_list,
                        "ASurfaceTransaction_create"));
  s.ASurfaceTransaction_release = reinterpret_cast<PFN_ASurfaceTransaction_release>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_release : empty_list,
                        "ASurfaceTransaction_release"));
  s.ASurfaceTransaction_setBufferSize = reinterpret_cast<PFN_ASurfaceTransaction_setBufferSize>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_setBufferSize : empty_list,
                        "ASurfaceTransaction_setBufferSize"));
  s.ASurfaceTransaction_setVisibility =
      reinterpret_cast<PFN_ASurfaceTransaction_setVisibility>(
          load_symbol_multi(s.libgui,
                            s.libandroid,
                            s.libnativewindow,
                            version ? version->ASurfaceTransaction_setVisibility : empty_list,
                            "ASurfaceTransaction_setVisibility"));
  s.ASurfaceTransaction_setLayer = reinterpret_cast<PFN_ASurfaceTransaction_setLayer>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_setLayer : empty_list,
                        "ASurfaceTransaction_setLayer"));
  s.ASurfaceTransaction_apply = reinterpret_cast<PFN_ASurfaceTransaction_apply>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_apply : empty_list,
                        "ASurfaceTransaction_apply"));
  s.ASurfaceTransaction_setBuffer = reinterpret_cast<PFN_ASurfaceTransaction_setBuffer>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        empty_list,
                        "ASurfaceTransaction_setBuffer"));
  s.ASurfaceTransaction_setAlpha = reinterpret_cast<PFN_ASurfaceTransaction_setAlpha>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_setAlpha : empty_list,
                        "ASurfaceTransaction_setAlpha"));
  s.ASurfaceTransaction_setOpaque = reinterpret_cast<PFN_ASurfaceTransaction_setOpaque>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        version ? version->ASurfaceTransaction_setOpaque : empty_list,
                        "ASurfaceTransaction_setOpaque"));
  s.ANativeWindow_fromSurfaceControl = reinterpret_cast<PFN_ANativeWindow_fromSurfaceControl>(
      load_symbol_multi(s.libgui,
                        s.libandroid,
                        s.libnativewindow,
                        empty_list,
                        "ANativeWindow_fromSurfaceControl"));

  s.ANativeWindow_lock = reinterpret_cast<PFN_ANativeWindow_lock>(
      load_symbol_list(s.libandroid, empty_list, "ANativeWindow_lock"));
  s.ANativeWindow_unlockAndPost = reinterpret_cast<PFN_ANativeWindow_unlockAndPost>(
      load_symbol_list(s.libandroid, empty_list, "ANativeWindow_unlockAndPost"));
  s.ANativeWindow_release = reinterpret_cast<PFN_ANativeWindow_release>(
      load_symbol_list(s.libandroid, empty_list, "ANativeWindow_release"));
  s.ANativeWindow_setBuffersGeometry = reinterpret_cast<PFN_ANativeWindow_setBuffersGeometry>(
      load_symbol_multi(s.libandroid,
                        s.libgui,
                        s.libnativewindow,
                        version ? version->ANativeWindow_setBuffersGeometry : empty_list,
                        "ANativeWindow_setBuffersGeometry"));

  s.AHardwareBuffer_allocate = reinterpret_cast<PFN_AHardwareBuffer_allocate>(
      load_symbol_multi(s.libandroid, s.libnativewindow, s.libgui, empty_list,
                        "AHardwareBuffer_allocate"));
  s.AHardwareBuffer_describe = reinterpret_cast<PFN_AHardwareBuffer_describe>(
      load_symbol_multi(s.libandroid, s.libnativewindow, s.libgui, empty_list,
                        "AHardwareBuffer_describe"));
  s.AHardwareBuffer_release = reinterpret_cast<PFN_AHardwareBuffer_release>(
      load_symbol_multi(s.libandroid, s.libnativewindow, s.libgui, empty_list,
                        "AHardwareBuffer_release"));
  s.AHardwareBuffer_lock = reinterpret_cast<PFN_AHardwareBuffer_lock>(
      load_symbol_multi(s.libandroid, s.libnativewindow, s.libgui, empty_list,
                        "AHardwareBuffer_lock"));
  s.AHardwareBuffer_unlock = reinterpret_cast<PFN_AHardwareBuffer_unlock>(
      load_symbol_multi(s.libandroid, s.libnativewindow, s.libgui, empty_list,
                        "AHardwareBuffer_unlock"));

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
  s.SurfaceComposerClient_createSurfaceChecked_v1 =
      reinterpret_cast<PFN_SurfaceComposerClient_createSurfaceChecked_v1>(
          load_symbol_list_filtered(s.libgui,
                                    version ? version->SurfaceComposerClient_createSurfaceChecked
                                            : empty_list,
                                    legacy_name_is_v1));
  resolve_create_surface_checked_dynamic(s);

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

  resolve_surfacecontrol_symbols_dynamic(s);
  return true;
}

static bool create_surface_asurface(EngineState& state,
                                    int width,
                                    int height,
                                    ANativeWindow* parent_window) {
  EngineSymbols& s = state.symbols;
  const bool can_window =
      s.ANativeWindow_fromSurfaceControl && s.ANativeWindow_lock && s.ANativeWindow_unlockAndPost;
  const bool can_ahb = s.ASurfaceTransaction_setBuffer && s.AHardwareBuffer_allocate &&
                       s.AHardwareBuffer_lock && s.AHardwareBuffer_unlock &&
                       s.AHardwareBuffer_release && s.AHardwareBuffer_describe;
  if ((!s.ASurfaceControl_createFromWindow && !s.ASurfaceControl_create) ||
      !s.ASurfaceTransaction_create ||
      (!can_window && !can_ahb)) {
    fprintf(stderr,
            "ASurfaceControl symbols missing: create=%p tx=%p window=%p ahb=%p\n",
            reinterpret_cast<void*>(s.ASurfaceControl_create),
            reinterpret_cast<void*>(s.ASurfaceTransaction_create),
            reinterpret_cast<void*>(s.ANativeWindow_fromSurfaceControl),
            reinterpret_cast<void*>(s.ASurfaceTransaction_setBuffer));
    return false;
  }

  if (parent_window && s.ASurfaceControl_createFromWindow) {
    state.surface = s.ASurfaceControl_createFromWindow(parent_window, "SystemProfiler");
  } else {
    fprintf(stderr,
            "ASurfaceControl_create requires non-null parent on this platform; "
            "skipping ASurfaceControl path.\n");
    return false;
  }
  if (!state.surface) {
    fprintf(stderr, "ASurfaceControl_create returned null\n");
    return false;
  }

  ASurfaceTransaction* tx = s.ASurfaceTransaction_create();
  if (!tx) {
    fprintf(stderr, "ASurfaceTransaction_create returned null\n");
    if (s.ASurfaceControl_release) {
      s.ASurfaceControl_release(state.surface);
    }
    state.surface = nullptr;
    return false;
  }

  if (s.ASurfaceTransaction_setVisibility) {
    s.ASurfaceTransaction_setVisibility(tx, state.surface, 1);
  }
  if (s.ASurfaceTransaction_setLayer) {
    s.ASurfaceTransaction_setLayer(tx, state.surface, INT_MAX);
  }
  if (s.ASurfaceTransaction_setAlpha) {
    s.ASurfaceTransaction_setAlpha(tx, state.surface, 1.0f);
  }
  if (s.ASurfaceTransaction_setOpaque) {
    s.ASurfaceTransaction_setOpaque(tx, state.surface, 0);
  }
  if (s.ASurfaceTransaction_setBufferSize && width > 0 && height > 0) {
    s.ASurfaceTransaction_setBufferSize(tx, state.surface, width, height);
  }
  if (s.ASurfaceTransaction_apply) {
    s.ASurfaceTransaction_apply(tx);
  }
  if (s.ASurfaceTransaction_release) {
    s.ASurfaceTransaction_release(tx);
  }

  if (can_window && s.ANativeWindow_fromSurfaceControl) {
    state.window = s.ANativeWindow_fromSurfaceControl(state.surface);
    if (!state.window) {
      fprintf(stderr, "ANativeWindow_fromSurfaceControl returned null\n");
      return false;
    }
    if (s.ANativeWindow_setBuffersGeometry) {
      s.ANativeWindow_setBuffersGeometry(state.window, width, height, WINDOW_FORMAT_RGBA_8888);
    }
    return true;
  }

  if (!can_ahb) {
    return false;
  }

  memset(&state.buffer_desc, 0, sizeof(state.buffer_desc));
  state.buffer_desc.width = static_cast<uint32_t>(width);
  state.buffer_desc.height = static_cast<uint32_t>(height);
  state.buffer_desc.layers = 1;
  state.buffer_desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  state.buffer_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                            AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                            AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

  if (s.AHardwareBuffer_allocate(&state.buffer_desc, &state.buffer) != 0 || !state.buffer) {
    fprintf(stderr, "AHardwareBuffer_allocate failed\n");
    return false;
  }
  s.AHardwareBuffer_describe(state.buffer, &state.buffer_desc);
  state.use_ahb = true;
  return true;
}

static bool create_surface_legacy(EngineState& state, int width, int height) {
  EngineSymbols& s = state.symbols;
  if (!s.SurfaceComposerClient_getDefault ||
      (!s.SurfaceComposerClient_createSurfaceChecked_v1 &&
       !s.SurfaceComposerClient_createSurfaceChecked_v2_parent &&
       !s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint &&
       !s.SurfaceComposerClient_createSurfaceChecked_v2_handle &&
      !s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint) ||
      !s.SurfaceControl_getSurface || !s.String8_ctor || !s.String8_dtor) {
    fprintf(stderr,
            "legacy symbols missing: getDefault=%p v1=%p v2_parent=%p v2_parent_hint=%p "
            "v2_handle=%p v2_handle_hint=%p getSurface=%p String8_ctor=%p String8_dtor=%p\n",
            reinterpret_cast<void*>(s.SurfaceComposerClient_getDefault),
            reinterpret_cast<void*>(s.SurfaceComposerClient_createSurfaceChecked_v1),
            reinterpret_cast<void*>(s.SurfaceComposerClient_createSurfaceChecked_v2_parent),
            reinterpret_cast<void*>(s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint),
            reinterpret_cast<void*>(s.SurfaceComposerClient_createSurfaceChecked_v2_handle),
            reinterpret_cast<void*>(s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint),
            reinterpret_cast<void*>(s.SurfaceControl_getSurface),
            reinterpret_cast<void*>(s.String8_ctor),
            reinterpret_cast<void*>(s.String8_dtor));
    return false;
  }

  SpObject client_sp{};
  s.SurfaceComposerClient_getDefault(&client_sp);
  if (!client_sp.ptr) {
    fprintf(stderr, "SurfaceComposerClient_getDefault returned null\n");
    return false;
  }

  String8Storage name_storage{};
  s.String8_ctor(name_storage.data, "SystemProfiler");

  SpObject control_sp{};
  alignas(void*) unsigned char control_sp_storage[32] = {};
  void* control_sp_ptr = control_sp_storage;
  int32_t status = -1;
  if (s.SurfaceComposerClient_createSurfaceChecked_v1) {
    status = s.SurfaceComposerClient_createSurfaceChecked_v1(
        client_sp.ptr,
        name_storage.data,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        WINDOW_FORMAT_RGBA_8888,
        control_sp_ptr,
        0,
        nullptr,
        -1,
        -1);
  } else {
    android::LayerMetadata metadata;
    if (s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint) {
      status = s.SurfaceComposerClient_createSurfaceChecked_v2_handle_hint(
          client_sp.ptr,
          name_storage.data,
          static_cast<uint32_t>(width),
          static_cast<uint32_t>(height),
          WINDOW_FORMAT_RGBA_8888,
          control_sp_ptr,
          0,
          android::sp<android::IBinder>(),
          metadata,
          nullptr);
    } else if (s.SurfaceComposerClient_createSurfaceChecked_v2_handle) {
      status = s.SurfaceComposerClient_createSurfaceChecked_v2_handle(
          client_sp.ptr,
          name_storage.data,
          static_cast<uint32_t>(width),
          static_cast<uint32_t>(height),
          WINDOW_FORMAT_RGBA_8888,
          control_sp_ptr,
          0,
          android::sp<android::IBinder>(),
          metadata);
    } else if (s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint) {
      status = s.SurfaceComposerClient_createSurfaceChecked_v2_parent_hint(
          client_sp.ptr,
          name_storage.data,
          static_cast<uint32_t>(width),
          static_cast<uint32_t>(height),
          WINDOW_FORMAT_RGBA_8888,
          control_sp_ptr,
          0,
          nullptr,
          metadata,
          nullptr);
    } else if (s.SurfaceComposerClient_createSurfaceChecked_v2_parent) {
      status = s.SurfaceComposerClient_createSurfaceChecked_v2_parent(
          client_sp.ptr,
          name_storage.data,
          static_cast<uint32_t>(width),
          static_cast<uint32_t>(height),
          WINDOW_FORMAT_RGBA_8888,
          control_sp_ptr,
          0,
          nullptr,
          metadata);
    }
  }
  s.String8_dtor(name_storage.data);

  memcpy(&control_sp, control_sp_ptr, sizeof(control_sp));
  if (status != 0 || !control_sp.ptr) {
    fprintf(stderr, "createSurfaceChecked failed: status=%d control=%p\n", status,
            control_sp.ptr);
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

  SpObject surface_sp{};
  s.SurfaceControl_getSurface(&surface_sp, control_sp.ptr);
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
  if (state.use_ahb) {
    if (!state.buffer || !state.symbols.AHardwareBuffer_lock) {
      return false;
    }
    void* out = nullptr;
    ARect rect{0, 0, static_cast<int32_t>(state.buffer_desc.width),
               static_cast<int32_t>(state.buffer_desc.height)};
    const uint64_t usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                           AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    if (state.symbols.AHardwareBuffer_lock(state.buffer, usage, -1, &rect, &out) != 0) {
      return false;
    }
    state.width = static_cast<int>(state.buffer_desc.width);
    state.height = static_cast<int>(state.buffer_desc.height);
    state.stride = static_cast<int>(state.buffer_desc.stride);
    if (state.stride == 0) {
      state.stride = state.width;
    }
    state.pixels = reinterpret_cast<uint32_t*>(out);
    return state.pixels != nullptr;
  }

  if (!state.window || !state.symbols.ANativeWindow_lock) {
    return false;
  }
  if (state.symbols.ANativeWindow_lock(state.window, &state.window_buffer, nullptr) != 0) {
    return false;
  }
  state.width = state.window_buffer.width;
  state.height = state.window_buffer.height;
  state.stride = state.window_buffer.stride;
  state.pixels = reinterpret_cast<uint32_t*>(state.window_buffer.bits);
  return state.pixels != nullptr;
}

static void unlock_post(EngineState& state) {
  if (state.use_ahb) {
    int fence = -1;
    if (state.symbols.AHardwareBuffer_unlock) {
      state.symbols.AHardwareBuffer_unlock(state.buffer, &fence);
      if (fence >= 0) {
        close(fence);
      }
    }
    if (state.symbols.ASurfaceTransaction_create && state.symbols.ASurfaceTransaction_apply &&
        state.symbols.ASurfaceTransaction_setBuffer) {
      ASurfaceTransaction* tx = state.symbols.ASurfaceTransaction_create();
      if (tx) {
        state.symbols.ASurfaceTransaction_setBuffer(tx, state.surface, state.buffer, -1);
        state.symbols.ASurfaceTransaction_apply(tx);
        if (state.symbols.ASurfaceTransaction_release) {
          state.symbols.ASurfaceTransaction_release(tx);
        }
      }
    }
    state.pixels = nullptr;
    return;
  }

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

  if (!create_surface_legacy(state, width, height)) {
    if (!create_surface_asurface(state, width, height, nullptr)) {
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
