#include "balcony/ui/Audio.h"

#include <Windows.h>
#include <mmsystem.h>

namespace balcony::ui
{
    bool Audio::SetSource(std::wstring_view path)
    {
        _path.assign(path);
        return !_path.empty();
    }

    void Audio::Play(bool loop)
    {
        if (_path.empty())
        {
            return;
        }

        // PlaySoundW mixes a single stream process-wide; a proper
        // voice-managed mixer (XAudio2) is future work once concurrent or
        // volume-controlled playback matters.
        DWORD flags = SND_FILENAME | SND_ASYNC | SND_NODEFAULT;
        if (loop)
        {
            flags |= SND_LOOP;
        }
        PlaySoundW(_path.c_str(), nullptr, flags);
    }

    void Audio::Stop()
    {
        PlaySoundW(nullptr, nullptr, 0);
    }
}
