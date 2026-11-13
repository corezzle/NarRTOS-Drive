#!/usr/bin/env bash
# Drive'ın donanımdan bağımsız mantığını Mac üzerinde derleyip çalıştırır.
# GERÇEK src/ dosyaları derlenir, kopyalanmış mantık değil; ESP-IDF başlıkları
# shims/ altındaki taklitlerle karşılanır.
#
#   can_service      kimlik başına son frame tablosu
#   store_forward    flash üstünde kayıt halkası (NOR semantiği zorlanarak)
#   telemetry_codec  telemetri dokümanı üretimi (altın çıktı karşılaştırması)
#   nmea_parser      GNSS cümleleri (sağlama toplamı zorunlu)
set -e
cd "$(dirname "$0")"

TMP=$(mktemp -d)
CXX_FLAGS=(-std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -I shims)

fail=0

echo "══ can_service ═════════════════════════════════════════════"
clang++ "${CXX_FLAGS[@]}" \
    test_can_service.cpp ../../src/services/can_service.cpp \
    -o "$TMP/can_test"
"$TMP/can_test" || fail=1

echo
echo "══ store_forward ═══════════════════════════════════════════"
# test_store_forward.cpp store_forward.cpp'yi include ediyor (yeniden başlatma
# taklidi için statik duruma erişmesi gerekiyor), o yüzden ayrıca link edilmez.
clang++ "${CXX_FLAGS[@]}" \
    test_store_forward.cpp fake_flash.cpp \
    -o "$TMP/sf_test"
"$TMP/sf_test" || fail=1

echo
echo "══ telemetry_codec ═════════════════════════════════════════"
# Modül SAF (donanım/ağ/FreeRTOS bilmiyor) — shim bile gerekmiyor.
clang++ "${CXX_FLAGS[@]}" \
    test_telemetry_codec.cpp ../../src/services/telemetry_codec.cpp \
    -o "$TMP/telem_test"
"$TMP/telem_test" || fail=1

echo
echo "══ nmea_parser ═════════════════════════════════════════════"
# Zaman kontrolünde telemetry_codec'in ISO biçimlendiricisi kullanılıyor.
clang++ "${CXX_FLAGS[@]}" \
    test_nmea_parser.cpp ../../src/services/nmea_parser.cpp \
    ../../src/services/telemetry_codec.cpp \
    -o "$TMP/nmea_test"
"$TMP/nmea_test" || fail=1

echo
echo "══ ota_manifest ════════════════════════════════════════════"
clang++ "${CXX_FLAGS[@]}" \
    test_ota_manifest.cpp ../../src/services/ota_manifest.cpp \
    -o "$TMP/ota_test"
"$TMP/ota_test" || fail=1

echo
echo "══ at_client ═══════════════════════════════════════════════"
# Senaryolu modem: taşıyıcı arayüz arkasında olduğu için modemsiz sürülüyor.
clang++ "${CXX_FLAGS[@]}" \
    test_at_client.cpp ../../src/services/at_client.cpp \
    ../../src/services/telemetry_codec.cpp \
    -o "$TMP/at_test"
"$TMP/at_test" || fail=1

echo
echo "== can_signals ===================================="
clang++ "${CXX_FLAGS[@]}" \
    test_can_signals.cpp ../../src/services/can_signals.cpp \
    -o "$TMP/cansig_test"
"$TMP/cansig_test" || fail=1

echo
echo "== dead_reckoning ================================="
clang++ "${CXX_FLAGS[@]}" \
    test_dead_reckoning.cpp ../../src/services/dead_reckoning.cpp \
    -o "$TMP/dr_test"
"$TMP/dr_test" || fail=1

echo
echo "== power_policy ==================================="
clang++ "${CXX_FLAGS[@]}" \
    test_power_policy.cpp ../../src/services/power_policy.cpp \
    -o "$TMP/power_test"
"$TMP/power_test" || fail=1

echo
if [ "$fail" -eq 0 ]; then echo "TÜM KOŞUMLAR GEÇTİ"; else echo "BAŞARISIZ"; fi
exit "$fail"
