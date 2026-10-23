# Ruby Achievements

**English** · [Português](README.pt-BR.md)

Ruby Achievements is a homebrew suite for the Nintendo Switch that adds playtime tracking and
achievements to any game. A background sysmodule counts playtime and checks achievement conditions
(save data, playtime or game memory), an overlay shows the running game and the "Achievement
unlocked" pop-up, and an app shows your library, playtime per profile and trophies.

It works offline. The internet is only used when you ask for it: SteamGridDB covers,
RetroAchievements sync and the Update tab.

## Install on the Switch

Requires Atmosphère. The overlay also needs nx-ovlloader with Tesla Menu or Ultrahand.

1. Download the latest [release](https://github.com/andrademats/ruby-achievements/releases), or
   build `dist/` yourself (below).
2. Copy the contents of `dist/` to the root of the SD card.
3. Reboot the console, then open **Ruby Achievements** from the Homebrew Menu.

## Build

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started). On Windows, use the devkitPro
MSYS2 shell.

```sh
sudo dkp-pacman -S switch-dev switch-cmake switch-pkg-config switch-glm deko3d uam
git clone --recursive https://github.com/andrademats/ruby-achievements.git
cd ruby-achievements/switch-ruby-achievements
make          # sysmodule, overlay and app (applies the library patches first)
make dist     # dist/ with the SD card layout
make test     # tests on the PC
```

## AchieveSet Studio

Achievement packs are made with **AchieveSet Studio**, a PC editor that has its own repository:
<!-- TODO: replace with the final link -->
[github.com/andrademats/achieveset-studio](https://github.com/andrademats/achieveset-studio).

## License

The app includes code adapted from [Sphaira](https://github.com/ITotalJustice/sphaira) and is
therefore distributed under GPL-3.0. Other third-party code: nx-hbloader (ISC) and
[Monocypher](https://monocypher.org) (BSD-2 or CC-0). Their license files are next to the code in
`switch-ruby-achievements/app_nro/`.
