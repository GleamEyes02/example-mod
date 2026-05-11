#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_ANDROID

#include <EGL/egl.h>
#include <time.h>

static bool s_vsyncDisabled = false;
static int s_targetFps = 60;
static int64_t s_lastFrameNs = 0;

static void applyDisableVsync() {
    EGLDisplay display = eglGetCurrentDisplay();
    if (display != EGL_NO_DISPLAY) {
        eglSwapInterval(display, 0);
        s_vsyncDisabled = true;
    }
}

static int64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleepUntilNextFrame() {
    if (s_targetFps <= 0) return;
    int64_t frameNs = 1000000000LL / s_targetFps;
    int64_t now = nowNs();

    if (s_lastFrameNs == 0) {
        s_lastFrameNs = now;
        return;
    }

    int64_t next = s_lastFrameNs + frameNs;
    int64_t sleepFor = next - now;

    if (sleepFor > 0 && sleepFor < frameNs * 2) {
        struct timespec req;
        req.tv_sec = sleepFor / 1000000000LL;
        req.tv_nsec = sleepFor % 1000000000LL;
        nanosleep(&req, nullptr);
        s_lastFrameNs = next;
    } else {
        s_lastFrameNs = nowNs();
    }
}

class $modify(FPSBypassDirector, CCDirector) {
    void drawScene() {
        bool vsyncEnabled = Mod::get()->getSettingValue<bool>("disable-vsync");

        if (vsyncEnabled && !s_vsyncDisabled) {
            applyDisableVsync();
        }

        CCDirector::drawScene();

        if (vsyncEnabled) {
            sleepUntilNextFrame();
        }
    }
};

$on_mod(Loaded) {
    s_targetFps = Mod::get()->getSettingValue<int64_t>("fps");
    s_vsyncDisabled = false;

    listenForSettingChanges<int64_t>("fps", [](int64_t value) {
        s_targetFps = (int)value;
        s_lastFrameNs = 0;
    });

    listenForSettingChanges<bool>("disable-vsync", [](bool value) {
        if (!value) {
            EGLDisplay display = eglGetCurrentDisplay();
            if (display != EGL_NO_DISPLAY) {
                eglSwapInterval(display, 1);
            }
            s_vsyncDisabled = false;
        } else {
            s_vsyncDisabled = false;
        }
    });
}

#endif
