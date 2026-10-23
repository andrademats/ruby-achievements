#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Parser {
    const char* p;
    const char* end;
    int depth = 0;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool lit(const char* s) {
        size_t n = strlen(s);
        if ((size_t)(end - p) < n || memcmp(p, s, n)) return false;
        p += n;
        return true;
    }
    static void put_utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool string(std::string& o) {
        if (p >= end || *p != '"') return false;
        p++;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { o += c; continue; }
            if (p >= end) return false;
            char e = *p++;
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (end - p < 4) return false;
                    char hex[5] = {p[0], p[1], p[2], p[3], 0};
                    p += 4;
                    put_utf8(o, (unsigned)strtoul(hex, nullptr, 16));  // pares substitutos não são tratados
                    break;
                }
                default: o += e; break;  // \" \\ \/
            }
        }
        if (p >= end) return false;
        p++;
        return true;
    }
    bool value(JVal& v) {
        if (++depth > 32) return false;
        ws();
        if (p >= end) return false;
        bool ok = true;
        if (*p == '{') {
            v.type = JVal::Obj;
            p++;
            ws();
            if (p < end && *p == '}') p++;
            else
                for (;;) {
                    ws();
                    std::pair<std::string, JVal> kv;
                    if (!string(kv.first)) { ok = false; break; }
                    ws();
                    if (p >= end || *p++ != ':' || !value(kv.second)) { ok = false; break; }
                    v.obj.push_back(std::move(kv));
                    ws();
                    if (p < end && *p == ',') { p++; continue; }
                    if (p < end && *p == '}') { p++; break; }
                    ok = false;
                    break;
                }
        } else if (*p == '[') {
            v.type = JVal::Arr;
            p++;
            ws();
            if (p < end && *p == ']') p++;
            else
                for (;;) {
                    JVal item;
                    if (!value(item)) { ok = false; break; }
                    v.arr.push_back(std::move(item));
                    ws();
                    if (p < end && *p == ',') { p++; continue; }
                    if (p < end && *p == ']') { p++; break; }
                    ok = false;
                    break;
                }
        } else if (*p == '"') {
            v.type = JVal::Str;
            ok = string(v.str);
        } else if (lit("true")) {
            v.type = JVal::Bool;
            v.b = true;
        } else if (lit("false")) {
            v.type = JVal::Bool;
        } else if (lit("null")) {
            v.type = JVal::Null;
        } else {
            char* stop = nullptr;
            std::string num(p, (size_t)(end - p) < 32 ? (size_t)(end - p) : 32);
            v.num = strtod(num.c_str(), &stop);
            if (stop == num.c_str()) ok = false;
            else {
                v.type = JVal::Num;
                p += stop - num.c_str();
            }
        }
        depth--;
        return ok;
    }
};

}  // namespace

const JVal* JVal::get(const char* key) const {
    if (type != Obj) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string JVal::get_str(const char* key, const char* def) const {
    const JVal* v = get(key);
    return v && v->type == Str ? v->str : std::string(def);
}

int64_t JVal::get_int(const char* key, int64_t def) const {
    const JVal* v = get(key);
    if (!v) return def;
    if (v->type == Num) return (int64_t)v->num;
    if (v->type == Str && !v->str.empty()) return (int64_t)strtoull(v->str.c_str(), nullptr, 0);  // 0x.. ou decimal
    return def;
}

bool json_parse(const char* text, size_t len, JVal& out, std::string* err) {
    Parser ps{text, text + len};
    out = JVal{};
    bool ok = ps.value(out);
    ps.ws();
    if (ok && ps.p != ps.end) ok = false;
    if (!ok && err) {
        char buf[48];
        snprintf(buf, sizeof(buf), "JSON invalido perto do byte %ld", (long)(ps.p - text));
        *err = buf;
    }
    return ok;
}
