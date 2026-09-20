#include "balcony/persistence/Persistence.h"

#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <Windows.h>
#include <knownfolders.h>
#include <shlobj_core.h>

#include <fstream>
#include <sstream>

namespace balcony::persistence
{
    namespace
    {
        void LogError(std::string_view context, std::string_view detail)
        {
            std::ostringstream message;
            message << "[Persistence] " << context << ": " << detail << "\n";
            OutputDebugStringA(message.str().c_str());
        }
    }

    std::filesystem::path AppDataDirectory()
    {
        PWSTR rawPath = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath);
        if (FAILED(hr) || !rawPath)
        {
            if (rawPath)
            {
                CoTaskMemFree(rawPath);
            }
            return {};
        }

        std::filesystem::path dir = std::filesystem::path(rawPath) / L"Balcony";
        CoTaskMemFree(rawPath);

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            LogError("AppDataDirectory", ec.message());
            return {};
        }

        return dir;
    }

    void StateStore::Load()
    {
        _state = DesktopEnvironmentState{};

        const std::filesystem::path dir = AppDataDirectory();
        if (dir.empty())
        {
            _filePath.clear();
            return;
        }
        _filePath = dir / L"state.json";

        std::ifstream file(_filePath);
        if (!file.is_open())
        {
            // Most commonly: first run, nothing saved yet.
            return;
        }

        try
        {
            DesktopEnvironmentState loaded;
            {
                cereal::JSONInputArchive archive(file);
                archive(loaded);
            }

            if (loaded.version != DesktopEnvironmentState::kCurrentVersion)
            {
                LogError("Load", "unrecognized state version, using defaults");
                return;
            }

            _state = std::move(loaded);
        }
        catch (const std::exception& e)
        {
            // Corrupt/unparseable file -- fall back to defaults rather
            // than crash. Left on disk untouched (not overwritten until
            // the next successful Save()) for manual inspection.
            LogError("Load", e.what());
        }
    }

    bool StateStore::Save()
    {
        if (_filePath.empty())
        {
            return false;
        }

        const std::filesystem::path tempPath = _filePath;
        std::wstring tempPathStr = tempPath.wstring() + L".tmp";

        try
        {
            std::ofstream file(tempPathStr, std::ios::trunc);
            if (!file.is_open())
            {
                LogError("Save", "could not open temp file for writing");
                return false;
            }

            {
                cereal::JSONOutputArchive archive(file);
                archive(_state);
            }
        }
        catch (const std::exception& e)
        {
            LogError("Save", e.what());
            return false;
        }

        // Atomic on the same volume -- a crash between these two lines
        // leaves the old state.json (or none) intact, never a half-
        // written one. See CLAUDE.md section 10.
        if (!MoveFileExW(tempPathStr.c_str(), _filePath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            LogError("Save", "MoveFileExW failed");
            return false;
        }

        return true;
    }
}
