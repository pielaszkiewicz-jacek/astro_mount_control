#!/usr/bin/env bash
#
# canopen_setup_stmp42sxi.sh — jednorazowa konfiguracja kontrolerów NiMotion
# STMP42SXI (BLDC/PMSM, CANopen/CiA 402) pod pracę z astro_mount_control.
#
# Źródło: docs/1787619745915-ymnq3q.pdf (rozdziały 10.2 i 10.3).
#
# STMP42SXI domyślnie startuje w trybie NiMotion open-loop (2002h:01h = 4),
# dlatego PRZED używaniem CiA 402 (607Ah, 6060h, 6040h) kontroler MUSI zostać
# przełączony w tryb CiA402 (2002h:01h = 0) i mieć ustawione przełożenie
# elektroniczne 6091h = 1:1 (brak przekładni). Bez tego pozycja 607Ah jest
# interpretowana w innych jednostkach i „360°” daje wielokrotność obrotu.
#
# Skrypt wykonuje dla każdego podanego węzła:
#   1) 2002h:01h = 0      — tryb CiA 402 (nie NiMotion open-loop)
#   2) 2002h:02h = 1      — pętla zamknięta (CloseLoopEn)
#   3) 6091h:01h = 1      — przełożenie: obroty silnika   = 1
#   4) 6091h:02h = 1      — przełożenie: obroty wału      = 1
#   5) 6060h:00h = 1      — tryb Profile Position (PP)
#   6) 1010h:01h = save   — zapis do EEPROM (0x65766173)
#   7) NMT reset          — przeładowanie konfiguracji
#
# Użycie:
#   ./scripts/canopen_setup_stmp42sxi.sh [node_id ...] [interfejs_can]
#   ./scripts/canopen_setup_stmp42sxi.sh --no-reset [node_id ...] [interfejs_can]
#
# Przykłady:
#   ./scripts/canopen_setup_stmp42sxi.sh 1 2 can0
#   CAN_IF=can1 ./scripts/canopen_setup_stmp42sxi.sh 1 2
#
# UWAGA: po zmianie 2002h:01h nowy tryb może wymagać wyłączenia napędu lub
# restartu — skrypt domyślnie wysyła NMT reset po zapisie EEPROM.

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_setup_stmp42sxi.sh [node_id ...] [interfejs_can]
  canopen_setup_stmp42sxi.sh --no-reset [node_id ...] [interfejs_can]

Argumenty:
  node_id          adresy CANopen (1..127), domyślnie: 1 2
  interfejs_can    interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --no-reset       nie wysyłaj NMT reset node (0x81) po zapisie
  --no-save        nie zapisuj parametrów do EEPROM (1010h:01h)

Przykłady:
  ./scripts/canopen_setup_stmp42sxi.sh 1 2 can0
  CAN_IF=can1 ./scripts/canopen_setup_stmp42sxi.sh 1 2
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die()  { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

hex16le() { printf '%02X%02X' $(( $1 & 0xFF )) $(( ($1 >> 8) & 0xFF )); }
hex32le() {
    local v="$1" h
    h=$(printf '%016X' "$v")
    h=${h:8:8}
    printf '%s%s%s%s' "${h:6:2}" "${h:4:2}" "${h:2:2}" "${h:0:2}"
}
hex8()    { printf '%02X' $(( $1 & 0xFF )); }

sdo_exchange() {
    local node="$1" frame="$2" expected="$3" timeout_ms="${4:-1000}"
    local resp_id tmp dump_pid line data
    resp_id=$(printf '%X' $((0x580 + node)))
    tmp=$(mktemp)
    timeout "$(( (timeout_ms + 999) / 1000 ))s" candump -n 1 "${CAN_IF},${resp_id}:7FF" >"$tmp" 2>/dev/null &
    dump_pid=$!
    sleep 0.1
    if ! cansend "${CAN_IF}" "${frame}" >/dev/null 2>&1; then
        kill "$dump_pid" 2>/dev/null
        wait "$dump_pid" 2>/dev/null
        rm -f "$tmp"
        return 1
    fi
    wait "$dump_pid" 2>/dev/null || true
    if [[ -s "$tmp" ]]; then
        line=$(head -n1 "$tmp")
        rm -f "$tmp"
        data=$(awk '{for(i=4;i<=NF;i++) printf "%s",$i}' <<<"$line" | tr -d '\r ')
        data=$(tr 'a-f' 'A-F' <<<"$data")
        printf '%s' "$data"
        [[ "${data:0:2}" == "$expected" ]]
    else
        rm -f "$tmp"
        return 1
    fi
}

# sdo_write <node> <idx_hex> <sub_hex> <cmd> <data_hex>
sdo_write() {
    local node="$1" idx="$2" sub="$3" cmd="$4" val_hex="$5"
    local lo hi data sdo_id
    lo=$(printf '%02X' $((16#$idx & 0xFF)))
    hi=$(printf '%02X' $(((16#$idx >> 8) & 0xFF)))
    data="${cmd}${lo}${hi}$(printf '%02X' $((16#$sub)))${val_hex}"
    while (( ${#data} < 16 )); do data+="00"; done
    sdo_id=$(printf '%X' $((0x600 + node)))
    log "TX ${sdo_id}#${data}"
    sdo_exchange "$node" "${sdo_id}#${data}" "60" 1000 >/dev/null
}

# sdo_read_u16 <node> <idx_hex> <sub_hex> → wypisuje wartość uint16 (LE).
sdo_read_u16() {
    local node="$1" idx="$2" sub="$3"
    local lo hi data sdo_id resp
    lo=$(printf '%02X' $((16#$idx & 0xFF)))
    hi=$(printf '%02X' $(((16#$idx >> 8) & 0xFF)))
    data="40${lo}${hi}$(printf '%02X' $((16#$sub)))00000000"
    sdo_id=$(printf '%X' $((0x600 + node)))
    if resp=$(sdo_exchange "$node" "${sdo_id}#${data}" "4B" 1000); then
        echo $((16#${resp:10:2}${resp:8:2}))
        return 0
    fi
    return 1
}

configure_node() {
    local node="$1" do_save="$2" do_reset="$3"
    local mode

    log "── node ${node}: konfiguracja STMP42SXI ──────────────────────────"

    # 0) Wyłącz napęd (6040h = 0) — zmiana trybu 2002h:01h wymaga zatrzymanego napędu.
    log "6040h:00h = 0 (disable — przed zmianą trybu)"
    sdo_write "$node" 6040 00 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia (kontynuuję)"
    sleep 0.3

    # 1) Tryb CiA 402 (2002h:01h = 0). Obiekt jest uint16 (słownik 10.2) — zapis 2B.
    log "2002h:01h = 0 (tryb CiA402)"
    sdo_write "$node" 2002 01 2B "$(hex16le 0)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"
    sleep 0.3
    if mode=$(sdo_read_u16 "$node" 2002 01); then
        log "Odczyt zwrotny 2002h:01h = ${mode}"
        if (( mode != 0 )); then
            log "   ⚠ OSTRZEŻENIE: 2002h:01h nadal ≠ 0 — tryb CiA402 NIE został aktywowany."
            log "   Sprawdź, czy napęd jest wyłączony (6040h=0) i powtórz konfigurację."
        fi
    else
        log "   UWAGA: brak odpowiedzi przy odczycie zwrotnym 2002h:01h"
    fi

    # 2) Pętla zamknięta (2002h:02h = 1).
    log "2002h:02h = 1 (pętla zamknięta)"
    sdo_write "$node" 2002 02 2B "$(hex16le 1)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    # 3) Przełożenie elektroniczne 1:1 (6091h:01h = 1, 6091h:02h = 1).
    log "6091h:01h = 1 (obroty silnika)"
    sdo_write "$node" 6091 01 23 "$(hex32le 1)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"
    log "6091h:02h = 1 (obroty wału)"
    sdo_write "$node" 6091 02 23 "$(hex32le 1)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    # 3b) Rozdzielczość enkodera pozycji (608Fh = 131072/1, enkoder 17-bitowy).
    #     Koryguje m.in. uszkodzone 608Fh:02h (np. 222176816 → 1).
    log "608Fh:01h = 131072 (encoder increments)"
    sdo_write "$node" 608F 01 23 "$(hex32le 131072)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia (608Fh może być RO)"
    log "608Fh:02h = 1 (motor revolutions)"
    sdo_write "$node" 608F 02 23 "$(hex32le 1)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia (608Fh może być RO)"

    # 4) Tryb Profile Position (6060h = 1, uint8).
    log "6060h:00h = 1 (Profile Position)"
    sdo_write "$node" 6060 00 2F "$(hex8 1)" \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    # 5) Zapis do EEPROM.
    if (( do_save )); then
        log "1010h:01h = 0x65766173 (save EEPROM)"
        sdo_write "$node" 1010 01 23 "$(hex32le 0x65766173)" \
            && log "   OK" || log "   UWAGA: brak potwierdzenia zapisu EEPROM"
    fi

    # 6) Restart węzła, aby nowy tryb wszedł w życie.
    if (( do_reset )); then
        log "NMT reset node (0x81)"
        cansend "${CAN_IF}" "000#81$(printf '%02X' "$node")"
        sleep 1
    fi

    log "── node ${node}: zakończono ──────────────────────────────────────"
}

main() {
    local ids=()
    local iface=""
    local do_save=1
    local do_reset=1

    while (( $# )); do
        case "$1" in
            --help|-h)  usage; exit 0 ;;
            --no-save)  do_save=0; shift ;;
            --no-reset) do_reset=0; shift ;;
            *)
                if [[ "$1" =~ ^(can[0-9]+|vcan[0-9]+)$ ]]; then
                    iface="$1"
                else
                    ids+=("$1")
                fi
                shift
                ;;
        esac
    done

    [[ -n "$iface" ]] && CAN_IF="$iface"
    [[ ${#ids[@]} -eq 0 ]] && ids=(1 2)

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    for node in "${ids[@]}"; do
        is_valid_node_id "$node" || { log "Pominięto nieprawidłowy node_id: $node"; continue; }
        configure_node "$node" "$do_save" "$do_reset"
    done

    log "Gotowe. Zweryfikuj ustawienia:"
    log "  ./scripts/canopen_params_2000h.sh ${ids[*]} --read 2002:01,2002:02 can0"
    log "  ./scripts/canopen_params_6000h.sh ${ids[*]} --read 6091:01,6091:02,608F:01 can0"
}

main "$@"
