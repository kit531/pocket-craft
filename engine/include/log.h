// Logging shim: logcat on Android, stderr elsewhere. Implemented in engine/core/log.cpp.
#pragma once

namespace pc {
enum class LogLevel { Debug, Info, Warn, Error };
void logPrint(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
}  // namespace pc

#define PC_LOGD(...) ::pc::logPrint(::pc::LogLevel::Debug, __VA_ARGS__)
#define PC_LOGI(...) ::pc::logPrint(::pc::LogLevel::Info, __VA_ARGS__)
#define PC_LOGW(...) ::pc::logPrint(::pc::LogLevel::Warn, __VA_ARGS__)
#define PC_LOGE(...) ::pc::logPrint(::pc::LogLevel::Error, __VA_ARGS__)
