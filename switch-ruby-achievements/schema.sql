-- Ruby Achievements - schema final (SQLite)
-- Fonte única: este arquivo é embutido no sysmodule e no app NRO durante o build.
-- As sessões são a fonte da verdade; games.total_playtime é apenas um cache
-- recalculado a partir de sessions.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_uid TEXT UNIQUE,
    nickname TEXT NOT NULL,
    is_shared INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    sync_id TEXT UNIQUE
);

CREATE TABLE IF NOT EXISTS games (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title_id TEXT,
    display_name TEXT NOT NULL,
    rom_path TEXT,
    source_type TEXT NOT NULL,  -- 'NATIVE', 'FORWARDER', 'EMULATOR_ROM'
    status TEXT NOT NULL DEFAULT 'INSTALLED',  -- 'INSTALLED' ou 'MERGED:<id destino>'
    cover_path TEXT,
    first_played INTEGER,
    last_played INTEGER,
    total_playtime INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    sync_id TEXT UNIQUE,
    last_synced_at INTEGER
);

CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    game_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    start_time INTEGER NOT NULL,
    end_time INTEGER,           -- NULL = sessão em andamento ou "ghost" (interrompida)
    duration INTEGER NOT NULL DEFAULT 0,  -- segundos efetivamente contados (sem sleep)
    created_at INTEGER NOT NULL,
    sync_id TEXT UNIQUE,
    synced INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (game_id) REFERENCES games(id),
    FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Sessões importadas do NX Activity Log (sync_id "nxal:...") que o usuário
-- excluiu no app: a importação seguinte não as recria e o tempo delas conta
-- como já registrado (o total do jogo fica como o usuário deixou).
CREATE TABLE IF NOT EXISTS import_deleted (
    sync_id TEXT PRIMARY KEY,
    duration INTEGER NOT NULL
);

-- Tracking de tempo por jogo. disabled = 1: o sysmodule não grava sessões, o
-- jogo some da tela inicial e aparece em "Apps e jogos desativados";
-- disabled = 0: o usuário reativou (o filtro automático não desativa de novo).
-- reason: 'user' (pelo app) ou 'auto' (homebrew utilitário: Sphaira, Ruby Achievements).
CREATE TABLE IF NOT EXISTS tracking_disabled (
    game_id INTEGER PRIMARY KEY,
    disabled INTEGER NOT NULL DEFAULT 1,
    reason TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);

-- Conquistas. Definições locais (PT_ACH_DIR/<chave>.json, avaliadas pelo
-- sysmodule) têm id 'L:<chave>:<id>'; as espelhadas do RetroAchievements (pelo
-- app), 'RA:<id>'. game_id segue as unificações (pt_db_merge_games).
-- Sem pontos (removidos em 2026-10-20): bancos criados antes guardam uma coluna
-- `points` com o valor padrão, que ninguém lê nem grava.
CREATE TABLE IF NOT EXISTS achievements (
    id TEXT PRIMARY KEY,
    game_id INTEGER NOT NULL,
    source TEXT NOT NULL,       -- 'LOCAL' ou 'RA'
    title TEXT NOT NULL,
    description TEXT,
    icon_url TEXT,
    icon_local_path TEXT,
    is_secret INTEGER NOT NULL DEFAULT 0,
    sort_order INTEGER NOT NULL DEFAULT 0,
    rarity TEXT,                -- 'bronze' (NULL = bronze), 'silver', 'gold' ou 'ruby' (platina)
    FOREIGN KEY (game_id) REFERENCES games(id)
);

-- Desbloqueio por perfil do Ruby Achievements (users.id).
CREATE TABLE IF NOT EXISTS user_achievements (
    achievement_id TEXT NOT NULL,
    user_id INTEGER NOT NULL,
    unlocked_at INTEGER NOT NULL,
    PRIMARY KEY (achievement_id, user_id),
    FOREIGN KEY (achievement_id) REFERENCES achievements(id),
    FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Pop-ups pendentes para o overlay (o sysmodule exporta em PT_POPUP_PATH).
CREATE TABLE IF NOT EXISTS achievement_queue (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    achievement_id TEXT NOT NULL,
    user_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL
);

-- MD5 das ROMs (RetroAchievements): recalculado só se tamanho ou data mudarem.
CREATE TABLE IF NOT EXISTS rom_hash_cache (
    file_path TEXT PRIMARY KEY,
    file_size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    md5_hash TEXT NOT NULL
);

-- Build id do executável principal de cada título (64 hex), gravado pelo sysmodule quando o
-- jogo abre (ldr:dmnt). O app compara com supported_build_ids dos pacotes da comunidade.
CREATE TABLE IF NOT EXISTS title_builds (
    title_id TEXT PRIMARY KEY,  -- 16 hex maiúsculos
    build_id TEXT NOT NULL,
    seen_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_achievements_game ON achievements(game_id);
CREATE INDEX IF NOT EXISTS idx_games_title_id ON games(title_id);
CREATE INDEX IF NOT EXISTS idx_games_rom_path ON games(rom_path);
CREATE INDEX IF NOT EXISTS idx_sessions_game_user ON sessions(game_id, user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_open ON sessions(end_time) WHERE end_time IS NULL;
