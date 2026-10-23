#include "md5.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t kK[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr uint8_t kR[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                            5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
inline uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
}  // namespace

Md5::Md5() : m_h{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::block(const uint8_t* p) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 |
               (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + rotl(a + f + kK[i] + w[g], kR[i]);
        a = t;
    }
    m_h[0] += a;
    m_h[1] += b;
    m_h[2] += c;
    m_h[3] += d;
}

void Md5::update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    m_len += len;
    while (len > 0) {
        size_t n = 64 - m_fill < len ? 64 - m_fill : len;
        memcpy(m_buf + m_fill, p, n);
        m_fill += n;
        p += n;
        len -= n;
        if (m_fill == 64) {
            block(m_buf);
            m_fill = 0;
        }
    }
}

std::string Md5::hex() {
    uint64_t bits = m_len * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (m_fill != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (8 * i));
    update(len, 8);
    char out[33];
    for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", (m_h[i / 4] >> (8 * (i % 4))) & 0xff);
    return out;
}
