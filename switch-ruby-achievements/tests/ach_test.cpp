#include "achieve.cpp"
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;
static bool EV(sqlite3* db, const AchieveTarget& t, const JVal& a) { EvalCtx c{db, t, true, {}, {}}; if (a.get_str("type") == "save_flag_hash_search") want_hash(c, a); return satisfied(c, a); }
static JVal J(const char* s) { JVal v; if (!json_parse(s, strlen(s), v)) { printf("BAD JSON %s\n", s); exit(1);} return v; }
static int fails = 0;
static void check(const char* name, bool got, bool want) { printf("%-44s %s\n", name, got == want ? "ok" : "FALHOU"); if (got != want) fails++; }
int main() {
  mkdir("sdmc:", 0777); mkdir("sdmc:/switch", 0777); mkdir("sdmc:/switch/ruby_achievements", 0777); mkdir("sdmc:/switch/ruby_achievements/cache", 0777); mkdir("sdmc:/sv", 0777); mkdir("sdmc:/0", 0777);
  // Save binário de ~100 KB com a flag cruzando a borda de 32 KB.
  std::vector<char> bin(100000, 'x');
  const char* flag = "IsGet_Paraglider";
  size_t at = 32 * 1024 - 5;
  memcpy(&bin[at], flag, strlen(flag));
  size_t val = at + strlen(flag) + 4;
  bin[val] = 1; bin[val + 1] = 0x02;
  memcpy(&bin[90000], "Flag_Late", 9); bin[90000 + 9 + 4] = 0x0F;
  memcpy(&bin[100000 - 12], "Flag_End", 8);  // valor ficaria fora do arquivo
  FILE* f = fopen("sdmc:/sv/game.sav", "wb"); fwrite(bin.data(), 1, bin.size(), f); fclose(f);
  f = fopen("sdmc:/0/game_data.sav", "wb"); fwrite(bin.data(), 1, bin.size(), f); fclose(f);
  f = fopen("sdmc:/sv/save.json", "wb");
  fputs("{\"player\":{\"stats\":{\"level\":25,\"dead\":false},\"items\":[3,{\"n\":7}]},\"done\":true,\"name\":\"x\"}", f); fclose(f);

  sqlite3* db; sqlite3_open(":memory:", &db);
  sqlite3_exec(db, "CREATE TABLE users(id INTEGER PRIMARY KEY, account_uid TEXT);"
                   "INSERT INTO users VALUES(1,'0000000000000001000000000000000A');", 0, 0, 0);
  AchieveTarget sd{1, 1, 0x0100000000000001ULL, "", true};
  AchieveTarget closed{1, 1, 0x01007EF00011E000ULL, "", false};
  AchieveTarget running = closed; running.running = true;

  auto S = [&](const AchieveTarget& t, const char* js) { return EV(db, t, J(js)); };
  check("flag na borda ==1", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"IsGet_Paraglider\",\"operator\":\"==\",\"expected_value_int\":1}"), true);
  check("flag na borda ==2 (falso)", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"IsGet_Paraglider\",\"expected_value_int\":2}"), false);
  check("flag size 2 == 0x0201", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"IsGet_Paraglider\",\"size\":2,\"expected_value_hex\":\"0x0201\"}"), true);
  check("flag offset 0 lê 'x' != 0", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"IsGet_Paraglider\",\"offset_after_flag\":0,\"operator\":\"!=\",\"expected_value_int\":0}"), true);
  check("flag tardia bit_check 0x05", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"Flag_Late\",\"operator\":\"bit_check\",\"expected_value_hex\":\"0x05\"}"), true);
  check("flag fora do arquivo", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"Flag_End\",\"operator\":\"!=\",\"expected_value_int\":99}"), false);
  check("flag inexistente", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/game.sav\",\"flag_name\":\"Nope\",\"operator\":\"!=\",\"expected_value_int\":99}"), false);
  check("arquivo inexistente", S(sd, "{\"type\":\"save_flag_search\",\"save_file\":\"/sv/none.sav\",\"flag_name\":\"x\",\"operator\":\"!=\",\"expected_value_int\":99}"), false);
  check("save do console, jogo fechado", S(closed, "{\"type\":\"save_flag_search\",\"save_file\":\"0/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"expected_value_int\":1}"), true);
  check("save do console, jogo aberto (lê cópia)", S(running, "{\"type\":\"save_flag_search\",\"save_file\":\"0/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"expected_value_int\":1}"), true);
  g_save_rc = 0x7D402;
  check("save do console, jogo aberto (FS recusa)", S(running, "{\"type\":\"save_flag_search\",\"save_file\":\"0/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"expected_value_int\":1}"), false);
  g_save_rc = 0;
  check("json level >= 20", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"player.stats.level\",\"operator\":\">=\",\"expected_value_int\":20}"), true);
  check("json level <= 20 (falso)", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"player.stats.level\",\"operator\":\"<=\",\"expected_value_int\":20}"), false);
  check("json bool true == 1", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"done\",\"operator\":\"==\",\"expected_value_int\":1}"), true);
  check("json bool false == 0", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"player.stats.dead\",\"operator\":\"==\",\"expected_value_int\":0}"), true);
  check("json índice de array", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"player.items.1.n\",\"operator\":\"==\",\"expected_value_int\":7}"), true);
  check("json índice fora", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"player.items.9\",\"operator\":\">=\",\"expected_value_int\":0}"), false);
  check("json chave texto", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"name\",\"operator\":\">=\",\"expected_value_int\":0}"), false);
  check("json chave ausente", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/save.json\",\"key_path\":\"player..x\",\"operator\":\">=\",\"expected_value_int\":0}"), false);
  check("json arquivo grande demais", S(sd, "{\"type\":\"save_json_key\",\"save_file\":\"/sv/game.sav\",\"key_path\":\"a\",\"operator\":\">=\",\"expected_value_int\":0}"), false);
  check("compat: save_binary_offset op ==", S(sd, "{\"type\":\"save_binary_offset\",\"save_file\":\"/sv/game.sav\",\"offset_hex\":\"0x0\",\"expected_value_hex\":\"0x78\",\"op\":\"==\"}"), true);
  check("compat: save_binary_offset sem op", S(sd, "{\"type\":\"save_binary_offset\",\"save_file\":\"/sv/game.sav\",\"offset_hex\":\"0x0\",\"expected_value_hex\":\"0x78\"}"), true);
  check("tipo desconhecido", S(sd, "{\"type\":\"xyz\"}"), false);
  printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
  return fails;
}
