#pragma once

#include "idf_c_includes.hpp"

namespace shelfbot {

struct Timestamp {
    int64_t monotonic_us;
    int64_t epoch_us;
};

struct FrameTimestamp {
    uint32_t sequence;

    int64_t capture_monotonic_us;

    int64_t capture_epoch_us;
};

class ShelfbotTimestamp {
public:

    static int64_t monotonicMicros();

    static int64_t epochMicros();

    static Timestamp now();

    static bool isEpochValid();

    static void toRosTime( int64_t epoch_us, int32_t& sec, uint32_t& nanosec);

    static FrameTimestamp capture( uint32_t sequence);
};

}
