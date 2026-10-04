// ============================================================
//  CIU mod for Android (libproject_top.so, ARM64, IDB 基址 0)
// ------------------------------------------------------------
//  移植自 Windows CI mod (CI/src/dllmain.cpp, v160.3.0)
//
//  功能 1  Invulnerable  —— 玩家撞实体不死（PlayerObject_TakeHit → return 0）
//  功能 2  Damage x5     —— 对鸡伤害 x5（Enemy_TakeDamage 伤害源 dmgSrc+0xFC 放大）
//  功能 2b NoOverheat    —— 武器无过热（B.PL->B.AL + ApplyOverheat 强置 0）
//  功能 3  LightUp        —— 点亮鸡（发光附着物，按钮触发，随机一只）
//  功能 4  NetRole Patch  —— 旁路两处客户端自检断言（NetRole.cpp）
//
//  SO 由 g/Android/ab（XP 模块加载器）通过 System.loadLibrary("ciu")
//  加载，触摸经 JNI (Java_com_ab_Loader_nativeTouchEvent) 传入。
//
//  点亮：Android 原生描述符 0x20 字节，Init(flags=3) -> RegisterAndAdd
//  -> Execute。0x1420168 是守卫内执行器，不是析构或异步创建队列。
//  enemy+0x679 必须为 1，enemy+0xE80 为附着物指针。
//  地址与偏移只适用于已核对的 ARM64 libproject_top.so 构建。
//
//  ★ NetRole 断言补丁（对应 Windows 0x11A5BC1 / 0x11A5D69 的 jnz→nop）：
//    0x1409BE8  B.EQ loc_1409DF0 (0x54001040) → B (0x14000082)
//               实体 ID 断言 (NetRole.cpp:476)，恒走"ID 匹配→跳过"路径
//    0x140974C  B.EQ loc_1409734 (0x54FFFF40) → B (0x17FFFFFA)
//               RNG 同步断言 (NetRole.cpp:608)，恒走"一致→继续"路径
//    两处断言都只上报进程内状态（ID/RNG 从不进网络包，见 Windows 侧
//    验证结论），绕过不影响任何上报内容。
// ============================================================

#include "MemUtils.hpp"
#include "ClickGui.hpp"
#include "LightupState.hpp"
#include <dobby.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <jni.h>
#include <android/input.h>
#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ctime>
#include <dlfcn.h>

#define LOG_TAG "CIU"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// ------------------------------------------------------------
//  libproject_top.so 内的 IDB 偏移（运行时 = base + va）
// ------------------------------------------------------------
namespace ciu {

// 敌人公共更新（每帧 × 每敌人，游戏线程；Windows 对应 sub_4A6030）
constexpr uintptr_t VA_ENEMY_COMMON_UPD = 0x109B708;

// 每帧渲染入口（Java GLSurfaceView -> JNI；a4/a5 = surface 宽/高）。
// GL 上下文在该线程现行，orig 返回后叠加覆盖层最合适。
constexpr uintptr_t VA_DRAW_FRAME       = 0x0C0735C;

// 玩家受击（PlayerObject::vtable[+0x8C]；Windows VA_TakeHit 0x7E3540 对应物）
constexpr uintptr_t VA_PLAYER_TAKEHIT   = 0x11B05FC;
constexpr uint32_t  TAKEHIT_PROLOGUE    = 0xD10283FFu; // SUB SP, SP, #0xA0

// 对敌伤害（Windows Enemy 侧；0x109C140 处 damage = *(float*)(dmgSrc+0xFC) * hits）
constexpr uintptr_t VA_ENEMY_TAKEDAMAGE = 0x109C0A4;
static float g_dmgScale = 5.0f;                        // ★ 伤害 x5（伤害源 = 第 6 参数）

// 武器无过热（Android/CIU 项目已验证的完整链条，见 reports/CIU_Android_武器无过热_结论.md）：
//   Weapon_UpdateFireOverheat(0x130D2B4) 每帧算出"过热值" a2
//     -> Weapon_ApplyOverheat(0x130DA78): v5 = 冷却槽值(+0xAEC) - a2
//        v5<0 且过热开关开 -> 过热块（0x130DAB0 的 B.PL 跳过判定）
//   两手：① B.PL->B.AL 堵过热触发块；② hook 强制 a2=0，冷却槽永不被消耗
constexpr uintptr_t VA_HEAT_BRANCH     = 0x130DAB0;    // B.PL loc_130DB78
constexpr uintptr_t VA_APPLY_OVERHEAT  = 0x130DA78;    // Weapon_ApplyOverheat

// 发光描述符类（原生点亮分支在 0x109BCC4 写入 off_1789390）
constexpr uintptr_t VA_GLOW_DESC_VTBL   = 0x1789390;
// 原生点亮的完整包装：RegisterToWorld(desc) -> AddToManager(desc, 0)
constexpr uintptr_t VA_DESC_INIT        = 0x141FD90;
constexpr uintptr_t VA_COLLECTIBLE_SPAWN = 0x141FDD8;
constexpr uintptr_t VA_DESC_EXECUTE    = 0x1420168;

// 游戏单例：root = *qword_189AC38；game = *(*(root+0x38)+0x10)
constexpr uintptr_t VA_GAME_GLOBAL       = 0x189AC38;

// 敌人字段（由 Android IDA 的 sub_109B708 / sub_109F030 闭环确认）
constexpr uintptr_t OFF_ENEMY_CLASS_ENABLE = 0x679; // u8，0x109BC54
constexpr uintptr_t OFF_ENEMY_GLOW_ATTACH  = 0xE80; // u64，0x109BC88/0x109BC94
// sub_109F030 传入 owner+0xE48，sub_1419BD8 写入该结构的 +0x38，
// 所以实际附着物槽位是 owner+0xE80；不存在可用的 0xE7C 槽位。

// NetRole 自检断言分支（Net_CreateEntityFromStream 内）
constexpr uintptr_t VA_ASSERT_ID  = 0x1409BE8; // B.EQ loc_1409DF0
constexpr uint32_t  ID_EXPECT     = 0x54001040u;
constexpr uint32_t  ID_PATCH      = 0x14000082u; // B loc_1409DF0
constexpr uintptr_t VA_ASSERT_RNG = 0x140974C; // B.EQ loc_1409734
constexpr uint32_t  RNG_EXPECT    = 0x54FFFF40u;
constexpr uint32_t  RNG_PATCH     = 0x17FFFFFAu; // B loc_1409734

// 函数指针（base 解析后填充）
using tDescInit   = void* (*)(void* desc, uint8_t flags);
using tSpawn      = uint64_t (*)(void* desc);
using tDescExecute   = void (*)(void* desc);
tDescInit  g_fnDescInit  = nullptr;
tSpawn     g_fnSpawn     = nullptr;
tDescExecute  g_fnDescExecute  = nullptr;

uintptr_t g_base = 0;

// ------------------------------------------------------------
//  Android 原生 Game_AddEntity 同步守卫由 0x1420168 管理：
//    flags=3 -> desc+8=1, desc+9=1
//    执行器保存 game+0x174/+0x175，设置为描述符 flags，调用 vtable+0x28，
//    再恢复原值。game+0x175=1 时，发光附着物不会进入同步流。
//  因此不能绕过描述符直接调用 sub_109F030，也不需要全局 hook 创建器。
// ------------------------------------------------------------

// 触摸线程只提交额度；只有当前活着的 self 能在更新入口消费额度。
static LightupRequests g_lightupRequests;
static std::atomic<bool> g_lightupReady{false};
static std::atomic<int64_t> g_lastRequestTick{0};
static int64_t g_lastLightupTick = 0;
constexpr int64_t kLightupCooldownMs = 250;
constexpr int64_t kRequestLifetimeMs = 3000;

static int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 私有选择随机数，不调用游戏 RNG，不更改确定性模拟的抽取次数。
static uint32_t g_pickState = 0xC1A04731u;
static uint32_t NextPick()
{
    uint32_t x = g_pickState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g_pickState = x;
}

void requestRandomLightup()
{
    if (!g_lightupReady.load(std::memory_order_acquire)) {
        LOGW("[LIGHTUP] unavailable: native path not verified or hook not installed");
        return;
    }
    g_lastRequestTick.store(NowMs(), std::memory_order_relaxed);
    if (g_lightupRequests.enqueue())
        LOGI("[LIGHTUP] request queued (pending=%d)", g_lightupRequests.pending());
}

static uintptr_t GetGame()
{
    uintptr_t root = *(const uintptr_t*)(g_base + VA_GAME_GLOBAL);
    if (!root) return 0;
    uintptr_t holder = *(const uintptr_t*)(root + 0x38);
    return holder ? *(const uintptr_t*)(holder + 0x10) : 0;
}

// 当前 self 来自更新入口，既不跨帧保存，也不在 orig 返回后解引用。
static bool IsLightupCandidate(uintptr_t enemy, uintptr_t game)
{
    if (!enemy || !game) return false;
    // vtable 只需落在本模块映像内即可，不做更窄的猜测区间。
    uintptr_t vtable = *(const uintptr_t*)enemy;
    if (vtable < g_base || vtable >= g_base + 0x189E498) return false;
    if (*(const int32_t*)(enemy + 0x60) <= 0) return false;
    if (*(const uint8_t*)(enemy + 0x30) & 1) return false;
    if (*(const uint8_t*)(enemy + OFF_ENEMY_CLASS_ENABLE) != 1) return false;
    if (*(const uintptr_t*)(enemy + OFF_ENEMY_GLOW_ATTACH)) return false;

    // 与 0x109BC60..0x109BC84 一致的关卡门禁。
    if (*(const uint8_t*)(enemy + 0x74C) == 1) {
        uintptr_t holder = *(const uintptr_t*)(game + 0x3A0);
        if (!holder) return false;
        uintptr_t level = *(const uintptr_t*)(holder + 0x10);
        if (!level || (*(const uint8_t*)(level + 0x16E) & 1)) return false;
    }
    return true;
}

static bool LightUpOne(uintptr_t enemy, uintptr_t game)
{
    NativeGlowDesc desc{};
    g_fnDescInit(&desc, 3);
    desc.vtable = g_base + VA_GLOW_DESC_VTBL;
    desc.owner = (void*)enemy;

    // 登记描述符事件，再由执行器的原生守卫创建附着物。
    // 不在此处用 C++ 异常做保护：本库以 -fno-unwind-tables 编译，
    // 异常无法穿过本帧展开，因此改用前置签名校验 + 后置条件校验。
    const uint16_t flagsBefore = *(const uint16_t*)(game + 0x174);
    const uint32_t nextIdBefore = *(const uint32_t*)(game + 0x398);
    g_fnSpawn(&desc);
    g_fnDescExecute(&desc);

    const uint16_t flagsAfter = *(const uint16_t*)(game + 0x174);
    const uint32_t nextIdAfter = *(const uint32_t*)(game + 0x398);
    const uintptr_t attach = *(const uintptr_t*)(enemy + OFF_ENEMY_GLOW_ATTACH);

    // 唯一必须硬停的情况：原生执行器没有恢复 game+0x174/+0x175。
    // 这代表上下文已损坏，继续注入会波及正常同步流。
    if (flagsAfter != flagsBefore) {
        *(uint16_t*)(game + 0x174) = flagsBefore;
        g_lightupReady.store(false, std::memory_order_release);
        g_lightupRequests.clear();
        LOGE("[LIGHTUP] guard not restored (%04X -> %04X); lightup disabled",
             (unsigned)flagsBefore, (unsigned)flagsAfter);
        return false;
    }

    if (attach) {
        LOGI("[LIGHTUP] OK enemy=%p id=%d attach=%p nextID=%u->%u",
             (void*)enemy, *(const int32_t*)(enemy + 0x60), (void*)attach,
             nextIdBefore, nextIdAfter);
        return true;
    }

    // 未产生附着物：不做二次注入。候选过滤依赖 +0xE80 != 0，所以这里
    // 保持启用是安全的，下一帧可以重新挑选别的敌人。
    LOGW("[LIGHTUP] no attachment at +0xE80 (nextID=%u->%u); retry with another enemy",
         nextIdBefore, nextIdAfter);
    return false;
}

// 保留 Android ABI：X0=this，D0/D1=时间参数，X0=原返回值。
using tEnemyUpd = uint64_t (*)(void*, double, double);
static tEnemyUpd g_origEnemyUpd = nullptr;

static uint64_t HookEnemyCommonUpd(void* self, double delta, double a3)
{
    if (g_lightupReady.load(std::memory_order_acquire) && g_lightupRequests.pending() > 0) {
        int64_t now = NowMs();
        if (now - g_lastRequestTick.load(std::memory_order_relaxed) > kRequestLifetimeMs) {
            LOGI("[LIGHTUP] request expired (no eligible enemy within 3s)");
            g_lightupRequests.clear();
            return g_origEnemyUpd(self, delta, a3);
        }
        uintptr_t game = GetGame();
        // game+0x154==1 是原生发光分支的关卡门禁（0x109B7A0）。
        // game+0x175==0 保证描述符事件会被真正写进同步流。
        if (game && *(const uint8_t*)(game + 0x154) == 1 &&
            *(const uint8_t*)(game + 0x175) == 0 &&
            now - g_lastLightupTick >= kLightupCooldownMs &&
            IsLightupCandidate((uintptr_t)self, game) && (NextPick() & 7u) == 0) {
            g_lastLightupTick = now;
            if (LightUpOne((uintptr_t)self, game)) g_lightupRequests.consume();
        }
    }
    // orig 可能移除对象，之后绝不再读 self。
    return g_origEnemyUpd(self, delta, a3);
}

// ------------------------------------------------------------
//  每帧渲染钩子：Java_my_uveandroidframework_NativeLibWrapper_drawFrame
//  先跑原函数（游戏画完这一帧），再在同一 GL 上下文上叠加覆盖层。
// ------------------------------------------------------------
typedef uint64_t (*tDrawFrame)(void* env, int64_t a2, int64_t a3,
                               int w, unsigned int h, unsigned char a6);
static tDrawFrame g_origDrawFrame = nullptr;

static uint64_t HookDrawFrame(void* env, int64_t a2, int64_t a3,
                              int w, unsigned int h, unsigned char a6)
{
    uint64_t r = g_origDrawFrame(env, a2, a3, w, h, a6);

    // GL 回调只画覆盖层，不保存或解引用敌人对象。
    ClickGui::getInstance()->onGameFrame((float)w, (float)h);
    return r;
}

// ------------------------------------------------------------
//  内存补丁
// ------------------------------------------------------------
static bool PatchU32(uintptr_t va, uint32_t expect, uint32_t patch, const char* what)
{
    uint32_t* p = (uint32_t*)(g_base + va);
    if (*p != expect) {
        LOGE("[NETROLE] %s: bytes mismatch @%lx: %08X (want %08X) - skip",
             what, (unsigned long)va, *p, expect);
        return false;
    }
    uintptr_t page = (g_base + va) & ~(uintptr_t)(getpagesize() - 1);
    if (mprotect((void*)page, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("[NETROLE] %s: mprotect failed", what);
        return false;
    }
    *p = patch;
    __builtin___clear_cache((char*)p, (char*)p + 4);
    LOGI("[NETROLE] %s patched @%lx : %08X -> %08X", what, (unsigned long)va, expect, patch);
    return true;
}

static void PatchNetRoleAsserts()
{
    PatchU32(VA_ASSERT_ID,  ID_EXPECT,  ID_PATCH,  "ID assert (NetRole.cpp:476)");
    PatchU32(VA_ASSERT_RNG, RNG_EXPECT, RNG_PATCH, "RNG-sync assert (NetRole.cpp:608)");
}

// ------------------------------------------------------------
//  功能 1：不死补丁 —— PlayerObject_TakeHit 入口改 MOV W0,#0 ; RET
//  （ARM64 无 ret N，调用方清理栈，直接 RET 安全；对应 Windows
//   xor eax,eax ; ret 0x2C）
// ------------------------------------------------------------
static void PatchPlayerTakeHit()
{
    uint32_t* p = (uint32_t*)(g_base + VA_PLAYER_TAKEHIT);
    if (p[0] != TAKEHIT_PROLOGUE) {
        LOGE("[mod] TakeHit prologue mismatch @%lx: %08X (want %08X) - skip",
             (unsigned long)VA_PLAYER_TAKEHIT, p[0], TAKEHIT_PROLOGUE);
        return;
    }
    // MOV W0, #0 ; RET
    const uint32_t patch[2] = { 0x52800000u, 0xD65F03C0u };
    uintptr_t page = (g_base + VA_PLAYER_TAKEHIT) & ~(uintptr_t)(getpagesize() - 1);
    if (mprotect((void*)page, getpagesize() * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("[mod] TakeHit: mprotect failed");
        return;
    }
    p[0] = patch[0];
    p[1] = patch[1];
    __builtin___clear_cache((char*)p, (char*)p + 8);
    LOGI("[mod] PATCHED %lx : PlayerObject::TakeHit -> return 0 (invulnerable)",
         (unsigned long)VA_PLAYER_TAKEHIT);
}

// ------------------------------------------------------------
//  功能 2：对敌伤害 x5 —— hook Enemy_TakeDamage（敌人扣血唯一入口）。
//  伤害源是第 6 参数 dmgSrc：0x109C140 处
//    v23 = *(float*)(dmgSrc+0xFC) * (float)hits
//  进原函数前把 dmgSrc+0xFC 乘倍率、返回后还原，
//  保留抗性/难度/人数分摊/单次上限等全部原版逻辑。
//  （照搬 Android/CIU 已验证实现）
// ------------------------------------------------------------
typedef uint32_t (*tEnemyTakeDamage)(void* enemy, void* a2,
                                     int hits, int dmgType, int capFlag, void* dmgSrc,
                                     float hx, float hy, float hz);
static tEnemyTakeDamage g_origEnemyTakeDamage = nullptr;

static uint32_t HookEnemyTakeDamage(void* enemy, void* a2,
                                    int hits, int dmgType, int capFlag, void* dmgSrc,
                                    float hx, float hy, float hz)
{
    float scale = g_dmgScale;
    float saved = 0.0f;
    bool scaled = false;
    if (dmgSrc && scale != 1.0f) {
        float* pdmg = (float*)((char*)dmgSrc + 0xFC);
        saved = *pdmg;
        *pdmg = saved * scale;
        scaled = true;
    }
    uint32_t r = g_origEnemyTakeDamage(enemy, a2, hits, dmgType, capFlag, dmgSrc, hx, hy, hz);
    if (scaled)
        *(float*)((char*)dmgSrc + 0xFC) = saved;
    return r;
}

// ------------------------------------------------------------
//  功能 2b：武器无过热（照搬 Android/CIU 已验证实现，完整链条见
//  reports/CIU_Android_武器无过热_结论.md）：
//    Weapon_UpdateFireOverheat(0x130D2B4) 每帧算出"过热值" a2
//      -> Weapon_ApplyOverheat(0x130DA78): v5 = 冷却槽值(+0xAEC) - a2
//         v5<0 且过热开关开 -> 过热块（0x130DAB0 的 B.PL 跳过判定）
//    ① B.PL -> B.AL：过热触发块永不执行；不改 RNG 抽取次数，不碰属性表
//    ② hook 强制 a2=0：v5 = 槽值 - 0 ⇒ 冷却槽永不消耗、永不触发负值分支
// ------------------------------------------------------------
typedef void (*tApplyOverheat)(void* a1, float a2);
static tApplyOverheat g_origApplyOverheat = nullptr;

static void HookApplyOverheat(void* a1, float a2)
{
    g_origApplyOverheat(a1, 0.0f);   // 过热值恒 0 => 冷却槽永不被消耗
}

static void PatchNoOverheat()
{
    uint32_t* p = (uint32_t*)(g_base + VA_HEAT_BRANCH);
    const uint32_t expect = 0x54000645u;   // B.PL loc_130DB78 (45 06 00 54)
    const uint32_t patch  = 0x5400064Eu;   // B.AL loc_130DB78 (4E 06 00 54)
    if (*p != expect) {
        LOGW("[noheat] branch mismatch @%lx: %08X (want %08X)",
             (unsigned long)VA_HEAT_BRANCH, *p, expect);
        return;
    }
    if (MemUtils::writeBytes((void*)p, &patch, 4))
        LOGI("[noheat] PATCHED %lx B.PL -> B.AL (overheat disabled)",
             (unsigned long)VA_HEAT_BRANCH);
    else
        LOGE("[noheat] write failed");

    void* fn = (void*)(g_base + VA_APPLY_OVERHEAT);
    if (DobbyHook(fn, (void*)HookApplyOverheat, (void**)&g_origApplyOverheat) == 0)
        LOGI("[noheat] HOOK Weapon_ApplyOverheat @ %lx OK (heat forced 0)",
             (unsigned long)VA_APPLY_OVERHEAT);
    else
        LOGE("[noheat] hook Weapon_ApplyOverheat FAILED");
}

// ------------------------------------------------------------
//  触摸事件（由 ab 加载器经 JNI 传入，View.dispatchTouchEvent 层，
//  天然多点；游戏逻辑也可走 ClickGui 内的 AInputQueue 钩子）
// ------------------------------------------------------------
} // namespace ciu

extern "C" __attribute__((visibility("default")))
jlong Java_com_ab_Loader_nativeGetNativePtr(JNIEnv* env, jclass, jobject event)
{
    // AMotionEvent_fromJava（API 29+，运行时解析；不取得所有权，勿 delete）
    using tFromJava = void* (*)(JNIEnv*, jobject);
    static tFromJava fromJava = (tFromJava)dlsym(RTLD_DEFAULT, "AMotionEvent_fromJava");
    if (!fromJava) return 0;
    return (jlong)fromJava(env, event);
}

extern "C" __attribute__((visibility("default")))
void Java_com_ab_Loader_nativeTouchEvent(JNIEnv*, jclass, jlong ptr, jint /*action*/)
{
    if (!ptr) return;
    const AInputEvent* ev = (const AInputEvent*)ptr;

    // 完整 action（含 POINTER_DOWN/UP 的 pointer index 位）
    int32_t full = AMotionEvent_getAction(ev);
    int32_t masked = full & AMOTION_EVENT_ACTION_MASK;
    size_t idx = (full & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                 >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    ClickGui::getInstance()->onSystemTouch(
        masked,
        AMotionEvent_getX(ev, idx),
        AMotionEvent_getY(ev, idx),
        AMotionEvent_getPointerId(ev, idx));
}

// ------------------------------------------------------------
//  初始化
// ------------------------------------------------------------
namespace ciu {

static bool VerifyLightupPath()
{
    struct Expected { uintptr_t offset; uint32_t word; };
    constexpr Expected expected[] = {
        {VA_ENEMY_COMMON_UPD, 0xD10343FFu},
        {VA_DESC_INIT, 0xF0001E68u},
        {VA_COLLECTIBLE_SPAWN, 0xA9BE7BFDu},
        {VA_COLLECTIBLE_SPAWN + 4, 0xF9000BF3u},
        {VA_DESC_EXECUTE, 0xA9BE7BFDu},
        {VA_DESC_EXECUTE + 4, 0xA9014FF4u},
        {0x109BC54, 0x3959E668u}, // LDRB W8, [X19,#0x679]
        {0x109BC88, 0xF9474269u}, // LDR X9, [X19,#0xE80]
        {0x109F0B8, 0x91392260u}, // ADD X0, X19, #0xE48
        {0x109FA40, 0xF9400800u}, // descriptor owner at +0x10
    };
    for (const auto& entry : expected) {
        uint32_t actual = *(const uint32_t*)(g_base + entry.offset);
        if (actual != entry.word) {
            LOGE("[LIGHTUP] signature mismatch @%lx: %08X (want %08X); disabled",
                 (unsigned long)entry.offset, actual, entry.word);
            return false;
        }
    }
    if (*(const uintptr_t*)(g_base + VA_GLOW_DESC_VTBL + 0x28) != g_base + 0x109FA40) {
        LOGE("[LIGHTUP] descriptor executor vtable mismatch; disabled");
        return false;
    }
    return true;
}

static void InitThread()
{
    // 等待游戏 so 加载（本 so 在进程启动时由 XP 模块注入，早于游戏逻辑）
    while (true) {
        uintptr_t base = MemUtils::getBase("libproject_top.so");
        if (base != 0) {
            MemUtils::setBase(base);
            g_base = base;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOGI("[mod] libproject_top.so base = 0x%lx", (unsigned long)g_base);

    // 等 游戏初始化（与 Android/CIU 已验证版本一致：base 后再等 2 秒装钩）
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // ---- 功能 1：不死补丁 ----
    PatchPlayerTakeHit();

    // ---- 功能 2：对敌伤害 x5 ----
    {
        void* target = (void*)(g_base + VA_ENEMY_TAKEDAMAGE);
        if (DobbyHook(target, (void*)HookEnemyTakeDamage, (void**)&g_origEnemyTakeDamage) == 0) {
            LOGI("[mod] HOOK %lx : Enemy_TakeDamage (scale=%g)",
                 (unsigned long)VA_ENEMY_TAKEDAMAGE, g_dmgScale);
        } else {
            LOGE("[mod] HOOK FAIL @ %lx (Enemy_TakeDamage)", (unsigned long)VA_ENEMY_TAKEDAMAGE);
        }
    }

    // ---- 功能 2b：武器无过热 ----
    PatchNoOverheat();

    // ---- 功能 4：NetRole 自检断言旁路 ----
    PatchNetRoleAsserts();

    // ---- 功能 3：只在地址签名匹配后安装敌人更新入口钩子 ----
    if (VerifyLightupPath()) {
        g_fnDescInit = (tDescInit)(g_base + VA_DESC_INIT);
        g_fnSpawn = (tSpawn)(g_base + VA_COLLECTIBLE_SPAWN);
        g_fnDescExecute = (tDescExecute)(g_base + VA_DESC_EXECUTE);
        void* target = (void*)(g_base + VA_ENEMY_COMMON_UPD);
        if (DobbyHook(target, (void*)HookEnemyCommonUpd, (void**)&g_origEnemyUpd) == 0 &&
            g_origEnemyUpd) {
            g_lightupReady.store(true, std::memory_order_release);
            LOGI("[LIGHTUP] native descriptor path ready: attach=+0xE80, update=%lx",
                 (unsigned long)VA_ENEMY_COMMON_UPD);
        } else {
            LOGE("[LIGHTUP] enemy update hook failed; lightup disabled");
        }
    }

    // ---- 覆盖层（固定"点亮"按钮 + ImGui 菜单）----
    ClickGui::getInstance()->init();

    // ---- 每帧渲染钩子（GLSurfaceView 的 drawFrame JNI 入口）----
    {
        void* target = (void*)(g_base + VA_DRAW_FRAME);
        if (DobbyHook(target, (void*)HookDrawFrame, (void**)&g_origDrawFrame) == 0) {
            LOGI("[mod] HOOK %lx : NativeLibWrapper_drawFrame (overlay renderer)",
                 (unsigned long)VA_DRAW_FRAME);
        } else {
            LOGE("[mod] HOOK FAIL @ %lx (drawFrame)", (unsigned long)VA_DRAW_FRAME);
        }
    }

    LOGI("[mod] init done. button: tap [点亮] = random lightup");
}

} // namespace ciu

__attribute__((constructor))
void so_entry()
{
    std::thread worker(ciu::InitThread);
    worker.detach();
}
