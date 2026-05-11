#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_ANDROID

#include <EGL/egl.h>
#include <jni.h>
#include <pthread.h>
#include <time.h>
#include <dlfcn.h>
#include <atomic>

static std::atomic<int> s_targetFps{60};
static std::atomic<bool> s_threadActive{false};
static pthread_t s_renderThread;
static JavaVM* s_jvm = nullptr;
static jobject s_glView = nullptr;
static jmethodID s_requestRender = nullptr;

static JavaVM* getJVM() {
    if (s_jvm) return s_jvm;
    void* art = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
    if (art) {
        auto fn = (jint(*)(JavaVM**, jsize, jsize*))dlsym(art, "JNI_GetCreatedJavaVMs");
        if (fn) { jsize n = 0; fn(&s_jvm, 1, &n); }
        dlclose(art);
    }
    return s_jvm;
}

static JNIEnv* getEnv() {
    JavaVM* vm = getJVM();
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static jobject findGLSurfaceView(JNIEnv* env, jobject view, jclass targetClass) {
    if (!view) return nullptr;
    if (env->IsInstanceOf(view, targetClass)) return view;

    jclass vgClass = env->FindClass("android/view/ViewGroup");
    if (!vgClass || !env->IsInstanceOf(view, vgClass)) return nullptr;

    jmethodID getCount = env->GetMethodID(vgClass, "getChildCount", "()I");
    jmethodID getChild  = env->GetMethodID(vgClass, "getChildAt",   "(I)Landroid/view/View;");

    jint n = env->CallIntMethod(view, getCount);
    for (jint i = 0; i < n; i++) {
        jobject child = env->CallObjectMethod(view, getChild, i);
        if (!child) continue;
        jobject found = findGLSurfaceView(env, child, targetClass);
        if (found) return found;
        env->DeleteLocalRef(child);
    }
    return nullptr;
}

static bool initGLView() {
    if (s_glView) return true;

    JNIEnv* env = getEnv();
    if (!env) return false;

    jclass helperClass = env->FindClass("org/cocos2dx/lib/Cocos2dxHelper");
    if (!helperClass) return false;

    jmethodID getActivity = env->GetStaticMethodID(helperClass, "getActivity", "()Landroid/app/Activity;");
    if (!getActivity) return false;

    jobject activity = env->CallStaticObjectMethod(helperClass, getActivity);
    if (!activity) return false;

    jclass actClass   = env->FindClass("android/app/Activity");
    jmethodID getWin  = env->GetMethodID(actClass, "getWindow", "()Landroid/view/Window;");
    jobject window    = env->CallObjectMethod(activity, getWin);
    if (!window) return false;

    jclass winClass      = env->FindClass("android/view/Window");
    jmethodID getDecor   = env->GetMethodID(winClass, "getDecorView", "()Landroid/view/View;");
    jobject decorView    = env->CallObjectMethod(window, getDecor);
    if (!decorView) return false;

    jclass glsvClass = env->FindClass("org/cocos2dx/lib/Cocos2dxGLSurfaceView");
    if (!glsvClass) glsvClass = env->FindClass("android/opengl/GLSurfaceView");
    if (!glsvClass) return false;

    jobject view = findGLSurfaceView(env, decorView, glsvClass);
    if (!view) return false;

    jclass viewClass = env->GetObjectClass(view);

    jmethodID setRenderMode = env->GetMethodID(viewClass, "setRenderMode", "(I)V");
    if (setRenderMode) env->CallVoidMethod(view, setRenderMode, 0);

    s_requestRender = env->GetMethodID(viewClass, "requestRender", "()V");
    s_glView = env->NewGlobalRef(view);

    return s_glView && s_requestRender;
}

static void* renderLoop(void*) {
    JNIEnv* env = getEnv();
    if (!env || !s_glView || !s_requestRender) return nullptr;

    while (s_threadActive) {
        env->CallVoidMethod(s_glView, s_requestRender);

        struct timespec ts;
        ts.tv_sec  = 0;
        ts.tv_nsec = 1000000000L / s_targetFps.load();
        nanosleep(&ts, nullptr);
    }

    getJVM()->DetachCurrentThread();
    return nullptr;
}

static void startBypassThread() {
    if (s_threadActive.exchange(true)) return;
    pthread_create(&s_renderThread, nullptr, renderLoop, nullptr);
}

static void stopBypassThread() {
    if (!s_threadActive.exchange(false)) return;
    pthread_join(s_renderThread, nullptr);
}

class $modify(FPSBypassDirector, CCDirector) {
    void drawScene() {
        static bool initialized = false;
        if (!initialized) {
            initialized = true;

            EGLDisplay display = eglGetCurrentDisplay();
            if (display != EGL_NO_DISPLAY) eglSwapInterval(display, 0);

            if (Mod::get()->getSettingValue<bool>("disable-vsync")) {
                if (initGLView()) startBypassThread();
            }

            CCDirector::sharedDirector()->setAnimationInterval(1.0 / s_targetFps.load());
        }
        CCDirector::drawScene();
    }
};

$on_mod(Loaded) {
    s_targetFps = Mod::get()->getSettingValue<int64_t>("fps");

    listenForSettingChanges<int64_t>("fps", [](int64_t value) {
        s_targetFps = (int)value;
        CCDirector::sharedDirector()->setAnimationInterval(1.0 / value);
    });

    listenForSettingChanges<bool>("disable-vsync", [](bool value) {
        if (value) {
            if (initGLView()) startBypassThread();
        } else {
            stopBypassThread();
            JNIEnv* env = getEnv();
            if (env && s_glView) {
                jclass cls = env->GetObjectClass(s_glView);
                jmethodID setMode = env->GetMethodID(cls, "setRenderMode", "(I)V");
                if (setMode) env->CallVoidMethod(s_glView, setMode, 1);
            }
        }
    });
}

#endif
