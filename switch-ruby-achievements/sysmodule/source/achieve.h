#pragma once
#include <switch.h>
#include <stdint.h>

#include <string>

#include "pt_db.h"

// Conquistas locais: o pacote ativo da chave em PT_ACH_DIR (ver common/ach/pt_pack.h:
// <chave>.json ou <chave>.<variante>.json), onde a chave é o Title ID (jogo nativo) ou o
// nome do arquivo da ROM sem a extensão.
//
// {
//   "game_name": "...",
//   "title_id": "01007EF00011E000",       // opcional, informativo
//   "build_id": "CD57B23FA4BBAD65...",     // opcional: trava de versão das condições de RAM
//   "achievements": [
//     {"id": "10h", "title": "...", "description": "...",
//      "type": "playtime_stat", "target_seconds": 36000},
//     {"id": "glider", "title": "...", "type": "save_binary_offset",
//      "save_file": "option.sav", "offset_hex": "0x12", "expected_value_hex": "0x01",
//      "op": "==" | ">=" | "bit_check", "size": 1},
//     {"id": "flag", "title": "...", "type": "save_flag_search", "save_file": "0/game_data.sav",
//      "flag_name": "IsGet_Paraglider", "offset_after_flag": 4, "size": 1,
//      "operator": "==", "expected_value_int": 1},
//     {"id": "glider", "title": "...", "type": "save_flag_hash_search", "save_file": "0/game_data.sav",
//      "flag_name": "IsGet_Paraglider", "hash_type": "crc32", "expected_value_int": 1},
//     {"id": "lvl", "title": "...", "type": "save_json_key", "save_file": "/switch/jogo/save.json",
//      "key_path": "player.stats.level", "operator": ">=", "expected_value_int": 20}
//   ]
// }
//
// "build_id" (hex, prefixo do build id do executável principal): as condições
// ram_flag_search só são avaliadas quando o jogo aberto tem esse build id;
// noutra versão ficam bloqueadas (uma linha no log). Save e tempo não mudam.
// "icon" numa conquista: PNG relativo a PT_ACH_DIR ("icons/<tid>/<id>.png"),
// mostrado na galeria e no pop-up. "category" e outros campos extras são ignorados.
// "rarity": "bronze" (padrão), "silver", "gold" ou "ruby". A Ruby (platina) é
// uma só por pacote e não tem condição: desbloqueia sozinha, logo depois da última
// das outras, na mesma avaliação (o pop-up dela vem por último). Condição escrita
// nela é ignorada; num pacote com mais de uma, só a primeira vale e as outras
// ficam de fora (nem contam para a platina).
// Operadores: == != >= <= e bit_check (só binários); "op" e "operator" valem.
// Valor esperado em "expected_value_int" ou "expected_value_hex".
// save_flag_search: primeira ocorrência de flag_name; o valor fica
// offset_after_flag bytes (padrão 4) depois do fim do nome.
// save_flag_hash_search: hash do flag_name ("murmur3", padrão, ou "crc32"),
// procurado como 4 bytes little-endian em posição múltipla de "align" (padrão
// 4); o valor (size padrão 4) fica offset_after_flag bytes (padrão 4) depois do
// início do hash. BOTW usa crc32; TotK, murmur3.
// count_of: {"type": "count_of", "min": 3, "conditions": [{...}, {...}]}
// cumprida com pelo menos "min" (padrão: todas) das condições; aceita
// qualquer tipo, inclusive outro count_of (até 4 níveis).
// save_json_key: caminho com pontos, índices numéricos para arrays; booleano
// vale 1/0. Saves JSON de até 64 KB.
// ram_flag_search: memória do jogo aberto pelo dmnt:cht do Atmosphère (sem FS,
// sem SD, sem 0xE02; só com o jogo aberto).
//   {"type": "ram_flag_search", "signature": "FD 7B C1 A9 ?? 4F 01 A9",
//    "pointer_offsets": ["0x10", "0x20"], "search_size": "0x10000",
//    "flag_name": "IsGet_PlayerStole2", "hash_type": "crc32" | "flag_hash": "0x1A2B3C4D",
//    "offset_after_flag": 4, "size": 4, "expected_value_int": 1}
// Partida: o início da assinatura no módulo principal (?? = qualquer byte;
// precisa aparecer exatamente uma vez), procurada uma vez por sessão; sem
// "signature", o início do módulo principal. Cada offset da cadeia é somado e
// o ponteiro naquele endereço é lido. Na janela de search_size bytes (padrão
// 64 KB, máx. 4 MB) a partir dali, procura o hash como no
// save_flag_hash_search; offset_after_flag negativo = valor antes do hash
// (objetos de flag do BOTW: FlagBool -1, FlagS32 -4); expected_value_int padrão 1. Offsets em número ou
// texto ("0x10": JSON não aceita 0x10 sem aspas).
//
// ram_pointer_chain: valor num endereço achado por cadeia de ponteiros, para
// qualquer engine (Unity, Unreal, própria). Mesma convenção do EdiZon-SE e do
// Cheat Engine: [[main+0x1A2B3C0]+0x28]+0x10 =
//   {"type": "ram_pointer_chain", "pointer_offsets": ["0x1A2B3C0", "0x28", "0x10"],
//    "data_type": "s32", "operator": ">=", "expected_value_int": 100}
// Todos os offsets menos o último são lidos como ponteiro; o último só é
// somado e dá o endereço do valor (um offset só = main+offset, endereço fixo).
// "signature" opcional como no ram_flag_search. data_type: u8 s8 u16 s16 u32
// s32 f32 u64 s64 (padrão s32). Operadores: == != >= <= > < e bit_set (todos os
// bits da máscara ligados; não vale para f32). Valor em expected_value_int,
// expected_value_hex ou expected_value_float (f32). Também respeita build_id.
// Os operadores > < e bit_set valem também nas outras condições.
// save_file relativo = save do console (save da conta do perfil); começando
// com "/" ou "sdmc:" = arquivo no cartão SD (ports, PPSSPP, emuladores).
// Só o nome ("game_data.sav") = o arquivo mais recente com esse nome em
// qualquer subpasta do save (0/, 1/, manual/0/...); com "/" no meio
// ("0/game_data.sav") = esse caminho exato.
// O save do console é montado só para leitura, também com o jogo aberto, pelo
// tempo de copiá-lo para PT_ACH_CACHE_DIR; as condições leem a cópia. Um save
// que não mudou desde a última avaliação não é lido de novo. Se o FS recusar a
// montagem com o jogo aberto, a avaliação fica para quando ele fechar.
// Valores com "size" bytes (1-8, little-endian).

struct AchieveTarget {
    int64_t game_id;       // games.id
    int64_t user_id;       // users.id da sessão
    u64 tid;               // título em execução (o emulador, no caso de ROMs)
    const char* rom_path;  // "" = jogo nativo
    bool running;          // jogo aberto
};

// Intervalo mínimo entre avaliações do jogo aberto.
#define ACHIEVE_LIVE_SECONDS 30

// Avalia as definições do jogo; desbloqueios vão para user_achievements e
// achievement_queue. Chamar com o banco aberto (dentro do lock) e dentro de uma
// transação; depois do COMMIT chamar achieve_commit(), e achieve_discard() se
// ela foi desfeita (senão um save lido e não gravado seria pulado depois).
//
// Com o jogo aberto, se o FS recusar a montagem do save (0xE02, save em uso),
// o save do console não é tentado de novo até o jogo fechar: suspenso na Home
// ele continua com o save preso.
void achieve_check(sqlite3* db, const AchieveTarget& t, int64_t now);
void achieve_commit(void);

// O app trocou o pacote ativo de algum jogo (PT_ACH_ACTIVE_PATH mudou desde a última
// chamada)? O tracker grava e reavalia na hora, para o overlay ver a lista nova.
bool achieve_packs_changed(void);
void achieve_discard(void);

// Apaga da fila o que o overlay já mostrou (PT_POPUP_SEEN_PATH) e exporta o
// restante em PT_POPUP_PATH, uma por linha: "id|raridade|ícone|título" (ícone =
// caminho do PNG no SD ou vazio; título por último, pode ter qualquer texto).
void achieve_export_popups(sqlite3* db);

// Caminho, dentro do save montado em `fs`, do arquivo `target` mais recente
// (mtime) em `save_root` e subpastas. `target` com "/" é usado como está.
// Nada encontrado: save_root + "/" + target.
std::string resolve_active_save_path(FsFileSystem* fs, const std::string& save_root, const std::string& target);

// Exporta em PT_ACH_CURRENT_PATH as conquistas do jogo aberto para o perfil da
// sessão (lista do overlay). game_id 0 = nenhum jogo aberto (arquivo vazio).
//   G|<nome do jogo>
//   A|<1 desbloqueada, 0 não>|<unlocked_at>|<título>|<descrição>
// Secretas bloqueadas saem sem título/descrição.
void achieve_export_current(sqlite3* db, int64_t game_id, int64_t user_id);
