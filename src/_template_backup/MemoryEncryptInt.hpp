#pragma once
#include "MemUtils.hpp"

extern uintptr_t ctor_addr;
extern uintptr_t get_addr;
extern uintptr_t set_addr;

struct MemoryEncryptInt {
    int _data1;
    int _data2;
    int _index;
    int _check_sum;

    MemoryEncryptInt(int value) {
        if (MemUtils::game_base != 0) {
            using CtorFunc = void (*)(MemoryEncryptInt*, int);
            CtorFunc ctor = reinterpret_cast<CtorFunc>(MemUtils::game_base + ctor_addr);
            ctor(this, value);
        }
    }

    int get_Data() {
        if (MemUtils::game_base == 0) {
            return 0;
        }
        using Getter = int(*)(MemoryEncryptInt*);
        Getter getter = reinterpret_cast<Getter>(MemUtils::game_base + get_addr);
        return getter(this);
    }

    void set_Data(int value) {
        if (MemUtils::game_base == 0) {
            return;
        }
        using Setter = int(*)(MemoryEncryptInt*, int);
        Setter setter = reinterpret_cast<Setter>(MemUtils::game_base + set_addr);
        setter(this, value);
    }

    
};