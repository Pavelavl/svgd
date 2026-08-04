#!/bin/bash
# tests/c/run.sh — компиляция и запуск C unit-тестов чистой логики svgd.
# Запускается целью `make test-c`. Не требует bin/svgd, RRD-данных, rrdcached.
#
# Каждый тест — отдельный бинарник (TEST/RUN/ASSERT из minitest.h).
# Сборка тем же $(CC), что и основной проект; флаги согласованы с CFLAGS,
# но без -rdynamic и с -O0 -g для удобной отладки тестов.
set -u

# Корень репозитория (скрипт лежит в tests/c/).
cd "$(dirname "$0")/../.." || exit 99

CC="${CC:-gcc}"
CFLAGS="-Ilsrp -Iinclude -Wall -Wextra -Wformat -Werror=format-security -O0 -g"
BUILD="tests/c/.build"
mkdir -p "$BUILD"

PASS=0
FAIL=0
FAILED=()

# run_test <name> <src...> -- <libs...>
run_test() {
    local name="$1"; shift
    local src=() libs=() in_libs=0 arg
    for arg in "$@"; do
        if [[ "$arg" == "--" ]]; then in_libs=1; continue; fi
        if (( in_libs )); then libs+=("$arg"); else src+=("$arg"); fi
    done

    local bin="$BUILD/$name"
    local log="$BUILD/$name.log"
    printf "  [CC ] %s\n" "$name"
    if ! "$CC" $CFLAGS "${src[@]}" -o "$bin" "${libs[@]}" 2>"$log"; then
        printf "  [FAIL] %s: компиляция не удалась (см. %s):\n" "$name" "$log"
        cat "$log"
        FAIL=$((FAIL+1)); FAILED+=("$name"); return
    fi

    printf "  [RUN] %s\n" "$name"
    if "$bin" >"$log" 2>&1; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1)); FAILED+=("$name")
    fi
    cat "$log"
}

echo "=== C unit tests (svgd pure logic) ==="
run_test test_step   tests/c/test_step.c   src/rrd/reader.c -- -lrrd -lm
run_test test_cfg    tests/c/test_cfg.c    src/cfg.c        -- -lduktape
run_test test_path   tests/c/test_path.c   src/path_util.c  --
run_test test_config tests/c/test_config.c src/cfg.c        -- -lduktape
run_test test_source tests/c/test_source.c --
run_test test_proc    tests/c/test_proc.c    src/proc_source.c src/rrd/reader.c -- -lrrd -lpthread -lm

echo
echo "C unit tests: $PASS passed, $FAIL failed"
if (( FAIL > 0 )); then
    printf "Failed: %s\n" "${FAILED[*]}"
    exit 1
fi
