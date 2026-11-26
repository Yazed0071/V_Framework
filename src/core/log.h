#pragma once
#include <cstdio>
#include <cstdarg>

void InitLog();
void Log(const char* fmt, ...);
void LogLevelImpl(const char* level, const char* fmt, ...);
void LogWarnOnceImpl(const char* fmt, ...);
void CrashLogf(const char* fmt, ...);
void CloseLog();
void CloseLogAtExit();
void EnsureConsole();
unsigned long LogIoStalledMs();
unsigned long LogIoOwnerThreadId();
unsigned long LogLineSerial();
bool LogFileOpen();
void LogFlushIfDue();   // once per frame: lines that landed inside the flush gap reach the file

#define LogInfo(...)  do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("info: ", __VA_ARGS__); } while (0)
#define LogWarn(...)  do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("WARNING: ", __VA_ARGS__); } while (0)
#define LogError(...) do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("ERROR: ", __VA_ARGS__); } while (0)
#define LogWarnOnce(...) do { if (false) std::printf(__VA_ARGS__); LogWarnOnceImpl(__VA_ARGS__); } while (0)

// debug builds only: probes, traces and diagnostics; a release build compiles the call away
#ifdef _DEBUG
#define LogDebug(...) do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("debug: ", __VA_ARGS__); } while (0)
#else
#define LogDebug(...) do { if (false) std::printf(__VA_ARGS__); } while (0)
#endif