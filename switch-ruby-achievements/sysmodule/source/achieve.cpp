#include "achieve.h"
#include "mjson.h"
#include "pt_pack.h"

#include "pt_log.h"
#include "pt_paths.h"
#include "pt_util.h"
#include "ram.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Chave da definição: Title ID, ou o nome da ROM sem pasta e extensão.
std::string def_key(const AchieveTarget& t) {
    if (t.rom_path && t.rom_path[0]) {
        std::string p = t.rom_path;
        size_t slash = p.find_last_of('/');
        if (slash != std::string::npos) p = p.substr(slash + 1);
        size_t dot = p.find_last_of('.');
        if (dot != std::string::npos && dot > 0) p = p.substr(0, dot);
        return p;
    }
    char hex[17];
    snprintf(hex, sizeof(hex), "%016lX", t.tid);
    return hex;
}

// Pacote ativo da chave (pt_pack_active); `file` recebe o nome dele.
bool load_defs(const std::string& key, JVal& root, std::string& file) {
    file = pt_pack_active(key);
    if (file.empty()) return false;
    std::string err;
    if (pt_pack_load(file, root, &err)) return true;
    // Uma linha por arquivo com erro (a checagem se repete a cada 30 s).
    static std::string last_bad;
    if (last_bad != file) {
        last_bad = file;
        pt_log("conquistas: %s/%s ignorado (%s)", PT_ACH_DIR, file.c_str(), err.c_str());
    }
    return false;
}

bool parse_uid(const char* s, AccountUid* uid) {
    if (!s || strlen(s) != 32) return false;
    char hi[17], lo[17];
    memcpy(hi, s, 16);
    hi[16] = 0;
    memcpy(lo, s + 16, 16);
    lo[16] = 0;
    uid->uid[1] = strtoull(hi, NULL, 16);  // formato de pt_uid_to_string
    uid->uid[0] = strtoull(lo, NULL, 16);
    return accountUidIsValid(uid);
}

bool user_uid(sqlite3* db, int64_t user_id, AccountUid* uid) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT account_uid FROM users WHERE id=?", -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, user_id);
    bool ok = sqlite3_step(st) == SQLITE_ROW && parse_uid((const char*)sqlite3_column_text(st, 0), uid);
    sqlite3_finalize(st);
    return ok;
}

// --- Arquivos de save --------------------------------------------------------
// Toda condição de save é avaliada sobre um arquivo no cartão SD: o próprio
// arquivo (ports, emuladores) ou uma cópia espelho do save do console em
// PT_ACH_CACHE_DIR. O save do console é montado só para leitura e só pelo tempo
// de localizar e copiar o arquivo, com o jogo aberto ou fechado.

constexpr size_t kChunk = 32 * 1024;

// Leitor por trechos de um arquivo no SD: o heap do sysmodule é pequeno e
// saves podem ter vários MB.
class SaveFile {
public:
    ~SaveFile() {
        if (m_fp) fclose(m_fp);
    }

    bool open(const std::string& path) {
        m_fp = fopen(path.c_str(), "rb");
        if (!m_fp) return false;
        if (fseek(m_fp, 0, SEEK_END) == 0) m_size = ftell(m_fp);
        return m_size >= 0;
    }

    int64_t size() const { return m_size; }

    // Lê até `n` bytes em `offset`; retorna quantos leu (-1 = erro).
    long read_at(int64_t offset, void* buf, size_t n) {
        if (offset < 0 || offset >= m_size) return 0;
        if (fseek(m_fp, (long)offset, SEEK_SET) != 0) return -1;
        return (long)fread(buf, 1, n, m_fp);
    }

private:
    FILE* m_fp = NULL;
    int64_t m_size = -1;
};

uint32_t crc32_update(uint32_t c, const void* data, size_t n) {
    const uint8_t* d = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c;
}

// CRC32 (IEEE, o mesmo do zlib), usado nas flags de GameData do BOTW.
uint32_t crc32_ieee(const std::string& s) { return ~crc32_update(0xFFFFFFFFu, s.data(), s.size()); }

// Identifica o conteúdo de um save sem guardá-lo.
struct Fingerprint {
    int64_t size = -1;
    u64 mtime = 0;      // 0 = o FS não informa a data
    uint32_t crc = 0;   // só para saves do console
    bool operator==(const Fingerprint& o) const { return size == o.size && mtime == o.mtime && crc == o.crc; }
};

// Conteúdo já avaliado por chave "TID|perfil|save_file": se o save não mudou,
// as condições dele não precisam ser lidas de novo.
std::vector<std::pair<std::string, Fingerprint>> g_evaluated;

// Lido nesta transação, aplicado em g_evaluated só depois do COMMIT.
std::vector<std::pair<std::string, Fingerprint>> g_pending;

// Save do console recusado com o jogo aberto (TID e perfil): não remonta até o
// jogo fechar (suspenso na Home ele continua com o save preso).
u64 g_locked_tid = 0;
int64_t g_locked_user = 0;

const Fingerprint* evaluated(const std::string& key) {
    for (auto& e : g_evaluated)
        if (e.first == key) return &e.second;
    return nullptr;
}

void set_evaluated(const std::string& key, const Fingerprint& fp) {
    for (auto& e : g_evaluated)
        if (e.first == key) {
            e.second = fp;
            return;
        }
    if (g_evaluated.size() >= 64) g_evaluated.erase(g_evaluated.begin());  // limite de memória
    g_evaluated.push_back({key, fp});
}

bool is_sd_path(const std::string& file) { return file.rfind("sdmc:", 0) == 0 || file.rfind("/", 0) == 0; }

std::string join_path(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

struct SaveCandidate {
    std::string path;
    u64 mtime = 0;
    bool has_time = false;
};

// Arquivos chamados `name` em `dir` e subpastas (até 4 níveis).
void find_save_files(FsFileSystem* fs, const std::string& dir, const std::string& name, int depth,
                     std::vector<SaveCandidate>& out) {
    if (depth > 4 || out.size() >= 64) return;
    FsDir d;
    if (R_FAILED(fsFsOpenDirectory(fs, dir.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) return;
    std::vector<FsDirectoryEntry> entries(8);
    std::vector<std::string> subdirs;
    for (;;) {
        s64 n = 0;
        if (R_FAILED(fsDirRead(&d, &n, entries.size(), entries.data())) || n <= 0) break;
        for (s64 k = 0; k < n; k++) {
            const FsDirectoryEntry& e = entries[k];
            std::string path = join_path(dir, e.name);
            if (e.type == FsDirEntryType_Dir) {
                if (subdirs.size() < 32) subdirs.push_back(path);
            } else if (name == e.name) {
                SaveCandidate c;
                c.path = path;
                FsTimeStampRaw ts;
                if (R_SUCCEEDED(fsFsGetFileTimeStampRaw(fs, path.c_str(), &ts)) && ts.is_valid) {
                    c.mtime = ts.modified;
                    c.has_time = true;
                }
                out.push_back(c);
            }
        }
    }
    fsDirClose(&d);  // fechado antes de descer: limite de diretórios abertos
    for (auto& sub : subdirs) find_save_files(fs, sub, name, depth + 1, out);
}

}  // namespace

std::string resolve_active_save_path(FsFileSystem* fs, const std::string& save_root, const std::string& target) {
    std::string root = save_root.empty() ? "/" : save_root;
    std::string fallback = join_path(root, target);
    if (target.empty() || target.find('/') != std::string::npos) return fallback;  // caminho explícito
    std::vector<SaveCandidate> found;
    find_save_files(fs, root, target, 0, found);
    if (found.empty()) return fallback;
    std::sort(found.begin(), found.end(), [](const SaveCandidate& a, const SaveCandidate& b) { return a.path < b.path; });
    const SaveCandidate* best = nullptr;
    for (auto& c : found)
        if (c.has_time && (!best || c.mtime > best->mtime)) best = &c;
    if (best) return best->path;
    // Sem datas no FS: o da raiz, se existir; senão o primeiro em ordem alfabética.
    for (auto& c : found)
        if (c.path == fallback) return fallback;
    return found.front().path;
}

namespace {

// Contexto de uma avaliação: arquivos já resolvidos e buscas de hash em lote.
struct EvalCtx {
    sqlite3* db;
    const AchieveTarget& t;
    bool force;  // definições mudaram: avalia mesmo com o save igual

    struct File {
        std::string json_name;  // save_file como está no JSON
        bool ok = false;
        bool changed = false;   // conteúdo diferente do já avaliado
        std::string local;      // arquivo no SD a ler
        std::string key;
        Fingerprint fp;
    };
    std::vector<File> files;

    // Hashes pedidos por (save_file, align) e as posições achadas.
    struct HashGroup {
        std::string file;
        int64_t align;
        std::vector<uint32_t> wanted;
        bool scanned = false;
        std::vector<std::pair<uint32_t, int64_t>> found;
    };
    std::vector<HashGroup> hashes;

    // Memória do jogo: conexão ao dmnt:cht nesta avaliação e as janelas de
    // busca já resolvidas (as cadeias de ponteiros são relidas a cada avaliação).
    bool ram_tried = false;
    bool ram_ok = false;
    RamProcess proc{};
    struct RamWindow {
        std::string key;
        bool ok;
        u64 base;
        int64_t size;
        std::string why;  // motivo da falha, para o log
    };
    std::vector<RamWindow> windows;
    std::string cur_id;  // conquista em avaliação (log da RAM)
    std::string build_id;  // "build_id" do pacote: as condições de RAM só valem nesse executável
};

std::string state_key(const AchieveTarget& t, const std::string& file) {
    char head[48];
    snprintf(head, sizeof(head), "%016lX|%lld|", t.tid, (long long)t.user_id);
    return head + file;
}

std::string mirror_path(const AchieveTarget& t, const std::string& resolved) {
    std::string name = resolved;
    for (char& ch : name)
        if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-') ch = '_';
    char head[64];
    snprintf(head, sizeof(head), "%s/%016lX_%lld", PT_ACH_CACHE_DIR, t.tid, (long long)t.user_id);
    return head + name;
}

// Lê o arquivo aberto no save do console inteiro, por trechos; com `out`, grava
// a cópia (atômica). Retorna o CRC32 (crc_ok=false em erro).
uint32_t stream_save(FsFile* f, int64_t size, const std::string* out, bool* crc_ok) {
    *crc_ok = false;
    std::vector<char> buf(kChunk);
    FILE* w = NULL;
    std::string tmp;
    if (out) {
        tmp = *out + ".tmp";
        if (!(w = fopen(tmp.c_str(), "wb"))) return 0;
    }
    uint32_t c = 0xFFFFFFFFu;
    int64_t off = 0;
    bool ok = true;
    while (ok && off < size) {
        u64 got = 0;
        ok = R_SUCCEEDED(fsFileRead(f, off, buf.data(), buf.size(), FsReadOption_None, &got)) && got > 0;
        if (!ok) break;
        c = crc32_update(c, buf.data(), (size_t)got);
        if (w) ok = fwrite(buf.data(), 1, (size_t)got, w) == got;
        off += (int64_t)got;
    }
    if (w) {
        ok = fclose(w) == 0 && ok;
        if (ok) {
            unlink(out->c_str());  // o FS do Switch não sobrescreve no rename
            ok = rename(tmp.c_str(), out->c_str()) == 0;
        }
        if (!ok) unlink(tmp.c_str());
    }
    *crc_ok = ok;
    return ~c;
}

// Save do console: resolve o arquivo ativo, compara com o já avaliado e, se
// mudou, atualiza a cópia espelho no SD.
bool mirror_console_save(EvalCtx& c, EvalCtx::File& out) {
    const AchieveTarget& t = c.t;
    if (t.tid == 0 || (t.rom_path && t.rom_path[0])) return false;
    bool locked = g_locked_tid == t.tid && g_locked_user == t.user_id;
    if (t.running && locked) return false;  // espera o jogo fechar
    AccountUid uid;
    if (!user_uid(c.db, t.user_id, &uid)) return false;
    FsSaveDataAttribute attr;
    memset(&attr, 0, sizeof(attr));
    attr.application_id = t.tid;
    attr.uid = uid;
    attr.save_data_type = FsSaveDataType_Account;
    FsFileSystem fs;
    Result rc = fsOpenReadOnlySaveDataFileSystem(&fs, FsSaveDataSpaceId_User, &attr);
    if (R_FAILED(rc)) {
        if (t.running) {
            if (!locked)
                pt_log("conquistas: save de %016lX em uso pelo jogo (rc=0x%X); avaliado ao fechar o jogo", t.tid, rc);
            g_locked_tid = t.tid;
            g_locked_user = t.user_id;
        } else {
            pt_log("conquistas: save de %016lX indisponivel com o jogo fechado (rc=0x%X)", t.tid, rc);
        }
        return false;
    }
    bool ok = false;
    std::string path = resolve_active_save_path(&fs, "/", out.json_name);
    FsFile f;
    if (R_SUCCEEDED(fsFsOpenFile(&fs, path.c_str(), FsOpenMode_Read, &f))) {
        s64 size = 0;
        Fingerprint fp;
        FsTimeStampRaw ts;
        if (R_SUCCEEDED(fsFsGetFileTimeStampRaw(&fs, path.c_str(), &ts)) && ts.is_valid) fp.mtime = ts.modified;
        if (R_SUCCEEDED(fsFileGetSize(&f, &size))) {
            fp.size = size;
            out.local = mirror_path(t, path);
            struct stat st;
            bool have_mirror = stat(out.local.c_str(), &st) == 0 && st.st_size == size;
            const Fingerprint* prev = evaluated(out.key);
            if (have_mirror && prev && fp.mtime != 0 && prev->mtime == fp.mtime && prev->size == fp.size) {
                fp.crc = prev->crc;  // data e tamanho iguais: nem lê o conteúdo
                ok = true;
            } else {
                bool crc_ok;
                fp.crc = stream_save(&f, size, nullptr, &crc_ok);
                ok = crc_ok;
                if (ok && !(have_mirror && prev && *prev == fp)) stream_save(&f, size, &out.local, &ok);
            }
            out.fp = fp;
            const Fingerprint* now_prev = evaluated(out.key);
            out.changed = ok && (!now_prev || !(*now_prev == fp));
        }
        fsFileClose(&f);
    }
    fsFsClose(&fs);
    return ok;
}

// Arquivo a ler para `save_file` (resolvido uma vez por avaliação).
EvalCtx::File* resolve_file(EvalCtx& c, const std::string& save_file) {
    for (auto& f : c.files)
        if (f.json_name == save_file) return f.ok ? &f : nullptr;
    c.files.push_back(EvalCtx::File());
    EvalCtx::File& f = c.files.back();
    f.json_name = save_file;
    f.key = state_key(c.t, save_file);
    if (is_sd_path(save_file)) {
        f.local = save_file.rfind("sdmc:", 0) == 0 ? save_file : "sdmc:" + save_file;
        struct stat st;
        if (stat(f.local.c_str(), &st) == 0) {
            f.fp.size = st.st_size;
            f.fp.mtime = (u64)st.st_mtime;
            const Fingerprint* prev = evaluated(f.key);
            f.changed = !prev || !(*prev == f.fp);
            f.ok = true;
        }
    } else {
        f.ok = mirror_console_save(c, f);
    }
    return f.ok ? &f : nullptr;
}

// Arquivo da condição, ou nullptr se não dá para ler ou não mudou.
EvalCtx::File* file_to_eval(EvalCtx& c, const JVal& a) {
    std::string file = a.get_str("save_file");
    if (file.empty()) return nullptr;
    EvalCtx::File* f = resolve_file(c, file);
    return f && (f->changed || c.force) ? f : nullptr;
}

// Valor de `size` bytes (1-8, little-endian) em `offset`.
bool read_le(SaveFile& f, int64_t offset, int size, uint64_t* out) {
    uint8_t buf[8] = {0};
    if (f.read_at(offset, buf, (size_t)size) != size) return false;
    uint64_t v = 0;
    for (int i = 0; i < size; i++) v |= (uint64_t)buf[i] << (8 * i);
    *out = v;
    return true;
}

// Operador da condição ("op" ou "operator").
std::string cond_op(const JVal& a) {
    std::string op = a.get_str("operator");
    return op.empty() ? a.get_str("op", "==") : op;
}

// Valor esperado: "expected_value_int" ou "expected_value_hex".
int64_t cond_expected(const JVal& a) {
    return a.get("expected_value_int") ? a.get_int("expected_value_int", 0) : a.get_int("expected_value_hex", 0);
}

// bit_check e bit_set: todos os bits da máscara (o valor esperado) ligados.
bool compare_u(uint64_t v, const std::string& op, uint64_t expected) {
    if (op == "==") return v == expected;
    if (op == "!=") return v != expected;
    if (op == ">=") return v >= expected;
    if (op == "<=") return v <= expected;
    if (op == ">") return v > expected;
    if (op == "<") return v < expected;
    if (op == "bit_check" || op == "bit_set") return (v & expected) == expected;
    return false;  // operador desconhecido: nunca desbloqueia
}

bool compare_i(int64_t v, const std::string& op, int64_t expected) {
    if (op == "==") return v == expected;
    if (op == "!=") return v != expected;
    if (op == ">=") return v >= expected;
    if (op == "<=") return v <= expected;
    if (op == ">") return v > expected;
    if (op == "<") return v < expected;
    if (op == "bit_check" || op == "bit_set") return (v & expected) == expected;
    return false;
}

bool compare_f(double v, const std::string& op, double expected) {
    if (op == "==") return v == expected;
    if (op == "!=") return v != expected;
    if (op == ">=") return v >= expected;
    if (op == "<=") return v <= expected;
    if (op == ">") return v > expected;
    if (op == "<") return v < expected;
    return false;  // bit_set não vale para float
}

// Lê o valor `size` bytes em `pos` e compara com a condição.
bool eval_value_at(SaveFile& f, const JVal& a, int64_t pos, int size) {
    uint64_t v = 0;
    if (pos < 0 || pos + size > f.size() || !read_le(f, pos, size, &v)) return false;
    return compare_u(v, cond_op(a), (uint64_t)cond_expected(a));
}

bool eval_save_binary_offset(EvalCtx& c, const JVal& a) {
    int size = (int)a.get_int("size", 1);
    if (size < 1 || size > 8) return false;
    EvalCtx::File* file = file_to_eval(c, a);
    SaveFile f;
    return file && f.open(file->local) && eval_value_at(f, a, a.get_int("offset_hex", 0), size);
}

// Primeira ocorrência de `pat` no arquivo cuja posição é múltipla de `align`
// (-1 = não achou). Busca em blocos de 32 KB; os últimos len-1 bytes de cada
// bloco são repetidos no próximo para achar padrões que cruzam a borda.
int64_t find_pattern(SaveFile& f, const std::string& pat, int64_t align) {
    if (pat.empty() || align < 1) return -1;
    const size_t keep = pat.size() - 1;
    std::vector<char> buf(kChunk + keep);
    size_t have = 0;   // bytes válidos no início de buf (sobra do bloco anterior)
    int64_t base = 0;  // posição no arquivo de buf[0]
    for (;;) {
        long n = f.read_at(base + (int64_t)have, buf.data() + have, kChunk);
        if (n <= 0) return -1;
        size_t len = have + (size_t)n;
        auto end = buf.begin() + len;
        for (auto it = std::search(buf.begin(), end, pat.begin(), pat.end()); it != end;
             it = std::search(it + 1, end, pat.begin(), pat.end())) {
            int64_t pos = base + (it - buf.begin());
            if (pos % align == 0) return pos;
        }
        size_t carry = len < keep ? len : keep;
        memmove(buf.data(), buf.data() + len - carry, carry);
        base += (int64_t)(len - carry);
        have = carry;
    }
}

// Procura `flag_name` no save (primeira ocorrência) e lê o valor que fica
// `offset_after_flag` bytes depois do fim do nome.
bool eval_save_flag_search(EvalCtx& c, const JVal& a) {
    std::string flag = a.get_str("flag_name");
    int size = (int)a.get_int("size", 1);
    int64_t after = a.get_int("offset_after_flag", 4);
    if (flag.empty() || flag.size() > 256 || size < 1 || size > 8 || after < 0) return false;
    EvalCtx::File* file = file_to_eval(c, a);
    SaveFile f;
    if (!file || !f.open(file->local)) return false;
    int64_t found = find_pattern(f, flag, 1);
    return found >= 0 && eval_value_at(f, a, found + (int64_t)flag.size() + after, size);
}

// MurmurHash3 x86_32 (semente 0), usado nas flags de GameData do TotK.
uint32_t murmur3_32(const std::string& s) {
    const uint8_t* d = (const uint8_t*)s.data();
    size_t n = s.size(), blocks = n / 4;
    uint32_t h = 0;
    const uint32_t c1 = 0xcc9e2d51, c2 = 0x1b873593;
    auto rotl = [](uint32_t x, int r) { return (x << r) | (x >> (32 - r)); };
    for (size_t i = 0; i < blocks; i++) {
        uint32_t k = (uint32_t)d[i * 4] | (uint32_t)d[i * 4 + 1] << 8 | (uint32_t)d[i * 4 + 2] << 16 |
                     (uint32_t)d[i * 4 + 3] << 24;
        k *= c1;
        k = rotl(k, 15);
        k *= c2;
        h ^= k;
        h = rotl(h, 13);
        h = h * 5 + 0xe6546b64;
    }
    const uint8_t* tail = d + blocks * 4;
    uint32_t k = 0;
    switch (n & 3) {
        case 3: k ^= (uint32_t)tail[2] << 16; [[fallthrough]];
        case 2: k ^= (uint32_t)tail[1] << 8; [[fallthrough]];
        case 1:
            k ^= tail[0];
            k *= c1;
            k = rotl(k, 15);
            k *= c2;
            h ^= k;
    }
    h ^= (uint32_t)n;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

std::string ram_window_key(const JVal& a);

// Parâmetros de uma busca por hash (save_flag_hash_search, ram_flag_search):
// o hash ("flag_hash" pronto, ou o de "flag_name" pelo "hash_type": murmur3,
// padrão, ou crc32), o alinhamento e o grupo da busca em lote (o save_file ou
// a janela de memória). false = condição inválida.
bool hash_params(const JVal& a, uint32_t* h, int64_t* align, std::string* group) {
    *align = a.get_int("align", 4);
    *group = a.get_str("type") == "ram_flag_search" ? ram_window_key(a) : a.get_str("save_file");
    if (*align < 1 || group->empty()) return false;
    if (a.get("flag_hash")) {
        *h = (uint32_t)a.get_int("flag_hash", 0);
        return true;
    }
    std::string flag = a.get_str("flag_name"), kind = a.get_str("hash_type", "murmur3");
    if (flag.empty()) return false;
    if (kind == "murmur3") *h = murmur3_32(flag);
    else if (kind == "crc32") *h = crc32_ieee(flag);
    else return false;  // tipo de hash desconhecido: nunca desbloqueia
    return true;
}

// Registra o hash para a busca em lote (uma leitura do save, ou da janela de
// memória, para todas as flags).
void want_hash(EvalCtx& c, const JVal& a) {
    uint32_t h;
    int64_t align;
    std::string group;
    if (!hash_params(a, &h, &align, &group)) return;
    for (auto& g : c.hashes)
        if (g.file == group && g.align == align) {
            g.wanted.push_back(h);
            return;
        }
    c.hashes.push_back({group, align, {h}, false, {}});
}

// Uma passada no arquivo (ou na janela de memória) achando a primeira posição
// alinhada de cada hash.
template <class Reader>
void scan_hashes(Reader& f, EvalCtx::HashGroup& g) {
    std::sort(g.wanted.begin(), g.wanted.end());
    g.wanted.erase(std::unique(g.wanted.begin(), g.wanted.end()), g.wanted.end());
    std::vector<uint8_t> buf(kChunk + 3);
    size_t have = 0;
    int64_t base = 0;
    while (g.found.size() < g.wanted.size()) {
        long n = f.read_at(base + (int64_t)have, buf.data() + have, kChunk);
        if (n <= 0) break;
        size_t len = have + (size_t)n;
        int64_t first = (base + g.align - 1) / g.align * g.align;
        for (int64_t pos = first; pos + 4 <= base + (int64_t)len; pos += g.align) {
            const uint8_t* p = buf.data() + (pos - base);
            uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
            if (!std::binary_search(g.wanted.begin(), g.wanted.end(), v)) continue;
            bool seen = false;
            for (auto& e : g.found)
                if (e.first == v) seen = true;
            if (!seen) g.found.push_back({v, pos});
        }
        size_t carry = len < 3 ? len : 3;
        memmove(buf.data(), buf.data() + len - carry, carry);
        base += (int64_t)(len - carry);
        have = carry;
    }
}

// Procura o hash de `flag_name` (4 bytes little-endian, em posição múltipla de
// `align`, padrão 4) e lê o valor `offset_after_flag` bytes depois do início
// do hash (padrão 4: logo depois dele, como nas entradas hash+valor).
// Posição do hash `h` no grupo (a busca em lote roda na primeira consulta).
template <class Reader>
const int64_t* find_hash(EvalCtx& c, Reader& f, const std::string& group, int64_t align, uint32_t h) {
    for (auto& g : c.hashes) {
        if (g.file != group || g.align != align) continue;
        if (!g.scanned) {
            scan_hashes(f, g);
            g.scanned = true;
        }
        for (auto& e : g.found)
            if (e.first == h) return &e.second;
        return nullptr;
    }
    return nullptr;
}

bool eval_save_flag_hash_search(EvalCtx& c, const JVal& a) {
    uint32_t h;
    int64_t align;
    std::string group;
    int size = (int)a.get_int("size", 4);
    int64_t after = a.get_int("offset_after_flag", 4);
    if (!hash_params(a, &h, &align, &group) || size < 1 || size > 8 || after < 0) return false;
    EvalCtx::File* file = file_to_eval(c, a);
    SaveFile f;
    if (!file || !f.open(file->local)) return false;
    const int64_t* pos = find_hash(c, f, group, align, h);
    return pos && eval_value_at(f, a, *pos + after, size);
}

// --- Memória do jogo (ram_flag_search) ----------------------------------------
// Lida pelo dmnt:cht com o jogo aberto: sem FS, sem cartão SD, sem 0xE02.
// A assinatura é procurada uma vez por processo no módulo principal (supera o
// ASLR); a cadeia de ponteiros e a janela são relidas a cada avaliação.

constexpr int64_t kRamWindowDefault = 0x10000;
constexpr int64_t kRamWindowMax = 0x400000;
constexpr size_t kMaxSignature = 64;
constexpr size_t kMaxPointerChain = 8;

// Assinaturas já procuradas no processo atual: endereço do início, ou 0 se
// não achou ou se apareceu mais de uma vez (ambígua).
struct RamState {
    u64 pid = 0;
    u64 failed_tid = 0;  // dmnt:cht indisponível para este jogo (log uma vez)
    std::vector<std::pair<std::string, u64>> anchors;
    // Último resultado registrado por conquista+hash: o log só ganha uma linha
    // quando o resultado muda (valor novo, ponteiro que passou a valer...).
    std::vector<std::pair<std::string, std::string>> said;
    u64 build_warned_pid = 0;  // build id diferente do pacote já registrado neste processo
};
RamState g_ram;

// "main+0x..." dentro do módulo principal; senão o endereço absoluto.
std::string ram_addr(const EvalCtx& c, u64 addr) {
    char b[40];
    if (addr >= c.proc.main_base && addr < c.proc.main_base + c.proc.main_size)
        snprintf(b, sizeof(b), "main+0x%lX", addr - c.proc.main_base);
    else
        snprintf(b, sizeof(b), "0x%lX", addr);
    return b;
}

// Linha "ram: <conquista>: ..." no log, só quando o resultado muda.
void ram_note(EvalCtx& c, uint32_t h, const std::string& msg) {
    char k[16];
    snprintf(k, sizeof(k), "|%08X", h);
    std::string key = c.cur_id + k;
    for (auto& e : g_ram.said)
        if (e.first == key) {
            if (e.second == msg) return;
            e.second = msg;
            pt_log("ram: %s: %s", c.cur_id.c_str(), msg.c_str());
            return;
        }
    if (g_ram.said.size() >= 256) g_ram.said.erase(g_ram.said.begin());
    g_ram.said.push_back({key, msg});
    pt_log("ram: %s: %s", c.cur_id.c_str(), msg.c_str());
}

// "FD 7B C1 A9 ?? 4F" (espaços opcionais, ?? = qualquer byte). Normaliza em
// `key` (maiúsculas, sem espaços). false = inválida.
bool parse_signature(const std::string& text, std::vector<uint8_t>& sig, std::vector<uint8_t>& mask,
                     std::string& key) {
    std::string t;
    for (char ch : text)
        if (!isspace((unsigned char)ch)) t += (char)toupper((unsigned char)ch);
    if (t.empty() || t.size() % 2 || t.size() / 2 > kMaxSignature) return false;
    for (size_t i = 0; i < t.size(); i += 2) {
        std::string b = t.substr(i, 2);
        if (b == "??") {
            sig.push_back(0);
            mask.push_back(0);
            continue;
        }
        if (!isxdigit((unsigned char)b[0]) || !isxdigit((unsigned char)b[1])) return false;
        sig.push_back((uint8_t)strtoul(b.c_str(), nullptr, 16));
        mask.push_back(0xFF);
    }
    if (std::find(mask.begin(), mask.end(), 0xFF) == mask.end()) return false;  // só curingas
    key = t;
    return true;
}

// Ocorrências da assinatura no módulo principal (para na 2ª); `at` = a primeira.
int scan_signature(const RamProcess& p, const std::vector<uint8_t>& sig, const std::vector<uint8_t>& mask, u64* at) {
    const size_t keep = sig.size() - 1;
    std::vector<uint8_t> buf(kChunk + keep);
    const u64 end = p.main_base + p.main_size;
    u64 base = p.main_base;  // endereço de buf[0]
    size_t have = 0;
    int hits = 0;
    while (base + have < end && hits < 2) {
        size_t want = (size_t)std::min<u64>(kChunk, end - (base + have));
        if (!ram_read(base + have, buf.data() + have, want)) {  // trecho sem leitura: pula
            base += have + want;
            have = 0;
            continue;
        }
        size_t len = have + want;
        for (size_t i = 0; i + sig.size() <= len && hits < 2; i++) {
            size_t k = 0;
            while (k < sig.size() && (buf[i + k] & mask[k]) == sig[k]) k++;
            if (k == sig.size() && hits++ == 0) *at = base + i;
        }
        size_t carry = len < keep ? len : keep;
        memmove(buf.data(), buf.data() + len - carry, carry);
        base += len - carry;
        have = carry;
    }
    return hits;
}

// Conecta ao processo do jogo (uma vez por avaliação). Processo novo: esquece
// as assinaturas do anterior.
bool ram_process(EvalCtx& c) {
    if (c.ram_tried) return c.ram_ok;
    c.ram_tried = true;
    if (!c.t.running || c.t.tid == 0) return false;
    c.ram_ok = ram_open(&c.proc) && c.proc.tid == c.t.tid;
    if (!c.ram_ok) {
        if (g_ram.failed_tid != c.t.tid)
            pt_log("ram: memoria de %016lX indisponivel (dmnt:cht do Atmosphere ausente ou ocupado)", c.t.tid);
        g_ram.failed_tid = c.t.tid;
        return false;
    }
    g_ram.failed_tid = 0;
    if (c.proc.pid != g_ram.pid) {
        g_ram.pid = c.proc.pid;
        g_ram.anchors.clear();
        g_ram.said.clear();
        pt_log("ram: memoria de %016lX via dmnt:cht (modulo principal 0x%lX, %lu KB, build id %.32s)", c.t.tid,
               c.proc.main_base, (unsigned long)(c.proc.main_size / 1024), c.proc.build_id);
    }
    // Trava de versão: offsets de RAM só valem no executável para o qual foram achados.
    if (!c.build_id.empty() && strncasecmp(c.proc.build_id, c.build_id.c_str(), c.build_id.size()) != 0) {
        if (g_ram.build_warned_pid != c.proc.pid)
            pt_log("ram: build id do jogo %.32s difere do pacote (%s); conquistas de RAM bloqueadas nesta versao",
                   c.proc.build_id, c.build_id.c_str());
        g_ram.build_warned_pid = c.proc.pid;
        c.ram_ok = false;
        return false;
    }
    return true;
}

// Endereço de partida: o início da assinatura ou, sem assinatura, o início do
// módulo principal (o "main+X" dos cheats).
bool ram_anchor(EvalCtx& c, const std::string& text, u64* out) {
    if (text.empty()) {
        *out = c.proc.main_base;
        return true;
    }
    std::vector<uint8_t> sig, mask;
    std::string key;
    if (!parse_signature(text, sig, mask, key)) return false;
    for (auto& e : g_ram.anchors)
        if (e.first == key) {
            *out = e.second;
            return e.second != 0;
        }
    u64 at = 0;
    int hits = scan_signature(c.proc, sig, mask, &at);
    if (hits == 1)
        pt_log("ram: assinatura %s achada em main+0x%lX", key.c_str(), at - c.proc.main_base);
    else
        pt_log("ram: assinatura %s %s; as conquistas dela ficam bloqueadas nesta sessao", key.c_str(),
               hits ? "aparece mais de uma vez (ambigua)" : "nao encontrada");
    g_ram.anchors.push_back({key, hits == 1 ? at : 0});
    *out = hits == 1 ? at : 0;
    return hits == 1;
}

// Inteiro de um elemento JSON: número ou texto ("0x10", "-8"). Texto que não é
// número inteiro ("PREENCHER") é inválido.
bool json_i64(const JVal& v, int64_t* out) {
    if (v.type == JVal::Num) {
        *out = (int64_t)v.num;
        return true;
    }
    if (v.type != JVal::Str || v.str.empty()) return false;
    char* end = nullptr;
    *out = (int64_t)strtoll(v.str.c_str(), &end, 0);
    return end && *end == 0;
}

int64_t ram_window_size(const JVal& a) { return a.get_int("search_size", kRamWindowDefault); }

// Identifica a janela (assinatura + cadeia + tamanho) sem ler a memória.
// Vazia = parâmetros inválidos.
std::string ram_window_key(const JVal& a) {
    std::string key = "\x01ram|";
    std::string text = a.get_str("signature");
    if (!text.empty()) {
        std::vector<uint8_t> sig, mask;
        std::string norm;
        if (!parse_signature(text, sig, mask, norm)) return "";
        key += norm;
    }
    if (const JVal* offs = a.get("pointer_offsets")) {
        if (offs->type != JVal::Arr || offs->arr.size() > kMaxPointerChain) return "";
        for (const JVal& o : offs->arr) {
            int64_t v;
            if (!json_i64(o, &v)) return "";
            char b[24];
            snprintf(b, sizeof(b), "|%llx", (unsigned long long)v);
            key += b;
        }
    }
    int64_t size = ram_window_size(a);
    if (size < 4 || size > kRamWindowMax) return "";
    char b[24];
    snprintf(b, sizeof(b), "#%llx", (unsigned long long)size);
    return key + b;
}

// Percorre a cadeia: endereço de partida (assinatura ou início do módulo
// principal) e, para cada offset, soma e lê o ponteiro ([[partida+o1]+o2]...).
// deref_last = false: o último offset só é somado, como nos ponteiros do
// EdiZon e do Cheat Engine ([[main+o1]+o2]+o3 = endereço do valor).
// false com `why` vazio = memória indisponível (já registrado em ram_process).
bool ram_walk(EvalCtx& c, const JVal& a, bool deref_last, u64* out, std::string* why) {
    u64 addr = 0;
    if (!ram_process(c)) return false;
    if (!ram_anchor(c, a.get_str("signature"), &addr)) {
        *why = "sem ponto de partida: a assinatura falhou (ver a linha \"ram: assinatura\" acima)";
        return false;
    }
    if (const JVal* offs = a.get("pointer_offsets")) {
        for (size_t i = 0; i < offs->arr.size(); i++) {
            const JVal& o = offs->arr[i];
            int64_t off = 0;
            json_i64(o, &off);
            u64 at = addr + (u64)off;
            if (!deref_last && i + 1 == offs->arr.size()) {
                addr = at;
                break;
            }
            u64 next = 0;
            bool read = ram_read(at, &next, sizeof(next));
            if (!read || next == 0) {  // ainda carregando, ou cadeia errada
                char b[160], rc[32];
                snprintf(rc, sizeof(rc), "falhou (rc=0x%X)", ram_last_rc());
                snprintf(b, sizeof(b), "cadeia parou no offset %d (%s): leitura de %s %s", (int)i + 1,
                         o.type == JVal::Str ? o.str.c_str() : "num", ram_addr(c, at).c_str(),
                         read ? "deu ponteiro nulo" : rc);
                *why = b;
                return false;
            }
            addr = next;
        }
    }
    *out = addr;
    return true;
}

// Resolve a janela do ram_flag_search (todos os offsets lidos como ponteiro).
EvalCtx::RamWindow* ram_window(EvalCtx& c, const JVal& a, const std::string& key) {
    for (auto& w : c.windows)
        if (w.key == key) return w.ok ? &w : nullptr;
    c.windows.push_back({key, false, 0, ram_window_size(a)});
    EvalCtx::RamWindow& w = c.windows.back();
    if (!ram_walk(c, a, true, &w.base, &w.why)) return nullptr;
    w.ok = true;
    return &w;
}

// Leitor por trechos de uma janela de memória (mesma interface do SaveFile).
struct RamReader {
    u64 base;
    int64_t n;
    int64_t size() const { return n; }
    long read_at(int64_t off, void* buf, size_t len) {
        if (off < 0 || off >= n) return 0;
        size_t k = (size_t)std::min<int64_t>((int64_t)len, n - off);
        return ram_read(base + (u64)off, buf, k) ? (long)k : -1;
    }
};

// Procura o hash da flag na janela e lê o valor `offset_after_flag` bytes
// depois do início dele (padrão 4; negativo = antes do hash, como nos objetos
// de flag do BOTW na RAM). Só com o jogo aberto. Cada resultado novo vira uma
// linha "ram:" no log.
constexpr int64_t kRamMinOffset = -0x100, kRamMaxOffset = 0x1000;

bool eval_ram_flag_search(EvalCtx& c, const JVal& a) {
    if (!c.t.running) return false;
    uint32_t h = 0;
    int64_t align;
    std::string group;
    int size = (int)a.get_int("size", 4);
    int64_t after = a.get_int("offset_after_flag", 4);
    if (!hash_params(a, &h, &align, &group) || size < 1 || size > 8 || after < kRamMinOffset ||
        after > kRamMaxOffset) {
        ram_note(c, 0,
                 "parametros invalidos (signature, pointer_offsets, search_size, flag_name/flag_hash, size ou "
                 "offset_after_flag)");
        return false;
    }
    EvalCtx::RamWindow* w = ram_window(c, a, group);
    if (!w) {
        if (c.ram_ok) {
            for (auto& x : c.windows)
                if (x.key == group) ram_note(c, 0, x.why);  // uma linha por conquista, não por flag
        }
        return false;
    }
    RamReader r{w->base, w->size};
    const int64_t* pos = find_hash(c, r, group, align, h);
    char b[200];
    if (!pos) {
        snprintf(b, sizeof(b), "hash 0x%08X nao encontrado na janela %s (+0x%lX bytes)", h, ram_addr(c, w->base).c_str(),
                 (unsigned long)w->size);
        ram_note(c, h, b);
        return false;
    }
    u64 at = w->base + (u64)(*pos + after);
    uint64_t v = 0;
    if (!ram_read(at, &v, (size_t)size)) {
        snprintf(b, sizeof(b), "hash 0x%08X em %s, mas o valor em %s nao pode ser lido (rc=0x%X)", h,
                 ram_addr(c, w->base + (u64)*pos).c_str(), ram_addr(c, at).c_str(), ram_last_rc());
        ram_note(c, h, b);
        return false;
    }
    int64_t expected = a.get("expected_value_int") || a.get("expected_value_hex") ? cond_expected(a) : 1;
    bool ok = compare_u(v, cond_op(a), (uint64_t)expected);
    snprintf(b, sizeof(b), "flag lida com sucesso: valor %llu (0x%llX) no hash 0x%08X em %s -> %s",
             (unsigned long long)v, (unsigned long long)v, h, ram_addr(c, w->base + (u64)*pos).c_str(),
             ok ? "cumprida" : "ainda nao cumprida");
    ram_note(c, h, b);
    return ok;
}

// Caminho com pontos ("player.stats.level"); segmentos numéricos indexam arrays.
const JVal* json_path(const JVal& root, const std::string& path) {
    const JVal* v = &root;
    size_t start = 0;
    while (v && start <= path.size()) {
        size_t dot = path.find('.', start);
        std::string seg = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (seg.empty()) return nullptr;
        if (v->type == JVal::Arr) {
            char* end = nullptr;
            unsigned long idx = strtoul(seg.c_str(), &end, 10);
            v = *end == 0 && idx < v->arr.size() ? &v->arr[idx] : nullptr;
        } else {
            v = v->get(seg.c_str());
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return v;
}

// Save em JSON: valor numérico ou booleano (true = 1) em `key_path`.
bool eval_save_json_key(EvalCtx& c, const JVal& a) {
    // O parser monta a árvore inteira na memória: limite baixo para o heap do sysmodule.
    constexpr int64_t kMaxJsonSave = 64 * 1024;
    std::string key = a.get_str("key_path");
    if (key.empty()) return false;
    EvalCtx::File* file = file_to_eval(c, a);
    SaveFile f;
    if (!file || !f.open(file->local)) return false;
    if (f.size() <= 0 || f.size() > kMaxJsonSave) {
        static std::string warned;
        if (warned != file->json_name) {
            warned = file->json_name;
            pt_log("conquistas: %s tem %lld bytes (limite %lld para save_json_key)", file->json_name.c_str(),
                   (long long)f.size(), (long long)kMaxJsonSave);
        }
        return false;
    }
    std::string text((size_t)f.size(), '\0');
    if (f.read_at(0, &text[0], text.size()) != (long)text.size()) return false;
    JVal root;
    if (!json_parse(text.data(), text.size(), root)) return false;
    const JVal* v = json_path(root, key);
    if (!v) return false;
    int64_t num;
    if (v->type == JVal::Num) num = (int64_t)v->num;
    else if (v->type == JVal::Bool) num = v->b ? 1 : 0;
    else return false;
    return compare_i(num, cond_op(a), cond_expected(a));
}

constexpr int kMaxNesting = 4;  // count_of dentro de count_of

// --- ram_pointer_chain: valor num endereço achado por cadeia de ponteiros ----
// Para qualquer engine (Unity, Unreal, própria): os ponteiros que o EdiZon-SE,
// o Breeze ou o Cheat Engine mostram como [[main+A]+B]+C viram
// "pointer_offsets": ["0xA", "0xB", "0xC"]; o valor é lido no endereço final.
struct RamType {
    const char* name;
    int size;
    bool is_signed, is_float;
};
constexpr RamType kRamTypes[] = {{"u8", 1, false, false},  {"s8", 1, true, false},  {"u16", 2, false, false},
                                 {"s16", 2, true, false},  {"u32", 4, false, false}, {"s32", 4, true, false},
                                 {"f32", 4, false, true},  {"u64", 8, false, false}, {"s64", 8, true, false}};

const RamType* ram_type(const std::string& name) {
    for (const RamType& t : kRamTypes)
        if (name == t.name) return &t;
    return nullptr;
}

// Inteiro sem sinal do JSON: número ou texto ("0xFFFFFFFFFFFFFFFF" cabe inteiro).
bool json_u64(const JVal* v, uint64_t* out) {
    if (!v) return false;
    if (v->type == JVal::Num) {
        *out = (uint64_t)(int64_t)v->num;
        return true;
    }
    if (v->type != JVal::Str || v->str.empty()) return false;
    char* end = nullptr;
    *out = v->str[0] == '-' ? (uint64_t)strtoll(v->str.c_str(), &end, 0) : strtoull(v->str.c_str(), &end, 0);
    return end && *end == 0;
}

bool eval_ram_pointer_chain(EvalCtx& c, const JVal& a) {
    if (!c.t.running) return false;
    std::string key = ram_window_key(a);  // valida cadeia e assinatura
    const RamType* t = ram_type(a.get_str("data_type", "s32"));
    const JVal* offs = a.get("pointer_offsets");
    uint32_t note = crc32_ieee(key + a.get_str("data_type"));  // uma linha de log por condição
    const JVal* exp = a.get("expected_value_float")  ? a.get("expected_value_float")
                      : a.get("expected_value_int") ? a.get("expected_value_int")
                                                     : a.get("expected_value_hex");
    std::string op = cond_op(a);
    if (key.empty() || !t || !offs || offs->arr.empty() || !exp || (t->is_float && op.rfind("bit_", 0) == 0)) {
        ram_note(c, note, "parametros invalidos (pointer_offsets, data_type, operator ou valor esperado)");
        return false;
    }
    u64 at = 0;
    std::string why;
    if (!ram_walk(c, a, false, &at, &why)) {
        if (!why.empty()) ram_note(c, note, why);
        return false;
    }
    uint64_t raw = 0;
    char b[200];
    if (!ram_read(at, &raw, (size_t)t->size)) {
        snprintf(b, sizeof(b), "valor %s em %s nao pode ser lido (rc=0x%X)", t->name, ram_addr(c, at).c_str(),
                 ram_last_rc());
        ram_note(c, note, b);
        return false;
    }
    bool ok;
    char val[48];
    if (t->is_float) {
        float f;
        uint32_t bits = (uint32_t)raw;
        memcpy(&f, &bits, 4);
        double want = exp->type == JVal::Num ? exp->num : strtod(exp->str.c_str(), nullptr);
        ok = compare_f(f, op, want);
        snprintf(val, sizeof(val), "%g", f);
    } else if (t->is_signed) {
        int shift = 64 - 8 * t->size;
        int64_t v = (int64_t)(raw << shift) >> shift;  // estende o sinal
        uint64_t want = 0;
        ok = json_u64(exp, &want) && compare_i(v, op, (int64_t)want);
        snprintf(val, sizeof(val), "%lld", (long long)v);
    } else {
        uint64_t want = 0;
        ok = json_u64(exp, &want) && compare_u(raw, op, want);
        snprintf(val, sizeof(val), "%llu (0x%llX)", (unsigned long long)raw, (unsigned long long)raw);
    }
    snprintf(b, sizeof(b), "ponteiro lido com sucesso: %s %s em %s -> %s", t->name, val, ram_addr(c, at).c_str(),
             ok ? "cumprida" : "ainda nao cumprida");
    ram_note(c, note, b);
    return ok;
}

bool satisfied(EvalCtx& c, const JVal& a, int depth = 0);

// count_of: pelo menos `min` (padrão: todas) das `conditions` cumpridas.
bool eval_count_of(EvalCtx& c, const JVal& a, int depth) {
    const JVal* list = a.get("conditions");
    if (!list || list->type != JVal::Arr || list->arr.empty() || depth >= kMaxNesting) return false;
    int64_t need = a.get_int("min", (int64_t)list->arr.size());
    if (need < 1 || need > (int64_t)list->arr.size()) return false;
    // Com partes que não vêm do save (tempo de jogo), um save igual ao já
    // avaliado ainda pode completar a contagem: lê o save mesmo assim.
    bool mixed = false;
    for (const JVal& sub : list->arr)
        if (sub.get_str("save_file").empty()) mixed = true;
    bool saved_force = c.force;
    if (mixed) c.force = true;
    int64_t got = 0;
    for (size_t i = 0; i < list->arr.size() && got < need; i++) {
        if (satisfied(c, list->arr[i], depth + 1)) got++;
        if (got + (int64_t)(list->arr.size() - i - 1) < need) break;  // não dá mais para chegar
    }
    c.force = saved_force;
    return got >= need;
}

// Registra os hashes da condição (e das partes de um count_of) para a busca em lote.
void want_hashes(EvalCtx& c, const JVal& a, int depth = 0) {
    std::string type = a.get_str("type");
    if (type == "save_flag_hash_search" || type == "ram_flag_search") want_hash(c, a);
    const JVal* list = a.get("conditions");
    if (type == "count_of" && list && list->type == JVal::Arr && depth < kMaxNesting)
        for (const JVal& sub : list->arr) want_hashes(c, sub, depth + 1);
}

// true = condição cumprida. false também quando não dá para avaliar agora ou
// quando o save não mudou desde a última avaliação.
bool satisfied(EvalCtx& c, const JVal& a, int depth) {
    if (a.type != JVal::Obj) return false;
    std::string type = a.get_str("type");
    if (type == "count_of") return eval_count_of(c, a, depth);
    if (type == "playtime_stat") {
        int64_t target = a.get_int("target_seconds", 0);
        int64_t total = pt_db_scalar(c.db, "SELECT COALESCE(SUM(duration),0) FROM sessions WHERE game_id=? AND user_id=?",
                                     c.t.game_id, c.t.user_id, 0);
        return target > 0 && total >= target;
    }
    if (type == "save_binary_offset") return eval_save_binary_offset(c, a);
    if (type == "save_flag_search") return eval_save_flag_search(c, a);
    if (type == "save_json_key") return eval_save_json_key(c, a);
    if (type == "save_flag_hash_search") return eval_save_flag_hash_search(c, a);
    if (type == "ram_flag_search") return eval_ram_flag_search(c, a);
    if (type == "ram_pointer_chain") return eval_ram_pointer_chain(c, a);
    return false;
}

// Definições alteradas desde a última avaliação deste jogo/perfil.
bool defs_changed(const AchieveTarget& t, const std::string& file) {
    struct stat st;
    std::string path = std::string(PT_ACH_DIR) + "/" + file;
    Fingerprint fp;
    if (stat(path.c_str(), &st) == 0) {
        fp.size = st.st_size;
        fp.mtime = (u64)st.st_mtime;
    }
    std::string k = state_key(t, "#defs");
    const Fingerprint* prev = evaluated(k);
    bool changed = !prev || !(*prev == fp);
    g_pending.push_back({k, fp});
    return changed;
}

bool exec_text(sqlite3* db, const char* sql, const char* a, int64_t b, int64_t c) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, a, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, b);
    sqlite3_bind_int64(st, 3, c);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool unlocked(sqlite3* db, const char* id, int64_t user) {
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM user_achievements WHERE achievement_id=? AND user_id=?", -1, &st,
                           NULL) != SQLITE_OK)
        return true;  // na dúvida, não desbloqueia de novo
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, user);
    bool yes = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return yes;
}

bool record_unlock(sqlite3* db, const char* id, const AchieveTarget& t, int64_t now, const std::string& title) {
    bool ok = exec_text(db, "INSERT INTO user_achievements(achievement_id,user_id,unlocked_at) VALUES(?,?,?)", id,
                        t.user_id, now) &&
              exec_text(db, "INSERT INTO achievement_queue(achievement_id,user_id,created_at) VALUES(?,?,?)", id,
                        t.user_id, now);
    pt_log("conquista %s: %s (perfil %lld, \"%s\")", ok ? "desbloqueada" : "FALHOU ao gravar", id,
           (long long)t.user_id, title.c_str());
    return ok;
}

}  // namespace

void achieve_check(sqlite3* db, const AchieveTarget& t, int64_t now) {
    if (!t.running && g_locked_tid == t.tid) g_locked_tid = 0;  // jogo fechado: save livre
    if (t.game_id <= 0 || t.user_id <= 0) return;
    std::string key = def_key(t), file;
    JVal root;
    if (!load_defs(key, root, file)) return;
    const JVal* list = root.get("achievements");
    if (!list || list->type != JVal::Arr) return;

    // Reavalia tudo se o pacote mudou ou se os metadados sumiram do banco (troca de
    // pacote no app: o progresso foi apagado e os saves iguais precisam ser lidos de novo).
    bool changed = defs_changed(t, file);
    EvalCtx c{db, t, changed || !pt_pack_has_meta(db, key), {}, {}};
    if (c.force) pt_pack_sync_meta(db, key, t.game_id, root);
    c.build_id = root.get_str("build_id");
    if (c.build_id.size() > 64 || c.build_id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        c.build_id = "-";  // inválido: nunca bate, as condições de RAM ficam bloqueadas
    // 1ª passada: metadados e conquistas ainda bloqueadas (hashes em lote). A Ruby
    // (platina) não tem condição: fica de fora e é decidida no fim.
    std::vector<std::pair<const JVal*, std::string>> pending;
    const JVal* ruby = NULL;
    std::string ruby_id;
    bool ruby_locked = false;
    int others = 0, extra_rubies = 0;
    for (size_t i = 0; i < list->arr.size(); i++) {
        const JVal& a = list->arr[i];
        std::string aid = a.get_str("id");
        if (a.type != JVal::Obj || aid.empty()) continue;
        std::string id = "L:" + key + ":" + aid;
        bool is_ruby = !strcmp(pt_pack_rarity(a), "ruby");
        if (is_ruby && ruby) {
            extra_rubies++;  // só a primeira Ruby vale
            continue;
        }
        if (is_ruby) {
            ruby = &a;
            ruby_id = id;
            ruby_locked = !unlocked(db, id.c_str(), t.user_id);
            continue;
        }
        others++;
        if (unlocked(db, id.c_str(), t.user_id)) continue;
        want_hashes(c, a);
        pending.push_back({&a, id});
    }
    if (extra_rubies && c.force)
        pt_log("conquistas %s: %d Ruby a mais no pacote (só a primeira vale; as outras ficam de fora)", file.c_str(),
               extra_rubies);
    // 2ª passada: avaliação.
    size_t still_locked = pending.size();
    for (auto& p : pending) {
        const JVal& a = *p.first;
        const char* id = p.second.c_str();
        c.cur_id = a.get_str("id");
        if (!satisfied(c, a)) continue;
        if (record_unlock(db, id, t, now, a.get_str("title"))) still_locked--;
    }
    // Ruby: todas as outras desbloqueadas (agora ou antes) = desbloqueia em seguida,
    // na mesma transação; o id da fila é maior, então o pop-up dela vem por último.
    if (ruby && ruby_locked && others > 0 && still_locked == 0)
        record_unlock(db, ruby_id.c_str(), t, now, ruby->get_str("title"));
    // O conteúdo lido nesta avaliação não precisa ser lido de novo até mudar
    // (aplicado em achieve_commit, depois do COMMIT).
    for (auto& f : c.files)
        if (f.ok) g_pending.push_back({f.key, f.fp});
    if (c.ram_tried) ram_close();
}

bool achieve_packs_changed(void) {
    static bool known = false;
    static Fingerprint last;
    struct stat st;
    Fingerprint fp;
    if (stat(PT_ACH_ACTIVE_PATH, &st) == 0) {
        fp.size = st.st_size;
        fp.mtime = (u64)st.st_mtime;
    }
    bool changed = known && !(fp == last);
    known = true;
    last = fp;
    return changed;
}

void achieve_commit(void) {
    for (auto& p : g_pending) set_evaluated(p.first, p.second);
    g_pending.clear();
}

void achieve_discard(void) { g_pending.clear(); }

namespace {

// Texto para um campo da linha: sem separadores e com tamanho limitado.
std::string field(const unsigned char* s, size_t max) {
    std::string out = s ? (const char*)s : "";
    if (out.size() > max) {
        out.resize(max);
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back();  // UTF-8 inteiro
        if (!out.empty() && (unsigned char)out.back() >= 0xC0) out.pop_back();
        out += "...";
    }
    for (char& c : out)
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
    return out;
}

// Grava `text` em `path` só quando mudou.
void write_if_changed(const char* path, const std::string& text, std::string& last, bool& written) {
    if (written && text == last) return;
    if (pt_write_file_atomic(path, text.data(), text.size()) == 0) {
        last = text;
        written = true;
    }
}

}  // namespace

void achieve_export_current(sqlite3* db, int64_t game_id, int64_t user_id) {
    static std::string last;
    static bool written = false;
    std::string text;
    sqlite3_stmt* st = NULL;
    if (game_id > 0 &&
        sqlite3_prepare_v2(db, "SELECT display_name FROM games WHERE id=?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, game_id);
        if (sqlite3_step(st) == SQLITE_ROW) text = "G|" + field(sqlite3_column_text(st, 0), 120) + "\n";
        sqlite3_finalize(st);
        st = NULL;
    }
    // Desbloqueadas primeiro (mais recentes no topo), depois a ordem do jogo.
    if (!text.empty() &&
        sqlite3_prepare_v2(db,
                           "SELECT u.unlocked_at IS NOT NULL, COALESCE(u.unlocked_at,0), a.title, "
                           "COALESCE(a.description,''), a.is_secret FROM achievements a "
                           "LEFT JOIN user_achievements u ON u.achievement_id=a.id AND u.user_id=?2 "
                           "WHERE a.game_id=?1 ORDER BY u.unlocked_at IS NULL, u.unlocked_at DESC, a.sort_order "
                           "LIMIT 150",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, game_id);
        sqlite3_bind_int64(st, 2, user_id);
        while (sqlite3_step(st) == SQLITE_ROW) {
            bool got = sqlite3_column_int(st, 0) != 0;
            bool hide = !got && sqlite3_column_int(st, 4) != 0;
            char head[48];
            snprintf(head, sizeof(head), "A|%d|%lld|", got ? 1 : 0, (long long)sqlite3_column_int64(st, 1));
            text += head;
            text += hide ? "" : field(sqlite3_column_text(st, 2), 80);
            text += "|";
            text += hide ? "" : field(sqlite3_column_text(st, 3), 140);
            text += "\n";
        }
        sqlite3_finalize(st);
    }
    write_if_changed(PT_ACH_CURRENT_PATH, text, last, written);
}

void achieve_export_popups(sqlite3* db) {
    char seen_buf[32];
    int64_t seen = pt_read_file(PT_POPUP_SEEN_PATH, seen_buf, sizeof(seen_buf)) > 0 ? strtoll(seen_buf, NULL, 10) : 0;
    if (seen > 0) pt_db_scalar(db, "DELETE FROM achievement_queue WHERE id <= ?", seen, 0, 0);

    // "id|raridade|ícone|título" (a descrição não aparece no pop-up).
    sqlite3_stmt* st = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT q.id, COALESCE(a.rarity,'bronze'), COALESCE(a.icon_local_path,''), a.title "
                           "FROM achievement_queue q JOIN achievements a ON a.id = q.achievement_id "
                           "ORDER BY q.id LIMIT ?",
                           -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_int(st, 1, PT_POPUP_QUEUE_MAX);
    std::string text;
    while (sqlite3_step(st) == SQLITE_ROW) {
        char head[32];
        snprintf(head, sizeof(head), "%lld|", (long long)sqlite3_column_int64(st, 0));
        text += head;
        text += field(sqlite3_column_text(st, 1), 8) + "|";
        text += field(sqlite3_column_text(st, 2), 300) + "|";
        text += field(sqlite3_column_text(st, 3), 120) + "\n";
    }
    sqlite3_finalize(st);

    static std::string last;
    static bool written = false;
    write_if_changed(PT_POPUP_PATH, text, last, written);
}
