# Patch da QLaunch: jogo em destaque na Home

O HUD do overlay só aparece na Home e mostra o jogo **sob o cursor**. A Home
(QLaunch, `0100000000001000`) não publica esse dado em nenhum serviço, então
um patch na QLaunch precisa gravá-lo para o sysmodule.

## Contrato com o sysmodule (já implementado)

- Arquivo: `sdmc:/switch/ruby_achievements/qlaunch_focus.txt` (`PT_FOCUS_PATH`).
- Conteúdo: o Title ID em hex, uma linha (ex.: `0100152000022000`). `0`, vazio
  ou arquivo ausente = nada em destaque (HUD oculto).
- O sysmodule lê o arquivo a cada segundo enquanto a Home está em primeiro
  plano, busca o total no `ruby_achievements.db` e publica em `status.txt`
  (`is_qlaunch`, `focused_tid`, `focused_name`, `focused_playtime`).

Teste sem o patch: grave um Title ID nesse arquivo pelo PC/FTP, volte à Home e
o HUD mostra o jogo. Apague o arquivo e o HUD some.

## O que o patch precisa fazer

1. Achar, no `main` da QLaunch da **sua** versão do firmware, a função chamada
   quando o foco da grade muda (recebe o índice/entrada focada; a entrada tem o
   `application_id`).
2. Trocar uma instrução dessa função por um `B` para uma *code cave* (área sem
   uso no `.text`) com o trampolim:
   - salva os registradores usados;
   - lê o `application_id` da entrada focada;
   - grava o Title ID no arquivo acima (via as funções de FS que a própria
     QLaunch já importa: `fs::OpenFile`/`WriteFile`, ou o `nn::fs` do SDK);
   - executa a instrução original e volta (`B` para a instrução seguinte).
3. Gerar o IPS com `make_ips.py` e copiar para
   `atmosphere/exefs_patches/ruby_achievements_qlaunch/<BUILD_ID>.ips`.

Os offsets e os bytes **mudam a cada versão da QLaunch** e só saem de
engenharia reversa do binário (dump do `main` com o nxdumptool + Ghidra/IDA).
Este repositório não traz offsets: nenhum valor inventado funcionaria, e um
patch errado na QLaunch impede a Home de abrir (o console continua bootando
no Hekate/Atmosphère; apague a pasta do patch para voltar ao normal).

### Build id

Atmosphère aplica o IPS pelo build id do `main`. Com o `main` extraído:
`hactool -t nso main` (campo *Build Id*), ou os 0x20 bytes em 0x40 do
cabeçalho NSO.

### Gerar o IPS

```
python make_ips.py patch.txt <BUILD_ID> dist
```

`patch.txt`: uma linha por trecho, `<offset hex> <bytes hex>`, com o offset a
partir do início do `.text` (o script soma o cabeçalho NSO de 0x100).

## Alternativa sem IPS

Em vez de um patch que grava em arquivo, o sysmodule pode ler a memória da
QLaunch (`svcDebugActiveProcess` + `svcReadDebugProcessMemory`, base do módulo
por `ldrDmntGetProcessModuleInfo`). Também precisa do endereço do dado por
versão do firmware, e o attach pausa a QLaunch por um instante a cada leitura.
Não está implementado.
