#pragma once
#include <stdint.h>

// Detecção da ROM ativa dentro de emuladores e frontends, para que o tempo
// conte para a ROM e não para o app que a executa.
//
// Fontes "de início" (gravadas quando a ROM abre):
//   - RetroArch: content_history.lpl (item mais recente primeiro)
//   - PPSSPP:    ppsspp.ini, seção [Recent], FileName0
// Fonte "de saída" (frontends sem histórico, ex.: Tico): o save ou save state
// que o frontend grava ao sair do jogo, procurado nas pastas dele.
//
// Nada aqui abre um arquivo que o emulador possa estar gravando: as consultas
// usam só a data de modificação (fsFsGetFileTimeStampRaw) e listagem de pastas,
// e um arquivo de histórico só é lido depois de ficar estável entre duas consultas.

typedef struct {
    char path[512];   // rom_path ("/roms/..."); "nome:<rótulo>" quando o arquivo não é achado
    char label[128];  // nome exibido
    bool at_start;    // true = a ROM acabou de abrir; false = o jogo acabou de fechar
    const char* source;
} RomHit;

// Referência das fontes de início (abertura do título: só datas de arquivos).
void roms_baseline_start(void);
// Referência da varredura de saves (fim da janela de abertura).
void roms_baseline_scan(void);

// Consulta as fontes. `scan` inclui a varredura de saves (mais cara).
// Retorna true se apareceu uma ROM nova desde a última consulta.
bool roms_poll(RomHit* out, bool scan);

// Só a varredura de saves, na hora (usada quando o título fecha: o frontend
// costuma gravar o save state justamente ao sair).
bool roms_scan_now(RomHit* out);
