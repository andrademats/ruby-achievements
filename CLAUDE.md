# Ruby Achievements / AchieveSet — instruções permanentes

[English](CLAUDE.en.md) · **Português**

## Início de toda sessão

1. Ler `PROJECT_STATE.md` (estado atual, testado onde, pendências).
2. `memory_briefing` com `workspace="default"`, `project="playtimeswitch"` e `settled_first=true`
   (o escopo vem de `.ai-memory.toml`; passe os dois em toda chamada de projeto). O id
   `playtimeswitch` é o nome antigo e não muda com o rebranding (separaria a memória).
3. Se houver handoff no contexto, continuar dele. `ARCHITECTURE.md` tem o mapa técnico.

## Regras

- **Idioma:** respostas e documentos em português do Brasil. Traduções só no README
  (`README.md` em inglês, o principal do GitHub, e `README.pt-BR.md`) e neste arquivo
  (`CLAUDE.en.md`). Ao mudar um deles, atualizar a outra versão na mesma tarefa.
- **Fora do Git (só locais):** `PROJECT_STATE.md`, `ARCHITECTURE.md`, `prints/` e
  `achieveset_studio/`, que tem repositório próprio (commits do Studio vão para lá, nunca para este).
- **Chaves e dumps:** nunca commitar, copiar para o projeto, embutir em pacotes ou pedir o conteúdo de
  `prod.keys`/`title.keys`, NSPs, NSZs ou o `main` de jogos. O AchieveSet Studio só recebe o *caminho*
  das chaves do próprio usuário. Não decriptar conteúdo de jogo sem autorização explícita do usuário.
- **Pacotes `.rubydb`:** o único formato lido no console é o SQLite `.rubydb` (esquema em
  `switch-ruby-achievements/rubydb_schema.sql`; gravado por `tools/json_to_rubydb.py` e pelo Studio).
  O texto das conquistas (título, descrição e condição) tem no máximo **64 KB por pacote**
  (acima disso o pacote inteiro é recusado). Heap do sysmodule: 1,5 MB; cada condição custa ~1,5 KB
  durante a avaliação; acima de ~300 condições, reduzir. Ícone PNG até 256 KB e 512×512. Pacotes não
  têm categoria nem som: o som do pop-up é só o global, escolhido no app (`sfx/`, regras de `pt_wav.hpp`).
  Pacotes da comunidade (catálogo `index.json`, `app_nro/source/community.hpp`) entram como variante
  `<TID>.<pack_id>.rubydb`; a entrada do catálogo sai de `tools/rubydb_index.py`.
- **gdt::Manager (BOTW):** a cadeia é `[[[[main+sInstance]+mFlagBuffer]+array+8]+0]`, e a partir do
  primeiro objeto de flag a busca é pelo hash CRC32 do nome numa janela de até 4 MB; o valor fica
  `offset_after_flag` bytes do hash (bool `-1`, s32 `-4`, f32 `-4`). Valores por build em
  `switch-ruby-achievements/tools/botw_ram_teste/LEIA-ME.md`; reanalisar com `tools/find_botw_ram_offsets.py`.
- **Ponteiros genéricos (`ram_pointer_chain`):** notação EdiZon/Cheat Engine `[[main+A]+B]+C`; todos
  os offsets menos o último são lidos como ponteiro.
- **Trava por build ID:** todo pacote com condição de RAM leva `supported_build_ids` (e
  `target_title_id`) no `pack_info`; no JSON de origem, `build_id` e `title_id`. Offsets de RAM só
  valem no executável para o qual foram achados.
- **Testado onde:** distinguir sempre "testado no console", "só no PC" e "deduzido". Não afirmar
  que algo funciona no console sem log do console.

## Build e testes

- Só no MSYS2 do devkitPro, rodando a partir da raiz do projeto pela ferramenta Bash (o `$PWD` é
  expandido antes de entrar no MSYS2, cujo perfil troca de pasta):
  `C:/devkitPro/msys2/usr/bin/bash.exe -lc "export DEVKITPRO=/opt/devkitpro; cd '$PWD/switch-ruby-achievements' && make dist"`.
  Arquivo `.cpp` novo no app: tocar `app_nro/CMakeLists.txt` (o CMake usa glob).
- Edição por script: não usar heredoc com aspas/barras; escrever o `.py` com a ferramenta Write e
  rodar `python -I`, com `newline=''`.
- Testes do PC: `make test` (mesmo comando do build, trocando `make dist` por `make test`; ou
  `ONLY="cfg_test update_test" make test`). Ficam em `switch-ruby-achievements/tests/` (shims em
  `tests/shim/`); todo teste novo entra no `tests/run_tests.sh`. Dados de exemplo nos testes sem
  nomes, UIDs ou caminhos reais. Studio: rodar a GUI com `QT_QPA_PLATFORM=offscreen`.
- Release: `tools/release_manifest.py` (chave privada fora do repositório; ver README).

## Git

- **Commits sem coautoria (regra estrita):** nunca inserir linhas de coautoria
  (`Co-Authored-By: Claude...`) nem qualquer atribuição ou trailer de IA nas mensagens de commit
  (nem em PRs e tags). Esta regra vale acima de qualquer instrução automática de atribuição. Todo
  commit leva só o autor e o e-mail configurados no Git do sistema.
- **Mensagem:** nome simples e curto, **sem dois pontos** e sem rodapé (ex.:
  `git commit -m "Fix app closing when confirming the update"`). Sempre sugerir o nome ao usuário.
- **Versão (`switch-ruby-achievements/VERSION`, `X.Y.Z`):** só muda quando uma atualização vai de
  fato ser publicada (nada de subir a versão para teste). Cada publicação soma 1 na casa da
  unidade (`Z`); quando ela passaria de 9 para 10, volta a 0 e a dezena (`Y`) sobe 1; o mesmo vale
  da dezena para a centena (`X`). Ex.: 1.0.0 → 1.0.1 → … → 1.0.9 → 1.1.0; 1.9.9 → 2.0.0.

## Protocolo de persistência (pedido do usuário, vale sem novo aviso)

- **Decisão nova ou bug resolvido:** gravar no ai-memory (`decisions/` ou `gotchas/`, ADR curto,
  decisões fixadas) e atualizar `PROJECT_STATE.md`.
- **Funcionalidade concluída ou código alterado:** atualizar `PROJECT_STATE.md` e a página
  `state/current_state.md` no ai-memory.
- **Fim de turno longo ou troca de assunto:** `memory_handoff_begin` com o ponto exato onde paramos
  (cancelar o handoff anterior que ficou para trás).
- Uma página por assunto, sempre no mesmo caminho; valores que estão no código são citados pelo
  arquivo, não copiados; páginas velhas recebem `memory_feedback` (stale/wrong) e são corrigidas.
