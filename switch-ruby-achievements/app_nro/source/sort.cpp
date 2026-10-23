#include "sort.hpp"

#include <algorithm>
#include <cctype>

#include "pt_config.h"

namespace ui {

namespace {
int g_sort = -1;  // -1 = ainda não lido do config.ini

// Letra base de um caractere latino de 2 bytes (U+00C0..U+00FF).
char fold_latin1(unsigned char second) {
    static const char* map =
        "aaaaaaaceeeeiiii"  // C3 80..8F  À..Ï
        "dnooooo*ouuuuyts"  // C3 90..9F  Ð..ß
        "aaaaaaaceeeeiiii"  // C3 A0..AF  à..ï
        "dnooooo/ouuuuyty"; // C3 B0..BF  ð..ÿ
    return second >= 0x80 && second <= 0xBF ? map[second - 0x80] : 0;
}
}  // namespace

std::string sort_key(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == 0xC3 && i + 1 < name.size()) {
            char f = fold_latin1((unsigned char)name[i + 1]);
            if (f) {
                out.push_back(f);
                i++;
                continue;
            }
        }
        out.push_back(c < 0x80 ? (char)std::tolower(c) : (char)c);
    }
    return out;
}

int sort_mode() {
    if (g_sort < 0) {
        PtConfig c;
        pt_config_load(&c);
        g_sort = c.app_sort;
    }
    return g_sort;
}

void set_sort_mode(int mode) {
    if (mode < 0 || mode >= SORT_COUNT) mode = SORT_RECENT;
    if (mode == sort_mode()) return;
    g_sort = mode;
    PtConfig c;
    pt_config_load(&c);
    c.app_sort = mode;
    pt_config_save_app(&c);
}

void sort_games(std::vector<GameRow>& games, const std::map<int64_t, std::pair<int, int>>& ach, int mode) {
    struct Item {
        const GameRow* g;
        std::string key;
        bool has;
        int64_t value, num, den;  // valor do modo; % = num/den
    };
    std::vector<Item> items;
    items.reserve(games.size());
    for (auto& g : games) {
        Item it{&g, sort_key(g.name), true, 0, 0, 1};
        if (mode == SORT_RECENT) {
            it.value = g.last_played;
            it.has = g.last_played > 0;
        } else if (mode == SORT_TIME) {
            it.value = g.total;
            it.has = g.total > 0;
        } else if (mode == SORT_ACHIEVEMENTS) {
            auto f = ach.find(g.id);
            it.has = f != ach.end() && f->second.second > 0;
            if (it.has) {
                it.num = f->second.first;
                it.den = f->second.second;
            }
        }
        items.push_back(std::move(it));
    }
    auto alpha = [](const Item& a, const Item& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.g->id < b.g->id;
    };
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        if (mode == SORT_AZ) return alpha(a, b);
        if (a.has != b.has) return a.has;  // com dados primeiro
        if (!a.has) return alpha(a, b);
        if (mode == SORT_ACHIEVEMENTS) {
            int64_t l = a.num * b.den, r = b.num * a.den;  // a% vs b% sem ponto flutuante
            if (l != r) return l > r;
        } else if (a.value != b.value) {
            return a.value > b.value;
        }
        return alpha(a, b);
    });
    std::vector<GameRow> out;
    out.reserve(games.size());
    for (auto& it : items) out.push_back(*it.g);
    games.swap(out);
}

}  // namespace ui
