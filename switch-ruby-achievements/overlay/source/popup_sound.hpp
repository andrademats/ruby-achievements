#pragma once
// Som do pop-up de conquista (o escolhido no app ou PT_POPUP_SFX_PATH; regras em pt_wav.hpp):
// WAV PCM 16 bits 48 kHz lido do SD aos pedaços por uma thread e tocado pelo audout. Falha em qualquer etapa = pop-up mudo.
#include <switch.h>

namespace sound {

// Chamar dentro da sessão do sm (initServices) e no fim (exitServices).
void init();
void exit();

// Duração do arquivo em ms se ele puder ser tocado; 0 = sem arquivo ou formato errado.
u32 duration_ms(const char* path);

// Toca `path` por no máximo `max_ms` (o tempo do pop-up). Não bloqueia; um novo play
// ou stop interrompe o anterior.
void play(const char* path, u32 max_ms);
void stop();

}  // namespace sound
