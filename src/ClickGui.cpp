#include "ClickGui.hpp"
#include "MemUtils.hpp"
#include <dlfcn.h>
#include <dobby.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_android.h>
#include <imgui/imgui_impl_opengl3.h>
#include <android/native_window.h>
#include "FontDroidSansFallback.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// ClickGui 的日志也打到 CIU tag，避免散落在 MH tag 里看不到
#undef LOG_TAG
#undef LOGI
#undef LOGW
#undef LOGE
#define LOG_TAG "CIU"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 字体二进制数据（由外部生成）
extern const unsigned char DroidSansFallback_ttf[];
extern const unsigned int DroidSansFallback_ttf_len;

// ========== 全局实例指针 ==========
static ClickGui* g_clickGuiInstance = nullptr;

// ========== 原始函数指针 ==========
typedef EGLSurface(*eglCreateWindowSurface_t)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
static eglCreateWindowSurface_t original_eglCreateWindowSurface = nullptr;

typedef int32_t(*AInputQueue_getEvent_t)(AInputQueue*, AInputEvent**);
typedef int32_t(*AInputQueue_finishEvent_t)(AInputQueue*, AInputEvent*, int);
static AInputQueue_getEvent_t original_getEvent = nullptr;
static AInputQueue_finishEvent_t original_finishEvent = nullptr;

// ========== eglCreateWindowSurface 钩子（获取 ANativeWindow）==========
static EGLSurface hooked_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
    EGLNativeWindowType win, const EGLint* attrib_list) {
    ANativeWindow* window = (ANativeWindow*)win;
    if (window && !MemUtils::getWindow()) {
        MemUtils::setWindow(window);
        LOGI("[ClickGui] Captured ANativeWindow via eglCreateWindowSurface: %p", window);
        int w = ANativeWindow_getWidth(window);
        int h = ANativeWindow_getHeight(window);
        LOGI("[ClickGui] Window size: %dx%d", w, h);
    }
    return original_eglCreateWindowSurface(dpy, config, win, attrib_list);
}

// ========== 输入事件钩子 ==========
static int32_t hooked_getEvent(AInputQueue* queue, AInputEvent** outEvent) {
    LOGI("触摸调用");
    int32_t result = original_getEvent(queue, outEvent);
    if (result == 0 && outEvent && *outEvent && g_clickGuiInstance) {
        LOGI("处理白球触摸");
        g_clickGuiInstance->handleInputEvent(*outEvent);
    }
    return result;
}

static int32_t hooked_finishEvent(AInputQueue* queue, AInputEvent* event, int handled) {
    if (g_clickGuiInstance && event && g_clickGuiInstance->shouldConsumeEvent()) {
        return original_finishEvent(queue, event, 1);
    }
    return original_finishEvent(queue, event, handled);
}

// ========== ClickGui 实现 ==========
ClickGui* ClickGui::instance = nullptr;

ClickGui* ClickGui::getInstance() {
    if (!instance) {
        instance = new ClickGui();
        g_clickGuiInstance = instance;
    }
    return instance;
}

void ClickGui::destroyInstance() {
    if (instance) {
        instance->shutdown();
        delete instance;
        instance = nullptr;
        g_clickGuiInstance = nullptr;
    }
}

ClickGui::ClickGui() {
}

ClickGui::~ClickGui() {
}

void ClickGui::init() {
    if (hooksInstalled) return;

    // 1. Hook eglCreateWindowSurface（拿 ANativeWindow 供尺寸/边界用。
    //    每帧渲染走 SoEntrance 的 drawFrame 钩子，不再 hook eglSwapBuffers，
    //    避免 CIU 这种 GLSurfaceView 引擎上双路径/零路径的不确定性）
    void* eglLib = dlopen("libEGL.so", RTLD_LAZY);
    if (eglLib) {
        void* eglCreateAddr = dlsym(eglLib, "eglCreateWindowSurface");
        if (eglCreateAddr && DobbyHook(eglCreateAddr, (void*)hooked_eglCreateWindowSurface, (void**)&original_eglCreateWindowSurface) == 0) {
            LOGI("ClickGui: Hooked eglCreateWindowSurface");
        }
        dlclose(eglLib);
    }

    // 2. Hook 输入函数
    void* androidLib = dlopen("libandroid.so", RTLD_LAZY);
    if (androidLib) {
        void* getEventAddr = dlsym(androidLib, "AInputQueue_getEvent");
        void* finishEventAddr = dlsym(androidLib, "AInputQueue_finishEvent");
        if (getEventAddr) DobbyHook(getEventAddr, (void*)hooked_getEvent, (void**)&original_getEvent);
        if (finishEventAddr) DobbyHook(finishEventAddr, (void*)hooked_finishEvent, (void**)&original_finishEvent);
        dlclose(androidLib);
        LOGI("ClickGui: Hooked input functions");
    }

    hooksInstalled = true;
}

void ClickGui::shutdown() {
    if (!hooksInstalled) return;
    if (imGuiReady) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        imGuiReady = false;
    }
    hooksInstalled = false;
    LOGI("ClickGui shutdown");
}

bool ClickGui::tryInitImGui(float w, float h) {
    // 必须在 GL 上下文现行的线程调用（drawFrame 钩子内满足）
    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) {
        return false;
    }
    if (w <= 0 || h <= 0) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // 加载自定义字体
    if (DroidSansFallback_ttf && DroidSansFallback_ttf_len > 0) {
        ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)DroidSansFallback_ttf, DroidSansFallback_ttf_len, 18.0f);
        if (font) {
            io.FontDefault = font;
            LOGI("Loaded DroidSansFallback.ttf");
        }
        else {
            io.Fonts->AddFontDefault();
        }
    }
    else {
        io.Fonts->AddFontDefault();
    }

    io.DisplaySize = ImVec2(w, h);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    if (!ImGui_ImplAndroid_Init()) {
        LOGE("ImGui_ImplAndroid_Init failed");
        return false;
    }
    // glsl_version 传 nullptr：后端按运行时 GL 版本自动选
    // （工程以 IMGUI_IMPL_OPENGL_ES2 编译 => GLSL "#version 100"，
    //   兼容 GLSurfaceView 的 ES2/ES3 上下文）
    if (!ImGui_ImplOpenGL3_Init(nullptr)) {
        LOGE("ImGui_ImplOpenGL3_Init failed");
        return false;
    }

    setupImGuiStyle();
    LOGI("ImGui initialized, size=%.0fx%.0f", w, h);
    return true;
}

void ClickGui::setupImGuiStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.TouchExtraPadding = ImVec2(8, 8);
    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.ScrollbarSize = 16.0f;
    style.WindowBorderSize = 0.0f;
}

void ClickGui::onGameFrame(float w, float h) {
    if (!hooksInstalled) return;

    if (!imGuiReady) {
        if (tryInitImGui(w, h)) {
            imGuiReady = true;
            LOGI("[ClickGui] overlay ready (drawFrame path)");
        }
        else {
            return;
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(ImVec2(w, h));
    ImGui::NewFrame();

    renderFixedButton();
    if (menuOpen) {
        renderMenu();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ClickGui::onFrame() {
    // 旧 eglSwapBuffers 路径已弃用（CIU 引擎不经过它渲染覆盖层）。
    // 保留空实现以兼容调用点。
}

// ============================================================
//  "点亮"固定按钮（多点触摸；由 SoEntrance 的 JNI 触摸路径驱动）
// ============================================================
namespace ciu { void requestRandomLightup(); }

static void buttonSize(float& bw, float& bh) {
    ANativeWindow* window = MemUtils::getWindow();
    float w = window ? (float)ANativeWindow_getWidth(window) : 1080.0f;
    float h = window ? (float)ANativeWindow_getHeight(window) : 1920.0f;
    bw = (w < h) ? 150.0f : 200.0f;   // 固定位置，尺寸随屏幕方向缩放
    bh = bw * 0.5f;
}

ImVec2 ClickGui::getButtonRect() const {
    ANativeWindow* window = MemUtils::getWindow();
    float w = window ? (float)ANativeWindow_getWidth(window) : 1080.0f;
    float bw, bh;
    buttonSize(bw, bh);
    return ImVec2(w - bw - 24.0f, 36.0f);   // 顶部靠右
}

bool ClickGui::isOverButton(float x, float y) {
    float bw, bh;
    buttonSize(bw, bh);
    ImVec2 r = getButtonRect();
    return x >= r.x && x <= r.x + bw && y >= r.y && y <= r.y + bh;
}

void ClickGui::buttonPress(int32_t pointerId) {
    if (pointerId < 0) return;
    for (int& slot : buttonIds) {
        if (slot == pointerId) return;              // 已记录
    }
    for (int& slot : buttonIds) {
        if (slot == -1) { slot = pointerId; buttonPressedAny = true; return; }
    }
}

void ClickGui::buttonRelease(int32_t pointerId, bool trigger) {
    bool was = false;
    for (int& slot : buttonIds) {
        if (slot == pointerId) { slot = -1; was = true; break; }
    }
    buttonPressedAny = false;
    for (int slot : buttonIds) {
        if (slot != -1) { buttonPressedAny = true; break; }
    }
    if (was && trigger) {
        ciu::requestRandomLightup();                // ★ 点击 → 随机点亮一只鸡
    }
}

void ClickGui::buttonCancelAll() {
    for (int& slot : buttonIds) slot = -1;
    buttonPressedAny = false;
}

void ClickGui::onSystemTouch(int32_t maskedAction, float x, float y, int32_t pointerId) {
    switch (maskedAction) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        // 多点：任意手指按到按钮区域都记录（一次一根手指）
        if (isOverButton(x, y)) buttonPress(pointerId);
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
        // 该手指原先按在按钮上才触发（无论抬起时位置）
        buttonRelease(pointerId, true);
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        buttonCancelAll();
        break;
    default:
        break;
    }
}

void ClickGui::renderFixedButton() {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float bw, bh;
    buttonSize(bw, bh);
    ImVec2 r = getButtonRect();
    ImVec2 max(r.x + bw, r.y + bh);

    ImU32 bg = buttonPressedAny ? IM_COL32(90, 170, 90, 230) : IM_COL32(30, 90, 30, 190);
    draw->AddRectFilled(r, max, bg, 14.0f);
    draw->AddRect(r, max, buttonPressedAny ? IM_COL32(200, 255, 200, 255)
                                           : IM_COL32(140, 220, 140, 220), 14.0f, 0, 2.5f);
    ImVec2 ts = ImGui::CalcTextSize("LightUpOnce");
    draw->AddText(ImVec2(r.x + (bw - ts.x) * 0.5f, r.y + (bh - ts.y) * 0.5f),
                  IM_COL32(255, 255, 255, 255), "LightUpOnce");
}

void ClickGui::renderMenu() {
    ANativeWindow* window = MemUtils::getWindow();
    if (!window) return;
    int width = ANativeWindow_getWidth(window);
    int height = ANativeWindow_getHeight(window);
    if (width <= 0 || height <= 0) return;

    float menuWidth = width * 0.35f;
    float menuHeight = menuWidth * 1.333f;
    float menuX = width - menuWidth - 10.0f;
    float menuY = (height - menuHeight) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(menuWidth, menuHeight), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("ClickGuiMenu", nullptr, flags);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(30, 30, 30, 200));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);

    if (ImGui::Button("增加100金币", ImVec2(-1, 45))) {
        if (addGoldCallback) addGoldCallback();
    }
    ImGui::Separator();

    ImGui::Checkbox("倍率增加金币", &enableGoldMultiplier);
    ImGui::Checkbox("金币不减反增", &enableGoldIncrease);
    ImGui::Checkbox("无限复活", &enableInfiniteRevive);
    ImGui::Checkbox("倍率成长", &enableMultiplierGrowth);
    ImGui::Checkbox("敌人倍率成长", &enableEnemyGrowth);
    ImGui::Separator();

    ImGui::SliderFloat("金币增加倍率", &goldMultiplier, 0.0f, 10.0f);
    ImGui::SliderFloat("成长倍率", &growthMultiplier, 0.0f, 10.0f);
    ImGui::SliderFloat("敌人成长倍率", &enemyGrowthMultiplier, 0.0f, 10.0f);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::End();
}

bool ClickGui::handleInputEvent(AInputEvent* event) {
    if (!imGuiReady) return false;
    return ImGui_ImplAndroid_HandleInputEvent(event) != 0;
}

bool ClickGui::shouldConsumeEvent() {
    if (!imGuiReady) return false;
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || menuOpen;
}


void ClickGui::setAddGoldCallback(std::function<void()> callback) {
    addGoldCallback = callback;
}