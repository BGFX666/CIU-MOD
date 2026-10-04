 # CMakeLists.txt - Snake 项目的 CMake 配置 (ARM64 Release only)
cmake_minimum_required(VERSION 3.18)
 
project(ciu VERSION 1.0.0 LANGUAGES C CXX)

# =================   === 强制 Release 构建 ====================
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# ==================== 设置 C++ 标准 ====================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================== 查找源文件和头文件 ====================
# 查找根目录 /src 中的所有 .cpp 文件（不递归子文件夹）
file(GLOB ALL_SOURCE_FILES
    "src/*.cpp"
    "include/imgui/*.cpp"
)

# 查找根目录 /src 中的所有 .hpp 文件
file(GLOB ALL_HEADER_FILES
    "src/*.hpp"
    "src/*.h"
    
) 

# 显示找到的文件数量
list(LENGTH ALL_SOURCE_FILES SOURCE_COUNT)
list(LENGTH ALL_HEADER_FILES HEADER_COUNT)
message(STATUS "Found ${SOURCE_COUNT} source files")
message(STATUS "Found ${HEADER_COUNT} header files")
 
# ==================== 创建共享库 ====================
add_library(ciu SHARED ${ALL_SOURCE_FILES} ${ALL_HEADER_FILES})

# 设置库属性
set_target_properties(ciu PROPERTIES
    OUTPUT_NAME "ciu" 
    PREFIX "lib"
    SUFFIX ".so"
)  

# ==================== 包含目录配置 ====================
target_include_directories(ciu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ANDROID_NDK}/sources/android/native_app_glue

)

# ==================== 预编译头文件 ====================
# 检查 pch.hpp 是否存在
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp")
    # 启用预编译头文件
    target_precompile_headers(ciu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp
    )
    
    # 强制所有源文件包含 pch.hpp（使用编译器选项）
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # GCC/Clang编译器使用-include选项
        target_compile_options(ciu PRIVATE
            -include "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp"
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        # MSVC编译器使用/FI选项
        target_compile_options(ciu PRIVATE
            "/FI\"${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp\""
        )      
    endif()         
              
    # 显示成功信息
    message(STATUS "强制包含预编译头文件: ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp")
else()
    message(WARNING "pch.hpp not found at ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp")
endif()

# ==================== 链接静态库 ====================
set(STATIC_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/arm64-v8a")

# 链接 libdobby.a
if(EXISTS "${STATIC_LIB_DIR}/libdobby.a")
    target_link_libraries(ciu PRIVATE "${STATIC_LIB_DIR}/libdobby.a")
    message(STATUS "Found libdobby.a at ${STATIC_LIB_DIR}/libdobby.a")
else()
    message(WARNING "libdobby.a not found at ${STATIC_LIB_DIR}/libdobby.a")
endif()

set(FREETYPE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/arm64-v8a")

if(EXISTS "${FREETYPE_DIR}/libfreetype.a")
    target_link_libraries(ciu PRIVATE "${FREETYPE_DIR}/libfreetype.a")
    target_include_directories(ciu PRIVATE
        "${FREETYPE_DIR}/include"
        "${FREETYPE_DIR}/include/freetype"
    )
    message(STATUS "✓ FreeType static library linked")
else()
    message(WARNING "libfreetype.a not found, text rendering may not work")
endif()

add_library(app-glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c  )

    # 设置包含目录
target_include_directories(app-glue PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue)
# 链接 native_app_glue 到你的库
target_link_libraries(ciu PRIVATE app-glue)


# ==================== 链接系统库 ====================
target_link_libraries(ciu PRIVATE
    log
    android
    dl  
    m
    EGL          # 新增
    GLESv3       # 新增
)

# ==================== 编译选项 ====================
target_compile_options(ciu PRIVATE
    -fPIC
    -O3                          # 最高优化
    -DNDEBUG=1                   # Release 定义
    -fvisibility=hidden          # 隐藏符号
    -fvisibility-inlines-hidden
    -fno-stack-protector         # 关闭栈保护
    -fomit-frame-pointer         # 省略帧指针
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -ffunction-sections
    -fdata-sections
    -w                           # 关闭所有警告
    -fms-extensions              # MSVC 扩展支持
    -DIMGUI_IMPL_OPENGL_ES2      # ImGui GLES2 路径（GLSurfaceView 上下文通吃 ES2/ES3）
    -frtti                       # RTTI
    -fexceptions                 # 异常处理
    -Wno-multichar
    -Wno-char-subscripts
    -Wno-narrowing 
    -fpermissive
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -fextended-identifiers 
)

# ===========   ========= 链接选项 ====================
target_link_options(ciu PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,--build-id=none
    -Wl,--exclude-libs,ALL
    -s
)
 
# ==================== 输出配置 ====================
set_target_properties(ciu PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ==================== 验证配置 ====================
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Target ABI: ARM64")  