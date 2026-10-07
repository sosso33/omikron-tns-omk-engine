// SPDX-License-Identifier: GPL-3.0-or-later
// THE OPENXR HOST - see `xrhost.h`. `todo/quest-port.md` §5 step 5a.
#include "xrhost.h"

#include "input/pad.h"
#include "platform/frontend.h"

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

// The screen: 2 m ahead, its centre at the head's height when the session
// began (LOCAL space), 2.4 m wide at the frame's own aspect.
constexpr float kScreenDistance = 2.0f, kScreenWidth = 2.4f;

struct Hand {
    XrPath path = XR_NULL_PATH;
};

struct Host {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false, exitRequested = false;

    // the quad's swapchain, an FBO per image
    XrSwapchain quad = XR_NULL_HANDLE;
    int quadW = 0, quadH = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> fbos;
    bool srgb = false, srgbWriteControl = false;

    // this frame
    bool begun = false, imageHeld = false;
    GLuint fbo = 0;                 // the held image's framebuffer
    XrFrameState frame{XR_TYPE_FRAME_STATE};

    // the controllers
    XrActionSet set = XR_NULL_HANDLE;
    XrAction stick = XR_NULL_HANDLE, a = XR_NULL_HANDLE, b = XR_NULL_HANDLE,
             x = XR_NULL_HANDLE, y = XR_NULL_HANDLE, trigger = XR_NULL_HANDLE,
             grip = XR_NULL_HANDLE, menu = XR_NULL_HANDLE, thumbClick = XR_NULL_HANDLE;
    Hand hand[2];
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
    XrPath subs[2] = {g.hand[0].path, g.hand[1].path};
    ci.countSubactionPaths = 2;
    ci.subactionPaths = subs;
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
    g.hand[0].path = path("/user/hand/left");
    g.hand[1].path = path("/user/hand/right");
    g.stick      = makeAction("stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g.a          = makeAction("a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.b          = makeAction("b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.x          = makeAction("x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.y          = makeAction("y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.trigger    = makeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    g.grip       = makeAction("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
    g.menu       = makeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g.thumbClick = makeAction("thumbclick", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT);
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
    };
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.suggestedBindings = b;
    sb.countSuggestedBindings = static_cast<std::uint32_t>(std::size(b));
    return ok(xrSuggestInteractionProfileBindings(g.instance, &sb), "xrSuggestInteractionProfileBindings");
}

bool makeQuad(int screenW, int screenH) {
    std::uint32_t n = 0;
    xrEnumerateSwapchainFormats(g.session, 0, &n, nullptr);
    std::vector<std::int64_t> formats(n);
    xrEnumerateSwapchainFormats(g.session, n, &n, formats.data());
    std::int64_t format = 0;
    if (std::find(formats.begin(), formats.end(), kSrgb8Alpha8) != formats.end()) format = kSrgb8Alpha8;
    else if (std::find(formats.begin(), formats.end(), kRgba8) != formats.end()) format = kRgba8;
    else if (n) format = formats[0];
    g.srgb = format == kSrgb8Alpha8;
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    g.srgbWriteControl = ext && std::strstr(ext, "GL_EXT_sRGB_write_control");

    g.quadW = screenW;
    g.quadH = screenH;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = static_cast<std::uint32_t>(g.quadW);
    ci.height = static_cast<std::uint32_t>(g.quadH);
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!ok(xrCreateSwapchain(g.session, &ci, &g.quad), "xrCreateSwapchain (quad)")) return false;
    xrEnumerateSwapchainImages(g.quad, 0, &n, nullptr);
    g.images.assign(n, XrSwapchainImageOpenGLESKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(g.quad, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(g.images.data()));
    g.fbos.assign(n, 0);
    glGenFramebuffers(static_cast<GLsizei>(n), g.fbos.data());
    for (std::uint32_t i = 0; i < n; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, g.fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.images[i].image, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::printf("openxr: quad image %u framebuffer incomplete\n", i);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::printf("openxr: quad %dx%d, %u images, format 0x%llx (%s), sRGB write control %s\n",
                g.quadW, g.quadH, n, static_cast<unsigned long long>(format),
                g.srgb ? "sRGB" : "UNORM", g.srgbWriteControl ? "yes" : "NO");
    return true;
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
            } else if (g.state == XR_SESSION_STATE_STOPPING) {
                ok(xrEndSession(g.session), "xrEndSession");
                g.running = false;
            } else if (g.state == XR_SESSION_STATE_EXITING || g.state == XR_SESSION_STATE_LOSS_PENDING) {
                g.running = false;
                g.exitRequested = true;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g.running = false;
            g.exitRequested = true;
        }
        ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

}  // namespace

bool start(int screenW, int screenH) {
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

    const char* exts[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ai.applicationVM = vm;
    ai.applicationActivity = activity;
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &ai;
    std::snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "OMK");
    ci.applicationInfo.applicationVersion = 1;
    std::snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "OMK");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ci.enabledExtensionCount = 2;
    ci.enabledExtensionNames = exts;
    if (!ok(xrCreateInstance(&ci, &g.instance), "xrCreateInstance")) return false;
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(g.instance, &ip);
    std::printf("openxr: runtime %s %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
                XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

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

    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &g.set;
    g.actionsAttached = ok(xrAttachSessionActionSets(g.session, &at), "xrAttachSessionActionSets");

    if (!makeQuad(screenW, screenH)) return false;
    std::printf("openxr: session created - the frame goes to a %.1f m screen %.1f m ahead\n",
                kScreenWidth, kScreenDistance);
    pollEvents();
    return true;
}

bool running() { return g.session != XR_NULL_HANDLE && g.running; }

bool frameTarget(unsigned& fbo, int& w, int& h) {
    if (!g.session) return false;
    if (!g.begun) {
        pollEvents();
        if (!g.running) return false;
        g.frame = XrFrameState{XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
        if (!ok(xrWaitFrame(g.session, &wi, &g.frame), "xrWaitFrame")) return false;
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
        if (!ok(xrBeginFrame(g.session, &bi), "xrBeginFrame")) return false;
        g.begun = true;
        if (g.frame.shouldRender) {
            std::uint32_t idx = 0;
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi2.timeout = XR_INFINITE_DURATION;
            if (ok(xrAcquireSwapchainImage(g.quad, &ai, &idx), "xrAcquireSwapchainImage") &&
                ok(xrWaitSwapchainImage(g.quad, &wi2), "xrWaitSwapchainImage")) {
                g.imageHeld = true;
                g.fbo = g.fbos[idx];
            }
        }
    }
    if (!g.imageHeld) {
        // a frame the runtime does not want drawn: end it empty, at once
        submit();
        return false;
    }
    fbo = g.fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (g.srgb && g.srgbWriteControl) glDisable(kFramebufferSrgb);
    w = g.quadW;
    h = g.quadH;
    return true;
}

void submit() {
    if (!g.begun) return;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[1] = {};
    std::uint32_t nLayers = 0;
    if (g.imageHeld) {
        glFlush();
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        ok(xrReleaseSwapchainImage(g.quad, &ri), "xrReleaseSwapchainImage");
        g.imageHeld = false;
        quad.space = g.local;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = g.quad;
        quad.subImage.imageRect = {{0, 0}, {g.quadW, g.quadH}};
        quad.pose.orientation.w = 1.0f;
        quad.pose.position = {0.0f, 0.0f, -kScreenDistance};
        quad.size = {kScreenWidth, kScreenWidth * static_cast<float>(g.quadH) / static_cast<float>(g.quadW)};
        layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        nLayers = 1;
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = g.frame.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = nLayers;
    ei.layers = layers;
    ok(xrEndFrame(g.session, &ei), "xrEndFrame");
    g.begun = false;
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
    if (!g.session || !g.actionsAttached || g.state != XR_SESSION_STATE_FOCUSED) return;
    XrActiveActionSet active{g.set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (!ok(xrSyncActions(g.session, &si), "xrSyncActions")) return;

    const auto boolean = [](XrAction a, int hand) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = g.hand[hand].path;
        XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(g.session, &gi, &s)) && s.isActive && s.currentState;
    };
    const auto value = [](XrAction a, int hand) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = g.hand[hand].path;
        XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
        return XR_SUCCEEDED(xrGetActionStateFloat(g.session, &gi, &s)) && s.isActive ? s.currentState : 0.0f;
    };
    const auto stick = [](int hand, int& sx, int& sy) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = g.stick;
        gi.subactionPath = g.hand[hand].path;
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
    out.pad.buttons |= b;
    stick(0, out.pad.lx, out.pad.ly);
    stick(1, out.pad.rx, out.pad.ry);
    if (b & pad::Start) out.held.insert(pad::kEscape);
}

}  // namespace omk::xr
