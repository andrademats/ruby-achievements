// Cliente mínimo do dmnt:cht (Atmosphère, stratosphere/dmnt/source/cheat).
// libnx não traz este serviço; os comandos são os do dmnt_cheat_service.
#include "ram.h"

#include <stdio.h>
#include <string.h>

namespace {

enum : u32 {
    kHasCheatProcess = 65000,
    kGetCheatProcessMetadata = 65002,
    kForceOpenCheatProcess = 65003,
    kReadCheatProcessMemory = 65102,
};

struct Extents {
    u64 base;
    u64 size;
};

struct Metadata {
    u64 process_id;
    u64 program_id;
    Extents main_nso;
    Extents heap;
    Extents alias;
    Extents aslr;
    u8 main_nso_module_id[0x20];
};
static_assert(sizeof(Metadata) == 0x70, "CheatProcessMetadata");

Service g_srv;
bool g_open = false;
Result g_last_rc = 0;

// O sm deixa o pedido de um serviço não registrado esperando para sempre:
// confere antes, como o Tesla faz (registrar o nome falha se ele já existe).
bool service_running(const char* name) {
    Handle h;
    SmServiceName n = smEncodeName(name);
    if (R_FAILED(smRegisterService(&h, n, false, 1))) return true;
    svcCloseHandle(h);
    smUnregisterService(n);
    return false;
}

}  // namespace

bool ram_open(RamProcess* out) {
    if (!g_open) {
        if (R_FAILED(smInitialize())) return false;
        g_open = service_running("dmnt:cht") && R_SUCCEEDED(smGetService(&g_srv, "dmnt:cht"));
        smExit();
        if (!g_open) return false;
    }
    u8 has = 0;
    if (R_FAILED(serviceDispatchOut(&g_srv, kHasCheatProcess, has))) return false;
    if (!(has & 1) && R_FAILED(serviceDispatch(&g_srv, kForceOpenCheatProcess))) return false;
    Metadata m;
    memset(&m, 0, sizeof(m));
    if (R_FAILED(serviceDispatchOut(&g_srv, kGetCheatProcessMetadata, m))) return false;
    out->pid = m.process_id;
    out->tid = m.program_id;
    out->main_base = m.main_nso.base;
    out->main_size = m.main_nso.size;
    for (int i = 0; i < 0x20; i++) snprintf(out->build_id + 2 * i, 3, "%02X", m.main_nso_module_id[i]);
    return true;
}

bool ram_read(u64 addr, void* buf, size_t n) {
    if (!g_open || n == 0) return false;
    const struct {
        u64 address;
        u64 size;
    } in = {addr, (u64)n};
    // O dmnt declara um sf::OutBuffer comum (MapAlias, modo Normal): pedir o modo
    // NonSecure faz o servidor recusar o pedido antes de ler.
    Result rc = serviceDispatchIn(&g_srv, kReadCheatProcessMemory, in,
                                  .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                  .buffers = {{buf, n}});
    if (R_FAILED(rc)) g_last_rc = rc;
    return R_SUCCEEDED(rc);
}

u32 ram_last_rc(void) { return g_last_rc; }

void ram_close(void) {
    if (!g_open) return;
    serviceClose(&g_srv);
    g_open = false;
}
