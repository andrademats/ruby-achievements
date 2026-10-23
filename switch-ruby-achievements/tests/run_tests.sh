#!/bin/bash
# Testes de PC do sysmodule, do app, do overlay e do common, com os serviços do Switch simulados
# (shim/). Rodar com `make test` na pasta switch-ruby-achievements, no bash do MSYS2 do devkitPro
# (usa o g++ do MSYS2). Saída: um bloco por teste e o resumo no fim; código 1 se algo falhar.
#
# Variáveis: PYTHON (caminho do Python 3, para o i18n_check), ONLY (nomes dos testes a rodar,
# separados por espaço; vazio = todos).
set -u

T="$(cd "$(dirname "$0")" && pwd)"
R="$(dirname "$T")"
B="$T/build"
mkdir -p "$B"
cd "$B" || exit 1
# Resultados antigos saem; o SQLite compilado fica em cache (é o passo mais lento).
find "$B" -mindepth 1 -maxdepth 1 ! -name sqlite_other.o -exec rm -rf {} +

mkdir -p gen
{ printf 'R"PTSQL('; cat "$R/schema.sql"; printf ')PTSQL"\n'; } > gen/schema_sql.inc
cp "$T/hash_ref.txt" .

if [ ! -f sqlite_other.o ] || [ "$R/common/sqlite/sqlite3.c" -nt sqlite_other.o ] ||
   [ "$R/common/sqlite/pt_sqlite_config.h" -nt sqlite_other.o ]; then
    echo "compilando o SQLite (só na 1ª vez)..."
    gcc -O1 -w -c "$R/common/sqlite/sqlite3.c" -I"$R/common/sqlite" \
        -DSQLITE_CUSTOM_INCLUDE=pt_sqlite_config.h -o sqlite_other.o || exit 1
fi

PASS=0
FAILS=()

selected() { [ -z "${ONLY:-}" ] || [[ " $ONLY " == *" $1 "* ]]; }

# build <nome> <args do g++...>: compila <nome>.exe; erro de compilação conta como falha.
build() {
    local name=$1; shift
    if ! g++ "$@" -o "$name.exe" > "$name.build.log" 2>&1; then
        echo "== $name: ERRO DE COMPILAÇÃO"
        grep -E "error|undefined" "$name.build.log" | head -20
        FAILS+=("$name (compilação)")
        return 1
    fi
}

# run <nome> <pasta de trabalho> <comando...>: roda na pasta (criada vazia) e confere o código.
run() {
    local name=$1 dir=$2; shift 2
    echo "== $name"
    mkdir -p "$dir"
    (cd "$dir" && "$@")
    local rc=$?
    if [ $rc -ne 0 ]; then FAILS+=("$name (saída $rc)"); else PASS=$((PASS + 1)); fi
}

SD=sdmc:/switch/ruby_achievements
BASE=(-O1 -w -std=gnu++20 "-DPT_REPO=\"$R\"" -I"$T/shim" -I"$B/gen" -I"$R/common/core" -I"$R/common/db"
      -I"$R/common/ach" -I"$R/common/sqlite" -include "$T/shim/switch.h")
DB=("$R/common/core/pt_util.cpp" "$R/common/core/pt_log.cpp" "$R/common/core/pt_update.cpp"
    "$R/common/db/pt_db.cpp" "$R/common/db/pt_vfs.cpp" "$B/sqlite_other.o" "$T/lock_stub.cpp")
CORE=("$R/common/core/pt_util.cpp" "$R/common/core/pt_log.cpp" "$R/common/core/pt_update.cpp" "$T/lock_stub.cpp")
PACK=("$R/common/ach/mjson.cpp" "$R/common/ach/pt_pack.cpp")
S=$R/sysmodule/source
A=$R/app_nro/source

# Conquistas do sysmodule (achieve.cpp)
for t in botw_test mirror_test hash_test ach_test ram_test ruby_test; do
    selected $t || continue
    build $t "${BASE[@]}" -I"$S" -include "$T/shim/save_shim.h" "$T/$t.cpp" "${PACK[@]}" "$T/ram_fake.cpp" \
        "${DB[@]}" || continue
    if [ $t = ruby_test ]; then
        run $t run_$t sh -c "../$t.exe prep && ../$t.exe"
    else
        run $t run_$t "../$t.exe"
    fi
done

# Tracker do sysmodule
for t in tt2 gb_test; do
    selected $t || continue
    build $t "${BASE[@]}" -I"$S" -include "$T/shim/pdm_shim2.h" -include "$T/shim/save_shim.h" "$T/$t.cpp" \
        "$T/ram_fake.cpp" "$S/tracker.cpp" "$S/store.cpp" "$S/roms.cpp" "$S/achieve.cpp" "${PACK[@]}" \
        "$R/common/core/pt_config.cpp" "$R/common/core/pt_status.cpp" "${DB[@]}" || continue
    run $t run_$t "../$t.exe"
done

# App: banco (data.cpp)
for t in it2 it_fix pack_app_test; do
    selected $t || continue
    build $t "${BASE[@]}" -I"$A" -include "$T/shim/pdm_shim2.h" "$T/$t.cpp" "$A/data.cpp" "$A/i18n.cpp" \
        "${PACK[@]}" "$R/common/core/pt_status.cpp" "${DB[@]}" || continue
    run $t "run_$t/$SD" sh -c "cd ../../.. && ../$t.exe"
done

# status.txt e config.ini
for t in st_test cfg_test; do
    selected $t || continue
    build $t "${BASE[@]}" "$T/$t.cpp" "$R/common/core/pt_status.cpp" "$R/common/core/pt_config.cpp" "${CORE[@]}" ||
        continue
    run $t "run_$t/$SD" sh -c "cd ../../.. && ../$t.exe"
done

# Migração da pasta do nome antigo (pt_migrate_legacy)
if selected migrate_test && build migrate_test "${BASE[@]}" "$T/migrate_test.cpp" "${CORE[@]}"; then
    run migrate_test run_migrate "../migrate_test.exe"
fi

# Ordem da grade (sort.cpp)
if selected sort_test && build sort_test "${BASE[@]}" -I"$A" "$T/sort_test.cpp" "$A/sort.cpp" \
    "$R/common/core/pt_config.cpp" "${CORE[@]}"; then
    run sort_test "run_sort_test/$SD" sh -c "cd ../../.. && ../sort_test.exe"
fi

# Geometria da borda giratória (ring_geom.hpp)
if selected ring_test && build ring_test -O1 -w -std=gnu++20 -I"$A" "$T/ring_test.cpp"; then
    run ring_test run_ring_test "../ring_test.exe"
fi

# Aba Interface (i18n.cpp, prefs.cpp) e som do pop-up / assistente da 1ª abertura
for t in interface_test sfx_test; do
    selected $t || continue
    build $t "${BASE[@]}" -I"$A" "$T/$t.cpp" "$A/i18n.cpp" "$A/prefs.cpp" "$R/common/core/pt_config.cpp" \
        "${CORE[@]}" || continue
    mkdir -p "run_$t/$SD/sfx"
    run $t "run_$t/$SD" sh -c "cd ../../.. && ../$t.exe"
done

# Atualização do app (update.cpp + pt_update.cpp; Monocypher compilado como C; HTTP falso)
if selected update_test; then
    M=$R/app_nro/lib/monocypher
    if gcc -O1 -c "$M/monocypher.c" -o monocypher.o > update_test.build.log 2>&1 &&
       gcc -O1 -c "$M/monocypher-ed25519.c" -I"$M" -o monocypher-ed25519.o >> update_test.build.log 2>&1 &&
       build update_test "${BASE[@]}" -I"$A" -I"$M" -I"$R/app_nro/lib/borealis/library/include/borealis/extern" \
           "-DPT_VERSION=\"$(tr -d ' \r\n' < "$R/VERSION")\"" -include "$T/shim/sha256_shim.h" \
           "$T/update_test.cpp" "$A/update.cpp" "$T/http_fake.cpp" "$A/i18n.cpp" "$A/prefs.cpp" \
           "$R/common/core/pt_config.cpp" "${CORE[@]}" monocypher.o monocypher-ed25519.o; then
        run update_test run_update_test "../update_test.exe"
    elif [ ! -f update_test.exe ]; then
        grep -E "error" update_test.build.log | head -10
        [[ " ${FAILS[*]} " == *"update_test"* ]] || FAILS+=("update_test (compilação do Monocypher)")
    fi
fi

# Tabela de tradução contra o código
if selected i18n_check; then
    PY=${PYTHON:-}
    if [ -z "$PY" ]; then
        # O bash do MSYS2 não herda o PATH nem o LOCALAPPDATA do Windows: locais padrão do instalador.
        for c in python3 python /c/Users/*/AppData/Local/Python/pythoncore-3*/python.exe \
                 /c/Users/*/AppData/Local/Programs/Python/Python3*/python.exe; do
            if command -v "$c" > /dev/null 2>&1; then PY=$c; break; fi
        done
    fi
    if [ -z "$PY" ]; then
        echo "== i18n_check: Python 3 não encontrado (rode com PYTHON=/caminho/python.exe)"
        FAILS+=("i18n_check (sem Python)")
    else
        run i18n_check run_i18n env PYTHONIOENCODING=utf-8 "$PY" -I "$T/i18n_check.py" "$A"
    fi
fi

# Ícone do forwarder fantasma (recorte/redução e leitura real com o stb_image do Borealis)
if selected icon_test && build icon_test -O1 -std=gnu++17 -I"$A" "$T/icon_test.cpp"; then
    run icon_test run_icon_test "../icon_test.exe"
fi
if selected icon_decode_test && build icon_decode_test -O1 -w -std=gnu++17 \
    -I"$R/app_nro/lib/borealis/library/include" -I"$A" "$T/icon_decode_test.cpp"; then
    run icon_decode_test run_icon_decode "../icon_decode_test.exe" "$R/app_nro/resources/img/fundo.jpg" \
        "$R/app_nro/resources/img/fundo_settings.jpg" "$T/alpha_test.png"
fi

# Overlay: pop-up (popup_anim.hpp, pt_wav.hpp) e proteção contra flood (popup_queue.hpp)
if selected popup_test && build popup_test -O1 -std=gnu++20 -I"$R/overlay/source" -I"$R/common/core" \
    "$T/popup_test.cpp"; then
    run popup_test run_popup "../popup_test.exe" "$R/sd_card_structure/switch/ruby_achievements/sfx/conquista.wav"
fi
if selected flood_test && build flood_test -O1 -std=gnu++20 -I"$R/overlay/source" -I"$R/common/core" \
    "$T/flood_test.cpp"; then
    run flood_test run_flood "../flood_test.exe"
fi

echo
echo "================================================================"
if [ ${#FAILS[@]} -eq 0 ]; then
    echo "make test: $PASS testes, todos ok"
    exit 0
fi
echo "make test: $PASS ok, ${#FAILS[@]} com falha:"
for f in "${FAILS[@]}"; do echo "  - $f"; done
exit 1
