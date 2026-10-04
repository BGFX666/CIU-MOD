#pragma once

#include <functional>
#include <android/input.h>
#include <imgui/imgui.h>
class ClickGui {
public:
    static ClickGui* getInstance();
    static void destroyInstance();

    // 初始化（安装所有钩子，包括 pthread_create、eglSwapBuffers、输入事件）
    void init();

    // 关闭并卸载钩子
    void shutdown();

    // 每帧渲染（由 SoEntrance 的 drawFrame JNI 钩子调用；GL 上下文现行，
    // 游戏已画完一帧，此处叠加覆盖层；w/h 为游戏上报的 surface 尺寸）
    void onGameFrame(float w, float h);

    // 每帧渲染（旧 eglSwapBuffers 路径，已弃用，保留兼容）
    void onFrame();

    // 处理输入事件（在输入钩子中调用），返回 true 表示事件被消费
    bool handleInputEvent(AInputEvent* event);

    // 处理系统触摸（由 XP 加载器经 JNI 传入的 View.dispatchTouchEvent 层事件。
    // 多点：每个 pointerId 独立跟踪，任意手指按到"点亮"按钮都会触发）
    void onSystemTouch(int32_t maskedAction, float x, float y, int32_t pointerId);

    // 检查是否需要消费事件（供 finishEvent 钩子调用）
    bool shouldConsumeEvent();

    // 设置增加金币的回调（由游戏逻辑实现）
    void setAddGoldCallback(std::function<void()> callback);

    // ========== 公共状态变量（游戏线程可直接读写）==========
    bool enableGoldMultiplier = false;      // 倍率增加金币
    bool enableGoldIncrease = false;        // 金币不减反增
    bool enableInfiniteRevive = false;      // 无限复活
    bool enableMultiplierGrowth = false;    // 倍率成长
    bool enableEnemyGrowth = false;         // 敌人倍率成长

    float goldMultiplier = 1.0f;            // 金币增加倍率
    float growthMultiplier = 1.0f;          // 成长倍率
    float enemyGrowthMultiplier = 1.0f;     // 敌人成长倍率

private:
    ClickGui();
    ~ClickGui();

    static ClickGui* instance;

    bool hooksInstalled = false;     // 是否已安装钩子
    bool imGuiReady = false;         // ImGui 是否已初始化成功
    bool menuOpen = false;           // 菜单是否显示

    // "点亮"固定按钮（多点：跟踪所有按在按钮上的 pointerId，-1 = 空槽）
    bool   buttonPressedAny = false;             // 是否有手指按在按钮上（渲染态）
    int32_t buttonIds[10] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    ImVec2 getButtonRect() const;                // 固定位置（顶部靠右，随屏幕宽度定尺寸）
    bool   isOverButton(float x, float y);
    void   buttonPress(int32_t pointerId);
    void   buttonRelease(int32_t pointerId, bool trigger);
    void   buttonCancelAll();
    void   renderFixedButton();

    // 回调
    std::function<void()> addGoldCallback;

    // 内部方法
    bool tryInitImGui(float w, float h); // 尝试初始化 ImGui（须在 GL 上下文现行线程调用）
    void setupImGuiStyle();
    void renderMenu();
};