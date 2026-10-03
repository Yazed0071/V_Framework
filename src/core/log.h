#pragma once
#include <cstdio>
#include <cstdarg>

void InitLog();
void Log(const char* fmt, ...);
void LogLevelImpl(const char* level, const char* fmt, ...);
void CrashLogf(const char* fmt, ...);
void CloseLog();
void EnsureConsole();
unsigned long LogIoStalledMs();
unsigned long LogIoOwnerThreadId();
unsigned long LogLineSerial();

#define LogInfo(...)  do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("info: ", __VA_ARGS__); } while (0)
#define LogWarn(...)  do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("WARNING: ", __VA_ARGS__); } while (0)
#define LogError(...) do { if (false) std::printf(__VA_ARGS__); LogLevelImpl("ERROR: ", __VA_ARGS__); } while (0)

#ifdef _DEBUG
#define LogDebug(...) Log(__VA_ARGS__)
#else
#define LogDebug(...) ((void)0)
#endif