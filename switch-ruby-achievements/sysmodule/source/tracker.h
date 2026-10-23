#pragma once
#include <stdint.h>

// Contador de tempo de jogo. Chamado uma vez por segundo pelo laço principal.
//
// Regras de precisão:
// - Só conta enquanto existe um aplicativo em execução.
// - O tempo é a soma dos deltas do relógio entre ticks; um delta negativo ou
//   maior que TRACKER_GAP_SECONDS indica sleep (ou ajuste do relógio) e nunca
//   conta. Se o mesmo jogo continua aberto depois de um sleep de até 3 h, a
//   sessão segue a mesma; senão ela fecha no instante em que o sleep começou.
// - Nos primeiros segundos de um título só o tempo é contado; NACP, conta e
//   banco ficam para depois da abertura (jogos/emuladores pesados).
// - Sessões ficam em memória e são gravadas no banco quando o lock está livre;
//   um checkpoint a cada TRACKER_CHECKPOINT_SECONDS limita a perda em queda de energia.
#define TRACKER_GAP_SECONDS 10
#define TRACKER_CHECKPOINT_SECONDS 60

void tracker_init(int64_t now);
void tracker_tick(int64_t now);
