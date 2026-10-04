#include "GameSDK.hpp"
std::unordered_map<int, uintptr_t> GameSDK::attr_addrs;
uintptr_t GameSDK::array_base = 0;
int GameSDK::array_max_length = 0;
// 定义全局变量
uintptr_t Method_Singleton_UserInfo__get_Instance__ptr = 0x1572AB0;
uintptr_t get_userInfo_addr = 0xEC7294;
uintptr_t AddGold_addr = 0xAA2354;
uintptr_t Decode_addr = 0x961BD4;
uintptr_t GetTime_addr = 0x1370748; //unused
// 定义静态成员
uintptr_t GameSDK::userInfoPtr = 0;
uintptr_t GameSDK::AttriPtr = 0;

void GameSDK::initAttrAddresses() {
    GameSDK::decodePtr(GameSDK::userInfoPtr);
    auto user_info = (uintptr_t)GameSDK::getUserInfo();
    uintptr_t arr = *(uintptr_t*)(user_info + 40);
    if (!arr) return;
    array_base = arr;
    array_max_length = *(int*)(arr + 24);

    // 预计算需要 hook 的属性地址
    CommonUserAttrib attri;
    int props[] = { attri.IsCheat, attri.RemovedAd, attri.RemovedAd5s, attri.Language };
    for (int prop : props) {
        if (prop < array_max_length) {
            attr_addrs[prop] = array_base + 32 + 16 * prop;
        }
    }
}

int GameSDK::getAttrByAddr(uintptr_t addr) {
    for (auto& [attr, base_addr] : attr_addrs) {
        if (base_addr == addr) return attr;
    }
    return -1;
}

static void patchSnakeDataAddCountLimit() {
    uintptr_t base = MemUtils::game_base;

    // 修改 MOV W25, #0x9400  → MOV W25, #0x9401
    // 原指令: 19 80 92 52 (小端 0x52928019)
    // 改后: 39 80 92 52 (小端 0x52928039)
    uint32_t new_mov_w25 = 0x52928039;
    MemUtils::writeBytes(reinterpret_cast<void*>(base + 0xB53240), &new_mov_w25, 4);

    // 修改 MOV W1, #0x77359400 中的低16位 0x9400 → 0x9401
    // 原指令是两条指令的组合，只需要改第一条 MOV W1, #0x9400
    // 原指令: 01 80 92 52 (小端 0x52928001)
    // 改后: 21 80 92 52 (小端 0x52928021)
    uint32_t new_mov_w1 = 0x52928021;
    MemUtils::writeBytes(reinterpret_cast<void*>(base + 0xB53260), &new_mov_w1, 4);

  //  LOGI("补丁完成，分数上限改为 2000000001");
}

void GameSDK::init() {
    userInfoPtr = MemUtils::game_base + Method_Singleton_UserInfo__get_Instance__ptr;
    AttriPtr = MemUtils::game_base + 0x151FFD0;
    void* funcAddr = (void*)(MemUtils::game_base + 0xB46864);   // SnakeUserData__AddUserScore 的地址 A68664
    uint32_t retInstr = 0xD65F03C0;                       // ARM64 ret
    MemUtils::writeBytes(funcAddr, &retInstr, 4);

    uint32_t patch_branch = 0x1400000B;
    MemUtils::writeBytes(reinterpret_cast<void*>(MemUtils::game_base + 0xB46AA8), &patch_branch, 4);

   
    uint32_t nop = 0xD503201F;
    MemUtils::writeBytes(reinterpret_cast<void*>(MemUtils::game_base + 0xB46AD4), &nop, 4);

    patchSnakeDataAddCountLimit();
}

UserInfo* GameSDK::getUserInfo() {
    using Getter = UserInfo * (*)(uintptr_t);
    Getter getter = reinterpret_cast<Getter>(MemUtils::game_base + get_userInfo_addr);
    uintptr_t ptr1 = *((uintptr_t*)userInfoPtr);
    return getter(ptr1);
}

CommonUserAttrib* GameSDK::getAttri() {
    uintptr_t addr = *(uintptr_t*)(GameSDK::getAttriPtr());
    if (addr == 0) return nullptr;
    return reinterpret_cast<CommonUserAttrib*>(addr + 0xB8);
}



void UserInfo::AddGold(int goldform, MemoryEncryptInt mvalue) {
    using Adder = void(*)(UserInfo*, int, MemoryEncryptInt, int64_t);
    Adder orig = reinterpret_cast<Adder>(MemUtils::game_base + AddGold_addr);
    orig(this, goldform, mvalue,0);

}

uintptr_t GameSDK::decodePtr(uintptr_t ptr) {
    using Decode = uintptr_t(*)(uintptr_t);
    Decode decode = reinterpret_cast<Decode>(MemUtils::game_base + Decode_addr);
    return decode(ptr);
}

float GameSDK::getTime() {
    using Getter = float(*)();
    Getter getter = (Getter)(MemUtils::game_base + GetTime_addr);
    return getter();
}

void UserInfo::setBool(int attri,bool value) {
    using Setting = void(*)(UserInfo*, int, bool);
    Setting set = (Setting)(MemUtils::game_base + 0xA92B10);
    set(this, attri, value);
}

void UserInfo::setPassExem(bool value) {
    using Setting = void(*)(UserInfo*,bool);
    Setting set = (Setting)(MemUtils::game_base + 0xA920EC);
    set(this, value);
}


// 在 GameSDK.cpp 中，其他函数实现之后添加

void GameSDK::il2cpp_runtime_class_init(void* klass) {
    if (klass == nullptr) return;

    using ClassInitFunc = void(*)(void*);
    static ClassInitFunc func = reinterpret_cast<ClassInitFunc>(MemUtils::game_base + 0x94D354);

    if (func) {
        func(klass);
    }
}