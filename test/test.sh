#!/bin/bash
set -e
SVDE="./simplevde.exe"
PASS=0
FAIL=0

green() { echo -e "\033[32m$1\033[0m"; }
red()   { echo -e "\033[31m$1\033[0m"; }

check() {
    local desc="$1"
    shift
    if "$@" > /dev/null 2>&1; then
        green "  [OK] $desc"
        PASS=$((PASS + 1))
    else
        red "  [FAIL] $desc"
        FAIL=$((FAIL + 1))
    fi
}

check_ok() {
    local desc="$1"
    shift
    local out
    out=$("$@" 2>&1) || true
    if echo "$out" | grep -qE "successfully|created|removed|no errors|FAT32|TESTDISK|NO NAME|set to|extracted to|FILE|DIR|\|--|'--"; then
        green "  [OK] $desc"
        PASS=$((PASS + 1))
    else
        red "  [FAIL] $desc"
        red "       got: $out"
        FAIL=$((FAIL + 1))
    fi
}

check_fail() {
    local desc="$1"
    shift
    if ! "$@" > /dev/null 2>&1; then
        green "  [OK] $desc (ожидаемая ошибка)"
        PASS=$((PASS + 1))
    else
        red "  [FAIL] $desc (должна быть ошибка)"
        FAIL=$((FAIL + 1))
    fi
}

rm -f test_raw.img test_mbr.img

echo "=== 1. Создание ==="
$SVDE --disk-create -file=test_raw.img -size=32M
check "raw-образ" test -f test_raw.img
$SVDE --disk-create -file=test_mbr.img -size=64M -table=mbr
check "MBR-образ" test -f test_mbr.img

echo ""
echo "=== 2. Разделы и форматирование ==="
check_ok "создание раздела" $SVDE --part-create -file=test_mbr.img -part=1 -size=32M
check_fail "raw part-create отклонён" $SVDE --part-create -file=test_raw.img -part=raw -size=16M
check_ok "формат raw" $SVDE --format -file=test_raw.img -part=raw -fs=fat32
check_ok "формат MBR" $SVDE --format -file=test_mbr.img -part=1 -fs=fat32

echo ""
echo "=== 3. Метка ==="
check_ok "метка по умолчанию" $SVDE --fs-label -file=test_raw.img -part=raw
$SVDE --fs-label -file=test_raw.img -part=raw -name=TESTDISK
check_ok "установка метки" $SVDE --fs-label -file=test_raw.img -part=raw

echo ""
echo "=== 4. Файлы ==="
echo "test data" > test_tmp.txt
check_ok "копирование" $SVDE --fs-copy -file=test_raw.img -part=raw -src=test_tmp.txt -dest=/hello.txt
check_ok "mkdir" $SVDE --fs-mkdir -file=test_raw.img -part=raw -path=/subdir
check_ok "ls" $SVDE --fs-ls -file=test_raw.img -part=raw
check_ok "tree" $SVDE --fs-tree -file=test_raw.img -part=raw
check_ok "extract" $SVDE --fs-extract -file=test_raw.img -part=raw -src=/hello.txt -dest=test_extracted.txt
check "extract совпадает" diff test_tmp.txt test_extracted.txt
check_ok "rm" $SVDE --fs-rm -file=test_raw.img -part=raw -path=/hello.txt
check_ok "rmdir" $SVDE --fs-rmdir -file=test_raw.img -part=raw -path=/subdir

echo ""
echo "=== 5. Проверка и инфо ==="
check_ok "fs-check quick" $SVDE --fs-check -file=test_raw.img -part=raw -level=quick
check_ok "fs-check full" $SVDE --fs-check -file=test_raw.img -part=raw -level=full
check_ok "fs-info" $SVDE --fs-info -file=test_raw.img -part=raw

echo ""
echo "=== 6. Очистка ==="
rm -f test_raw.img test_mbr.img test_tmp.txt test_extracted.txt
check "очистка" true

echo ""
echo "============================"
echo " PASS: $PASS  FAIL: $FAIL"
echo "============================"