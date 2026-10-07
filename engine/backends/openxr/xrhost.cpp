// SPDX-License-Identifier: GPL-3.0-or-later
// THE OPENXR HOST - see `xrhost.h`. `todo/quest-port.md` §5 step 5.
#include "xrhost.h"

#include "input/pad.h"
#include "platform/frontend.h"
#include "vr/xrspace.h"

#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace omk::xr {
namespace {

// GL_EXT_sRGB_write_control: the game's colours are ALREADY gamma-encoded, so
// they are written into the sRGB image as they are (no linear -> sRGB encode
// on the way in) and the compositor reads them as the sRGB they are.
constexpr GLenum kFramebufferSrgb = 0x8DB9;
constexpr std::int64_t kSrgb8Alpha8 = 0x8C43, kRgba8 = 0x8058;

// The screen: 1.2 m ahead, its centre at the head's height when the session
// began (LOCAL space), 1.2 m wide (~53 degrees) at the frame's own aspect.
// It was 2 m and 2.4 m (5a); over the eyes (6a) the scene is often NEARER
// than 2 m - a speaker at a table - and text read at 2 m over a face at 1 m
// "hurts to read (stereoscopic issue)" (the reader, 2026-10-07): the panel
// comes in front of most of what is around.
constexpr float kScreenDistance = 1.2f, kScreenWidth = 1.2f;

// One swapchain and an FBO per image: the quad's, or an eye's.
struct Chain {
    XrSwapchain sc = XR_NULL_HANDLE;
    int w = 0, h = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> fbos;
    bool held = false;      // an image acquired this frame and not yet released
    bool drawn = false;     // released this frame: it goes in a layer
    GLuint fbo = 0;         // the held image's framebuffer
};

struct Host {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE, view = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false, exitRequested = false;
    std::int64_t format = 0;
    bool srgb = false, srgbWriteControl = false;

    Chain quad, eye[2];
    int recW = 0, recH = 0, maxW = 0, maxH = 0;   // the runtime's eye sizes
    // XR_EXT_performance_settings: the clocks asked for, when the runtime has it
    PFN_xrPerfSettingsSetPerformanceLevelEXT setPerf = nullptr;
    int perfGpu = -1, perfCpu = -1;   // the levels last asked (XrPerfSettingsLevelEXT)

    // this frame
    bool begun = false;
    bool quadOverEyes = false;      // a screen is open: the composed frame over the eyes
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    bool viewsValid = false;
    long frames = 0, eyeFrames = 0;

    // the controllers
    XrActionSet set = XR_NULL_HANDLE;
    XrAction stick = XR_NULL_HANDLE, a = XR_NULL_HANDLE, b = XR_NULL_HANDLE,
             x = XR_NULL_HANDLE, y = XR_NULL_HANDLE, trigger = XR_NULL_HANDLE,
             grip = XR_NULL_HANDLE, menu = XR_NULL_HANDLE, thumbClick = XR_NULL_HANDLE,
             aim = XR_NULL_HANDLE;
    XrPath hand[2]{XR_NULL_PATH, XR_NULL_PATH};
    XrSpace aimSpace[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool actionsAttached = false;
};
Host g;

bool ok(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r)) return true;
    char name[XR_MAX_RESULT_STRING_SIZE] = "";
    if (g.instance) xrResultToString(g.instance, r, name);
    std::printf("openxr: %s failed: %d %s\n", what, static_cast<int>(r), name);
    return false;
}

XrPath path(const char* s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(g.instance, s, &p);
    return p;
}

XrAction makeAction(const char* name, const char* label, XrActionType type) {
    XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
    std::snprintf(ci.actionName, sizeof ci.actionName, "%s", name);
    std::snprintf(ci.localizedActionName, sizeof ci.localizedActionName, "%s", label);
    ci.actionType = type;
    ci.countSubactionPaths = 2;
    ci.subactionPaths = g.hand;
    XrAction act = XR_NULL_HANDLE;
    ok(xrCreateAction(g.set, &ci, &act), name);
    return act;
}

// The Touch controllers' bindings (the Oculus Touch interaction profile, which
// every Quest runtime serves). Which button is which slot is in readControllers.
bool makeActions() {
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(si.actionSetName, sizeof si.actionSetName, "omk");
    std::snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "OMK");
    if (!ok(xrCreateActionSet(g.instance, &si, &g.set), "xrCreateActionSet")) return false;
    g.hand[0] = path("/user/hand/left");
    g.hand[1] = path("/user/hand/right");
    g.stick      = makeAction("stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g.a          = makeAction("a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.b          = makeAction("b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.x          = makeAction("x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.y          = makeAction("y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.trigger    = makeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    g.grip       = makeAction("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
    g.menu       = makeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.thumbClick = makeAction("thumbclick", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.aim        = makeAction("aim", "Aim", XR_ACTION_TYPE_POSE_INPUT);
    const XrActionSuggestedBinding b[] = {
        {g.stick, path("/user/hand/left/input/thumbstick")},
        {g.stick, path("/user/hand/right/input/thumbstick")},
        {g.a, path("/user/hand/right/input/a/click")},
        {g.b, path("/user/hand/right/input/b/click")},
        {g.x, path("/user/hand/left/input/x/click")},
        {g.y, path("/user/hand/left/input/y/click")},
        {g.trigger, path("/user/hand/left/input/trigger/value")},
        {g.trigger, path("/user/hand/right/input/trigger/value")},
        {g.grip, path("/user/hand/left/input/squeeze/value")},
        {g.grip, path("/user/hand/right/input/squeeze/value")},
        {g.menu, path("/user/hand/left/input/menu/click")},
        {g.thumbClick, path("/user/hand/left/input/thumbstick/click")},
        {g.thumbClick, path("/user/hand/right/input/thumbstick/click")},
        {g.aim, path("/user/hand/left/input/aim/pose")},
        {g.aim, path("/user/hand/right/input/aim/pose")},
    };
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.suggestedBindings = b;
    sb.countSuggestedBindings = static_cast<std::uint32_t>(std::size(b));
    return ok(xrSuggestInteractionProfileBindings(g.instance, &sb), "xrSuggestInteractionProfileBindings");
}

void pickFormat() {
    std::uint32_t n = 0;
    xrEnumerateSwapchainFormats(g.session, 0, &n, nullptr);
    std::vector<std::int64_t> formats(n);
    xrEnumerateSwapchainFormats(g.session, n, &n, formats.data());
    if (std::find(formats.begin(), formats.end(), kSrgb8Alpha8) != formats.end()) g.format = kSrgb8Alpha8;
    else if (std::find(formats.begin(), formats.end(), kRgba8) != formats.end()) g.format = kRgba8;
    else if (n) g.format = formats[0];
    g.srgb = g.format == kSrgb8Alpha8;
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    g.srgbWriteControl = ext && std::strstr(ext, "GL_EXT_sRGB_write_control");
}

bool makeChain(Chain& c, int w, int h, const char* what) {
    c.w = w;
    c.h = h;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = g.format;
    ci.sampleCount = 1;
    ci.width = static_cast<std::uint32_t>(w);
    ci.height = static_cast<std::uint32_t>(h);
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!ok(xrCreateSwapchain(g.session, &ci, &c.sc), what)) return false;
    std::uint32_t n = 0;
    xrEnumerateSwapchainImages(c.sc, 0, &n, nullptr);
    c.images.assign(n, XrSwapchainImageOpenGLESKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(c.sc, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(c.images.data()));
    c.fbos.assign(n, 0);
    glGenFramebuffers(static_cast<GLsizei>(n), c.fbos.data());
    for (std::uint32_t i = 0; i < n; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, c.fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.images[i].image, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::printf("openxr: %s image %u framebuffer incomplete\n", what, i);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::printf("openxr: %s %dx%d, %u images\n", what, w, h, n);
    return true;
}

// Acquire `c`'s next image and bind its framebuffer; idempotent in a frame.
bool acquire(Chain& c) {
    if (!c.held) {
        if (c.drawn) return false;   // one image a frame
        std::uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        if (!ok(xrAcquireSwapchainImage(c.sc, &ai, &idx), "xrAcquireSwapchainImage")) return false;
        if (!ok(xrWaitSwapchainImage(c.sc, &wi), "xrWaitSwapchainImage")) return false;
        c.held = true;
        c.fbo = c.fbos[idx];
    }
    glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
    if (g.srgb && g.srgbWriteControl) glDisable(kFramebufferSrgb);
    return true;
}

void release(Chain& c) {
    if (!c.held) return;
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    ok(xrReleaseSwapchainImage(c.sc, &ri), "xrReleaseSwapchainImage");
    c.held = false;
    c.drawn = true;
}

// THE CLOCKS (the reader, 2026-10-07: "can a perf request be sent at run
// time, when the scale is modified?"). At 1.3x with 4x MSAA a Quest 2 ran
// 62-65 of 72 fps, GPU-bound, while the runtime held the GPU at level 2 of 4.
// The CPU asks SUSTAINED_HIGH always (the game thread is the nearer budget
// with the costly enhancements); the GPU asks SUSTAINED_HIGH above 1.0x the
// recommended eye, and is left at SUSTAINED_LOW - the runtime's own range -
// at or below it. Hints, not orders: the VrApi line says what was granted.
const char* perfName(int l) {
    switch (l) {
    case XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT:  return "POWER_SAVINGS";
    case XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT:  return "SUSTAINED_LOW";
    case XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT: return "SUSTAINED_HIGH";
    case XR_PERF_SETTINGS_LEVEL_BOOST_EXT:          return "BOOST";
    default:                                        return "?";
    }
}
void applyPerf() {
    if (!g.setPerf || !g.session || !g.running) return;
    const bool above = g.recW > 0 && g.eye[0].w > g.recW;
    const int gpu = above ? XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT : XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
    const int cpu = XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
    if (gpu != g.perfGpu &&
        ok(g.setPerf(g.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, static_cast<XrPerfSettingsLevelEXT>(gpu)),
           "xrPerfSettingsSetPerformanceLevelEXT (GPU)")) {
        g.perfGpu = gpu;
        std::printf("openxr: GPU asked %s (eyes %dx%d, %s the recommended)\n", perfName(gpu),
                    g.eye[0].w, g.eye[0].h, above ? "above" : "at or below");
    }
    if (cpu != g.perfCpu &&
        ok(g.setPerf(g.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, static_cast<XrPerfSettingsLevelEXT>(cpu)),
           "xrPerfSettingsSetPerformanceLevelEXT (CPU)")) {
        g.perfCpu = cpu;
        std::printf("openxr: CPU asked %s\n", perfName(cpu));
    }
}

void pollEvents() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (g.instance && xrPollEvent(g.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& sc = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            g.state = sc.state;
            std::printf("openxr: session state %d\n", static_cast<int>(g.state));
            if (g.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                g.running = ok(xrBeginSession(g.session, &bi), "xrBeginSession");
                g.perfGpu = g.perfCpu = -1;   // a new session asks again
                applyPerf();
            } else if (g.state == XR_SESSION_STATE_STOPPING) {
                ok(xrEndSession(g.session), "xrEndSession");
                g.running = false;
            } else if (g.state == XR_SESSION_STATE_EXITING || g.state == XR_SESSION_STATE_LOSS_PENDING) {
                g.running = false;
                g.exitRequested = true;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT) {
            // the runtime's own word on the clocks: a domain warming, throttled
            const auto& pe = *reinterpret_cast<XrEventDataPerfSettingsEXT*>(&ev);
            static const char* kSub[] = {"?", "compositing", "rendering", "thermal"};
            // the notification levels are 0 / 25 / 75 (XrPerfSettingsNotificationLevelEXT)
            const auto note = [](XrPerfSettingsNotificationLevelEXT l) {
                return l == XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT ? "normal"
                     : l == XR_PERF_SETTINGS_NOTIF_LEVEL_WARNING_EXT ? "WARNING"
                     : l == XR_PERF_SETTINGS_NOTIF_LEVEL_IMPAIRED_EXT ? "IMPAIRED" : "?";
            };
            std::printf("openxr: perf event - %s %s: %s -> %s\n",
                        pe.domain == XR_PERF_SETTINGS_DOMAIN_CPU_EXT ? "CPU" : "GPU",
                        pe.subDomain >= 1 && pe.subDomain <= 3 ? kSub[pe.subDomain] : "?",
                        note(pe.fromLevel), note(pe.toLevel));
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g.running = false;
            g.exitRequested = true;
        }
        ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

// The headset's frame begun - waited for (this is what paces) and begun -
// once per game frame, by whichever of headPose / frameTarget comes first.
bool ensureFrame() {
    if (!g.session) return false;
    if (g.begun) return true;
    pollEvents();
    if (!g.running) return false;
    g.frame = XrFrameState{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    if (!ok(xrWaitFrame(g.session, &wi, &g.frame), "xrWaitFrame")) return false;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (!ok(xrBeginFrame(g.session, &bi), "xrBeginFrame")) return false;
    g.begun = true;
    g.viewsValid = false;
    return true;
}

void toPose(const XrPosef& p, vr::Pose& out) {
    out.pos[0] = p.position.x; out.pos[1] = p.position.y; out.pos[2] = p.position.z;
    out.quat[0] = p.orientation.x; out.quat[1] = p.orientation.y;
    out.quat[2] = p.orientation.z; out.quat[3] = p.orientation.w;
}

}  // namespace

bool start(int screenW, int screenH, float scale) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    JavaVM* vm = nullptr;
    if (!env || !activity || env->GetJavaVM(&vm) != JNI_OK) {
        std::printf("openxr: no Java VM or activity - flat\n");
        return false;
    }
    activity = env->NewGlobalRef(activity);   // kept for the instance's life

    PFN_xrInitializeLoaderKHR initLoader = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                          reinterpret_cast<PFN_xrVoidFunction*>(&initLoader));
    if (initLoader) {
        XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        li.applicationVM = vm;
        li.applicationContext = activity;
        if (!ok(initLoader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&li)), "xrInitializeLoaderKHR"))
            return false;
    }

    // XR_EXT_performance_settings where the runtime lists it (the clocks,
    // `applyPerf`); the two the session cannot do without, always
    bool havePerf = false;
    {
        std::uint32_t ne = 0;
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &ne, nullptr);
        std::vector<XrExtensionProperties> props(ne, {XR_TYPE_EXTENSION_PROPERTIES});
        xrEnumerateInstanceExtensionProperties(nullptr, ne, &ne, props.data());
        for (const auto& p : props)
            if (!std::strcmp(p.extensionName, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) havePerf = true;
    }
    const char* exts[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                          XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ai.applicationVM = vm;
    ai.applicationActivity = activity;
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &ai;
    std::snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "OMK");
    ci.applicationInfo.applicationVersion = 1;
    std::snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "OMK");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ci.enabledExtensionCount = havePerf ? 3 : 2;
    ci.enabledExtensionNames = exts;
    if (!ok(xrCreateInstance(&ci, &g.instance), "xrCreateInstance")) return false;
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(g.instance, &ip);
    std::printf("openxr: runtime %s %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
                XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    if (havePerf)
        xrGetInstanceProcAddr(g.instance, "xrPerfSettingsSetPerformanceLevelEXT",
                              reinterpret_cast<PFN_xrVoidFunction*>(&g.setPerf));
    std::printf("openxr: performance settings %s\n",
                g.setPerf ? "available - the clocks follow the eye scale" : "NOT available - the runtime's own clocks");

    XrSystemGetInfo sg{XR_TYPE_SYSTEM_GET_INFO};
    sg.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!ok(xrGetSystem(g.instance, &sg, &g.system), "xrGetSystem")) return false;

    // required before the session: the runtime's GLES version range
    PFN_xrGetOpenGLESGraphicsRequirementsKHR glesReq = nullptr;
    xrGetInstanceProcAddr(g.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                          reinterpret_cast<PFN_xrVoidFunction*>(&glesReq));
    XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!glesReq || !ok(glesReq(g.instance, g.system, &req), "xrGetOpenGLESGraphicsRequirementsKHR")) return false;
    std::printf("openxr: GLES %u.%u .. %u.%u wanted; the context is %s\n",
                XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
                XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // what the runtime would render an eye at - said, for the resolution choice
    std::uint32_t nv = 0;
    xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, nullptr);
    std::vector<XrViewConfigurationView> vcv(nv, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nv, &nv, vcv.data());
    // THE EYE'S SIZE, the runtime's recommendation (2026-10-07: half the
    // 1280x720 frame, 640x720, was "too low" in the street on a Quest 2,
    // which recommends 1440x1584 - and the GPU had the time, ~5 ms a frame)
    int eyeW = screenW / 2, eyeH = screenH;
    if (nv) {
        g.recW = static_cast<int>(vcv[0].recommendedImageRectWidth);
        g.recH = static_cast<int>(vcv[0].recommendedImageRectHeight);
        g.maxW = static_cast<int>(vcv[0].maxImageRectWidth);
        g.maxH = static_cast<int>(vcv[0].maxImageRectHeight);
        // ...times `--vr-scale` (the reader, the same day: "maybe a little
        // higher, it is an old game"; at 1.0 the GPU took 6.7 of 13.9 ms),
        // capped at the runtime's own maximum
        const float s = std::max(0.25f, scale);
        eyeW = std::min(static_cast<int>(std::lround(vcv[0].recommendedImageRectWidth * s)),
                        static_cast<int>(vcv[0].maxImageRectWidth));
        eyeH = std::min(static_cast<int>(std::lround(vcv[0].recommendedImageRectHeight * s)),
                        static_cast<int>(vcv[0].maxImageRectHeight));
        std::printf("openxr: the runtime recommends %ux%u an eye - drawn at %dx%d (--vr-scale %.2f)\n",
                    vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight, eyeW, eyeH,
                    static_cast<double>(s));
    }

    if (!makeActions()) return false;

    // the session, on SDL's own EGL context
    XrGraphicsBindingOpenGLESAndroidKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gb.display = eglGetCurrentDisplay();
    gb.context = eglGetCurrentContext();
    EGLint cfgId = 0, nCfg = 0;
    eglQueryContext(gb.display, gb.context, EGL_CONFIG_ID, &cfgId);
    const EGLint attrs[] = {EGL_CONFIG_ID, cfgId, EGL_NONE};
    EGLConfig cfg = nullptr;
    eglChooseConfig(gb.display, attrs, &cfg, 1, &nCfg);
    gb.config = cfg;
    XrSessionCreateInfo sc{XR_TYPE_SESSION_CREATE_INFO};
    sc.next = &gb;
    sc.systemId = g.system;
    if (!ok(xrCreateSession(g.instance, &sc, &g.session), "xrCreateSession")) return false;

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    if (!ok(xrCreateReferenceSpace(g.session, &rs, &g.local), "xrCreateReferenceSpace (LOCAL)")) return false;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!ok(xrCreateReferenceSpace(g.session, &rs, &g.view), "xrCreateReferenceSpace (VIEW)")) return false;

    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo as{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        as.action = g.aim;
        as.subactionPath = g.hand[h];
        as.poseInActionSpace.orientation.w = 1.0f;
        ok(xrCreateActionSpace(g.session, &as, &g.aimSpace[h]), "xrCreateActionSpace (aim)");
    }
    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &g.set;
    g.actionsAttached = ok(xrAttachSessionActionSets(g.session, &at), "xrAttachSessionActionSets");

    pickFormat();
    std::printf("openxr: format 0x%llx (%s), sRGB write control %s\n",
                static_cast<unsigned long long>(g.format), g.srgb ? "sRGB" : "UNORM",
                g.srgbWriteControl ? "yes" : "NO");
    if (!makeChain(g.quad, screenW, screenH, "quad")) return false;
    if (!makeChain(g.eye[0], eyeW, eyeH, "left eye")) return false;
    if (!makeChain(g.eye[1], eyeW, eyeH, "right eye")) return false;
    std::printf("openxr: session created - a flat frame goes to a %.1f m screen %.1f m ahead, "
                "a world frame to the eyes\n", kScreenWidth, kScreenDistance);
    pollEvents();
    return true;
}

bool running() { return g.session != XR_NULL_HANDLE && g.running; }

void eyeSize(int& w, int& h) {
    w = g.eye[0].w;
    h = g.eye[0].h;
}

namespace {
void destroyChain(Chain& c) {
    if (!c.fbos.empty()) glDeleteFramebuffers(static_cast<GLsizei>(c.fbos.size()), c.fbos.data());
    if (c.sc) xrDestroySwapchain(c.sc);
    c = Chain{};
}
}  // namespace

int eyeModes(std::vector<std::string>& names) {
    names.clear();
    if (!g.recW || !g.recH) return -1;
    static const float kScales[] = {0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f};
    int cur = -1, lastW = 0, lastH = 0;
    for (float s : kScales) {
        const int w = std::min(static_cast<int>(std::lround(g.recW * s)), g.maxW ? g.maxW : 1 << 14);
        const int h = std::min(static_cast<int>(std::lround(g.recH * s)), g.maxH ? g.maxH : 1 << 14);
        if (w == lastW && h == lastH) continue;   // capped twice
        lastW = w;
        lastH = h;
        if (w == g.eye[0].w && h == g.eye[0].h) cur = static_cast<int>(names.size());
        // the SCALE beside the size (the reader: "use the resolution line for
        // the scale"); the row parses only the leading "W x H"
        char n[48];
        std::snprintf(n, sizeof n, "%d x %d (%.1fx)", w, h, static_cast<double>(s));
        names.push_back(n);
    }
    if (cur < 0) {   // a size from --vr-scale that is not on the ladder
        cur = static_cast<int>(names.size());
        names.push_back(std::to_string(g.eye[0].w) + " x " + std::to_string(g.eye[0].h));
    }
    return cur;
}

bool setEyeSize(int w, int h) {
    if (!g.session || g.begun || w < 64 || h < 64) return false;
    if (w == g.eye[0].w && h == g.eye[0].h) return true;
    if ((g.maxW && w > g.maxW) || (g.maxH && h > g.maxH)) return false;
    for (Chain& c : g.eye) destroyChain(c);
    const bool ok = makeChain(g.eye[0], w, h, "left eye") && makeChain(g.eye[1], w, h, "right eye");
    std::printf("openxr: the eyes remade at %dx%d (options row 2)%s\n", w, h, ok ? "" : " - FAILED");
    applyPerf();   // the GPU's level follows the scale
    return ok;
}

bool headPose(vr::HeadPose& out) {
    out.valid = false;
    if (!ensureFrame()) return false;
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = g.frame.predictedDisplayTime;
    li.space = g.local;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    std::uint32_t n = 0;
    g.views[0] = g.views[1] = XrView{XR_TYPE_VIEW};
    if (!ok(xrLocateViews(g.session, &li, &vs, 2, &n, g.views), "xrLocateViews") || n != 2) return false;
    const XrViewStateFlags need = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if ((vs.viewStateFlags & need) != need) return false;
    g.viewsValid = true;
    for (int e = 0; e < 2; ++e) {
        toPose(g.views[e].pose, out.eye[e]);
        out.fov[e].left = g.views[e].fov.angleLeft;
        out.fov[e].right = g.views[e].fov.angleRight;
        out.fov[e].up = g.views[e].fov.angleUp;
        out.fov[e].down = g.views[e].fov.angleDown;
    }
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace(g.view, g.local, g.frame.predictedDisplayTime, &loc)))
        toPose(loc.pose, out.head);
    for (int h = 0; h < 2; ++h) {
        out.handValid[h] = false;
        XrSpaceLocation hl{XR_TYPE_SPACE_LOCATION};
        if (g.aimSpace[h] && XR_SUCCEEDED(xrLocateSpace(g.aimSpace[h], g.local, g.frame.predictedDisplayTime, &hl)) &&
            (hl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            toPose(hl.pose, out.hand[h]);
            out.handValid[h] = true;
        }
    }
    out.eyeW = g.eye[0].w;
    out.eyeH = g.eye[0].h;
    out.valid = true;
    return true;
}

void setQuadOverEyes(bool on) { g.quadOverEyes = on; }
bool eyesDrawn() { return g.eye[0].drawn && g.eye[1].drawn; }

bool eyeTarget(int e, unsigned& fbo, int& w, int& h) {
    if (e < 0 || e > 1 || !g.begun || !g.viewsValid || !g.frame.shouldRender) return false;
    if (!acquire(g.eye[e])) return false;
    fbo = g.eye[e].fbo;
    w = g.eye[e].w;
    h = g.eye[e].h;
    return true;
}

void eyeDone(int e) {
    if (e < 0 || e > 1) return;
    release(g.eye[e]);
}

bool frameTarget(unsigned& fbo, int& w, int& h) {
    if (!ensureFrame()) return false;
    if (!g.frame.shouldRender) return false;
    if (!acquire(g.quad)) return false;
    fbo = g.quad.fbo;
    w = g.quad.w;
    h = g.quad.h;
    return true;
}

void submit() {
    if (!g.begun) return;
    glFlush();
    for (Chain* c : {&g.quad, &g.eye[0], &g.eye[1]}) release(*c);
    const XrCompositionLayerBaseHeader* layers[2] = {};
    std::uint32_t nLayers = 0;

    XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView pv[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                           {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    if (g.eye[0].drawn && g.eye[1].drawn && g.viewsValid) {
        for (int e = 0; e < 2; ++e) {
            pv[e].pose = g.views[e].pose;   // the pose the eye was drawn from, raw
            pv[e].fov = g.views[e].fov;
            pv[e].subImage.swapchain = g.eye[e].sc;
            pv[e].subImage.imageRect = {{0, 0}, {g.eye[e].w, g.eye[e].h}};
        }
        proj.space = g.local;
        proj.viewCount = 2;
        proj.views = pv;
        layers[nLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj);
        if (++g.eyeFrames == 1) std::printf("openxr: the first frame drawn per eye\n");
    }
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (g.quad.drawn) {
        // over the eyes the quad is the interface with the world showing
        // through it (step 6a): its alpha, premultiplied
        if (nLayers) quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.space = g.local;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = g.quad.sc;
        quad.subImage.imageRect = {{0, 0}, {g.quad.w, g.quad.h}};
        quad.pose.orientation.w = 1.0f;
        quad.pose.position = {0.0f, 0.0f, -kScreenDistance};
        quad.size = {kScreenWidth, kScreenWidth * static_cast<float>(g.quad.h) / static_cast<float>(g.quad.w)};
        layers[nLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = g.frame.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = nLayers;
    ei.layers = layers;
    ok(xrEndFrame(g.session, &ei), "xrEndFrame");
    g.begun = false;
    g.quad.drawn = g.eye[0].drawn = g.eye[1].drawn = false;
    ++g.frames;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// THE TOUCH CONTROLLERS AS A PAD (step 5a's mapping, a CHOICE for the play
// pass to judge). The pad's slots are the four control schemes' joystick
// buttons (`input/pad.h`):
//   A South (Action / the UI's confirm)      B East (Cancel / Jump; the UI's back)
//   X West                                   Y North (first-person view)
//   left stick: the move stick (and the menus' arrows)   right stick: the look / its slots
//   left grip LeftShoulder, right grip RightShoulder (side step / run)
//   right trigger: the shoot group's Action (RightStickSide)
//   left trigger and either stick click: Back (the sneak / weapon; the UI's close)
//   the left menu button: START (the game's menu, ESCAPE)
void readControllers(HostInput& out) {
    if (g.exitRequested) out.quit = true;
    // what SDL's own pad and keyboard handed over, before this adds anything
    // (`[in]` like android_main.cpp's raw log; the first 40 nonzero frames)
    static int told = 0;
    if ((out.pad.buttons || !out.held.empty()) && told < 40) {
        ++told;
        std::printf("[in] sdl state: pad buttons 0x%x sticks %d,%d %d,%d; held", out.pad.buttons,
                    out.pad.lx, out.pad.ly, out.pad.rx, out.pad.ry);
        for (int k : out.held) std::printf(" 0x%x", k);
        std::printf("\n");
    }
    // VISIBLE as well as FOCUSED: on 2026-10-07 the reader walked the menus
    // while the log had the session at VISIBLE only. A runtime that does not
    // give the app input there reports the actions inactive.
    if (!g.session || !g.actionsAttached ||
        (g.state != XR_SESSION_STATE_FOCUSED && g.state != XR_SESSION_STATE_VISIBLE)) return;
    XrActiveActionSet active{g.set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    const XrResult sync = xrSyncActions(g.session, &si);
    if (sync != XR_SUCCESS) {
        static int said = 0;
        if (said++ < 3) std::printf("openxr: xrSyncActions -> %d (state %d)\n", static_cast<int>(sync),
                                    static_cast<int>(g.state));
        if (XR_FAILED(sync)) return;
    }

    const auto boolean = [](XrAction a, int hand) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = g.hand[hand];
        XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(g.session, &gi, &s)) && s.isActive && s.currentState;
    };
    const auto value = [](XrAction a, int hand) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = g.hand[hand];
        XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
        return XR_SUCCEEDED(xrGetActionStateFloat(g.session, &gi, &s)) && s.isActive ? s.currentState : 0.0f;
    };
    const auto stick = [](int hand, int& sx, int& sy) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = g.stick;
        gi.subactionPath = g.hand[hand];
        XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(xrGetActionStateVector2f(g.session, &gi, &s)) && s.isActive) {
            // OpenXR's +y is UP, the pad's is DOWN
            const int x = static_cast<int>(std::lround(s.currentState.x * 1000.0f));
            const int y = static_cast<int>(std::lround(-s.currentState.y * 1000.0f));
            if (std::abs(x) > std::abs(sx)) sx = x;
            if (std::abs(y) > std::abs(sy)) sy = y;
        }
    };
    std::uint32_t b = 0;
    if (boolean(g.a, 1)) b |= pad::South;
    if (boolean(g.b, 1)) b |= pad::East;
    if (boolean(g.x, 0)) b |= pad::West;
    if (boolean(g.y, 0)) b |= pad::North;
    if (value(g.grip, 0) > 0.5f) b |= pad::LeftShoulder;
    if (value(g.grip, 1) > 0.5f) b |= pad::RightShoulder;
    if (value(g.trigger, 1) > 0.5f) b |= pad::RightStickSide;
    if (value(g.trigger, 0) > 0.5f || boolean(g.thumbClick, 0) || boolean(g.thumbClick, 1)) b |= pad::Back;
    if (boolean(g.menu, 0)) b |= pad::Start;
    // the controllers' own buttons, logged the first times they are pressed:
    // which path a press took is a measurement (the 2026-10-07 question)
    static int pressed = 0;
    if (b && pressed < 20) { ++pressed; std::printf("[in] openxr: pad buttons 0x%x (state %d)\n", b,
                                                    static_cast<int>(g.state)); }
    out.pad.buttons |= b;
    stick(0, out.pad.lx, out.pad.ly);
    stick(1, out.pad.rx, out.pad.ry);
    if (b & pad::Start) out.held.insert(pad::kEscape);
}

}  // namespace omk::xr
