// il2cpp_resolver.h
#pragma once
#include <dlfcn.h>
#include <string>


class Il2CppResolver {
public:
    static bool Il2CppResolver::Init() {
        if (s_initialized) return true;

        // 尝试从当前进程的全局符号表中解析符号
        // 注意：某些系统需要 RTLD_DEFAULT 或 RTLD_NEXT
#define RESOLVE_GLOBAL(name) \
    name = (decltype(name))dlsym(RTLD_DEFAULT, #name); \
    if (!name) { \
        LOGI("Failed to resolve " #name " from global symbols"); \
        /* 可尝试从 RTLD_NEXT 获取 */ \
        name = (decltype(name))dlsym(RTLD_NEXT, #name); \
        if (!name) { \
            LOGI("Also failed from RTLD_NEXT"); \
            return false; \
        } \
    }

        RESOLVE_GLOBAL(il2cpp_domain_get);
        RESOLVE_GLOBAL(il2cpp_domain_assembly_open);
        RESOLVE_GLOBAL(il2cpp_assembly_get_image);
        RESOLVE_GLOBAL(il2cpp_class_from_name);
        RESOLVE_GLOBAL(il2cpp_class_from_type);
        RESOLVE_GLOBAL(il2cpp_class_get_method_from_name);
        RESOLVE_GLOBAL(il2cpp_class_get_field_from_name);
        RESOLVE_GLOBAL(il2cpp_runtime_invoke);
        RESOLVE_GLOBAL(il2cpp_string_new);
        RESOLVE_GLOBAL(il2cpp_object_new);
        RESOLVE_GLOBAL(il2cpp_thread_attach);
        RESOLVE_GLOBAL(il2cpp_field_static_get_value);
        RESOLVE_GLOBAL(il2cpp_type_get_object);
        RESOLVE_GLOBAL(il2cpp_exception_from_name_msg);

#undef RESOLVE_GLOBAL

        s_initialized = true;
        LOGI("Il2CppResolver initialized successfully via global symbols");
        return true;
    }

    // 导出函数指针声明
    static void* (*il2cpp_domain_get)();
    static void* (*il2cpp_domain_assembly_open)(void* domain, const char* name);
    static void* (*il2cpp_assembly_get_image)(void* assembly);
    static void* (*il2cpp_class_from_name)(void* image, const char* namespaze, const char* name);
    static void* (*il2cpp_class_from_type)(void* type);
    static void* (*il2cpp_class_get_method_from_name)(void* klass, const char* name, int argsCount);
    static void* (*il2cpp_class_get_field_from_name)(void* klass, const char* name);
    static void* (*il2cpp_runtime_invoke)(void* method, void* obj, void** params, void** exc);
    static void* (*il2cpp_string_new)(const char* str);
    static void* (*il2cpp_object_new)(void* klass);
    static void* (*il2cpp_thread_attach)(void* domain);
    static void (*il2cpp_field_static_get_value)(void* field, void* value);
    static void* (*il2cpp_type_get_object)(void* type);
   

    static const char* (*il2cpp_exception_from_name_msg)(void* exc);

private:
    static bool s_initialized;
};