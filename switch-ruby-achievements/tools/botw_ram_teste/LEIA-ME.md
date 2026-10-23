# Teste de conquistas por RAM no BOTW (v1114112)

`01007EF00011E000.json` = 58 conquistas lidas só da memória com o jogo aberto, mais a Ruby
(desbloqueada sozinha quando as 58 estiverem desbloqueadas)
(`ram_flag_search`, IDs `rt_...`), sem conquistas de save nem de tempo de jogo.
Ele substitui o arquivo de mesmo nome em `/switch/ruby_achievements/achievements/` no
SD. Enquanto ele estiver lá, as conquistas de save e de tempo do pacote normal
não são avaliadas. Para voltar, copie de novo o arquivo de `dist/`.

As 115 flags usadas foram conferidas no decomp (`gdt.cpp`): o nome existe e o
tipo (bool, s32 ou f32) é o da cadeia usada. Cobrem runas, asa-delta, câmera,
torres, santuários, Ganons e Bestas Divinas, poderes dos campeões, Master
Sword, Escudo Hyliano, lembranças, corações (`MaxHartValue`, em quartos de
coração), stamina (`StaminaMax`, f32, 1000 por roda), Guardiões, Hinox, Talus,
Molduga, Lynel, sementes Korok, pratos, baús de santuário, armaduras e o fim
do jogo.

O arquivo é compacto (uma conquista por linha) porque o sysmodule lê no
máximo 64 KB por jogo. Por isso pratos, baús e armaduras usam só parte das
flags (15 pratos, 15 santuários, 4 conjuntos). O jogo não tem contador total
de refeições, baús ou guardiões: as conquistas contam flags diferentes, e os
guardiões contam por tipo.

## Cadeia de ponteiros (BOTW v1114112, build id CD57B23FA4BBAD65803D9788C01821EE)

Achada por `tools/find_botw_ram_offsets.py` no `main` tirado do console pelo
DBI. O módulo tem 31328 KB (0x1E98000), o mesmo tamanho que o sysmodule
registrou no log com o jogo rodando. Cada passo é conferido no código, e o
script para com erro se algum padrão não bater:

| Passo | Offset | De onde veio |
|---|---|---|
| `main + 0x1E3B188` | `gdt::Manager::sInstance` | `createInstance` (`new` de 0xDC8 bytes em main+0x112AA54), lido e gravado direto (ADRP+LDR/STR) |
| `Manager + 0xC00` | `mFlagBuffer` (TriggerParam*) | `allocParam1` grava o buffer novo em +0xBF8 (`mFlagBuffer1`) e copia de +0xC00 |
| `TriggerParam + 0x10` | buffer de `mBoolFlags` | construtor (main+0x11323C0) e cópia (main+0x1134BE0): `mBoolFlags` em +0x8, `PtrArray` guarda o buffer em +8 |
| `TriggerParam + 0x20` | buffer de `mS32Flags` | o array seguinte, em +0x18 |
| `+ 0x0` | primeiro objeto de flag | início da janela de busca |

No JSON: `["0x1E3B188", "0xC00", "0x10", "0x0"]` para bool,
`["0x1E3B188", "0xC00", "0x20", "0x0"]` para s32 e
`["0x1E3B188", "0xC00", "0x30", "0x0"]` para f32 (`mF32Flags` em +0x28; a
comparação usa os bits do float, que para números positivos têm a mesma ordem).

Outra build (o `main` de 34,7 MB, build id 2094F152...) dava `0x22DF9E8`, que
fica fora do módulo desta. Se o log mostrar `cadeia parou no offset 1 ...
falhou (rc=0x...)`, confira se o tamanho do "modulo principal" no log
bate com o do `main` analisado; se bater, o rc diz por que o dmnt:cht
recusou a leitura.

Layout dos objetos (decomp zeldaret/botw, `gdtFlag.h`; o tamanho 0x18 do
`FlagBool` aparece na cópia da 1.6.0):

- `FlagBool` (0x18 bytes): valor no byte 0x0B (bit 0), hash CRC32 do nome em
  0x0C. Por isso o arquivo usa `offset_after_flag: -1`, `size: 1` e
  `bit_check 1`.
- `FlagS32` (0x28 bytes): valor em 0x18, hash em 0x1C, ou seja
  `offset_after_flag: -4`.

**Ainda não confirmado no console:** que todos os objetos fiquem dentro da
janela de 4 MB (`search_size`) a partir do primeiro. Se o log mostrar
`hash ... nao encontrado na janela`, os objetos estão espalhados no heap e a
janela precisa crescer (máximo 0x400000) ou a busca precisa seguir o array de
ponteiros. Se a versão do jogo não for a 1.6.0, os offsets não valem: rode o
script de novo com o `main` daquela versão.

## Conferir ou achar os ponteiros à mão (EdiZon-SE ou Breeze, com o jogo aberto)

Hashes CRC32 a procurar (u32, little-endian):

| Flag | Hash |
|---|---|
| `IsGet_PlayerStole2` | `0xFE4D1501` |
| `MapTower_01` | `0x7BF47AB8` |
| `DungeonClearCounter` | `0xE605CE62` |

1. Procure `0xFE4D1501` no heap. O objeto `FlagBool` começa 0x0C bytes antes do
   resultado; o byte logo antes do hash é o valor (bit 0 = asa-delta obtida).
2. Procure quem aponta para o início desse objeto: é a entrada dele no array
   de ponteiros de `mBoolFlags`. O primeiro item desse array aponta para o
   primeiro objeto, que é o início da janela.
3. Com a busca de ponteiros (pointer search), suba até um endereço
   `main+X`. A cadeia que sair, por exemplo `[[[main+X]+A]+B]`, preenche
   `pointer_offsets` como `["0xX", "0xA", "0xB", "0x0"]`.

## O que o sysmodule escreve no log

Uma linha por conquista sempre que o resultado muda:

- `ram: memoria de 01007EF00011E000 via dmnt:cht (modulo principal 0x..., N KB)`
- `ram: <id>: cadeia parou no offset 2 (0x...): leitura de main+0x... deu ponteiro nulo`
  (ou `falhou (rc=0x...)`: o dmnt:cht recusou a leitura, por endereço fora do jogo ou outro motivo dado pelo rc): o offset indicado está errado, ou o jogo ainda está carregando;
- `ram: <id>: hash 0xFE4D1501 nao encontrado na janela 0x... (+0x400000 bytes)`:
  a cadeia chegou a algum lugar, mas não às flags;
- `ram: <id>: flag lida com sucesso: valor 3 (0x3) no hash 0xFE4D1501 em 0x... -> cumprida`
  (o valor mostrado é o byte inteiro; só o bit 0 conta).
