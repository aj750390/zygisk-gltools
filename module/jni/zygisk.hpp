#pragma once

#include <jni.h>
#include <stdint.h>

#define ZYGISK_API_VERSION 5

namespace zygisk {

struct ApiTable;
struct AppSpecializeArgs;
struct ServerSpecializeArgs;

class Api {
public:
    virtual bool registerModule(struct ModuleAbi *module) = 0;
    virtual void hookJniNativeMethods(JNIEnv *env, const char *className, const JNINativeMethod *methods, int numMethods) = 0;
    virtual void pltHookRegister(const char *regex, const char *symbol, void *newFunc, void **oldFunc) = 0;
    virtual bool pltHookCommit() = 0;
    virtual int connectCompanion() = 0;
    virtual void setOption(int opt) = 0;
    virtual int getFlags() = 0;
};

struct AppSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jint &mount_external;
    jstring &se_info;
    jstring &nice_name;
    jboolean &is_child_zygote;
    jstring &instruction_set;
    jstring &app_data_dir;
    jboolean &is_top_app;
    jobjectArray &pkg_data_info_list;
    jobjectArray &whitelisted_data_info_list;
    jboolean &mount_data_dirs;
    jboolean &mount_storage_dirs;
};

struct ModuleAbi {
    long api_version;
    struct ModuleImpl *impl;
    void (*preAppSpecialize)(struct ModuleImpl *, AppSpecializeArgs *);
    void (*postAppSpecialize)(struct ModuleImpl *, const AppSpecializeArgs *);
    void (*preServerSpecialize)(struct ModuleImpl *, ServerSpecializeArgs *);
    void (*postServerSpecialize)(struct ModuleImpl *, const ServerSpecializeArgs *);
};

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(clazz)                                              \
extern "C" [[gnu::visibility("default")]]                                         \
void zygisk_module_entry(zygisk::Api *api, JNIEnv *env) {                         \
    static clazz module;                                                           \
    static zygisk::ModuleAbi abi = {                                               \
        .api_version = ZYGISK_API_VERSION,                                         \
        .impl = reinterpret_cast<zygisk::ModuleImpl *>(&module),                   \
        .preAppSpecialize = [](zygisk::ModuleImpl *m, zygisk::AppSpecializeArgs *args) { \
            reinterpret_cast<clazz *>(m)->preAppSpecialize(args);                 \
        },                                                                         \
        .postAppSpecialize = [](zygisk::ModuleImpl *m, const zygisk::AppSpecializeArgs *args) { \
            reinterpret_cast<clazz *>(m)->postAppSpecialize(args);                \
        },                                                                         \
        .preServerSpecialize = nullptr,                                            \
        .postServerSpecialize = nullptr,                                           \
    };                                                                             \
    api->registerModule(&abi);                                                     \
}
