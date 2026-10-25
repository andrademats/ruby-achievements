# Ruby Achievements / AchieveSet — standing instructions for AI agents

**English** · [Português](CLAUDE.md)

This is a translation for readers. Claude Code reads `CLAUDE.md` (Portuguese), which is the
authoritative version.

## At the start of every session

1. Read `PROJECT_STATE.md` (current state, where things were tested, open items).
2. Run `memory_briefing` with `workspace="default"`, `project="playtimeswitch"` and
   `settled_first=true` (the scope comes from `.ai-memory.toml`, a local file outside Git; pass both
   on every project call). The id `playtimeswitch` is the old name and does not change with the
   rebranding (it would split the memory).
3. If there is a handoff in the context, continue from it. `ARCHITECTURE.md` has the technical map.

## Rules

- **Language:** answers and documents in Brazilian Portuguese. Translations exist only for the
  README (`README.md` in English, GitHub's main one, and `README.pt-BR.md`) and for this file
  (`CLAUDE.en.md`). When changing one of them, update the other version in the same task.
- **Outside Git (local only):** `PROJECT_STATE.md`, `ARCHITECTURE.md`, `prints/` and
  `achieveset_studio/`, which has its own repository (Studio commits go there, never here).
- **Keys and dumps:** never commit, copy into the project, embed in packages or ask for the content
  of `prod.keys`/`title.keys`, NSPs, NSZs or a game's `main`. AchieveSet Studio only receives the
  *path* to the user's own keys. Do not decrypt game content without the user's explicit permission.
- **`.rubydb` packs:** the only format read on the console is the SQLite `.rubydb` (schema in
  `switch-ruby-achievements/rubydb_schema.sql`; written by `tools/json_to_rubydb.py` and by Studio).
  The achievement text (title, description and condition) is at most **64 KB per pack**
  (above that the whole pack is refused). Sysmodule heap: 1.5 MB; each condition costs ~1.5 KB during
  evaluation; above ~300 conditions, cut down. PNG icon up to 256 KB and 512×512. Packs have no category
  and no sound: the pop-up sound is only the global one, picked in the app (`sfx/`, rules in `pt_wav.hpp`).
  Community packs (`index.json` catalog, `app_nro/source/community.hpp`) are installed as the variant
  `<TID>.<pack_id>.rubydb`; the catalog entry comes from `tools/rubydb_index.py`.
- **gdt::Manager (BOTW):** the chain is `[[[[main+sInstance]+mFlagBuffer]+array+8]+0]`; from the
  first flag object on, the search is by the CRC32 hash of the name in a window of up to 4 MB; the
  value sits `offset_after_flag` bytes from the hash (bool `-1`, s32 `-4`, f32 `-4`). Per-build
  values in `switch-ruby-achievements/tools/botw_ram_teste/LEIA-ME.md`; re-analyze with
  `tools/find_botw_ram_offsets.py`.
- **Generic pointers (`ram_pointer_chain`):** EdiZon/Cheat Engine notation `[[main+A]+B]+C`; every
  offset except the last is read as a pointer.
- **Build ID lock:** every pack with a RAM condition carries `supported_build_ids` (and
  `target_title_id`) in `pack_info`; in the source JSON, `build_id` and `title_id`. RAM offsets only
  hold for the executable they were found in.
- **Where it was tested:** always distinguish "tested on the console", "PC only" and "deduced". Do
  not claim something works on the console without a console log.

## Build and tests

- Only in devkitPro's MSYS2, run from the project root through the Bash tool (`$PWD` is expanded
  before entering MSYS2, whose profile changes directory):
  `C:/devkitPro/msys2/usr/bin/bash.exe -lc "export DEVKITPRO=/opt/devkitpro; cd '$PWD/switch-ruby-achievements' && make dist"`.
  New `.cpp` file in the app: touch `app_nro/CMakeLists.txt` (CMake uses a glob).
- Scripted edits: do not use heredocs with quotes/backslashes; write the `.py` with the Write tool
  and run `python -I`, with `newline=''`.
- PC tests: `make test` (same command as the build, replacing `make dist` with `make test`; or
  `ONLY="cfg_test update_test" make test`). They live in `switch-ruby-achievements/tests/` (shims in
  `tests/shim/`); every new test goes into `tests/run_tests.sh`. Sample data in tests has no real
  names, UIDs or paths. Studio: run the GUI with `QT_QPA_PLATFORM=offscreen`.
- Releases: `tools/release_manifest.py` (private key outside the repository; see the README).

## Persistence protocol (requested by the user, applies without further notice)

- **New decision or fixed bug:** record it in ai-memory (`decisions/` or `gotchas/`, short ADR,
  settled decisions) and update `PROJECT_STATE.md`.
- **Finished feature or changed code:** update `PROJECT_STATE.md` and the `state/current_state.md`
  page in ai-memory.
- **End of a long turn or change of subject:** `memory_handoff_begin` with the exact point where we
  stopped (cancel the previous handoff that was left behind).
- One page per subject, always at the same path; values that live in the code are cited by file, not
  copied; stale pages get `memory_feedback` (stale/wrong) and are fixed.
