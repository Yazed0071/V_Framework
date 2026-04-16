#include "pch.h"
#include "V_FrameWorkModLoader.h"
#include "V_FrameWorkState.h"
#include "log.h"

#include <Windows.h>
#include <string>
#include <vector>

namespace V_FrameWorkModLoader
{
    namespace
    {
        static constexpr const char* kModDirectory = "mod\\V_FrameWork";
    }

    std::vector<std::string> FindModFiles()
    {
        std::vector<std::string> files;

        std::string searchPath = std::string(kModDirectory) + "\\*.lua";
        WIN32_FIND_DATAA findData{};
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE)
        {
            Log("[ModLoader] No mod directory or no .lua files in '%s'\n", kModDirectory);
            return files;
        }

        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            std::string fullPath = std::string(kModDirectory) + "\\" + findData.cFileName;
            files.push_back(fullPath);

        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);

        Log("[ModLoader] Found %zu mod files in '%s'\n", files.size(), kModDirectory);
        return files;
    }
}
