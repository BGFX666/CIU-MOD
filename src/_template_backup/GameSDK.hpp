#pragma once
#include "MemoryEncryptInt.hpp"
#include "MemUtils.hpp"
#include <unordered_map>
extern uintptr_t Method_Singleton_UserInfo__get_Instance__ptr;
extern uintptr_t get_userInfo_addr;
extern uintptr_t AddGold_addr;
class CommonUserAttrib
{
public:
    int RemovedAd = 21;
    int RemovedAd5s = 38;
    int ContinueLoginDays = 32;
    int LastLoginDay = 12;
    int IsPassExame = 6;
    int MuteMusic = 5;
    int MuteSound = 4;
    int Language = 16;
    int Gold = 3;
    int StopVibrate = 22;
    int ShareDay = 11;
    int ShareCount = 10;
    int GoldEver = 23;
    int ShareCode = 25;
    int IsCheat = 27;
    int CheatVersion = 68;
    int IsDailyTaskGiveSishen = 37;
    std::string AppName = "";
    std::string AppScoreName = "";
    int ChargeValue = 0;
    int Theme = 0;
    int OnlyOneCode = 102663488;
    int AccountCreateTime = 29228;
    int totalLoginDays = 1665760992;
    int AdCount = 29227;
    int AddKeepSec = 1665760880;
    int RebornTicket = 29227;
};
/*========== CommonUserAttrib 字段值 ==========
04-10 19:22:49.460 10283 10309 I MH      : RemovedAd: 21
04-10 19:22:49.460 10283 10309 I MH      : RemovedAd5s: 38
04-10 19:22:49.460 10283 10309 I MH      : ContinueLoginDays: 32
04-10 19:22:49.460 10283 10309 I MH      : LastLoginDay: 12
04-10 19:22:49.460 10283 10309 I MH      : IsPassExame: 6
04-10 19:22:49.460 10283 10309 I MH      : MuteMusic: 5
04-10 19:22:49.460 10283 10309 I MH      : MuteSound: 4
04-10 19:22:49.460 10283 10309 I MH      : Language: 16
04-10 19:22:49.460 10283 10309 I MH      : Gold: 3
04-10 19:22:49.460 10283 10309 I MH      : StopVibrate: 22
04-10 19:22:49.460 10283 10309 I MH      : ShareDay: 11
04-10 19:22:49.460 10283 10309 I MH      : ShareCount: 10
04-10 19:22:49.460 10283 10309 I MH      : GoldEver: 23
04-10 19:22:49.460 10283 10309 I MH      : ShareCode: 25
04-10 19:22:49.460 10283 10309 I MH      : IsCheat: 27
04-10 19:22:49.460 10283 10309 I MH      : CheatVersion: 68
04-10 19:22:49.460 10283 10309 I MH      : IsDailyTaskGiveSishen: 37
04-10 19:22:49.460 10283 10309 I MH      : ChargeValue: 0
04-10 19:22:49.460 10283 10309 I MH      : Theme: 0
04-10 19:22:49.460 10283 10309 I MH      : OnlyOneCode: 102663488
04-10 19:22:49.460 10283 10309 I MH      : AccountCreateTime: 29228
04-10 19:22:49.460 10283 10309 I MH      : totalLoginDays: 1665760992
04-10 19:22:49.460 10283 10309 I MH      : AdCount: 29227
04-10 19:22:49.460 10283 10309 I MH      : AddKeepSec: 1665760880
04-10 19:22:49.460 10283 10309 I MH      : RebornTicket: 29227
04-10 19:22:49.460 10283 10309 I MH      : =============================================*/
class UserInfo {
public:
    void AddGold(int goldForm, MemoryEncryptInt value);
    void setPassExem(bool);
    void setBool(int, bool);
};

class GameSDK {
private:
    static uintptr_t userInfoPtr;
    static uintptr_t AttriPtr;

    static uintptr_t array_base;      // _lst_encrypt_data 数组基址
    static int array_max_length;
    static std::unordered_map<int, uintptr_t> attr_addrs;
public:
    
    static void init();
    static UserInfo* getUserInfo();
    static CommonUserAttrib* getAttri();
    static void il2cpp_runtime_class_init(void* klass);
    static void initAttrAddresses();   // 预计算属性地址
    static int getAttrByAddr(uintptr_t addr);  // 根据地址返回属性值，-1表示未找到

    static uintptr_t getUserInfoPtr() {
        
        return userInfoPtr;
    }
    static uintptr_t getAttriPtr() {
        return AttriPtr;
    }
    static float getTime();
    static uintptr_t decodePtr(uintptr_t ptr);
    
};
struct UnityEngine_Rect_Fields {
	float m_XMin;
	float m_YMin;
	float m_Width;
	float m_Height;
};

struct SnakeSpeedConfig {
    float maxSpeedRate;
    int maxLength;
};
class SnakeConfig {
public:
    CLASS_FIELD(float, SpeedRate, 0x60);
    CLASS_FIELD(float, maxSpeedRate, 0x70);
    CLASS_FIELD(int, maxLength, 0x74);
};
class Move;
class Snake {
public:
    CLASS_FIELD( bool, invincible, 0x164);
    CLASS_FIELD(int, index, 0x140);
    CLASS_FIELD(int, RebornCount, 0xF8);
    CLASS_FIELD(float, RebornTime, 0x178);
    CLASS_FIELD(float, SpeedValue, 0x11C);
    CLASS_FIELD(float, SpeedValue1, 0x118);
   
    SnakeConfig* getSnakeConfig() {
        return MemUtils::member_at<SnakeConfig*>(this, 0x60);
    }

    Move* getController() {
        return MemUtils::member_at<Move*>(this, 0xC0);
    }

    bool isLocalPlayer() {
        return index == 0;
    }
};

class Move {
public:
    CLASS_FIELD(float, Speed, 0x40);
    Snake* getSnake() {
        return MemUtils::member_at<Snake*>(this, 0x48);
    }
};