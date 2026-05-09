//
// Created by Perfare on 2020/7/4.
//

#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <vector>
#include <string>

void hack_start(const char *game_data_dir, int target_pid) {
    bool load = false;

    target_pid = target_pid == 0 ? gettid() : target_pid;
    
    LOGD("Starting scan for libil2cpp.so");

    for (int i = 0; i < 1; i++) {
        void *handle = xdl_open("libil2cpp.so\0", 0, target_pid);
        
        if (handle != nullptr) {
            load = true;
            il2cpp_api_init(handle);
            il2cpp_dump(game_data_dir);
            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        LOGI("libil2cpp.so not found in thread %d", target_pid);
    }
}

std::string GetLibDir(JavaVM *vms) {
    LOGD("GetLibDir called");

    JNIEnv *env = nullptr;

    vms->AttachCurrentThread(&env, nullptr);
    
    LOGD("Attached to Thread");

    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");

    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                  "getApplicationInfo",
                                                                  "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application,
                                                                     get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                            env->GetObjectClass(application_info), "nativeLibraryDir",
                            "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    } else {
                        LOGE("nativeLibraryDir not found");
                    }
                } else {
                    LOGE("getApplicationInfo not found");
                }
            } else {
                LOGE("application class not found");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
    }
    return {};
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;

    void *(*loadLibrary)(const char *libpath, int flag);

    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);

    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;

    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

void* NativeApexARTLoad(){
    std::string apex_art_path = (sizeof(void*) == 4) ? "/apex/com.android.art/lib/" : "/apex/com.android.art/lib64/";

    std::vector<std::string> libraries = {
        "libartpalette.so",
        "libartbase.so",
        "libnativebridge.so",
        "libdexfile.so",
        "libprofile.so",
        "libart.so", // last value must be libart.so so 'lib_handler' references it after the 'for' loop.
    };

    void* lib_handler;
    std::string lib_path;
    
    for(std::string lib_name : libraries){
        lib_path = apex_art_path + lib_name;

        lib_handler = dlopen(lib_path.c_str(), RTLD_NOW);

        if(lib_handler == nullptr){
            LOGE("Failed to include %s library. Error: %s.", lib_path.c_str(), dlerror());
            
            return nullptr;
        }else{
            LOGE("Library loaded %s located in %p", lib_path.c_str(), lib_handler);
        }
    }
 
    return lib_handler;
}

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    //TODO 等待houdini初始化
    LOGI("Waiting 5s to be ready");
    sleep(5);

    void* runtime_lib = dlopen("libart.so", RTLD_NOW);
    
    LOGI("libart.so %p", runtime_lib);

    if(runtime_lib == nullptr){
        LOGE("Failed to open ART library. Error: %s.\n Trying to open in apex", dlerror());

        if((runtime_lib = NativeApexARTLoad()) == nullptr){
            LOGE("Failed to include Apex ART libraries.");

            return false;
        }
    }

    LOGI("apex libart.so %p", runtime_lib);

    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(runtime_lib, "JNI_GetCreatedJavaVMs");
    
    LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);

    JavaVM *vms_buf[1];
    JavaVM *vms;
    jsize num_vms;

    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);

    LOGD("JNI_GetCreatedJavaVMs status: %d | num_vms: %d | vms: %p", status, num_vms, vms_buf[0]);

    if (status == JNI_OK && num_vms > 0) {
        vms = vms_buf[0];
    } else {
        LOGE("GetCreatedJavaVMs error");
        return false;
    }

    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        munmap(data, length);
        return false;
    }

    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);

            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            ftruncate(fd, (off_t) length);
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);

            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, int target_pid, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir, target_pid);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    // TODO: Add target pid here
    
    auto game_data_dir = (const char *) reserved;

    std::thread hack_thread(hack_start, game_data_dir, 0);
    hack_thread.detach();

    return JNI_VERSION_1_6;
}

#endif