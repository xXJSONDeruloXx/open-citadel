#include "asset_manager.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <string>

#include "jni_internals.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "trace.h"

namespace fs = std::filesystem;

extern "C" {

struct AAssetManager {
    int marker;
};

struct AAsset {
    int fd;
#if defined(_WIN32)
    int64_t length;
#else
    off_t length;
#endif
};

}

namespace {

class AndroidAssetManagerObject : public Object {
public:
    static Class clazz;
    Class *_getClass() override { return &clazz; }
};

Class AndroidAssetManagerObject::clazz = {
    .classpath = "android/content/res/AssetManager",
    .classname = "AssetManager",
    .managed_methods = nullptr,
    .native_methods = nullptr,
    .fields = nullptr,
    .instance_size = sizeof(AndroidAssetManagerObject),
};

static const int registered_asset_manager =
    ClassRegistry::register_class(AndroidAssetManagerObject::clazz);
static AndroidAssetManagerObject g_java_asset_manager;
static AAssetManager g_native_asset_manager{0x41535354};
static fs::path g_assets_dir;

#if defined(_WIN32)
using asset_offset_t = int64_t;

static int open_asset_file(const fs::path &path)
{
    return _wopen(path.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT);
}

static bool get_asset_file_length(int fd, asset_offset_t *length)
{
    struct _stat64 st {};
    if (_fstat64(fd, &st) != 0 || (st.st_mode & _S_IFMT) != _S_IFREG)
        return false;
    *length = (asset_offset_t)st.st_size;
    return true;
}

static int duplicate_asset_fd(int fd) { return _dup(fd); }
static int close_asset_fd(int fd) { return _close(fd); }
#else
using asset_offset_t = off_t;

static int open_asset_file(const fs::path &path)
{
    return open(path.c_str(), O_RDONLY | O_CLOEXEC);
}

static bool get_asset_file_length(int fd, asset_offset_t *length)
{
    struct stat st {};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    *length = st.st_size;
    return true;
}

static int duplicate_asset_fd(int fd) { return dup(fd); }
static int close_asset_fd(int fd) { return close(fd); }
#endif

static bool safe_asset_name(const char *name, fs::path *relative)
{
    if (!name || !*name)
        return false;
    fs::path p(name);
    if (p.is_absolute())
        return false;

    fs::path clean;
    for (const auto &part : p) {
        const std::string s = part.string();
        if (s.empty() || s == "." || s == "..")
            return false;
        clean /= part;
    }
    *relative = clean;
    return true;
}

} // namespace

extern "C" void open_citadel_asset_manager_configure(const char *game_dir)
{
    g_assets_dir = fs::path(game_dir ? game_dir : "") / "assets";
}

extern "C" jobject open_citadel_asset_manager_java_object(void)
{
    return reinterpret_cast<jobject>(&g_java_asset_manager);
}

extern "C" AAssetManager *AAssetManager_fromJava(JNIEnv *, jobject manager)
{
    if (!manager)
        return nullptr;
    return &g_native_asset_manager;
}

extern "C" AAsset *AAssetManager_open(AAssetManager *manager, const char *name,
                                      int mode)
{
    (void)mode;
    if (manager != &g_native_asset_manager)
        return nullptr;

    fs::path relative;
    if (!safe_asset_name(name, &relative))
        return nullptr;

    fs::path path = g_assets_dir / relative;
    const std::string path_text = path.string();
    int fd = open_asset_file(path);
    if (fd < 0) {
        trace("AAssetManager_open miss: %s", path_text.c_str());
        return nullptr;
    }

    asset_offset_t length = 0;
    if (!get_asset_file_length(fd, &length)) {
        close_asset_fd(fd);
        return nullptr;
    }

    AAsset *asset = new AAsset{fd, length};
    trace("AAssetManager_open: %s (%lld bytes)", path_text.c_str(),
          (long long)asset->length);
    return asset;
}

extern "C" int AAsset_openFileDescriptor(AAsset *asset,
                                         int32_t *out_start,
                                         int32_t *out_length)
{
    /* This API exposes bionic's 32-bit off_t, even though the Windows host
     * stores the actual asset size in 64 bits. All current donor files fit;
     * reject larger inputs rather than writing past the guest's 32-bit output
     * slots or silently truncating the length. */
    if (!asset || asset->fd < 0 || asset->length > INT32_MAX)
        return -1;
    int fd = duplicate_asset_fd(asset->fd);
    if (fd < 0)
        return -1;
    if (out_start)
        *out_start = 0;
    if (out_length)
        *out_length = asset->length;
    return fd;
}

extern "C" void AAsset_close(AAsset *asset)
{
    if (!asset)
        return;
    if (asset->fd >= 0)
        close_asset_fd(asset->fd);
    delete asset;
}

DynLibFunction symtable_android[] = {
    THUNK_DIRECT(AAssetManager_fromJava),
    THUNK_DIRECT(AAssetManager_open),
    THUNK_DIRECT(AAsset_openFileDescriptor),
    THUNK_DIRECT(AAsset_close),
    {nullptr, 0},
};
