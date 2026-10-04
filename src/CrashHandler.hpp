// [文件1] CrashHandler.hpp
#pragma once

#include <string>
#include <functional>
#include <memory>

class CrashHandler {
public:
    // 初始化崩溃处理器
    static void init(const std::string& logDir = "");

    // 设置自定义崩溃回调
    static void setCrashCallback(std::function<void()> callback);

    // 获取上次崩溃的信息
    static std::string getLastCrashInfo();

    // 清理崩溃处理器
    static void cleanup();

private:
    // 安装信号处理器
    static void installSignalHandlers();

    // 安装未捕获异常处理器
    static void installTerminateHandler();

    // 崩溃处理函数
    static void handleCrash(int signal, void* context);

    // 获取调用栈信息
    static std::string getStackTrace();

    // 获取寄存器信息
    static std::string getRegisterInfo(void* context);

    // 获取内存映射信息
    static std::string getMemoryMaps();

    // 获取线程信息
    static std::string getThreadInfo();

    // 获取应用程序信息
    static std::string getAppInfo();

    // 写入崩溃日志到文件
    static void writeCrashLog(const std::string& content);

    // 生成时间戳
    static std::string getTimestamp();

    // 获取信号名称
    static std::string getSignalName(int signal);

    // 安全地执行回调
    static void safeExecuteCallback();
};