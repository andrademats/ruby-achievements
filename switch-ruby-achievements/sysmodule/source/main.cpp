// Ruby Achievements - sysmodule
// Serviço em segundo plano: detecta o título em execução, conta o tempo de jogo
// (excluindo sleep), grava sessões no SQLite e publica o estado para o overlay.
#include <switch.h>

#include "detect.h"
#include "pt_log.h"
#include "pt_db.h"
#include "tracker.h"
#include "pt_util.h"
#include "pt_update.h"

// Heap pequeno: SQLite (cache de 128 KiB) + buffer de NACP alocado sob demanda.
#define INNER_HEAP_SIZE 0x180000

extern "C" {

void __libnx_init_time(void);  // libnx: inicializa time()/gettimeofday()

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();

    rc = timeInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));
    __libnx_init_time();

    // Serviços opcionais: sem eles o tracker degrada (sem nome/usuário), não aborta.
    pmdmntInitialize();
    pminfoInitialize();
    nsInitialize();
    setInitialize();  // idioma do sistema para o NACP
    accountInitialize(AccountServiceType_System);

    smExit();
}

void __appExit(void) {
    accountExit();
    setExit();
    nsExit();
    pminfoExit();
    pmdmntExit();
    timeExit();
    fsdevUnmountAll();
    fsExit();
}

}  // extern "C"

static void db_log(const char* line) { pt_log("%s", line); }

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    pt_ensure_dirs();  // também desfaz uma atualização interrompida (pt_update_recover)
    pt_update_boot_done();  // console reiniciado: a atualização instalada já vale
    // Versão e data do build confirmam no log o que está instalado no SD.
    pt_log("sysmodule iniciado (versao " PT_VERSION ", build " __DATE__ " " __TIME__ ")");
    pt_db_set_log(db_log);
    detect_init();

    // Aguarda o relógio do sistema ficar disponível após o boot.
    int64_t now = pt_now();
    while (now == 0) {
        svcSleepThread(1'000'000'000ULL);
        now = pt_now();
    }
    tracker_init(now);

    for (;;) {
        svcSleepThread(1'000'000'000ULL);
        int64_t t = pt_now();
        if (t != 0) tracker_tick(t);
    }
    return 0;
}
