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

static int GetAndroidApiLevel() {
    char prop_value[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.sdk", prop_value);
    return atoi(prop_value);
}

void hack_start(const char *game_data_dir)
{
	bool load = false;
	bool check_have_il2cpp = false;
	for (int i = 0; i < 10; i++)
	{
		void *handle = xdl_open("libil2cpp.so", 0);
		if (handle)
		{
			load = true;
			il2cpp_api_init(handle);
			il2cpp_dump(game_data_dir);
			break;
		}
		else
		{
			if (!check_have_il2cpp)
			{
				void *libunity = xdl_open("libunity.so", 0);
				if (libunity)
				{
					xdl_info_t info;
					xdl_info(libunity, XDL_DI_DLINFO, &info);
					std::string lib_path = info.dli_fname;
					lib_path = lib_path.substr(0, lib_path.find_last_of('/'));
					lib_path += "/libil2cpp.so";
					LOGI("libil2cpp.so path %s", lib_path.data());
					if (access(lib_path.data(), F_OK) == -1)
					{
						LOGI("game not have libil2cpp.so,use default libunity.so");
						load = true;
						il2cpp_api_init(libunity);
						il2cpp_dump(game_data_dir);
						xdl_close(libunity);
						break;
					}
					check_have_il2cpp = true;
					xdl_close(libunity);
				}
			}

			sleep(1);
		}
	}
	if (!load)
	{
		LOGI("libil2cpp.so not found in thread %d", gettid());
	}
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

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = GetAndroidApiLevel();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    //TODO 等待houdini初始化
    sleep(5);

    auto libhoudini = dlopen("libhoudini.so", RTLD_NOW);
    if (libhoudini) {
        LOGI("houdini %p", libhoudini);

        int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
        ftruncate(fd, (off_t) length);
        void *mem = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        memcpy(mem, data, length);
        munmap(mem, length);
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
        LOGI("arm path %s", path);

        auto callbacks = (NativeBridgeCallbacks *) dlsym(libhoudini, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);
            auto libart = dlopen("libart.so", RTLD_NOW);
            auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart,
                                                                                     "JNI_GetCreatedJavaVMs");
            LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);
            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                JavaVM *vms_buf[1];
                jsize num_vms;
                jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
                if (status == JNI_OK && num_vms > 0) {
                    auto init = (void (*)(JavaVM *vm, void *reserved)) callbacks->getTrampoline(
                            arm_handle, "JNI_OnLoad", nullptr, 0);
                    LOGI("JNI_OnLoad %p", init);
                    init(vms_buf[0], (void *) game_data_dir);
                }
            }
        }
        close(fd);
    } else {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}

#endif
