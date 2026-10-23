#pragma once
#include <stddef.h>

// Troca dos binários pela atualização do app (aba Atualização). Usado pelo app (download e
// troca) e pelo sysmodule (recuperação no boot). Sem exceções nem alocação grande.
//
// Só os arquivos da lista fixa abaixo podem ser trocados; o manifesto da release não
// acrescenta nada, então dados do usuário (banco, config.ini, secrets.ini, covers/, sfx/,
// achievements/, icons/, cache/, logs) nunca são tocados. Um destino que não existe no SD
// (o usuário removeu o overlay, por exemplo) não é reinstalado.
//
// Troca (pt_update_apply), com todos os arquivos já baixados e conferidos em staging/:
//   1. apaga o backup anterior e grava o diário (versão + destinos);
//   2. para cada destino: destino -> backup/<nome>, staging/<nome> -> destino (rename no
//      mesmo SD; a FS do Switch não faz rename sobre arquivo existente);
//   3. apaga o diário e grava a marca de reinício.
// Se algo falhar no meio, ou se o console desligar, pt_update_recover (chamada por
// pt_ensure_dirs no app e no sysmodule) devolve o backup de cada destino listado no diário:
// volta tudo à versão anterior, nunca fica uma mistura nem um destino faltando.

// Destinos possíveis (índice estável).
int pt_update_target_count(void);
// Caminho relativo à raiz do SD, como no manifesto: "switch/ruby_achievements/ruby_achievements.nro".
const char* pt_update_target_rel(int i);
// Nome do asset na release e do arquivo em staging/ e backup/ (único na lista).
const char* pt_update_target_name(int i);
// Índice do destino com esse caminho exato, ou -1 (fora da lista).
int pt_update_target_find(const char* rel);
// "sdmc:/" + caminho relativo.
void pt_update_target_sd_path(int i, char* out, size_t cap);
// staging/<nome>.
void pt_update_staged_path(int i, char* out, size_t cap);
// 1 se o destino existe no SD.
int pt_update_target_installed(int i);

// Cria as pastas da atualização e esvazia staging/. 0 = ok.
int pt_update_prepare_staging(void);

// Troca os `n` destinos (índices) pelos arquivos de staging/. Todos precisam existir no SD e em
// staging/. 0 = ok (marca de reinício gravada com `version`); -1 = falhou e foi desfeita
// (motivo em `err`).
int pt_update_apply(const char* version, const int* targets, int n, char* err, size_t cap);

// Se ficou um diário (troca interrompida), devolve o backup de cada destino dele e apaga o
// diário. Retorna quantos arquivos voltaram, 0 sem diário, -1 se algum não pôde voltar.
int pt_update_recover(void);

// 1 se há atualização instalada esperando o reinício (versão em `version`), 0 se não.
int pt_update_pending(char* version, size_t cap);
// Sysmodule, ao iniciar: o console reiniciou, então a marca de reinício sai.
void pt_update_boot_done(void);
