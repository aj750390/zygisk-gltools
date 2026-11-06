#pragma once
#include <jni.h>
#include <stddef.h>
#include <stdint.h>

#define ZYGISK_API_VERSION 4

namespace zygisk {

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

struct ServerSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jlong &permitted_capabilities;
    jlong &effective_capabilities;
};

class Api;
class ModuleBase {
public:
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
};

enum StateFlags : uint32_t {
    PROCESS_GRANTED_ROOT = (1u << 0),
    PROCESS_ON_DENYLIST = (1u << 1),
};

enum DlcloseFlag : uint32_t {
    DLCLOSE_MODULE_LIBRARY = 0,
};

class Api {
public:
    bool connectCompanion();
    int getModuleDir();
    void setOption(DlcloseFlag flag);
    uint32_t getFlags();
    void hookJniNativeMethods(JNIEnv *env, const char *className, const JNINativeMethod *methods, int numMethods);
    void pltHookRegister(const char *regex, const char *symbol, void *newFunc, void **oldFunc);
    bool pltHookCommit();

    struct {
        bool (*connectCompanion)(void*);
        int (*getModuleDir)(void*);
        void (*setOption)(void*, DlcloseFlag);
        uint32_t (*getFlags)(void*);
        void (*hookJniNativeMethods)(void*, JNIEnv*, const char*, const JNINativeMethod*, int);
        void (*pltHookRegister)(void*, const char*, const char*, void*, void**);
        bool (*pltHookCommit)(void*);
    } *impl;
    void *instance;
};

inline bool Api::connectCompanion() { return impl->connectCompanion(instance); }
inline int Api::getModuleDir() { return impl->getModuleDir(instance); }
inline void Api::setOption(DlcloseFlag flag) { impl->setOption(instance, flag); }
inline uint32_t Api::getFlags() { return impl->getFlags(instance); }
inline void Api::hookJniNativeMethods(JNIEnv *env, const char *className, const JNINativeMethod *methods, int numMethods) {
    impl->hookJniNativeMethods(instance, env, className, methods, numMethods);
}
inline void Api::pltHookRegister(const char *regex, const char *symbol, void *newFunc, void **oldFunc) {
    impl->pltHookRegister(instance, regex, symbol, newFunc, oldFunc);
}
inline bool Api::pltHookCommit() { return impl->pltHookCommit(instance); }

typedef void (*CompanionHandler)(int);

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(clazz) \
extern "C" [[gnu::visibility("default")]] void zygisk_module_entry( \
    zygisk::Api *api, JNIEnv *env) { \
    static clazz module; \
    module.onLoad(api, env); \
}

#define REGISTER_ZYGISK_COMPANION(func) \
extern "C" [[gnu::visibility("default")]] void zygisk_companion_entry(int client) { \
    func(client); \
}
