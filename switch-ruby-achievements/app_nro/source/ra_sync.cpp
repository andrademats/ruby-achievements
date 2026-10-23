#include "ra_sync.hpp"
#include "i18n.hpp"

#include <nlohmann/json.hpp>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data.hpp"
#include "http.hpp"
#include "md5.hpp"
#include "pt_config.h"
#include "pt_log.h"
#include "pt_paths.h"
#include "pt_util.h"

namespace ra {
namespace {

constexpr const char* kSite = "https://retroachievements.org";
constexpr const char* kMedia = "https://media.retroachievements.org/Badge/";
constexpr int64_t kMaxRom = 64ll << 20;  // cartuchos; discos ficam de fora

#define RLOG(...) pt_log_file(PT_DIR "/retroachievements.log", __VA_ARGS__)

std::string lower_ext(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
}

// Campos do JSON que o RetroAchievements às vezes manda como texto.
int64_t num_of(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end()) return 0;
    if (it->is_number()) return it->get<int64_t>();
    if (it->is_string()) return strtoll(it->get<std::string>().c_str(), nullptr, 10);
    return 0;
}
std::string str_of(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// "2023-05-01 12:34:56" (UTC) -> epoch.
int64_t parse_utc(const std::string& s) {
    int y, mo, d, h = 0, mi = 0, se = 0;
    if (sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3 || y < 1970) return 0;
    y -= mo <= 2;
    int64_t era = y / 400, yoe = y - era * 400;
    int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

bool get_json(const std::string& url, nlohmann::json& out, std::string& error) {
    http::Response r;
    if (!http::get(url, {"Accept: application/json"}, r, error, 4u << 20)) return false;
    if (r.status == 401 || r.status == 403) {
        error = trf("Usuário ou chave da Web API do RetroAchievements inválidos (HTTP %d).", (int)r.status);
        return false;
    }
    if (r.status == 429) {
        error = tr("O RetroAchievements limitou as consultas (HTTP 429). Tente de novo em alguns minutos.");
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        error = trf("RetroAchievements respondeu HTTP %d.", (int)r.status);
        return false;
    }
    out = nlohmann::json::parse(r.body.begin(), r.body.end(), nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        error = tr("Resposta inválida do RetroAchievements.");
        return false;
    }
    return true;
}

// Jogo do RetroAchievements pelo hash (a mesma consulta que os emuladores fazem).
bool game_id_for_hash(const std::string& md5, int64_t& id, std::string& error) {
    nlohmann::json j;
    if (!get_json(std::string(kSite) + "/dorequest.php?r=gameid&m=" + md5, j, error)) return false;
    id = num_of(j, "GameID");
    return true;
}

// Ícone colorido da conquista, baixado uma vez (achievements/icons/RA_<badge>.png).
std::string badge_icon(const std::string& badge) {
    if (badge.empty()) return "";
    std::string path = std::string(PT_ACH_ICONS_DIR) + "/RA_" + badge + ".png";
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0) return path;
    http::Response r;
    std::string err;
    if (!http::get(kMedia + badge + ".png", {}, r, err, 1u << 20) || r.status != 200 || r.body.size() < 8 ||
        memcmp(r.body.data(), "\x89PNG", 4) != 0)
        return "";
    return pt_write_file_atomic(path.c_str(), (const char*)r.body.data(), r.body.size()) == 0 ? path : "";
}

}  // namespace

bool rom_hash(const std::string& path, std::string& md5, std::string& why) {
    std::string ext = lower_ext(path);
    static const char* plain[] = {"gb", "gbc", "gba", "md", "gen", "sms", "gg", "pce", "a26", "ngp", "ngc", "ws",
                                  "wsc", "32x", "sg", "col", "vb", "nes", "fds", "sfc", "smc", "z64", "n64", "v64"};
    if (std::find_if(std::begin(plain), std::end(plain), [&](const char* e) { return ext == e; }) == std::end(plain)) {
        why = ext == "zip" || ext == "7z" ? "ROM compactada" : "formato ." + ext + " sem hash suportado";
        return false;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        why = "arquivo não encontrado";
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > kMaxRom) {
        fclose(f);
        why = "tamanho fora do esperado para cartucho";
        return false;
    }
    std::vector<unsigned char> data((size_t)size);
    bool ok = fread(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) {
        why = "falha ao ler o arquivo";
        return false;
    }

    size_t skip = 0;
    if ((ext == "nes" && size > 16 && !memcmp(data.data(), "NES\x1a", 4)) ||
        (ext == "fds" && size > 16 && !memcmp(data.data(), "FDS\x1a", 4)))
        skip = 16;  // cabeçalho iNES/fwNES
    else if ((ext == "sfc" || ext == "smc") && size % 0x2000 == 512)
        skip = 512;  // cabeçalho de copiadora (mesma regra do rcheevos)
    else if ((ext == "z64" || ext == "n64" || ext == "v64") && size >= 4) {
        // O RetroAchievements usa a ordem big-endian (.z64).
        if (data[0] == 0x37 && data[1] == 0x80)
            for (size_t i = 0; i + 1 < data.size(); i += 2) std::swap(data[i], data[i + 1]);
        else if (data[0] == 0x40 && data[1] == 0x12)
            for (size_t i = 0; i + 3 < data.size(); i += 4) {
                std::swap(data[i], data[i + 3]);
                std::swap(data[i + 1], data[i + 2]);
            }
    }
    Md5 h;
    h.update(data.data() + skip, data.size() - skip);
    md5 = h.hex();
    return true;
}

bool sync(const Progress& progress, SyncReport& rep) {
    rep = {};
    PtConfig cfg;
    pt_config_load(&cfg);
    std::string user = cfg.ra_user, key = cfg.ra_key;
    if (user.empty() || key.empty()) {
        rep.error = tr("Configure o usuário e a chave da Web API em + > Configurações globais > Chaves > RetroAchievements.");
        return false;
    }

    std::vector<GameRow> all, roms;
    if (!data::games(0, all)) {
        rep.error = data::last_error();
        return false;
    }
    for (auto& g : all)
        if (g.source == "EMULATOR_ROM" && !g.rom_path.empty() && g.rom_path[0] == '/') roms.push_back(g);
    rep.roms = (int)roms.size();
    RLOG("---- sincronizacao: %d ROMs, conta %s ----", rep.roms, user.c_str());

    for (size_t i = 0; i < roms.size(); i++) {
        const GameRow& g = roms[i];
        progress((int)i, (int)roms.size(), g.name);
        std::string path = "sdmc:" + g.rom_path;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            rep.missing++;
            RLOG("%s: arquivo nao encontrado", g.rom_path.c_str());
            continue;
        }

        std::string md5, why;
        if (!data::rom_hash_lookup(g.rom_path, (int64_t)st.st_size, (int64_t)st.st_mtime, md5)) {
            if (!rom_hash(path, md5, why)) {
                rep.unsupported++;
                RLOG("%s: %s", g.rom_path.c_str(), why.c_str());
                continue;
            }
            data::rom_hash_store(g.rom_path, (int64_t)st.st_size, (int64_t)st.st_mtime, md5);
        }

        int64_t ra_game = 0;
        if (!game_id_for_hash(md5, ra_game, rep.error)) return false;
        if (ra_game <= 0) {
            rep.unknown++;
            RLOG("%s: hash %s sem jogo no RetroAchievements", g.rom_path.c_str(), md5.c_str());
            continue;
        }

        nlohmann::json info;
        std::string url = std::string(kSite) + "/API/API_GetGameInfoAndUserProgress.php?y=" + http::escape(key) +
                          "&u=" + http::escape(user) + "&g=" + std::to_string(ra_game);
        if (!get_json(url, info, rep.error)) return false;
        auto ach = info.find("Achievements");
        std::vector<RaAchievement> list;
        if (ach != info.end() && ach->is_object()) {
            for (auto it = ach->begin(); it != ach->end(); ++it) {
                const nlohmann::json& a = it.value();
                if (!a.is_object()) continue;
                RaAchievement r;
                r.id = num_of(a, "ID");
                r.title = str_of(a, "Title");
                r.description = str_of(a, "Description");
                r.order = (int)num_of(a, "DisplayOrder");
                std::string badge = str_of(a, "BadgeName");
                if (!badge.empty()) r.icon_url = kMedia + badge + ".png";
                std::string earned = str_of(a, "DateEarnedHardcore");
                if (earned.empty()) earned = str_of(a, "DateEarned");
                r.unlocked_at = parse_utc(earned);
                if (r.id <= 0 || r.title.empty()) continue;
                progress((int)i, (int)roms.size(), g.name + " · " + tr("ícones"));
                r.icon_path = badge_icon(badge);
                list.push_back(r);
            }
        }
        rep.matched++;
        int fresh = 0;
        if (!data::store_ra_achievements(g.id, data::main_user_for_game(g.id), list, fresh)) {
            rep.error = data::last_error();
            return false;
        }
        rep.achievements += (int)list.size();
        rep.new_unlocks += fresh;
        RLOG("%s: jogo %lld \"%s\", %zu conquistas, %d desbloqueios novos", g.rom_path.c_str(), (long long)ra_game,
             str_of(info, "Title").c_str(), list.size(), fresh);
    }
    progress((int)roms.size(), (int)roms.size(), "");
    RLOG("---- concluida: %d reconhecidas, %d sem jogo, %d sem suporte, %d ausentes ----", rep.matched, rep.unknown,
         rep.unsupported, rep.missing);
    return true;
}

}  // namespace ra
