#pragma once
#include <cstdio>
#include <cstdarg>

void InitLog();
void Log(const char* fmt, ...);
void CrashLogf(const char* fmt, ...);
void CloseLog();
void EnsureConsole();

#ifdef _DEBUG
#define LogDebug(...) Log(__VA_ARGS__)
#else
#define LogDebug(...) ((void)0)
#endif