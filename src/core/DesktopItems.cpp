#include "balcony/core/DesktopItems.h"

#include <Windows.h>
#include <knownfolders.h>
#include <shlobj_core.h>

#include <algorithm>

namespace balcony::core
{
    namespace
    {
        std::wstring KnownFolder(REFKNOWNFOLDERID id)
        {
            PWSTR raw = nullptr;
            const HRESULT hr = SHGetKnownFolderPath(id, 0, nullptr, &raw);
            if (FAILED(hr) || !raw)
            {
                if (raw)
                {
                    CoTaskMemFree(raw);
                }
                return {};
            }

            std::wstring path(raw);
            CoTaskMemFree(raw);
            return path;
        }

        std::wstring StripExtension(std::wstring_view fileName)
        {
            const size_t dot = fileName.find_last_of(L'.');
            // A leading dot (".gitignore"-style) isn't an extension to
            // strip -- rare on a desktop, but cheap to get right.
            if (dot == std::wstring_view::npos || dot == 0)
            {
                return std::wstring(fileName);
            }
            return std::wstring(fileName.substr(0, dot));
        }

        void EnumerateInto(const std::wstring& directory, std::vector<DesktopItemInfo>& out)
        {
            if (directory.empty())
            {
                return;
            }

            WIN32_FIND_DATAW findData{};
            const std::wstring pattern = directory + L"\\*";
            const HANDLE handle = FindFirstFileW(pattern.c_str(), &findData);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return;
            }

            do
            {
                const std::wstring_view name = findData.cFileName;
                if (name == L"." || name == L"..")
                {
                    continue;
                }

                // Matches Explorer's default view: desktop.ini/Thumbs.db
                // and anything else marked hidden or system stays off
                // the desktop.
                if (findData.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
                {
                    continue;
                }

                DesktopItemInfo item;
                item.path = directory + L"\\" + findData.cFileName;
                item.displayName = StripExtension(name);
                out.push_back(std::move(item));
            } while (FindNextFileW(handle, &findData));

            FindClose(handle);
        }
    }

    std::vector<DesktopItemInfo> EnumerateDesktopItems()
    {
        std::vector<DesktopItemInfo> items;

        // Real Explorer merges both -- the current user's own desktop
        // and the all-users one every account shares.
        EnumerateInto(KnownFolder(FOLDERID_Desktop), items);
        EnumerateInto(KnownFolder(FOLDERID_PublicDesktop), items);

        return items;
    }
}
