-- Pacote de conquistas .rubydb (formato 1): um arquivo SQLite por pacote, em
-- /switch/ruby_achievements/achievements/<Title ID>.rubydb, <Title ID>.<variante>.rubydb ou
-- <nome da ROM>.rubydb. Gravado por tools/json_to_rubydb.py (e pelo AchieveSet Studio, que
-- usa o mesmo módulo); lido por common/ach/pt_pack.cpp (app e sysmodule), que recusa
-- user_version diferente de 1.
--
-- Limites (o sysmodule tem 1,5 MB de heap): o texto das conquistas (título, descrição e
-- condição) soma no máximo 64 KB; ícone PNG de até 256 KB e 512x512. O pacote não tem som:
-- o do pop-up é o escolhido no app (sfx/ no SD).
PRAGMA user_version = 1;

-- Metadados do pacote e compatibilidade (uma linha).
CREATE TABLE IF NOT EXISTS pack_info (
    pack_id TEXT PRIMARY KEY,      -- nome do arquivo sem .rubydb
    title TEXT NOT NULL,           -- nome mostrado na lista de pacotes do app
    author TEXT NOT NULL,
    description TEXT,
    target_type TEXT NOT NULL,     -- 'native', 'port', 'emulator'
    target_title_id TEXT,          -- ex.: '01007EF00011E000'
    supported_build_ids TEXT,      -- build ids (hex, prefixo) separados por vírgula; vazio = qualquer
    version TEXT NOT NULL          -- versão do pacote
);

-- Conquistas. A condição é o mesmo objeto JSON dos pacotes antigos (type e parâmetros,
-- inclusive count_of aninhado), avaliado pelo sysmodule; a Ruby tem '{}'. Os BLOBs ficam
-- por último: ler o texto não percorre as páginas dos ícones.
CREATE TABLE IF NOT EXISTS achievements (
    id TEXT PRIMARY KEY,           -- [A-Za-z0-9_.-]; o progresso fica em "L:<chave>:<id>"
    sort_order INTEGER NOT NULL,
    title TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    rarity TEXT NOT NULL DEFAULT 'bronze',  -- bronze, silver, gold ou ruby (uma por pacote)
    secret INTEGER NOT NULL DEFAULT 0,
    condition_json TEXT NOT NULL,
    icon_unlocked BLOB,            -- PNG
    icon_locked BLOB               -- PNG opcional; sem ele o app mostra o desbloqueado apagado
);

