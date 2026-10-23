#include "i18n.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <string_view>
#include <unordered_map>

namespace i18n {

namespace {

struct Entry {
    const char* pt;
    const char* en;
};

// Tabela português -> inglês (uma linha por texto).
const Entry kEnglish[] = {
#include "i18n_en.inc"
};

int g_lang = LANG_EN;
int g_date = DATE_MDY;

const std::unordered_map<std::string_view, const char*>& english() {
    static const std::unordered_map<std::string_view, const char*> map = [] {
        std::unordered_map<std::string_view, const char*> m;
        m.reserve(sizeof(kEnglish) / sizeof(kEnglish[0]));
        for (const Entry& e : kEnglish) m.emplace(e.pt, e.en);
        return m;
    }();
    return map;
}

}  // namespace

int language() { return g_lang; }
void set_language(int lang) { g_lang = lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN; }
int date_format() { return g_date; }
void set_date_format(int fmt) { g_date = fmt >= 0 && fmt < DATE_COUNT ? fmt : DATE_MDY; }

const char* tr(const char* pt) {
    if (!pt || g_lang == LANG_PT) return pt;
    auto& m = english();
    auto it = m.find(pt);
    return it == m.end() ? pt : it->second;
}

bool has_translation(const char* pt) { return pt && english().count(pt) > 0; }

std::string trf(const char* pt_fmt, ...) {
    const char* fmt = tr(pt_fmt);
    va_list ap;
    va_start(ap, pt_fmt);
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out;
    if (n > 0) {
        out.resize((size_t)n + 1);
        vsnprintf(&out[0], out.size(), fmt, ap);
        out.resize((size_t)n);
    }
    va_end(ap);
    return out;
}

std::string format_day(int year, int month, int day, int fmt) {
    char buf[24];
    switch (fmt) {
        case DATE_DMY: snprintf(buf, sizeof(buf), "%02d/%02d/%04d", day, month, year); break;
        case DATE_YMD: snprintf(buf, sizeof(buf), "%04d/%02d/%02d", year, month, day); break;
        default: snprintf(buf, sizeof(buf), "%02d/%02d/%04d", month, day, year); break;
    }
    return buf;
}

std::string format_datetime(int year, int month, int day, int hour, int minute, int fmt) {
    char buf[12];
    snprintf(buf, sizeof(buf), " %02d:%02d", hour, minute);
    return format_day(year, month, day, fmt) + buf;
}

}  // namespace i18n
