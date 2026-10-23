#pragma once
#include <switch.h>

// Memória do jogo aberto, só leitura, pelo serviço dmnt:cht do Atmosphère (o
// mesmo dos cheats). Não passa pelo FS nem pelo cartão SD.
//
// ram_open conecta ao dmnt:cht e, se o dmnt ainda não estiver ligado ao jogo,
// pede que ele se ligue (ForceOpenCheatProcess). Chamar ram_close ao terminar:
// fecha só a conexão com o serviço; o dmnt continua ligado até o jogo fechar.

struct RamProcess {
    u64 pid;
    u64 tid;
    u64 main_base;  // módulo principal do jogo (código e dados globais)
    u64 main_size;
    char build_id[65];  // build id do módulo principal, em hex (64 dígitos)
};

bool ram_open(RamProcess* out);
bool ram_read(u64 addr, void* buf, size_t n);
u32 ram_last_rc(void);  // Result da última leitura que falhou (para o log)
void ram_close(void);
