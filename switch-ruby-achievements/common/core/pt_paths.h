#pragma once

// Caminhos no cartão SD compartilhados por sysmodule, overlay e app NRO.
// (O prefixo pt_/PT_ do código vem do nome antigo, Playtime; os caminhos são os do
// Ruby Achievements.)
#define PT_DIR          "sdmc:/switch/ruby_achievements"
#define PT_DB_PATH      PT_DIR "/ruby_achievements.db" // aberto pela VFS "switch" (pt_vfs)
#define PT_LOCK_DIR     PT_DIR "/db.lock"          // diretório = lock entre processos
#define PT_LOCK_STAMP   PT_LOCK_DIR "/owner"
#define PT_STATUS_PATH  PT_DIR "/status.txt"       // sysmodule -> overlay/app
#define PT_USERS_PATH   PT_DIR "/users.txt"        // sysmodule -> overlay
#define PT_CONFIG_PATH  PT_DIR "/config.ini"       // opções (app, overlay) -> sysmodule
#define PT_SECRETS_PATH PT_DIR "/secrets.ini"      // chaves de API (só o app grava; ver pt_config.h)
#define PT_ICONS_DIR    PT_DIR "/icons"            // ícones extraídos do NACP
#define PT_GHOSTS_DIR   PT_DIR "/ghosts"           // pacotes de forwarder fantasma
#define PT_GHOST_TIDS_PATH PT_DIR "/ghost_tids.txt" // forwarders fantasmas: o sysmodule ignora
#define PT_APP_NRO      PT_DIR "/ruby_achievements.nro" // app (alvo do forwarder fantasma)
#define PT_OVERLAY_PATH "sdmc:/switch/.overlays/ruby-achievements-overlay.ovl"
#define PT_MIGRATION_LOG_PATH PT_DIR "/migration.log"   // pt_migrate_legacy (pt_util)
// Nome antigo (Switch Playtime, até 2026-10-22): pt_ensure_dirs move o conteúdo da pasta
// antiga para PT_DIR e remove os binários velhos (ver pt_migrate_legacy).
#define PT_LEGACY_DIR   "sdmc:/switch/playtime"
#define PT_LEGACY_DB_NAME "playtime.db"
#define PT_LEGACY_NRO_NAME "playtime.nro"
#define PT_LEGACY_OVERLAY_PATH "sdmc:/switch/.overlays/playtime-overlay.ovl"
#define PT_COVERS_DIR   PT_DIR "/covers"           // capas manuais do usuário
// Jogo sob o cursor na Home: Title ID em hex (uma linha), gravado pelo patch da
// QLaunch (ver qlaunch_patch/). Arquivo ausente ou 0 = nenhum jogo em destaque.
#define PT_FOCUS_PATH   PT_DIR "/qlaunch_focus.txt"
#define PT_ACH_DIR      PT_DIR "/achievements"     // pacotes <TID>.rubydb, <TID>.<variante>.rubydb, <rom>.rubydb
// (os pacotes da comunidade entram como variante: <TID>.<pack_id>.rubydb)
#define PT_ACH_ICONS_DIR PT_ACH_DIR "/icons"       // ícones baixados (RetroAchievements)
// Pacote ativo de cada jogo, escolhido no app: "<chave>|<arquivo>" por linha (ver pt_pack.h).
#define PT_ACH_ACTIVE_PATH PT_ACH_DIR "/active_packs.txt"
#define PT_ACH_CACHE_DIR PT_DIR "/cache"           // cópias espelho de saves do console (conquistas)
// Catálogo de pacotes da comunidade (app/community.hpp): cópia do último index.json baixado.
#define PT_COMMUNITY_INDEX_PATH PT_ACH_CACHE_DIR "/index.json"
#define PT_POPUP_PATH   PT_DIR "/achievement_popup.txt"  // sysmodule -> overlay: "id|raridade|ícone|título"
// Ícones dos pop-ups tirados dos pacotes .rubydb pelo sysmodule (o overlay não tem
// SQLite): q<id da fila>.png; apagados quando saem da fila.
#define PT_POPUP_CACHE_DIR PT_ACH_CACHE_DIR "/popup"
// Linhas no máximo em PT_POPUP_PATH (as mais antigas da fila). Precisa passar do limite
// da proteção contra flood do overlay (popup::kFloodLimit), para ele ver a fila longa.
#define PT_POPUP_QUEUE_MAX 16
#define PT_POPUP_SEEN_PATH PT_DIR "/achievement_seen.txt" // overlay -> sysmodule: último id exibido
#define PT_SFX_DIR      PT_DIR "/sfx"                    // sons do pop-up (o escolhido no app: pt_wav.hpp)
#define PT_POPUP_SFX_PATH PT_SFX_DIR "/conquista.wav"    // som padrão do pop-up (ver sfx/LEIA-ME.txt)
#define PT_ACH_CURRENT_PATH PT_DIR "/achievement_current.txt" // sysmodule -> overlay: conquistas do jogo aberto
// Atualização pelo app (pt_update.h): arquivos baixados, backup da versão anterior, diário da
// troca (enquanto existe, a troca não terminou) e a marca "reinicie o console".
#define PT_UPDATE_DIR          PT_DIR "/update"
#define PT_UPDATE_STAGING_DIR  PT_UPDATE_DIR "/staging"
#define PT_UPDATE_PACKAGE_PATH PT_UPDATE_STAGING_DIR "/package.zip" // zip da release, apagado depois de extrair
#define PT_UPDATE_BACKUP_DIR   PT_UPDATE_DIR "/backup"
#define PT_UPDATE_JOURNAL_PATH PT_UPDATE_DIR "/journal.txt"
#define PT_UPDATE_PENDING_PATH PT_UPDATE_DIR "/pending_reboot.txt"
#define PT_UPDATE_LOG_PATH     PT_DIR "/update.log"
#define PT_UPDATE_DB_BACKUP_PATH PT_UPDATE_DIR "/ruby_achievements.db.bak" // banco antes da última atualização
#define PT_LOG_PATH     PT_DIR "/sysmodule.log"
#define PT_OVERLAY_LOG_PATH PT_DIR "/overlay.log"
// Marca gravada antes de ler o NACP de um título; se sobrar após um reboot, o
// console travou nessa leitura e o título vai para PT_NACP_SKIP_PATH.
#define PT_PROBE_PATH   PT_DIR "/nacp_probe.txt"
#define PT_NACP_SKIP_PATH PT_DIR "/nacp_skip.txt"   // um Title ID (hex) por linha
