#pragma once
// MD5 (RFC 1321), incremental: usado nos hashes de ROM do RetroAchievements.
#include <cstddef>
#include <cstdint>
#include <string>

class Md5 {
public:
    Md5();
    void update(const void* data, size_t len);
    std::string hex();  // finaliza; 32 dígitos minúsculos

private:
    void block(const uint8_t* p);
    uint32_t m_h[4];
    uint64_t m_len = 0;
    uint8_t m_buf[64];
    size_t m_fill = 0;
};
