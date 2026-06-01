#pragma once
#include <idf_c_includes.hpp>

class MicrorosSync {
public:
    static MicrorosSync& getInstance();
    bool init();
    void start();
    static void publishCompressedImage(const uint8_t* buf, size_t len, uint32_t seq);

private:
    MicrorosSync() = default;
    ~MicrorosSync() = default;
    MicrorosSync(const MicrorosSync&) = delete;
    MicrorosSync& operator=(const MicrorosSync&) = delete;
};