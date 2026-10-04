#pragma once
#include <unwind.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <android_native_app_glue.h>
struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        else {
            *state->current++ = reinterpret_cast<void*>(pc);
        }
    }
    return _URC_NO_REASON;
}

static size_t captureBacktrace(void** buffer, size_t max) {
    BacktraceState state = { buffer, buffer + max };
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}

static void printBacktrace() {
    const size_t maxDepth = 32;
    void* buffer[maxDepth];
    size_t depth = captureBacktrace(buffer, maxDepth);
    LOGI("=== Backtrace depth = %zu ===", depth);
    for (size_t i = 0; i < depth; ++i) {
        Dl_info info;
        if (dladdr(buffer[i], &info) && info.dli_fname) {
            const char* fname = info.dli_fname;
            uintptr_t offset = reinterpret_cast<uintptr_t>(buffer[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase);
            // 如果你只关心 libil2cpp.so 的调用者，可以过滤
            if (strstr(fname, "libil2cpp.so")) {
                LOGI("#%02zu pc %08lx  %s", i, offset, fname);
            }
            else {
                LOGI("#%02zu pc %08lx  %s", i, offset, fname);
            }
        }
        else {
            LOGI("#%02zu %p  (no symbol)", i, buffer[i]);
        }
    }
}

class MemUtils {
public:
	static uintptr_t game_base;
	static std::once_flag init_flag;
    static ANativeWindow* game_window;
    static android_app* game_app;
public:
	static uintptr_t getBase(const char* soName);
	static void setBase(uintptr_t base);
    static ANativeWindow* getWindow();
    static void setWindow(ANativeWindow*);
    static void setApp(android_app*);


	template<typename T>
	static T& member_at(void* _this, uintptr_t offset) {
		static T dummy;
		if (!_this) return dummy;
		return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(_this) + offset);
	}

	template<typename Ret, typename... Args>
	static auto getFunc(void* func) {
		return reinterpret_cast<Ret(*)(Args...)>(func);
	}


	template<typename Ret, typename... Args>
	static Ret callFunc(void* func, Args... args) {
		auto f = getFunc<Ret, Args...>(func);
		return f(args...);
	}

    static bool writeBytes(void* ptr, const void* data, size_t size);

#ifdef __INTELLISENSE__
    // 1. 禁用 __declspec 错误
#define __declspec(x)

// 2. 为 IntelliSense 定义简化版 CLASS_FIELD
#define CLASS_FIELD(type, name, offset) \
        type name; \
        type get##name() const; \
        void set##name(type value);

    // 3. 如果需要，可以添加其他宏的简化版
#define AS_FIELD(type, name, fn) type name
#else
    // 实际编译时使用原始定义（从 MemUtils.hpp 复制或保持原样）
#ifndef CLASS_FIELD
#define CLASS_FIELD(type, name, offset) \
            __declspec(property(get=get##name, put=set##name)) type name; \
            type get##name() const { return *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(this) + offset); } \
            void set##name(type value) { *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(this) + offset) = value; } \
            type name##_fuck;
#endif
#endif


};