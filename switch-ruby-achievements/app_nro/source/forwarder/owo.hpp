#pragma once
// Adaptado do Sphaira (https://github.com/ITotalJustice/sphaira), GPL-3.0.
// Monta um NSP de forwarder em memória (Program + Control + Meta NCAs, com o
// hbl do Sphaira no exefs) e instala direto no console via ncm/ns.
#include <switch.h>

#include <functional>
#include <string>
#include <vector>

namespace pt_fwd {

// Valores do campo AddressSpaceType do NPDM.
enum class ForwarderAddressSpace : u8 {
    Bit32 = 0,
    Bit36 = 1,
    Bit32NoAlias = 2,
    Bit39 = 3,
};

enum class ForwarderSvcDebugMode : u8 {
    Automatic,
    Enabled,
    Disabled,
};

enum class CpuCoreMode : u8 {
    Three = 3,
    Four = 4,
};

struct Config {
    std::string nro_path;   // ex.: sdmc:/switch/ruby_achievements/ruby_achievements.nro
    std::string args;       // argumentos depois do caminho do NRO
    std::string name;       // nome na Home
    std::string author;
    std::vector<u8> icon;   // JPEG 256x256
    std::string keys_path;  // prod.keys alternativo ("" = locais padrão)
};

// step de 1 a total; what = etapa em andamento.
using Progress = std::function<void(int step, int total, const char* what)>;

// Title ID do forwarder (faixa 0x05...), com a mesma fórmula do Sphaira:
// SHA-256 de nro_path + argv completo.
u64 forwarder_title_id(const std::string& nro_path, const std::string& args);

// Chave de cabeçalho NCA: derivada pelo spl do próprio console; se falhar, lida
// de header_key no prod.keys (keys_path ou /switch/prod.keys, /prod.keys).
// `source` descreve de onde veio.
Result header_key(const std::string& keys_path, u8 out[0x20], std::string* source);

// Monta e instala. `installed_tid` recebe o Title ID instalado.
Result install_forwarder(const Config& cfg, NcmStorageId storage, const Progress& progress, u64* installed_tid,
                         std::string* error);

}  // namespace pt_fwd
