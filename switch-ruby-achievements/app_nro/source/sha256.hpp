#pragma once
// SHA-256 de arquivos (libnx crypto): atualização do app e pacotes da comunidade.
#include <string>

// SHA-256 de um arquivo em hex minúsculo; "" se não leu.
std::string sha256_file(const std::string& path);
