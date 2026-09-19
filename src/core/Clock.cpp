#include "balcony/core/Clock.h"

#include <Windows.h>

namespace balcony::core
{
    TimeOfDay Clock::Now()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);

        TimeOfDay time{};
        time.year = st.wYear;
        time.month = st.wMonth;
        time.day = st.wDay;
        time.hour = st.wHour;
        time.minute = st.wMinute;
        time.second = st.wSecond;
        time.dayOfWeek = st.wDayOfWeek;
        return time;
    }
}
