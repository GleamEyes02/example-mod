#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_ANDROID

#include <EGL/egl.h>
#include <time.h>

static int s_targetFps = 60;

static void applyFps() {
    auto director = CCDirector::sharedDirector();
    if (!director) return;
    director->setAnimationInterval(1.0 / s_targetFps);

    EGLDisplay display = eglGetCurrentDisplay();
    if (display != EGL_NO_DISPLAY) {
        eglSwapInterval(display, 0);
    }
}

class $modify(FPSBypassDirector, CCDirector) {
    void drawScene() {
        static bool applied = false;
        if (!applied) {
            applyFps();
            applied = true;
        }
        CCDirector::drawScene();
    }
};

$on_mod(Loaded) {
    s_targetFps = Mod::get()->getSettingValue<int64_t>("fps");

    listenForSettingChanges<int64_t>("fps", [](int64_t value) {
        s_targetFps = (int)value;
        applyFps();
    });

    listenForSettingChanges<bool>("disable-vsync", [](bool value) {
        EGLDisplay display = eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY) {
            eglSwapInterval(display, value ? 0 : 1);
        }
    });
}

#endif
