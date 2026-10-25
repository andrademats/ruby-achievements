// Leitura dos pacotes .rubydb (common/ach/pt_pack.cpp): pacotes gravados pela ferramenta
// Python (tools/json_to_rubydb.py), a árvore montada para o sysmodule, ícones em BLOB,
// referências de ícone e os pacotes recusados. O .json antigo não é mais lido.
#include <switch.h>
#include "pt_pack.h"
#include "pt_paths.h"
#include "pt_util.h"
#include "rubydb_writer.hpp"

#include <string.h>
#include <sys/stat.h>

s64 g_fake_now = 1000;

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

static std::string ach(const char* file) { return std::string(PT_ACH_DIR) + "/" + file; }

static void sql(const char* file, const char* text) {
    sqlite3* db = nullptr;
    sqlite3_open_v2(ach(file).c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
    char* err = nullptr;
    if (sqlite3_exec(db, text, nullptr, nullptr, &err) != SQLITE_OK) printf("SQL: %s\n", err ? err : "?");
    sqlite3_free(err);
    sqlite3_close(db);
}

static const JVal* find(const JVal& root, const char* id) {
    for (auto& a : root.get("achievements")->arr)
        if (a.get_str("id") == id) return &a;
    return nullptr;
}

static int count_key(const JVal& a, const char* key) {
    int n = 0;
    for (auto& kv : a.obj) n += kv.first == key;
    return n;
}

static bool load_fails(const char* file, const char* why_part) {
    JVal root;
    std::string err;
    bool ok = pt_pack_load(file, root, &err);
    if (!ok && err.find(why_part) == std::string::npos) printf("  motivo: %s\n", err.c_str());
    return !ok && err.find(why_part) != std::string::npos && root.type == JVal::Null;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (const char* d : {"sdmc:", "sdmc:/switch", PT_DIR, PT_ACH_DIR, PT_ACH_CACHE_DIR}) mkdir(d, 0777);

    // --- Pacotes gravados pela ferramenta Python
    rubydb::copy(PT_REPO "/sd_card_structure/switch/ruby_achievements/achievements/01007EF00011E000.rubydb",
                 ach("01007EF00011E000.rubydb").c_str());
    rubydb::copy(PT_REPO "/tools/botw_ram_teste/01007EF00011E000.aventureiro.rubydb",
                 ach("01007EF00011E000.aventureiro.rubydb").c_str());
    JVal root;
    std::string err;
    check("pacote do dist (Python) lido", pt_pack_load("01007EF00011E000.rubydb", root, &err));
    const JVal* list = root.get("achievements");
    check("dist: 22 conquistas, Title ID e título do pack_info",
          list && list->arr.size() == 22 && root.get_str("title_id") == "01007EF00011E000" &&
              root.get_str("pack_name") == "The Legend of Zelda: Breath of the Wild");
    const JVal* glider = find(root, "paraglider");
    check("dist: condição volta inteira (type, save_file, hash_type)",
          glider && glider->get_str("type") == "save_flag_hash_search" &&
              glider->get_str("save_file") == "game_data.sav" && glider->get_str("hash_type") == "crc32" &&
              glider->get_int("expected_value_int") == 1);
    check("dist: ordem do pacote (sort_order)", list && list->arr[0].get_str("id") == "paraglider");
    JVal adv;
    check("aventureiro (Python) lido, build id e 55 conquistas",
          pt_pack_load("01007EF00011E000.aventureiro.rubydb", adv, &err) &&
              adv.get_str("build_id") == "CD57B23FA4BBAD65803D9788C01821EE" && adv.get("achievements")->arr.size() == 55);
    const JVal* ruby = find(adv, "ruby");
    check("aventureiro: Ruby sem condição", ruby && pt_pack_rarity(*ruby) == std::string("ruby") && !ruby->get("type"));
    const JVal* magnesis = find(adv, "rt_magnesis");
    const JVal* offs = magnesis ? magnesis->get("pointer_offsets") : nullptr;
    check("aventureiro: cadeia de ponteiros preservada",
          offs && offs->type == JVal::Arr && offs->arr.size() == 4 && offs->arr[0].str == "0x1E3B188");

    // --- Descoberta: só .rubydb
    rubydb::copy(PT_REPO "/tools/botw_ram_teste/01007EF00011E000.aventureiro.json", ach("0100FFFF00000000.json").c_str());
    auto files = pt_pack_files("01007EF00011E000");
    check("arquivos do jogo: principal e a variante .rubydb",
          files.size() == 2 && files[0] == "01007EF00011E000.rubydb" && files[1] == "01007EF00011E000.aventureiro.rubydb");
    check("variante pelo nome do arquivo", pt_pack_variant("01007EF00011E000", files[1]) == "aventureiro");
    check(".json antigo ignorado", pt_pack_files("0100FFFF00000000").empty() && pt_pack_title_keys().size() == 1);

    // --- Pacote com ícones e ícone bloqueado
    const std::string png_a = std::string("\x89PNG\r\n\x1a\n", 8) + "icone A", png_lock = "\x89PNG bloqueado";
    rubydb::write(ach("0100000000000AAA.rubydb"),
                  "{\"achievements\":["
                  "{\"id\":\"a\",\"title\":\"A\",\"description\":\"desc\",\"rarity\":\"gold\",\"secret\":true,"
                  "\"type\":\"playtime_stat\",\"target_seconds\":60},"
                  "{\"id\":\"b\",\"title\":\"B\",\"type\":\"count_of\",\"min\":1,\"conditions\":["
                  "{\"type\":\"playtime_stat\",\"target_seconds\":1},{\"type\":\"playtime_stat\",\"target_seconds\":2}]},"
                  "{\"id\":\"p\",\"title\":\"Plat\",\"rarity\":\"ruby\"}]}",
                  {{"a", png_a}}, {{"a", png_lock}});
    JVal pk;
    check("pacote com BLOBs lido", pt_pack_load("0100000000000AAA.rubydb", pk, &err));
    const JVal* a = find(pk, "a");
    const JVal* secret = a ? a->get("secret") : nullptr;
    check("colunas viram campos (raridade, secreta, descrição)",
          a && pt_pack_rarity(*a) == std::string("gold") && secret && secret->type == JVal::Bool && secret->b &&
              a->get_str("description") == "desc" &&
              a->get_int("target_seconds") == 60);
    check("ícone vira referência ao pacote", a && a->get_str("icon") == "rubydb:0100000000000AAA.rubydb#a");
    const JVal* b = find(pk, "b");
    const JVal* conds = b ? b->get("conditions") : nullptr;
    check("count_of aninhado preservado; sem ícone, sem referência",
          conds && conds->arr.size() == 2 && b->get_int("min") == 1 && !b->get("icon"));
    std::string png;
    check("ícone desbloqueado lido do BLOB", pt_pack_read_icon("0100000000000AAA.rubydb", "a", false, png) && png == png_a);
    check("ícone bloqueado lido do BLOB", pt_pack_read_icon("0100000000000AAA.rubydb", "a", true, png) && png == png_lock);
    check("sem ícone: false", !pt_pack_read_icon("0100000000000AAA.rubydb", "b", false, png) && png.empty());
    check("conquista inexistente: false", !pt_pack_read_icon("0100000000000AAA.rubydb", "zz", false, png));
    const std::string out_png = PT_ACH_CACHE_DIR "/a.png";
    check("ícone copiado para arquivo em pedaços",
          pt_pack_extract_icon("0100000000000AAA.rubydb", "a", out_png) && rubydb::slurp(out_png.c_str()) == png_a);
    check("sem ícone: extração false", !pt_pack_extract_icon("0100000000000AAA.rubydb", "b", PT_ACH_CACHE_DIR "/b.png"));

    // A condição não sobrescreve as colunas nem injeta um caminho de ícone.
    sql("0100000000000AAA.rubydb",
        "UPDATE achievements SET condition_json='{\"type\":\"playtime_stat\",\"target_seconds\":5,"
        "\"title\":\"FORJADO\",\"icon\":\"../../segredo.png\",\"rarity\":\"ruby\"}' WHERE id='b'");
    check("condição com title/icon/rarity: as colunas valem",
          pt_pack_load("0100000000000AAA.rubydb", pk, &err) && (b = find(pk, "b")) && b->get_str("title") == "B" &&
              count_key(*b, "title") == 1 && !b->get("icon") && pt_pack_rarity(*b) == std::string("bronze") &&
              b->get_int("target_seconds") == 5);

    // Ícone acima do limite: recusado na leitura e na extração.
    sql("0100000000000AAA.rubydb", "UPDATE achievements SET icon_unlocked=zeroblob(262145) WHERE id='a'");
    check("ícone acima de 256 KB recusado",
          !pt_pack_read_icon("0100000000000AAA.rubydb", "a", false, png) &&
              !pt_pack_extract_icon("0100000000000AAA.rubydb", "a", out_png + "2"));

    // --- Pacotes recusados
    rubydb::write(ach("0100000000000BBB.rubydb"), "{\"achievements\":[{\"id\":\"x\",\"type\":\"playtime_stat\"}]}");
    sql("0100000000000BBB.rubydb", "PRAGMA user_version=2");
    check("formato 2 (user_version) recusado", load_fails("0100000000000BBB.rubydb", "formato 1"));
    FILE* f = fopen(ach("0100000000000CCC.rubydb").c_str(), "wb");
    fputs("isto não é um banco SQLite, só texto qualquer com mais de cem bytes para o cabeçalho ser lido inteiro...", f);
    fclose(f);
    check("arquivo que não é SQLite recusado", load_fails("0100000000000CCC.rubydb", "formato 1"));
    rubydb::write(ach("0100000000000DDD.rubydb"), "{\"achievements\":[{\"id\":\"x\",\"type\":\"playtime_stat\"}]}");
    sql("0100000000000DDD.rubydb", "DELETE FROM pack_info");
    check("sem pack_info recusado", load_fails("0100000000000DDD.rubydb", "pack_info"));
    rubydb::write(ach("0100000000000EEE.rubydb"), "{\"achievements\":[{\"id\":\"x\",\"type\":\"playtime_stat\"}]}");
    sql("0100000000000EEE.rubydb", "UPDATE achievements SET condition_json='[1,2]'");
    check("condição que não é objeto recusada", load_fails("0100000000000EEE.rubydb", "condição inválida em x"));
    sql("0100000000000EEE.rubydb", "UPDATE achievements SET condition_json='{\"type\":'");
    check("condição com JSON quebrado recusada", load_fails("0100000000000EEE.rubydb", "condição inválida"));
    sql("0100000000000EEE.rubydb",
        "UPDATE achievements SET condition_json='{}', description=substr(hex(zeroblob(33000)),1,65600)");
    check("texto acima de 64 KB recusado", load_fails("0100000000000EEE.rubydb", "64 KB"));
    check("pacote ausente", load_fails("0100000000000FFF.rubydb", "não encontrado"));
    check("nome com pasta ou .. recusado",
          load_fails("../config.ini", "inválido") && load_fails("sub/x.rubydb", "inválido"));

    // --- Referências de ícone
    std::string rf, ra;
    check("referência: arquivo e id",
          pt_pack_parse_icon_ref(pt_pack_icon_ref("Jogo #1.rubydb", "a.b"), rf, ra) && rf == "Jogo #1.rubydb" && ra == "a.b");
    check("caminho comum não é referência",
          !pt_pack_parse_icon_ref(PT_ACH_ICONS_DIR "/ra/1.png", rf, ra) && !pt_pack_parse_icon_ref("", rf, ra));
    check("referência para fora da pasta recusada",
          !pt_pack_parse_icon_ref("rubydb:../x.rubydb#a", rf, ra) && !pt_pack_parse_icon_ref("rubydb:a/b.rubydb#a", rf, ra) &&
              !pt_pack_parse_icon_ref("rubydb:x.rubydb", rf, ra) && !pt_pack_parse_icon_ref("rubydb:x.rubydb#", rf, ra));

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
