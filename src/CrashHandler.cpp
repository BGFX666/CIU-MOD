// [文件2] CrashHandler.cpp (纯Android版本)
// 
#include "CrashHandler.hpp"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <unwind.h>
#include <dlfcn.h>
#include <sys/ucontext.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <vector>
#include <mutex>
#include <fstream>
#include <sys/stat.h>   // for mkdir
#include <fstream>     
//#include <execinfo.h>
// 对于旧版Android，我们实现简单的backtrace
struct backtrace_state {
    void** buffer;
    int size;
    int depth;
};

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context* context, void* arg) {
    backtrace_state* state = static_cast<backtrace_state*>(arg);
    if (state->depth >= state->size) {
        return _URC_END_OF_STACK;
    }

    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        state->buffer[state->depth] = reinterpret_cast<void*>(pc);
        state->depth++;
    }

    return _URC_NO_REASON;
}

int backtrace(void** buffer, int size) {
    backtrace_state state = { buffer, size, 0 };
    _Unwind_Backtrace(unwind_callback, &state);
    return state.depth;
}

char** backtrace_symbols(void* const* buffer, int size) {
    // 简化实现，只返回地址
    char** symbols = static_cast<char**>(malloc(size * sizeof(char*)));
    if (!symbols) return nullptr;

    for (int i = 0; i < size; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "0x%p", buffer[i]);
        symbols[i] = strdup(buf);
    }
    return symbols;
}

// 全局变量
static std::function<void()> g_crashCallback = nullptr;
static std::string g_lastCrashInfo;
static std::atomic<bool> g_inCrashHandler(false);
static std::mutex g_crashMutex;
static std::string g_logDir = "/data/data/com.netease.x19/nexus/";

// 用于保存原始信号处理器
static struct sigaction g_originalHandlers[32];

// 前向声明辅助函数
namespace {
    // 安全字符串复制
    void safeStrCopy(char* dest, const char* src, size_t destSize) {
        if (dest && src && destSize > 0) {
            strncpy(dest, src, destSize - 1);
            dest[destSize - 1] = '\0';
        }
    }

    // 反混淆C++符号
    std::string demangleSymbol(const char* symbol) {
        if (!symbol) return "null";

        std::string result = symbol;

        // 尝试反混淆
        int status = 0;
        char* demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
        if (demangled && status == 0) {
            result = demangled;
            free(demangled);
        }

        return result;
    }

    // 获取模块名称
    std::string getModuleName(void* address) {
        Dl_info info;
        if (dladdr(address, &info)) {
            if (info.dli_fname) {
                const char* baseName = strrchr(info.dli_fname, '/');
                return baseName ? (baseName + 1) : info.dli_fname;
            }
        }
        return "Unknown";
    }
}

// 初始化崩溃处理器
void CrashHandler::init(const std::string& logDir) {
    std::lock_guard<std::mutex> lock(g_crashMutex);

    if (!logDir.empty()) {
        g_logDir = logDir;
        // 确保目录以 '/' 结尾
        if (g_logDir.back() != '/') {
            g_logDir += '/';
        }
        // 尝试创建目录（如果不存在）
        mkdir(g_logDir.c_str(), 0755);
    }

    installSignalHandlers();
    installTerminateHandler();

    LOGI("[CrashHandler] Initialized. Log directory: %s", g_logDir.c_str());
}

// 安装信号处理器
void CrashHandler::installSignalHandlers() {
    // 需要处理的信号列表
    const int signals[] = {
        SIGSEGV, // 段错误（内存访问违规）
        SIGBUS,  // 总线错误（对齐错误）
        SIGFPE,  // 浮点异常
        SIGILL,  // 非法指令
        SIGABRT, // abort()调用
        SIGTRAP, // 跟踪/断点陷阱
    };

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = [](int sig, siginfo_t* info, void* context) {
        CrashHandler::handleCrash(sig, context);
        };
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    // 设置信号掩码，防止在处理崩溃时被其他信号中断
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP}) {
        sigaddset(&sa.sa_mask, sig);
    }

    // 为每个信号安装处理器
    for (int sig : signals) {
        if (sigaction(sig, &sa, &g_originalHandlers[sig]) != 0) {
            LOGE("[CrashHandler] Failed to set handler for signal %d", sig);
        }
    }
}

// 安装未捕获异常处理器
void CrashHandler::installTerminateHandler() {
    // 设置C++未捕获异常处理器
    std::set_terminate([]() {
        try {
            // 重新抛出以获取异常信息
            throw;
        }
        catch (const std::exception& e) {
            // 处理C++异常
            std::stringstream ss;
            ss << "=== C++ Uncaught Exception ===\n";
            ss << "Type: " << typeid(e).name() << "\n";
            ss << "What: " << e.what() << "\n\n";

            // 获取调用栈
            ss << "Stack Trace:\n";
            ss << getStackTrace();

            g_lastCrashInfo = ss.str();
            writeCrashLog(ss.str());
        }
        catch (...) {
            // 处理未知异常
            std::stringstream ss;
            ss << "=== Unknown C++ Exception ===\n\n";
            ss << "Stack Trace:\n";
            ss << getStackTrace();

            g_lastCrashInfo = ss.str();
            writeCrashLog(ss.str());
        }

        // 安全执行回调
        safeExecuteCallback();

        // 恢复原始终止处理器并退出
        std::_Exit(1);
        });
}

// 处理崩溃
void CrashHandler::handleCrash(int signal, void* context) {
    // 防止递归调用
    if (g_inCrashHandler.exchange(true)) {
        // 已经在处理崩溃，直接退出
        if (signal == SIGABRT) {
            // 对于SIGABRT，使用原始处理器
            g_originalHandlers[signal].sa_handler(signal);
        }
        return;
    }

    try {
        std::stringstream ss;

        // 1. 崩溃头部信息
        ss << "╔══════════════════════════════════════════════════════════════╗\n";
        ss << "║                    CRASH REPORT                              ║\n";
        ss << "╚══════════════════════════════════════════════════════════════╝\n\n";

        // 2. 基本信息
        ss << "=== BASIC INFORMATION ===\n";
        ss << "Timestamp: " << getTimestamp() << "\n";
        ss << "Signal: " << getSignalName(signal) << " (" << signal << ")\n";
        ss << "Process ID: " << getpid() << "\n";
        ss << "Thread ID: " << syscall(__NR_gettid) << "\n\n";

        // 3. 应用程序信息
        ss << getAppInfo() << "\n";

        // 4. 寄存器信息（如果有上下文）
        if (context) {
            ss << getRegisterInfo(context) << "\n";
        }

        // 5. 调用栈信息
        ss << "=== STACK TRACE ===\n";
        ss << getStackTrace() << "\n";

        // 6. 线程信息
        ss << "=== THREAD INFORMATION ===\n";
        ss << getThreadInfo() << "\n";

        // 7. 内存映射信息
        ss << "=== MEMORY MAPS (partial) ===\n";
        ss << getMemoryMaps() << "\n";

        // 8. 崩溃尾部信息
        ss << "\n╔══════════════════════════════════════════════════════════════╗\n";
        ss << "║               END OF CRASH REPORT                           ║\n";
        ss << "╚══════════════════════════════════════════════════════════════╝\n";

        g_lastCrashInfo = ss.str();

        // 写入日志文件
        writeCrashLog(ss.str());

        // 同时输出到logcat
        LOGI("%s", ss.str().c_str());

    }
    catch (...) {
        // 如果崩溃处理器本身崩溃，尽可能记录一些信息
        std::string simpleMsg = "Crash handler failed while processing signal: " +
            std::to_string(signal) + "\nTime: " + getTimestamp();
        writeCrashLog(simpleMsg);
    }

    // 安全执行回调
    safeExecuteCallback();

    // 恢复原始信号处理器并重新触发信号
    sigaction(signal, &g_originalHandlers[signal], nullptr);
    raise(signal);
}

// 获取调用栈信息
std::string CrashHandler::getStackTrace() {
    std::stringstream ss;
    const int maxDepth = 50;
    void* buffer[maxDepth];
    int depth = backtrace(buffer, maxDepth);
    if (depth <= 0) {
        ss << "  [Failed to get stack trace]\n";
        return ss.str();
    }

    char** symbols = backtrace_symbols(buffer, depth);
    if (!symbols) {
        ss << "  [Failed to get symbols]\n";
        return ss.str();
    }

    // 跳过崩溃处理器本身的帧
    int startFrame = 0;
    for (int i = 0; i < depth; i++) {
        std::string symbol = symbols[i];
        if (symbol.find("CrashHandler") == std::string::npos &&
            symbol.find("handleCrash") == std::string::npos &&
            symbol.find("_Unwind_") == std::string::npos) {
            startFrame = i;
            break;
        }
    }

    for (int i = startFrame; i < depth; i++) {
        ss << "  #" << std::setw(2) << (i - startFrame) << " ";

        Dl_info info;
        if (dladdr(buffer[i], &info)) {
            // 计算相对于模块基址的偏移
            uintptr_t absoluteAddr = reinterpret_cast<uintptr_t>(buffer[i]);
            uintptr_t moduleBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
            uintptr_t offset = absoluteAddr - moduleBase;

            // 获取模块名（不含路径）
            std::string moduleName = "Unknown";
            if (info.dli_fname) {
                const char* baseName = strrchr(info.dli_fname, '/');
                moduleName = baseName ? (baseName + 1) : info.dli_fname;
            }

            // 输出模块名 + 偏移（用户要求的核心信息）
            ss << moduleName << " + 0x" << std::hex << offset << std::dec;

            // 如果有函数名，继续输出详细信息
            if (info.dli_sname) {
                ss << " (";
                ss << demangleSymbol(info.dli_sname);
                ss << " + 0x" << std::hex << (absoluteAddr - (uintptr_t)info.dli_saddr) << std::dec;
                ss << ")";
            }
            ss << "\n";
        }
        else {
            // 无法解析时，输出原始符号
            ss << symbols[i] << "\n";
        }
    }

    // 释放符号表
    for (int i = 0; i < depth; i++) {
        free(symbols[i]);
    }
    free(symbols);

    return ss.str();
}

// 获取寄存器信息（针对Android ARM64）
std::string CrashHandler::getRegisterInfo(void* context) {
    std::stringstream ss;
    ss << "=== REGISTERS ===\n";

    if (!context) {
        ss << "  [No context available]\n";
        return ss.str();
    }

    // 转换为ucontext_t
    ucontext_t* ucontext = static_cast<ucontext_t*>(context);
    mcontext_t* mcontext = &ucontext->uc_mcontext;

#if defined(__aarch64__)
    // ARM64寄存器
    ss << "  PC: 0x" << std::hex << mcontext->pc << std::dec << "\n";
    ss << "  LR: 0x" << std::hex << mcontext->regs[30] << std::dec << "\n";
    ss << "  SP: 0x" << std::hex << mcontext->sp << std::dec << "\n";
    ss << "  X0: 0x" << std::hex << mcontext->regs[0] << std::dec << "\n";
    ss << "  X1: 0x" << std::hex << mcontext->regs[1] << std::dec << "\n";
    ss << "  X2: 0x" << std::hex << mcontext->regs[2] << std::dec << "\n";
    ss << "  X3: 0x" << std::hex << mcontext->regs[3] << std::dec << "\n";
    ss << "  X4: 0x" << std::hex << mcontext->regs[4] << std::dec << "\n";
    ss << "  X5: 0x" << std::hex << mcontext->regs[5] << std::dec << "\n";
#elif defined(__arm__)
    // ARM32寄存器
    ss << "  PC: 0x" << std::hex << mcontext->arm_pc << std::dec << "\n";
    ss << "  LR: 0x" << std::hex << mcontext->arm_lr << std::dec << "\n";
    ss << "  SP: 0x" << std::hex << mcontext->arm_sp << std::dec << "\n";
    ss << "  R0: 0x" << std::hex << mcontext->arm_r0 << std::dec << "\n";
    ss << "  R1: 0x" << std::hex << mcontext->arm_r1 << std::dec << "\n";
    ss << "  R2: 0x" << std::hex << mcontext->arm_r2 << std::dec << "\n";
    ss << "  R3: 0x" << std::hex << mcontext->arm_r3 << std::dec << "\n";
    ss << "  R4: 0x" << std::hex << mcontext->arm_r4 << std::dec << "\n";
    ss << "  R5: 0x" << std::hex << mcontext->arm_r5 << std::dec << "\n";
#elif defined(__x86_64__)
    // x86_64寄存器（用于模拟器）
    ss << "  RIP: 0x" << std::hex << mcontext->gregs[REG_RIP] << std::dec << "\n";
    ss << "  RSP: 0x" << std::hex << mcontext->gregs[REG_RSP] << std::dec << "\n";
    ss << "  RBP: 0x" << std::hex << mcontext->gregs[REG_RBP] << std::dec << "\n";
    ss << "  RAX: 0x" << std::hex << mcontext->gregs[REG_RAX] << std::dec << "\n";
    ss << "  RBX: 0x" << std::hex << mcontext->gregs[REG_RBX] << std::dec << "\n";
    ss << "  RCX: 0x" << std::hex << mcontext->gregs[REG_RCX] << std::dec << "\n";
    ss << "  RDX: 0x" << std::hex << mcontext->gregs[REG_RDX] << std::dec << "\n";
#else
    ss << "  [Unsupported architecture]\n";
#endif

    return ss.str();
}

// 获取内存映射信息
std::string CrashHandler::getMemoryMaps() {
    std::stringstream ss;

    // 读取/proc/self/maps
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        ss << "  [Failed to read memory maps]\n";
        return ss.str();
    }

    char line[512];
    int count = 0;
    const int maxLines = 30; // 限制输出行数

    while (fgets(line, sizeof(line), fp) && count < maxLines) {
        ss << "  " << line;
        count++;
    }

    if (count >= maxLines) {
        ss << "  [... truncated, " << maxLines << " lines shown ...]\n";
    }

    fclose(fp);
    return ss.str();
}

// 获取线程信息
std::string CrashHandler::getThreadInfo() {
    std::stringstream ss;

    // 使用pthread获取当前线程信息
    ss << "  Current thread ID: " << syscall(__NR_gettid) << "\n";
    ss << "  Main thread ID: " << getpid() << "\n";

    // 尝试读取/proc/self/task目录
    DIR* dir = opendir("/proc/self/task");
    if (!dir) {
        ss << "  [Failed to get thread list]\n";
        return ss.str();
    }

    struct dirent* entry;
    int threadCount = 0;
    char line[256];

    while ((entry = readdir(dir)) != nullptr) {
        // 跳过.和..
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // 读取线程状态
        std::string statusPath = std::string("/proc/self/task/") + entry->d_name + "/status";
        FILE* fp = fopen(statusPath.c_str(), "r");
        if (fp) {
            char name[256] = { 0 };
            char state[256] = { 0 };

            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "Name:")) {
                    safeStrCopy(name, line + 5, sizeof(name));
                    // 去除换行符
                    char* newline = strchr(name, '\n');
                    if (newline) *newline = '\0';
                }
                else if (strstr(line, "State:")) {
                    safeStrCopy(state, line + 6, sizeof(state));
                    char* newline = strchr(state, '\n');
                    if (newline) *newline = '\0';
                }
            }

            fclose(fp);

            if (threadCount < 10) { // 只显示前10个线程
                ss << "  TID: " << entry->d_name << " | Name: " << name
                    << " | State: " << state << "\n";
            }
            threadCount++;
        }
    }

    closedir(dir);

    if (threadCount >= 10) {
        ss << "  ... and " << (threadCount - 10) << " more threads\n";
    }
    ss << "  Total threads: " << threadCount << "\n";

    return ss.str();
}

// 获取应用程序信息
std::string CrashHandler::getAppInfo() {
    std::stringstream ss;

    ss << "=== APPLICATION INFO ===\n";

    // 1. 进程信息
    ss << "Process ID: " << getpid() << "\n";
    ss << "Parent PID: " << getppid() << "\n";

    // 2. 用户ID和组ID
    ss << "UID: " << getuid() << " GID: " << getgid() << "\n";

    // 3. 系统信息
    struct utsname sysinfo;
    if (uname(&sysinfo) == 0) {
        ss << "System: " << sysinfo.sysname << " " << sysinfo.release
            << " (" << sysinfo.machine << ")\n";
    }

    // 4. 模块信息（主模块）
    Dl_info info;
    if (dladdr((void*)getAppInfo, &info)) {
        if (info.dli_fname) {
            const char* baseName = strrchr(info.dli_fname, '/');
            ss << "Main Module: " << (baseName ? (baseName + 1) : info.dli_fname) << "\n";
        }
    }

    // 5. 内存使用情况（简单版本）
    FILE* fp = fopen("/proc/self/statm", "r");
    if (fp) {
        unsigned long size, resident, share, text, lib, data, dt;
        if (fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu",
            &size, &resident, &share, &text, &lib, &data, &dt) == 7) {
            ss << "Memory: " << size << " pages total, "
                << resident << " pages resident, "
                << data << " pages data\n";
        }
        fclose(fp);
    }

    return ss.str();
}

// 写入崩溃日志到文件
void CrashHandler::writeCrashLog(const std::string& content) {
    if (g_logDir.empty()) return;

    std::string logPath = g_logDir + "crashLog.txt";   // 文件名符合要求

    // 以追加模式打开文件，如果文件不存在则创建
    std::ofstream file(logPath, std::ios::out | std::ios::app);
    if (file.is_open()) {
        // 可选：每次崩溃添加分隔线便于区分
        file << "\n========== CRASH REPORT @ " << getTimestamp() << " ==========\n";
        file << content;
        file << "\n=========================================\n\n";
        file.close();
    }
    else {
        // 如果文件打开失败，至少输出到 logcat
        LOGE("[CrashHandler] Failed to open crash log file: %s", logPath.c_str());
    }

    // 同时输出到 logcat（原有行为保留）
    LOGI("%s", content.c_str());
}

// 获取时间戳
std::string CrashHandler::getTimestamp() {
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);

    return std::string(buffer);
}

// 获取信号名称
std::string CrashHandler::getSignalName(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
    case SIGBUS:  return "SIGBUS (Bus Error)";
    case SIGFPE:  return "SIGFPE (Floating Point Exception)";
    case SIGILL:  return "SIGILL (Illegal Instruction)";
    case SIGABRT: return "SIGABRT (Abort)";
    case SIGTRAP: return "SIGTRAP (Trap)";
    case SIGSYS:  return "SIGSYS (Bad System Call)";
    case SIGPIPE: return "SIGPIPE (Broken Pipe)";
    default:      return "Unknown Signal";
    }
}

// 安全执行回调
void CrashHandler::safeExecuteCallback() {
    if (g_crashCallback) {
        try {
            g_crashCallback();
        }
        catch (...) {
            // 忽略回调中的异常
        }
    }
}

// 设置崩溃回调
void CrashHandler::setCrashCallback(std::function<void()> callback) {
    g_crashCallback = callback;
}

// 获取上次崩溃信息
std::string CrashHandler::getLastCrashInfo() {
    return g_lastCrashInfo;
}

// 清理崩溃处理器
void CrashHandler::cleanup() {
    std::lock_guard<std::mutex> lock(g_crashMutex);

    // 恢复所有原始信号处理器
    const int signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP };
    for (int sig : signals) {
        sigaction(sig, &g_originalHandlers[sig], nullptr);
    }

    g_crashCallback = nullptr;
    g_inCrashHandler = false;

    // LOGI("[CrashHandler] Cleaned up");
}