#pragma once

#include <cereal/cereal.hpp> // Just NameValuePair/make_nvp -- archive-specific headers (JSON, vector/string type support) stay private to Persistence.cpp, not part of this public header.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Persistence: cereal-backed serialization for DE/package/config state.
// See CLAUDE.md section 10.
//
// The only thing persisted so far is small, whole-file, read/written
// rarely (a drag ending, a pin/unpin) -- a single flat JSON file is
// enough; there is no query pattern here that would justify SQLite.
namespace balcony::persistence
{
    // %LOCALAPPDATA%\Balcony, created if missing. Empty if it can't be
    // resolved/created -- callers must treat that as "persistence
    // unavailable this run," not a fatal error.
    std::filesystem::path AppDataDirectory();

    // A desktop icon's saved position, keyed by its launch path (the
    // only identity a desktop icon has -- see scripts/desktop.lua).
    struct DesktopIconPosition
    {
        std::string key;
        float x = 0.0f;
        float y = 0.0f;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("key", key), cereal::make_nvp("x", x), cereal::make_nvp("y", y));
        }
    };

    // A taskbar pin. `displayName` is captured at pin time (the running
    // window's title, or the desktop icon's label) so a pinned-but-not-
    // running app still has something to show as its label.
    struct PinnedApp
    {
        std::string path;
        std::string displayName;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("path", path), cereal::make_nvp("displayName", displayName));
        }
    };

    struct DesktopEnvironmentState
    {
        // Not a migration framework -- just a guard so a future
        // incompatible layout is ignored (falls back to defaults)
        // rather than misread. Add real migrations if/when a second
        // version actually ships.
        static constexpr uint32_t kCurrentVersion = 1;

        uint32_t version = kCurrentVersion;
        std::vector<DesktopIconPosition> desktopIconPositions;
        std::vector<PinnedApp> pinnedApps; // Order is taskbar pin order.

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(
                cereal::make_nvp("version", version),
                cereal::make_nvp("desktopIconPositions", desktopIconPositions),
                cereal::make_nvp("pinnedApps", pinnedApps));
        }
    };

    // Owns the single on-disk file backing DE-level customization that
    // must survive a restart (desktop icon positions, taskbar pins).
    class StateStore
    {
    public:
        // Resolves the state file's path and loads it if present. Never
        // throws: a missing file means first run (defaults); a corrupt
        // or unrecognized-version file is logged and treated the same
        // way, with the bad file left on disk untouched for manual
        // inspection rather than silently overwritten.
        void Load();

        // Serializes to a temp file next to the real one, then renames
        // over it -- atomic on the same volume, so a crash mid-write
        // never leaves a half-written state.json. Returns false
        // (never throws) on any failure.
        bool Save();

        DesktopEnvironmentState& State() { return _state; }
        const DesktopEnvironmentState& State() const { return _state; }

    private:
        std::filesystem::path _filePath;
        DesktopEnvironmentState _state;
    };
}
