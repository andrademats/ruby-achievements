// dmnt:cht simulado: regiões de memória em buffers, com contadores.
#include <switch.h>
#include <string.h>
#include <vector>
#include "ram.h"

struct FakeRegion {
    u64 base;
    std::vector<uint8_t> bytes;
};
std::vector<FakeRegion> g_fake_regions;
RamProcess g_fake_proc{};
bool g_fake_dmnt = true;
bool g_fake_session = false;
int g_fake_opens = 0, g_fake_closes = 0;
long g_fake_read_bytes = 0;

bool ram_open(RamProcess* out) {
    g_fake_opens++;
    if (!g_fake_dmnt) return false;
    g_fake_session = true;
    *out = g_fake_proc;
    return true;
}

static u32 g_fake_rc = 0;
u32 ram_last_rc(void) { return g_fake_rc; }
bool ram_read(u64 addr, void* buf, size_t n) {
    if (!g_fake_session) return false;
    for (auto& r : g_fake_regions)
        if (addr >= r.base && addr + n <= r.base + r.bytes.size()) {
            memcpy(buf, &r.bytes[addr - r.base], n);
            g_fake_read_bytes += (long)n;
            return true;
        }
    g_fake_rc = 0x6A0B;  // ResultCheatInvalidAddress? qualquer valor: o teste só confere que aparece
    return false;
}

void ram_close(void) {
    if (g_fake_session) g_fake_closes++;
    g_fake_session = false;
}

uint8_t* fake_at(u64 addr) {
    for (auto& r : g_fake_regions)
        if (addr >= r.base && addr < r.base + r.bytes.size()) return &r.bytes[addr - r.base];
    return nullptr;
}
