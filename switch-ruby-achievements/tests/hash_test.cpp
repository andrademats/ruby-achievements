#include "achieve.cpp"
s64 g_fake_now = 1000;
int g_save_opens = 0;
Result g_save_rc = 0;
static bool EV(sqlite3* db, const AchieveTarget& t, const JVal& a) { EvalCtx c{db, t, true, {}, {}}; if (a.get_str("type") == "save_flag_hash_search") want_hash(c, a); return satisfied(c, a); }
static JVal J(const char* s) { JVal v; json_parse(s, strlen(s), v); return v; }
static int fails = 0;
static void check(const char* name, bool ok) { printf("%-46s %s\n", name, ok ? "ok" : "FALHOU"); if (!ok) fails++; }
static void put32(std::vector<char>& b, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i)); }
int main() {
  // Hashes contra a referência em Python (zlib + implementação independente).
  FILE* r = fopen("../hash_ref.txt", "r"); char line[256];
  while (fgets(line, sizeof line, r)) {
    line[strcspn(line, "\n")] = 0;
    char* p1 = strchr(line, '|'); *p1 = 0; char* p2 = strchr(p1 + 1, '|'); *p2 = 0;
    uint32_t mm = strtoul(p1 + 1, 0, 16), cr = strtoul(p2 + 1, 0, 16);
    std::string s = line, name = "hash \"" + s + "\"";
    check(name.c_str(), murmur3_32(s) == mm && crc32_ieee(s) == cr);
  }
  fclose(r);
  check("murmur3 hello == 0x248bfa47 (publicado)", murmur3_32("hello") == 0x248bfa47);
  check("murmur3 quick fox == 0x2e4ff723 (publicado)", murmur3_32("The quick brown fox jumps over the lazy dog") == 0x2e4ff723);

  // Save no formato hash+valor (cabeçalho de 0x0C bytes, entradas de 8 bytes).
  mkdir("sdmc:", 0777); mkdir("sdmc:/sv", 0777);
  std::vector<char> b(200000, 0);
  uint32_t hc = crc32_ieee("IsGet_Paraglider"), hm = murmur3_32("IsGet_Paraglider");
  size_t e1 = 0x0C + 8 * 4100;               // ~32 KB: perto da borda do bloco
  put32(b, e1, hc); put32(b, e1 + 4, 1);
  size_t e2 = 0x0C + 8 * 20000;
  put32(b, e2, hm); put32(b, e2 + 4, 0x12345678);
  // Mesmo hash em posição desalinhada antes da verdadeira: deve ser ignorado.
  uint32_t hx = crc32_ieee("Flag_Count");
  put32(b, 1001, hx); put32(b, 1005, 99);
  size_t e3 = 0x0C + 8 * 15000; put32(b, e3, hx); put32(b, e3 + 4, 250);
  // Hash na borda exata de 32 KB (cruza dois blocos).
  uint32_t hy = crc32_ieee("Flag_Border"); put32(b, 32768 - 2, hy); put32(b, 32768 + 2, 7);
  FILE* f = fopen("sdmc:/sv/game_data.sav", "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f);

  sqlite3* db; sqlite3_open(":memory:", &db);
  AchieveTarget sd{1, 1, 0x0100000000000001ULL, "", true};
  auto S = [&](const char* js) { return EV(db, sd, J(js)); };
  check("crc32 ==1", S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"hash_type\":\"crc32\",\"expected_value_int\":1}"));
  check("crc32 ==0 (falso)", !S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"hash_type\":\"crc32\",\"expected_value_int\":0}"));
  check("murmur3 padrão, valor 4 bytes", S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"expected_value_hex\":\"0x12345678\"}"));
  check("murmur3 size 1 == 0x78", S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"size\":1,\"expected_value_hex\":\"0x78\"}"));
  check("desalinhado ignorado (>=200 acha 250)", S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"Flag_Count\",\"hash_type\":\"crc32\",\"operator\":\">=\",\"expected_value_int\":200}"));
  check("align 1 acha o desalinhado (99)", S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"Flag_Count\",\"hash_type\":\"crc32\",\"align\":1,\"expected_value_int\":99}"));
  check("hash cruzando a borda de 32 KB", S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"Flag_Border\",\"hash_type\":\"crc32\",\"align\":2,\"expected_value_int\":7}"));
  check("flag ausente", !S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"Nope_Flag\",\"hash_type\":\"crc32\",\"operator\":\">=\",\"expected_value_int\":0}"));
  check("hash_type desconhecido", !S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"hash_type\":\"fnv\",\"operator\":\">=\",\"expected_value_int\":0}"));
  check("arquivo ausente", !S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/nada.sav\",\"flag_name\":\"x\",\"operator\":\">=\",\"expected_value_int\":0}"));
  check("valor além do fim", !S("{\"type\":\"save_flag_hash_search\",\"save_file\":\"/sv/game_data.sav\",\"flag_name\":\"IsGet_Paraglider\",\"hash_type\":\"crc32\",\"offset_after_flag\":999999,\"operator\":\">=\",\"expected_value_int\":0}"));
  printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
  return fails;
}
