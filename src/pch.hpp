#include <jni.h>
#include <errno.h>
#include <iostream>
#include <stdexcept>
#include <exception>
#include <string>
#include <unistd.h>
#include <vector>
#include <thread>
#include <ranges>
#include <android/log.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <dobby.h>
#define LOG_TAG "MH"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
