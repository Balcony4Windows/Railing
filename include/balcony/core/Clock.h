#pragma once

// Wall-clock time (System.Time). Cheap enough to query directly -- no
// polling/delta state needed, unlike the other monitors. See CLAUDE.md
// section 6.
namespace balcony::core
{
    struct TimeOfDay
    {
        int year = 0, month = 0, day = 0;
        int hour = 0, minute = 0, second = 0;
        int dayOfWeek = 0; // 0 = Sunday.
    };

    class Clock
    {
    public:
        static TimeOfDay Now();
    };
}
