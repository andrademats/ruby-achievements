# Ruby Achievements

[English](README.md) · **Português**

Ruby Achievements é um conjunto homebrew para o Nintendo Switch que adiciona contagem de tempo de
jogo e conquistas a qualquer jogo. Um sysmodule em segundo plano conta o tempo e confere as
condições das conquistas (save, tempo de jogo ou memória do jogo), um overlay mostra o jogo aberto e
o pop-up de "Conquista desbloqueada", e um app mostra sua biblioteca, o tempo por perfil e os
troféus.

Funciona offline. A internet só é usada quando você pede: capas do SteamGridDB, sincronização com
o RetroAchievements e a aba Atualização.

## Instalação no Switch

Requer Atmosphère. O overlay também precisa do nx-ovlloader com o Tesla Menu ou o Ultrahand.

1. Baixe a [release](https://github.com/andrademats/ruby-achievements/releases) mais recente ou
   compile o `dist/` (abaixo).
2. Copie o conteúdo de `dist/` para a raiz do cartão SD.
3. Reinicie o console e abra o **Ruby Achievements** pelo Homebrew Menu.

## Compilação

Requer o [devkitPro](https://devkitpro.org/wiki/Getting_Started). No Windows, use o shell MSYS2 do
devkitPro.

```sh
sudo dkp-pacman -S switch-dev switch-cmake switch-pkg-config switch-glm deko3d uam
git clone --recursive https://github.com/andrademats/ruby-achievements.git
cd ruby-achievements/switch-ruby-achievements
make          # sysmodule, overlay e app (aplica antes os patches das bibliotecas)
make dist     # dist/ com a estrutura do cartão SD
make test     # testes no PC
```

## AchieveSet Studio

Os pacotes de conquistas são feitos no **AchieveSet Studio**, um editor para PC que tem
repositório próprio:
<!-- TODO: trocar pelo link definitivo -->
[github.com/andrademats/achieveset-studio](https://github.com/andrademats/achieveset-studio).

## Licença

O app inclui código adaptado do [Sphaira](https://github.com/ITotalJustice/sphaira) e por isso é
distribuído sob a GPL-3.0. Outros códigos de terceiros: nx-hbloader (ISC) e
[Monocypher](https://monocypher.org) (BSD-2 ou CC-0). As licenças ficam junto do código em
`switch-ruby-achievements/app_nro/`.
