#pragma once
// JSON mínimo (sem exceções) para as definições de conquistas em PT_ACH_DIR.
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    const JVal* get(const char* key) const;  // nullptr se não existe ou não é objeto
    std::string get_str(const char* key, const char* def = "") const;
    int64_t get_int(const char* key, int64_t def = 0) const;  // número ou texto ("0x12" vale)
};

// false = JSON inválido (err recebe a posição).
bool json_parse(const char* text, size_t len, JVal& out, std::string* err = nullptr);
