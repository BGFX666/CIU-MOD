// il2cpp_resolver.cpp
#include "il2cpp_resolver.hpp"

bool Il2CppResolver::s_initialized = false;

// 静态函数指针定义
void* (*Il2CppResolver::il2cpp_domain_get)() = nullptr;
void* (*Il2CppResolver::il2cpp_domain_assembly_open)(void*, const char*) = nullptr;
void* (*Il2CppResolver::il2cpp_assembly_get_image)(void*) = nullptr;
void* (*Il2CppResolver::il2cpp_class_from_name)(void*, const char*, const char*) = nullptr;
void* (*Il2CppResolver::il2cpp_class_from_type)(void*) = nullptr;
void* (*Il2CppResolver::il2cpp_class_get_method_from_name)(void*, const char*, int) = nullptr;
void* (*Il2CppResolver::il2cpp_class_get_field_from_name)(void*, const char*) = nullptr;
void* (*Il2CppResolver::il2cpp_runtime_invoke)(void*, void*, void**, void**) = nullptr;
void* (*Il2CppResolver::il2cpp_string_new)(const char*) = nullptr;
void* (*Il2CppResolver::il2cpp_object_new)(void*) = nullptr;
void* (*Il2CppResolver::il2cpp_thread_attach)(void*) = nullptr;
void (*Il2CppResolver::il2cpp_field_static_get_value)(void*, void*) = nullptr;
void* (*Il2CppResolver::il2cpp_type_get_object)(void*) = nullptr;
const char* (*Il2CppResolver::il2cpp_exception_from_name_msg)(void* exc) = nullptr;