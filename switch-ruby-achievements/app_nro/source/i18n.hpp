#pragma once
// Idioma e formato de data do app (aba Interface das Configurações globais).
//
// Os textos do código estão em português e são a chave da tradução: tr("Voltar")
// devolve "Back" em inglês e o próprio texto em português. A tabela fica em
// i18n_en.inc. Texto com valores: trf("%d sessões", n) traduz o formato e depois
// preenche (a tradução precisa ter os mesmos %d/%s na mesma ordem; o i18n_check
// do PC confere isso e que toda chave usada no código está na tabela).
//
// Sem Borealis nem config.ini: testável no PC. Quem grava a escolha é prefs.cpp.
#include <stdint.h>

#include <string>

namespace i18n {

enum Lang { LANG_EN = 0, LANG_PT = 1, LANG_COUNT };
enum DateFormat { DATE_MDY = 0, DATE_DMY = 1, DATE_YMD = 2, DATE_COUNT };

int language();
void set_language(int lang);  // fora do intervalo = inglês
int date_format();
void set_date_format(int fmt);  // fora do intervalo = MM/DD/YYYY

// Texto `pt` no idioma atual. Chave sem tradução devolve o próprio texto.
const char* tr(const char* pt);
// tr() do formato e depois printf com os argumentos.
std::string trf(const char* pt_fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// Tradução existe? (para os testes)
bool has_translation(const char* pt);

// "MM/DD/YYYY HH:MM" (ou a ordem escolhida) a partir dos campos do calendário.
std::string format_datetime(int year, int month, int day, int hour, int minute, int fmt);
// Só a data, sem a hora.
std::string format_day(int year, int month, int day, int fmt);

}  // namespace i18n

using i18n::tr;
using i18n::trf;
