// Adaptado do Sphaira (https://github.com/ITotalJustice/sphaira), GPL-3.0.
// Copyright (c) ITotalJustice e contribuidores do Sphaira.
// Mudanças: sem UI/i18n/log do Sphaira; chaves só com a header_key; a
// instalação (fim do arquivo) informa o progresso por callback e não
// apaga outros forwarders.
#include "owo.hpp"
#include "nx_formats.hpp"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#define R_TRY(expr)                    \
    do {                               \
        const Result _rc = (expr);     \
        if (R_FAILED(_rc)) return _rc; \
    } while (0)
#define R_UNLESS(cond, rc)    \
    do {                      \
        if (!(cond)) return (rc); \
    } while (0)
#define R_SUCCEED() return 0

namespace pt_fwd {
namespace {

// Executa ao sair do escopo (substitui ON_SCOPE_EXIT do Sphaira).
template <typename F>
struct ScopeExit {
    F f;
    ~ScopeExit() { f(); }
};
#define PT_CONCAT2(a, b) a##b
#define PT_CONCAT(a, b) PT_CONCAT2(a, b)
#define ON_SCOPE_EXIT(...) ScopeExit PT_CONCAT(_scope_exit_, __LINE__){[&]() { __VA_ARGS__; }}

struct Keys {
    u8 header_key[0x20];
};

}  // namespace
}  // namespace pt_fwd

namespace pt_fwd {
namespace {

constexpr u32 IVFC_MAX_LEVEL = 6;
constexpr u32 IVFC_HASH_BLOCK_SIZE = 0x4000;
constexpr u32 PFS0_EXEFS_HASH_BLOCK_SIZE = 0x10000;
constexpr u32 PFS0_LOGO_HASH_BLOCK_SIZE = 0x1000;
constexpr u32 PFS0_META_HASH_BLOCK_SIZE = 0x1000;
constexpr u32 PFS0_PADDING_SIZE = 0x200;
constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
constexpr u32 ROMFS_FILEPARTITION_OFS = 0x200;

constexpr const u8 HBL_MAIN_DATA[]{
    #embed <exefs/main>
};

constexpr const u8 HBL_NPDM_DATA[]{
    #embed <exefs/main.npdm>
};


// stdio-like wrapper for std::vector
struct BufHelper {
    BufHelper() = default;
    BufHelper(std::span<const u8> data) {
        write(data);
    }

    void write(const void* data, u64 size) {
        if (offset + size >= buf.size()) {
            buf.resize(offset + size);
        }
        std::memcpy(buf.data() + offset, data, size);
        offset += size;
    }

    void write(std::span<const u8> data) {
        write(data.data(), data.size());
    }

    void seek(u64 where_to) {
        offset = where_to;
    }

    [[nodiscard]]
    auto tell() const {
        return offset;
    }

    std::vector<u8> buf;
    u64 offset{};
};

struct NcaEntry {
    NcaEntry(const BufHelper& buf, NcmContentType _type) : data{buf.buf}, type{_type} {
        sha256CalculateHash(hash, data.data(), data.size());
    }

    const std::vector<u8> data;
    const u8 type;
    u8 hash[SHA256_HASH_SIZE];
};

struct CnmtHeader {
    u64 title_id;
    u32 title_version;
    u8 meta_type; // NcmContentMetaType
    u8 _0xD;
    NcmContentMetaHeader meta_header;
    u8 install_type; // NcmContentInstallType
    u8 _0x17;
    u32 required_sys_version;
    u8 _0x1C[0x4];
};
static_assert(sizeof(CnmtHeader) == 0x20);

struct NcmContentMetaData {
    NcmContentMetaHeader header;
    NcmApplicationMetaExtendedHeader extended;
    NcmContentInfo infos[3];
};

struct NcaMetaEntry {
    NcaMetaEntry(const BufHelper& buf, NcmContentType type) : nca_entry{buf, type} { }

    NcaEntry nca_entry;
    NcmContentMetaHeader content_meta_header{};
    NcmContentMetaKey content_meta_key{};
    ncm::ContentStorageRecord content_storage_record{};
    NcmContentMetaData content_meta_data{};
};

struct Pfs0Header {
    u32 magic;
    u32 total_files;
    u32 string_table_size;
    u32 padding;
};

struct Pfs0FileTable {
    u64 data_offset;
    u64 data_size;
    u32 name_offset;
    u32 padding;
};

struct Pfs0StringTable {
    char name[256];
};

struct FileEntry {
    std::string name;
    std::vector<u8> data;
};

using FileEntries = std::vector<FileEntry>;

struct NpdmPatch {
    char title_name[0x10]{"Application"};
    char product_code[0x10]{};
    u64 tid;
    ForwarderAddressSpace address_space{ForwarderAddressSpace::Bit36};
    CpuCoreMode core_mode{CpuCoreMode::Three};
    ForwarderSvcDebugMode svc_debug_mode{ForwarderSvcDebugMode::Automatic};
};

struct NcapPatch {
    std::string name;
    std::string author;
    u64 tid;
    bool profile_selection{};
    bool screenshot{true};
    bool video_capture{true};
};

typedef struct romfs_dirent_ctx {
    u32 entry_offset;
    struct romfs_dirent_ctx *parent; /* Parent node */
    struct romfs_dirent_ctx *child; /* Child node */
    struct romfs_dirent_ctx *sibling; /* Sibling node */
    struct romfs_fent_ctx *file; /* File node */
    struct romfs_dirent_ctx *next; /* Next node */
} romfs_dirent_ctx_t;

typedef struct romfs_fent_ctx {
    u32 entry_offset;
    u64 offset;
    u64 size;
    romfs_dirent_ctx_t *parent; /* Parent dir */
    struct romfs_fent_ctx *sibling; /* Sibling file */
    struct romfs_fent_ctx *next; /* Logical next file */
} romfs_fent_ctx_t;

typedef struct {
    romfs_fent_ctx_t *files;
    u64 num_dirs;
    u64 num_files;
    u64 dir_table_size;
    u64 file_table_size;
    u64 dir_hash_table_size;
    u64 file_hash_table_size;
    u64 file_partition_size;
} romfs_ctx_t;

auto write_padding(BufHelper& buf, u64 off, u64 block) -> u64 {
    const u64 size = block - (off % block);
    if (size) {
        std::vector<u8> padding(size);
        buf.write(padding.data(), padding.size());
    }
    return size;
}

auto romfs_get_direntry(romfs_dir *directories, u32 offset) -> romfs_dir* {
    return (romfs_dir*)((u8*)directories + offset);
}

auto romfs_get_fentry(romfs_file *files, u32 offset) -> romfs_file* {
    return (romfs_file*)((u8*)files + offset);
}

auto calc_path_hash(u32 parent, const u8 *path, u32 start, u32 path_len) -> u32 {
    u32 hash = parent ^ 123456789;
    for (u32 i = 0; i < path_len; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= path[start + i];
    }

    return hash;
}

auto align(u32 offset, u32 alignment) -> u32 {
    const u32 mask = ~(alignment - 1);
    return (offset + (alignment - 1)) & mask;
}

auto align64(u64 offset, u64 alignment) -> u64 {
    const u64 mask = ~(u64)(alignment - 1);
    return (offset + (alignment - 1)) & mask;
}

auto romfs_get_hash_table_count(u32 num_entries) -> u32 {
    if (num_entries < 3) {
        return 3;
    } else if (num_entries < 19) {
        return num_entries | 1;
    }

    u32 count = num_entries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 || count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
        count++;
    }

    return count;
}

void romfs_visit_dir(const FileEntries& entries, romfs_dirent_ctx_t *parent, romfs_ctx_t *romfs_ctx) {
    romfs_dirent_ctx_t *child_dir_tree = NULL;
    romfs_fent_ctx_t *child_file_tree = NULL;
    romfs_fent_ctx_t *cur_file = NULL;

    for (auto& e : entries) {
        /* File */
        cur_file = (romfs_fent_ctx_t*)calloc(1, sizeof(romfs_fent_ctx_t));

        romfs_ctx->num_files++;

        cur_file->parent = parent;
        cur_file->size = e.data.size();

        romfs_ctx->file_table_size += sizeof(romfs_file) + align(e.name.length() - 1, 4);

        /* Ordered insertion on sibling */
        if (child_file_tree == NULL) {
            cur_file->sibling = child_file_tree;
            child_file_tree = cur_file;
        } else {
            romfs_fent_ctx_t *child, *prev;
            prev = child_file_tree;
            child = child_file_tree->sibling;
            prev->sibling = cur_file;
            cur_file->sibling = child;
        }

        /* Ordered insertion on next */
        if (romfs_ctx->files == NULL) {
            cur_file->next = romfs_ctx->files;
            romfs_ctx->files = cur_file;
        } else {
            romfs_fent_ctx_t *child, *prev;
            prev = romfs_ctx->files;
            child = romfs_ctx->files->next;
            prev->next = cur_file;
            cur_file->next = child;
        }

        cur_file = NULL;
    }

    parent->child = child_dir_tree;
    parent->file = child_file_tree;
}

void build_romfs_into_file(const FileEntries& entries, BufHelper& buf) {
    auto root_ctx = (romfs_dirent_ctx_t*)calloc(1, sizeof(romfs_dirent_ctx_t));
    root_ctx->parent = root_ctx;

    romfs_ctx_t romfs_ctx{};

    romfs_ctx.dir_table_size = sizeof(romfs_dir); /* Root directory. */
    romfs_ctx.num_dirs = 1;

    /* Visit all directories. */
    romfs_visit_dir(entries, root_ctx, &romfs_ctx);
    const u32 dir_hash_table_entry_count = romfs_get_hash_table_count(romfs_ctx.num_dirs);
    const u32 file_hash_table_entry_count = romfs_get_hash_table_count(romfs_ctx.num_files);
    romfs_ctx.dir_hash_table_size = 4 * dir_hash_table_entry_count;
    romfs_ctx.file_hash_table_size = 4 * file_hash_table_entry_count;

    romfs_header header{};
    romfs_fent_ctx_t *cur_file{};
    romfs_dirent_ctx_t *cur_dir{};
    u32 entry_offset{};

    std::vector<u32> dir_hash_table(dir_hash_table_entry_count, ROMFS_ENTRY_EMPTY);
    std::vector<u32> file_hash_table(file_hash_table_entry_count, ROMFS_ENTRY_EMPTY);

    auto dir_table = (romfs_dir*)calloc(1, romfs_ctx.dir_table_size);
    auto file_table = (romfs_file *)calloc(1, romfs_ctx.file_table_size);

    /* Determine file offsets. */
    cur_file = romfs_ctx.files;
    entry_offset = 0;
    for (auto& e : entries) {
        romfs_ctx.file_partition_size = align64(romfs_ctx.file_partition_size, 0x10);
        cur_file->offset = romfs_ctx.file_partition_size;
        romfs_ctx.file_partition_size += cur_file->size;
        cur_file->entry_offset = entry_offset;
        entry_offset += sizeof(romfs_file) + align(e.name.length() - 1, 4);
        cur_file = cur_file->next;
    }

    /* Determine dir offsets. */
    root_ctx->entry_offset = 0x0;

    /* Populate file tables. */
    cur_file = romfs_ctx.files;
    for (auto& e : entries) {
        auto cur_entry = romfs_get_fentry(file_table, cur_file->entry_offset);
        cur_entry->parent = (cur_file->parent->entry_offset);
        cur_entry->sibling = (cur_file->sibling == NULL ? ROMFS_ENTRY_EMPTY : cur_file->sibling->entry_offset);
        cur_entry->dataOff = (cur_file->offset);
        cur_entry->dataSize = (cur_file->size);

        const u32 name_size = e.name.length() - 1;
        const u32 hash = calc_path_hash(cur_file->parent->entry_offset, (const u8 *)e.name.c_str(), 1, name_size);
        cur_entry->nextHash = file_hash_table[hash % file_hash_table_entry_count];
        file_hash_table[hash % file_hash_table_entry_count] = (cur_file->entry_offset);

        cur_entry->nameLen = name_size;
        std::memcpy(cur_entry->name, e.name.c_str() + 1, name_size);

        cur_file = cur_file->next;
    }

    /* Populate dir tables. */
    cur_dir = root_ctx;

    while (cur_dir != NULL) {
        auto cur_entry = romfs_get_direntry(dir_table, cur_dir->entry_offset);
        cur_entry->parent = cur_dir->parent->entry_offset;
        cur_entry->sibling = cur_dir->sibling == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->sibling->entry_offset;
        cur_entry->childDir = cur_dir->child == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->child->entry_offset;
        cur_entry->childFile = cur_dir->file == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->file->entry_offset;

        const auto hash = calc_path_hash(0, 0, 0, 0);
        cur_entry->nextHash = dir_hash_table[hash % dir_hash_table_entry_count];
        dir_hash_table[hash % dir_hash_table_entry_count] = (cur_dir->entry_offset);

        cur_entry->nameLen = 0;

        auto temp = cur_dir;
        cur_dir = cur_dir->next;
        free(temp);
    }

    header.headerSize = sizeof(header);
    header.fileHashTableSize = romfs_ctx.file_hash_table_size;
    header.fileTableSize = romfs_ctx.file_table_size;
    header.dirHashTableSize = romfs_ctx.dir_hash_table_size;
    header.dirTableSize = romfs_ctx.dir_table_size;
    header.fileDataOff = ROMFS_FILEPARTITION_OFS;

    header.dirHashTableOff = align64(romfs_ctx.file_partition_size + ROMFS_FILEPARTITION_OFS, 4);
    header.dirTableOff = header.dirHashTableOff + romfs_ctx.dir_hash_table_size;
    header.fileHashTableOff = header.dirTableOff + romfs_ctx.dir_table_size;
    header.fileTableOff = header.fileHashTableOff + romfs_ctx.file_hash_table_size;

    buf.write(&header, sizeof(header));

    /* Write files. */
    cur_file = romfs_ctx.files;
    for (auto&e : entries) {
        buf.seek(cur_file->offset + ROMFS_FILEPARTITION_OFS);
        buf.write(e.data.data(), e.data.size());

        auto temp = cur_file;
        cur_file = cur_file->next;
        free(temp);
    }

    buf.seek(header.dirHashTableOff);
    buf.write(dir_hash_table.data(), romfs_ctx.dir_hash_table_size);

    buf.write(dir_table, romfs_ctx.dir_table_size);
    free(dir_table);

    buf.write(file_hash_table.data(), romfs_ctx.file_hash_table_size);

    buf.write(file_table, romfs_ctx.file_table_size);
    free(file_table);
}

auto romfs_build(const FileEntries& entries, u64 *out_size) -> std::vector<u8> {
    BufHelper buf;

    build_romfs_into_file(entries, buf);

    // Write Padding
    buf.seek(buf.buf.size());
    *out_size = buf.tell();
    write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);

    return buf.buf;
}

auto npdm_patch_kc(std::vector<u8>& npdm, u32 off, u32 size, u32 bitmask, u32 value) -> bool {
    const u32 pattern = BIT(bitmask) - 1;
    const u32 mask = BIT(bitmask) | pattern;

    for (u32 i = 0; i < size; i += 4) {
        u32 cup;
        std::memcpy(&cup, npdm.data() + off + i, sizeof(cup));
        if ((cup & mask) == pattern) {
            cup = value | pattern;
            std::memcpy(npdm.data() + off + i, &cup, sizeof(cup));
            return true;
        }
    }

    return false;
}

constexpr auto npdm_kernel_flags(CpuCoreMode mode) -> u32 {
    const u32 lowest_priority = mode == CpuCoreMode::Four ? 63 : 59;
    const u32 highest_priority = 28;
    const u32 lowest_cpu = 0;
    const u32 highest_cpu = mode == CpuCoreMode::Four ? 3 : 2;
    const u32 descriptor = (((highest_cpu << 8) | lowest_cpu) << 6 | highest_priority) << 6 | lowest_priority;
    return descriptor << 4;
}

static_assert((npdm_kernel_flags(CpuCoreMode::Three) | 0x7) == 0x020073B7);
static_assert((npdm_kernel_flags(CpuCoreMode::Four) | 0x7) == 0x030073F7);

// todo: manually build npdm
auto patch_npdm(std::vector<u8>& npdm, const NpdmPatch& patch) -> bool {
    constexpr u8 ADDRESS_SPACE_SHIFT = 1;
    constexpr u8 ADDRESS_SPACE_MASK = 0x7 << ADDRESS_SPACE_SHIFT;

    npdm::Meta meta{};
    npdm::Aci0 aci0{};
    npdm::Acid acid{};
    std::memcpy(&meta, npdm.data(), sizeof(meta));
    std::memcpy(&aci0, npdm.data() + meta.aci0_offset, sizeof(aci0));
    std::memcpy(&acid, npdm.data() + meta.acid_offset, sizeof(acid));

    // apply patch
    std::memcpy(meta.title_name, &patch.title_name, sizeof(meta.title_name));
    std::memcpy(meta.product_code, &patch.product_code, sizeof(patch.product_code));
    meta.flags = (meta.flags & ~ADDRESS_SPACE_MASK) | (static_cast<u8>(patch.address_space) << ADDRESS_SPACE_SHIFT);
    aci0.program_id = patch.tid;
    acid.program_id_min = patch.tid;
    acid.program_id_max = patch.tid;

    const auto kernel_flags = npdm_kernel_flags(patch.core_mode);
    const auto aci0_core_patched = npdm_patch_kc(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 3, kernel_flags);
    const auto acid_core_patched = npdm_patch_kc(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 3, kernel_flags);

    const auto enable_svc_debug = [&patch](){
        if (patch.svc_debug_mode == ForwarderSvcDebugMode::Enabled) {
            return true;
        }
        if (patch.svc_debug_mode == ForwarderSvcDebugMode::Disabled) {
            return false;
        }

        // Atmosphere 1.8.0 updated the NPDM MiscFlags layout for HOS 19.0.0.
        // Preserve Sphaira's original behavior by selecting the new ForceDebug
        // bit automatically when that version (or newer) is detected.
        u64 ver{};
        if (R_FAILED(splInitialize())) {
            return false;
        }
        ON_SCOPE_EXIT(splExit());

        const auto SplConfigItem_ExosphereVersion = (SplConfigItem)65000;
        if (R_FAILED(splGetConfig(SplConfigItem_ExosphereVersion, &ver))) {
            return false;
        }
        ver >>= 40;
        return ver >= MAKEHOSVERSION(1,8,0);
    }();

    if (enable_svc_debug) {
        npdm_patch_kc(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 16, BIT(19));
        npdm_patch_kc(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 16, BIT(19));
    }

    std::memcpy(npdm.data(), &meta, sizeof(meta));
    std::memcpy(npdm.data() + meta.aci0_offset, &aci0, sizeof(aci0));
    std::memcpy(npdm.data() + meta.acid_offset, &acid, sizeof(acid));
    return aci0_core_patched && acid_core_patched;
}

void patch_nacp(NacpStruct& nacp, const NcapPatch& patch) {
    // patch title
    if (!patch.name.empty()) {
        for (auto& lang : NacpLanguageEntries(nacp)) {
            std::strncpy(lang.name, patch.name.c_str(), sizeof(lang.name)-1);
        }
    }

    // patch author
    if (!patch.author.empty()) {
        for (auto& lang : NacpLanguageEntries(nacp)) {
            std::strncpy(lang.author, patch.author.c_str(), sizeof(lang.author)-1);
        }
    }

    // misc
    nacp.startup_user_account = patch.profile_selection ? 0x01 : 0x00;
    nacp.user_account_switch_lock = 0x00; // allow account switch
    nacp.add_on_content_registration_type = 0x01; // on demand
    nacp.screenshot = patch.screenshot ? 0x0 : 0x1;
    nacp.video_capture = patch.video_capture ? 0x2 : 0x0; // auto or disabled
    nacp.logo_type = 0x2; // Nintendo
    nacp.logo_handling = 0x0; // auto
    nacp.data_loss_confirmation = 0x0; // disable as we don't use saves
    nacp.required_network_service_license_on_launch = 0x0; // don't require linked account
    const char error_code[] = "sphaira";
    static_assert(sizeof(error_code) <= 9);
    nacp.application_error_code_category = 0; // this is actually a char[8], not a u64 :)
    std::memcpy(&nacp.application_error_code_category, error_code, sizeof(error_code)-1);

    // update tid
    nacp.presence_group_id = patch.tid;
    nacp.save_data_owner_id = patch.tid;
    nacp.pseudo_device_id_seed = patch.tid;
    nacp.add_on_content_base_id = patch.tid ^ 0x1000;
    for (auto& id : nacp.local_communication_id) {
        id = patch.tid;
    }

    // enable play logging
    nacp.play_log_policy = 0x0; // open
    nacp.play_log_query_capability = 0x0;

    // disable save creation
    nacp.user_account_save_data_size = 0x0;
    nacp.user_account_save_data_journal_size = 0x0;
    nacp.device_save_data_size = 0x0;
    nacp.device_save_data_journal_size = 0x0;
    nacp.user_account_save_data_size_max = 0x0;
    nacp.user_account_save_data_journal_size_max = 0x0;
    nacp.device_save_data_size_max = 0x0;
    nacp.device_save_data_journal_size_max = 0x0;
}

void add_file_entry(FileEntries& entries, const char* name, const void* data, u64 size) {
    FileEntry entry;
    entry.name = name;
    entry.data.resize(size);
    std::memcpy(entry.data.data(), data, size);
    entries.emplace_back(entry);
}

void add_file_entry(FileEntries& entries, const char* name, std::span<const u8> data) {
    add_file_entry(entries, name, data.data(), data.size());
}

auto build_ivfc_master_hash(std::span<const u8> level1) -> std::vector<u8> {
    std::vector<u8> hash(SHA256_HASH_SIZE);
    sha256CalculateHash(hash.data(), level1.data(), level1.size());
    return hash;
}

auto build_pfs0(const FileEntries& entries) -> std::vector<u8> {
    BufHelper buf;

    Pfs0Header header{};
    std::vector<Pfs0FileTable> file_table(entries.size());
    std::vector<char> string_table;

    u64 string_offset{};
    u64 data_offset{};

    for (u32 i = 0; i < entries.size(); i++) {
        file_table[i].data_offset = data_offset;
        file_table[i].data_size = entries[i].data.size();
        file_table[i].name_offset = string_offset;
        file_table[i].padding = 0;

        string_table.resize(string_offset + entries[i].name.length() + 1);
        std::memcpy(string_table.data() + string_offset, entries[i].name.c_str(), entries[i].name.length() + 1);

        data_offset += entries[i].data.size();
        string_offset += entries[i].name.length() + 1;
    }

    // align table
    string_table.resize((string_table.size() + 0x1F) & ~0x1F);

    header.magic = 0x30534650;
    header.total_files = entries.size();
    header.string_table_size = string_table.size();
    header.padding = 0;

    buf.write(&header, sizeof(header));
    buf.write(file_table.data(), sizeof(Pfs0FileTable) * file_table.size());
    buf.write(string_table.data(), string_table.size());

    for (const auto&e : entries) {
        buf.write(e.data.data(), e.data.size());
    }

    return buf.buf;
}

auto build_pfs0_hash_table(const std::vector<u8>& pfs0, u32 block_size) -> std::vector<u8> {
    BufHelper buf;
    u8 hash[SHA256_HASH_SIZE];
    u32 read_size = block_size;

    for (u32 i = 0; i < pfs0.size(); i += read_size) {
        if (i + read_size >= pfs0.size()) {
            read_size = pfs0.size() - i;
        }
        sha256CalculateHash(hash, pfs0.data() + i, read_size);
        buf.write(hash, sizeof(hash));
    }

    return buf.buf;
}

auto build_pfs0_master_hash(const std::vector<u8>& pfs0_hash_table) -> std::vector<u8> {
    std::vector<u8> hash(SHA256_HASH_SIZE);
    sha256CalculateHash(hash.data(), pfs0_hash_table.data(), pfs0_hash_table.size());
    return hash;
}

void write_nca_padding(BufHelper& buf) {
    write_padding(buf, buf.tell(), 0x200);
}

void nca_encrypt_header(nca::Header* header, std::span<const u8> key) {
    Aes128XtsContext ctx{};
    aes128XtsContextCreate(&ctx, key.data(), key.data() + 0x10, true);

    u8 sector{};
    for (u64 pos = 0; pos < 0xC00; pos += 0x200) {
        aes128XtsContextResetSector(&ctx, sector++, true);
        aes128XtsEncrypt(&ctx, (u8*)header + pos, (const u8*)header + pos, 0x200);
    }
}

void write_nca_section(nca::Header& nca_header, u8 index, u64 start, u64 end) {
    auto& section = nca_header.fs_table[index];
    section.media_start_offset = start / 0x200; // 0xC00 / 0x200
    section.media_end_offset = end / 0x200; // Section end offset / 200
    section._0x8[0] = 0x1; // Always 1
}

void write_nca_fs_header_pfs0(nca::Header& nca_header, u8 index, const std::vector<u8>& master_hash, u64 hash_table_size, u32 block_size) {
    auto& fs_header = nca_header.fs_header[index];
    fs_header.hash_type = nca::HashType_HierarchicalSha256;
    fs_header.fs_type = nca::FileSystemType_PFS0;
    fs_header.version = 0x2; // Always 2
    fs_header.hash_data.hierarchical_sha256_data.layer_count = 0x2;
    fs_header.hash_data.hierarchical_sha256_data.block_size = block_size;
    fs_header.encryption_type = nca::EncryptionType_None;
    fs_header.hash_data.hierarchical_sha256_data.hash_layer.size = hash_table_size;
    std::memcpy(fs_header.hash_data.hierarchical_sha256_data.master_hash, master_hash.data(), master_hash.size());
    sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
}

void write_nca_fs_header_romfs(nca::Header& nca_header, u8 index) {
    auto& fs_header = nca_header.fs_header[index];
    fs_header.hash_type = nca::HashType_HierarchicalIntegrity;
    fs_header.fs_type = nca::FileSystemType_RomFS;
    fs_header.version = 0x2; // Always 2
    fs_header.hash_data.integrity_meta_info.magic = 0x43465649;
    fs_header.hash_data.integrity_meta_info.version = 0x20000; // Always 0x20000
    fs_header.hash_data.integrity_meta_info.master_hash_size = SHA256_HASH_SIZE;
    fs_header.hash_data.integrity_meta_info.info_level_hash.max_layers = 0x7;
    fs_header.encryption_type = nca::EncryptionType_None;
    fs_header.hash_data.integrity_meta_info.info_level_hash.levels[5].block_size = 0x0E; // 0x4000
    sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
}

void write_nca_pfs0(nca::Header& nca_header, u8 index, const FileEntries& entries, u32 block_size, BufHelper& buf) {
    const auto pfs0 = build_pfs0(entries);
    const auto pfs0_hash_table = build_pfs0_hash_table(pfs0, block_size);
    const auto pfs0_master_hash = build_pfs0_master_hash(pfs0_hash_table);

    buf.write(pfs0_hash_table.data(), pfs0_hash_table.size());
    const auto padding_size = write_padding(buf, pfs0_hash_table.size(), PFS0_PADDING_SIZE);

    nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.offset = pfs0_hash_table.size() + padding_size;
    nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.size = pfs0.size();

    buf.write(pfs0.data(), pfs0.size());
    write_nca_padding(buf);

    const auto section_start = index == 0 ? sizeof(nca_header) : nca_header.fs_table[index-1].media_end_offset * 0x200;
    write_nca_section(nca_header, index, section_start, buf.tell());
    write_nca_fs_header_pfs0(nca_header, index, pfs0_master_hash, pfs0_hash_table.size(), block_size);
}

auto ivfc_create_level(const std::vector<u8>& src) -> std::vector<u8> {
    BufHelper buf;
    u8 hash[SHA256_HASH_SIZE];
    u64 read_size = IVFC_HASH_BLOCK_SIZE;

    for (u32 i = 0; i < src.size(); i += read_size) {
        if (i + read_size >= src.size()) {
            read_size = src.size() - i;
        }
        sha256CalculateHash(hash, src.data() + i, read_size);
        buf.write(hash, sizeof(hash));
    }

    write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);

    return buf.buf;
}

void write_nca_romfs(nca::Header& nca_header, u8 index, const FileEntries& entries, u32 block_size, BufHelper& buf) {
    auto& fs_header = nca_header.fs_header[index];
    auto& meta_info = fs_header.hash_data.integrity_meta_info;
    auto& info_level_hash = meta_info.info_level_hash;

    std::vector<u8> ivfc[IVFC_MAX_LEVEL];

    ivfc[5] = romfs_build(entries, &info_level_hash.levels[5].hash_data_size);

    for (int b = 4; b >= 0; b--) {
        ivfc[b] = ivfc_create_level(ivfc[b + 1]);
        info_level_hash.levels[b].hash_data_size = ivfc[b].size();
        info_level_hash.levels[b].block_size = 0x0E; // 0x4000
    }

    info_level_hash.levels[0].logical_offset = 0;
    for (int i = 1; i <= 5; i++) {
        info_level_hash.levels[i].logical_offset = info_level_hash.levels[i - 1].logical_offset + info_level_hash.levels[i - 1].hash_data_size;
    }

    for (const auto& iv : ivfc) {
        buf.write(iv.data(), iv.size());
    }

    write_nca_padding(buf);

    const auto ivfc_master_hash = build_ivfc_master_hash(ivfc[0]);
    std::memcpy(meta_info.master_hash, ivfc_master_hash.data(), sizeof(meta_info.master_hash));

    const auto section_start = index == 0 ? sizeof(nca_header) : nca_header.fs_table[index-1].media_end_offset * 0x200;
    write_nca_section(nca_header, index, section_start, buf.tell());
    write_nca_fs_header_romfs(nca_header, index);
}

void write_nca_header_encypted(nca::Header& nca_header, u64 tid, const Keys& keys, nca::ContentType type, BufHelper& buf) {
    nca_header.magic = NCA3_MAGIC;
    nca_header.distribution_type = nca::DistributionType_System;
    nca_header.content_type = type;
    nca_header.program_id = tid;
    nca_header.sdk_version = 0x000C1100;
    nca_header.size = buf.tell();

    nca_encrypt_header(&nca_header, keys.header_key);
    buf.seek(0);
    buf.write(&nca_header, sizeof(nca_header));
}

auto create_program_nca(u64 tid, const Keys& keys, const FileEntries& exefs, const FileEntries& romfs, const FileEntries& logo) -> NcaEntry {
    BufHelper buf;
    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));

    write_nca_pfs0(nca_header, 0, exefs, PFS0_EXEFS_HASH_BLOCK_SIZE, buf);
    write_nca_romfs(nca_header, 1, romfs, IVFC_HASH_BLOCK_SIZE, buf);
    // only write logo if set (can only 1 file be added?)
    if (logo.size() == 2 && !logo[0].data.empty() && !logo[1].data.empty()) {
        write_nca_pfs0(nca_header, 2, logo, PFS0_LOGO_HASH_BLOCK_SIZE, buf);
    }
    write_nca_header_encypted(nca_header, tid, keys, nca::ContentType_Program, buf);

    return {buf, NcmContentType_Program};
}

auto create_control_nca(u64 tid, const Keys& keys, const FileEntries& romfs) -> NcaEntry{
    nca::Header nca_header{};
    BufHelper buf;
    buf.write(&nca_header, sizeof(nca_header));

    write_nca_romfs(nca_header, 0, romfs, IVFC_HASH_BLOCK_SIZE, buf);
    write_nca_header_encypted(nca_header, tid, keys, nca::ContentType_Control, buf);

    return {buf, NcmContentType_Control};
}

auto create_meta_nca(u64 tid, const Keys& keys, NcmStorageId storage_id, const std::vector<NcaEntry>& ncas) -> NcaMetaEntry {
    CnmtHeader cnmt_header{};
    NcmApplicationMetaExtendedHeader cnmt_extended{};
    NcmPackagedContentInfo packaged_content_info[2]{};
    u8 digest[0x20]{};
    BufHelper buf;

    cnmt_header.title_id = tid;
    cnmt_header.title_version = 0; // todo: parse nacp.disaply_version
    cnmt_header.meta_type = NcmContentMetaType_Application;
    cnmt_header.meta_header.extended_header_size = sizeof(cnmt_extended);
    cnmt_header.meta_header.content_count = 0x2; // program + control
    cnmt_header.meta_header.content_meta_count = 0x1; // only 1 meta
    cnmt_header.meta_header.attributes = 0x0;
    cnmt_header.meta_header.storage_id = storage_id;
    cnmt_extended.patch_id = cnmt_header.title_id | 0x800;

    for (u32 i = 0; i < ncas.size(); i++) {
        std::memcpy(packaged_content_info[i].hash, ncas[i].hash, sizeof(packaged_content_info[i].hash));
        std::memcpy(&packaged_content_info[i].info.content_id, ncas[i].hash, sizeof(packaged_content_info[i].info.content_id));
        packaged_content_info[i].info.content_type = ncas[i].type;
        ncmU64ToContentInfoSize(ncas[i].data.size(), &packaged_content_info[i].info);
    }

    // create control
    BufHelper cnmt_buf;
    cnmt_buf.write(&cnmt_header, sizeof(cnmt_header));
    cnmt_buf.write(&cnmt_extended, sizeof(cnmt_extended));
    cnmt_buf.write(&packaged_content_info, sizeof(packaged_content_info));
    cnmt_buf.write(digest, sizeof(digest));

    FileEntries cnmt;
    char cnmt_name[34];
    std::snprintf(cnmt_name, sizeof(cnmt_name), "Application_%016lX.cnmt", tid);
    add_file_entry(cnmt, cnmt_name, cnmt_buf.buf.data(), cnmt_buf.buf.size());

    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));
    write_nca_pfs0(nca_header, 0, cnmt, PFS0_META_HASH_BLOCK_SIZE, buf);
    write_nca_header_encypted(nca_header, tid, keys, nca::ContentType_Meta, buf);

    // entry
    NcaMetaEntry entry{buf, NcmContentType_Meta};

    // header
    entry.content_meta_header = cnmt_header.meta_header;
    entry.content_meta_header.content_count++;
    entry.content_meta_header.storage_id = 0;

    // key
    entry.content_meta_key.id = cnmt_header.title_id;
    entry.content_meta_key.version = cnmt_header.title_version;
    entry.content_meta_key.type = cnmt_header.meta_type;
    entry.content_meta_key.install_type = NcmContentInstallType_Full;
    std::memset(entry.content_meta_key.padding, 0, sizeof(entry.content_meta_key.padding));

    // record
    entry.content_storage_record.key = entry.content_meta_key;
    entry.content_storage_record.storage_id = storage_id;
    std::memset(entry.content_storage_record.padding, 0, sizeof(entry.content_storage_record.padding));

    // data
    entry.content_meta_data.header = entry.content_meta_header;
    entry.content_meta_data.extended = cnmt_extended;

    // meta content info
    std::memcpy(&entry.content_meta_data.infos[0].content_id, entry.nca_entry.hash, sizeof(entry.content_meta_data.infos[0].content_id));
    entry.content_meta_data.infos[0].content_type = entry.nca_entry.type;
    entry.content_meta_data.infos[0].attr = 0;
    ncmU64ToContentInfoSize(cnmt_buf.buf.size(), &entry.content_meta_data.infos[0]);
    entry.content_meta_data.infos[0].id_offset = 0;

    // program + control content info
    entry.content_meta_data.infos[1] = packaged_content_info[0].info;
    entry.content_meta_data.infos[2] = packaged_content_info[1].info;

    return entry;
}


// --- Chaves (de sphaira/source/yati/nx/keys.cpp) ---------------------------------
constexpr u8 HEADER_KEK_SRC[0x10] = {
    0x1F, 0x12, 0x91, 0x3A, 0x4A, 0xCB, 0xF0, 0x0D, 0x4C, 0xDE, 0x3A, 0xF6, 0xD5, 0x23, 0x88, 0x2A
};

constexpr u8 HEADER_KEY_SRC[0x20] = {
    0x5A, 0x3E, 0xD8, 0x4F, 0xDE, 0xC0, 0xD8, 0x26, 0x31, 0xF7, 0xE2, 0x5D, 0x19, 0x7B, 0xF5, 0xD0,
    0x1C, 0x9B, 0x7B, 0xFA, 0xF6, 0x28, 0x18, 0x3D, 0x71, 0xF6, 0x4D, 0x73, 0xF1, 0x50, 0xB9, 0xD2
};

constexpr Result kErrBadArgs = MAKERESULT(Module_Libnx, LibnxError_BadInput);
constexpr Result kErrNoKey = MAKERESULT(Module_Libnx, LibnxError_NotFound);
constexpr Result kErrNpdm = MAKERESULT(Module_Libnx, LibnxError_BadInput);

bool parse_hex(const char* hex, u8* out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(hex[i * 2]), lo = hi < 0 ? -1 : nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (u8)(hi << 4 | lo);
    }
    return true;
}

// header_key = <64 hex> num arquivo no formato do prod.keys (Lockpick_RCM).
bool read_header_key_file(const char* path, u8 out[0x20]) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char line[512];
    bool ok = false;
    while (!ok && std::fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (std::strncmp(p, "header_key", 10) != 0) continue;
        p += 10;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        ok = std::strlen(p) >= 64 && parse_hex(p, out, 0x20);
    }
    std::fclose(f);
    return ok;
}

// --- ns: ApplicationManagerInterface (de sphaira/source/yati/nx/ns.cpp) ---------
Result ns_push_application_record(Service* srv, u64 tid, const ncm::ContentStorageRecord* records, u32 count) {
    const struct {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = {0x3 /* Installed */, {0}, tid};
    return serviceDispatchIn(srv, 16, in, .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                             .buffers = {{records, sizeof(*records) * count}});
}

Result ns_invalidate_control_cache(Service* srv, u64 tid) { return serviceDispatchIn(srv, 404, tid); }

// Dimensões de um JPEG (marcador SOF). false se não for JPEG.
bool jpeg_size(const std::vector<u8>& d, int* w, int* h) {
    if (d.size() < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < d.size()) {
        if (d[i] != 0xFF) return false;
        u8 marker = d[i + 1];
        u16 len = (u16)(d[i + 2] << 8 | d[i + 3]);
        if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            *h = d[i + 5] << 8 | d[i + 6];
            *w = d[i + 7] << 8 | d[i + 8];
            return true;
        }
        i += 2 + len;
    }
    return false;
}

}  // namespace

u64 forwarder_title_id(const std::string& nro_path, const std::string& args) {
    // Sphaira: argv completo = caminho do NRO + ' ' + argumentos; TID = hash de nro_path + argv.
    const std::string argv = args.empty() ? nro_path : nro_path + ' ' + args;
    u64 hash_data[SHA256_HASH_SIZE / sizeof(u64)];
    const std::string hash_path = nro_path + argv;
    sha256CalculateHash(hash_data, hash_path.data(), hash_path.length());
    return 0x0500000000000000ULL | (hash_data[0] & 0x00FFFFFFFFFFF000ULL);
}

Result header_key(const std::string& keys_path, u8 out[0x20], std::string* source) {
    // Como o Sphaira: deriva a chave no próprio console, sem precisar de arquivo.
    Result rc = splCryptoInitialize();
    if (R_SUCCEEDED(rc)) {
        u8 header_kek[0x20];
        rc = splCryptoGenerateAesKek(HEADER_KEK_SRC, 0, 0, header_kek);
        if (R_SUCCEEDED(rc)) rc = splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC, out);
        if (R_SUCCEEDED(rc)) rc = splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC + 0x10, out + 0x10);
        splCryptoExit();
        if (R_SUCCEEDED(rc)) {
            if (source) *source = "derivada pelo spl do console";
            return 0;
        }
    }
    const char* candidates[] = {keys_path.c_str(), "sdmc:/switch/prod.keys", "sdmc:/prod.keys", "sdmc:/switch/keys.txt"};
    for (const char* p : candidates) {
        if (p[0] && read_header_key_file(p, out)) {
            if (source) *source = std::string("header_key de ") + p;
            return 0;
        }
    }
    if (source) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "spl falhou (0x%X) e nenhum prod.keys com header_key foi encontrado", rc);
        *source = buf;
    }
    return kErrNoKey;
}

Result install_forwarder(const Config& cfg, NcmStorageId storage_id, const Progress& progress, u64* installed_tid,
                         std::string* error) {
    constexpr int kSteps = 7;
    auto step = [&](int n, const char* what) {
        if (progress) progress(n, kSteps, what);
    };
    auto fail = [&](Result rc, const char* what) -> Result {
        if (error) {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "%s (0x%X)", what, rc);
            *error = buf;
        }
        return rc;
    };

    if (cfg.nro_path.empty() || cfg.name.empty()) return fail(kErrBadArgs, "Caminho do NRO ou nome vazio");
    int w = 0, h = 0;
    if (!jpeg_size(cfg.icon, &w, &h)) return fail(kErrBadArgs, "O ícone precisa ser JPEG");
    if (w != 256 || h != 256) {
        if (error) *error = "O ícone precisa ser JPEG 256x256 (este tem " + std::to_string(w) + "x" + std::to_string(h) + ")";
        return kErrBadArgs;
    }

    step(1, "Obtendo a chave de cabeçalho NCA");
    Keys keys{};
    std::string key_source;
    if (R_FAILED(header_key(cfg.keys_path, keys.header_key, &key_source))) return fail(kErrNoKey, key_source.c_str());

    Result rc = ncmInitialize();
    if (R_FAILED(rc)) return fail(rc, "ncm indisponível");
    ON_SCOPE_EXIT(ncmExit());
    rc = nsInitialize();
    if (R_FAILED(rc)) return fail(rc, "ns indisponível");
    ON_SCOPE_EXIT(nsExit());
    Service app_srv;
    rc = nsGetApplicationManagerInterface(&app_srv);
    if (R_FAILED(rc)) return fail(rc, "ns: ApplicationManagerInterface indisponível");
    ON_SCOPE_EXIT(serviceClose(&app_srv));

    const std::string argv = cfg.args.empty() ? cfg.nro_path : cfg.nro_path + ' ' + cfg.args;
    const u64 tid = forwarder_title_id(cfg.nro_path, cfg.args);
    std::vector<NcaEntry> nca_entries;

    // Program: hbl do Sphaira + nextArgv/nextNroPath na romfs.
    step(2, "Criando o Program NCA");
    {
        FileEntries exefs;
        add_file_entry(exefs, "main", HBL_MAIN_DATA);
        add_file_entry(exefs, "main.npdm", HBL_NPDM_DATA);

        FileEntries romfs;
        add_file_entry(romfs, "/nextArgv", argv.data(), argv.length());
        add_file_entry(romfs, "/nextNroPath", cfg.nro_path.data(), cfg.nro_path.length());

        NpdmPatch npdm_patch;
        npdm_patch.tid = tid;
        if (!patch_npdm(exefs[1].data, npdm_patch)) return fail(kErrNpdm, "Não foi possível ajustar o NPDM do hbl");

        FileEntries logo;
        nca_entries.emplace_back(create_program_nca(tid, keys, exefs, romfs, logo));
    }

    // Control: NACP com nome/autor + ícone.
    step(3, "Criando o Control NCA");
    {
        NacpStruct nacp{};
        std::strncpy(nacp.display_version, "1.0.0", sizeof(nacp.display_version) - 1);
        nacp.supported_language_flag = 0xFFFF;
        NcapPatch nacp_patch{};
        nacp_patch.tid = tid;
        nacp_patch.name = cfg.name;
        nacp_patch.author = cfg.author;
        patch_nacp(nacp, nacp_patch);

        FileEntries romfs;
        add_file_entry(romfs, "/control.nacp", &nacp, sizeof(nacp));
        add_file_entry(romfs, "/icon_AmericanEnglish.dat", cfg.icon);
        nca_entries.emplace_back(create_control_nca(tid, keys, romfs));
    }

    step(4, "Criando o Meta NCA");
    NcmContentMetaKey content_meta_key;
    ncm::ContentStorageRecord content_storage_record;
    NcmContentMetaData content_meta_data;
    {
        const auto meta_entry = create_meta_nca(tid, keys, storage_id, nca_entries);
        nca_entries.emplace_back(meta_entry.nca_entry);
        content_meta_key = meta_entry.content_meta_key;
        content_storage_record = meta_entry.content_storage_record;
        content_meta_data = meta_entry.content_meta_data;
    }

    step(5, "Gravando as NCAs");
    {
        NcmContentStorage cs;
        rc = ncmOpenContentStorage(&cs, storage_id);
        if (R_FAILED(rc)) return fail(rc, "Não foi possível abrir o armazenamento de conteúdo");
        ON_SCOPE_EXIT(ncmContentStorageClose(&cs));
        for (const auto& nca : nca_entries) {
            NcmContentId content_id;
            NcmPlaceHolderId placeholder_id;
            std::memcpy(&content_id, nca.hash, sizeof(content_id));
            if (R_FAILED(rc = ncmContentStorageGeneratePlaceHolderId(&cs, &placeholder_id))) return fail(rc, "ncm: placeholder");
            ncmContentStorageDeletePlaceHolder(&cs, &placeholder_id);
            if (R_FAILED(rc = ncmContentStorageCreatePlaceHolder(&cs, &content_id, &placeholder_id, nca.data.size())))
                return fail(rc, "ncm: criar placeholder");
            if (R_FAILED(rc = ncmContentStorageWritePlaceHolder(&cs, &placeholder_id, 0, nca.data.data(), nca.data.size())))
                return fail(rc, "ncm: gravar NCA");
            ncmContentStorageDelete(&cs, &content_id);
            if (R_FAILED(rc = ncmContentStorageRegister(&cs, &content_id, &placeholder_id))) return fail(rc, "ncm: registrar NCA");
        }
    }

    step(6, "Atualizando o banco de conteúdo");
    {
        NcmContentMetaDatabase db;
        rc = ncmOpenContentMetaDatabase(&db, storage_id);
        if (R_FAILED(rc)) return fail(rc, "Não foi possível abrir o banco de conteúdo");
        ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));
        if (R_FAILED(rc = ncmContentMetaDatabaseSet(&db, &content_meta_key, &content_meta_data, sizeof(content_meta_data))))
            return fail(rc, "ncm: gravar meta");
        if (R_FAILED(rc = ncmContentMetaDatabaseCommit(&db))) return fail(rc, "ncm: commit");
    }

    step(7, "Registrando na Home");
    nsDeleteApplicationEntity(tid);  // versão anterior deste mesmo forwarder
    if (R_FAILED(rc = ns_push_application_record(&app_srv, tid, &content_storage_record, 1)))
        return fail(rc, "ns: registrar aplicativo");
    ns_invalidate_control_cache(&app_srv, tid);

    if (installed_tid) *installed_tid = tid;
    R_SUCCEED();
}

}  // namespace pt_fwd
