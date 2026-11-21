#!/usr/bin/env bash
#
# canopen_vdi_set_zero.sh — konfiguracja wirtualnego wejścia VDI2 jako
# "Set zero point" (funkcja 33) dla napędów NiMotion STMP42SXI.
#
# Wymagane przez mechanizm position_rewind w astro_mount_control:
#   - 200Ch:07h = 1      (CommunicationVDI enable)
#   - 2017h:03h = 33     (VDI2FunSelec  = Set zero point)
#   - 2017h:04h = 0      (VDI2LogicSelec = high-level-valid)
#   - 1010h:01h = save   (zapis do EEPROM)
#
# Użycie: ./scripts/canopen_vdi_set_zero.sh [node_id ...] [interfejs_can]

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die()  { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

hex16le() { printf '%02X%02X' $(( $1 & 0xFF )) $(( ($1 >> 8) & 0xFF )); }
hex32le() {
    local v="$1" h
    h=$(printf '%016X' "$v")
    h=${h:8:8}
    printf '%s%s%s%s' "${h:6:2}" "${h:4:2}" "${h:2:2}" "${h:0:2}"
}

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
    local node="$1"

    log "── node ${node}: konfiguracja VDI2 = Set zero point ────────────────"

    log "6040h:00h = 0 (disable — przed zmianą)"
    sdo_write "$node" 6040 00 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"
    sleep 0.3

    log "200Ch:07h = 1 (CommunicationVDI enable)"
    sdo_write "$node" 200C 07 2B "$(hex16le 1)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    log "2017h:03h = 33 (VDI2FunSelec = Set zero point)"
    sdo_write "$node" 2017 03 2B "$(hex16le 33)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    log "2017h:04h = 0 (VDI2LogicSelec = high-level-valid)"
    sdo_write "$node" 2017 04 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    log "1010h:01h = 0x65766173 (save EEPROM)"
    sdo_write "$node" 1010 01 23 "$(hex32le 0x65766173)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia zapisu EEPROM"

    log "6040h:00h = 6 → 7 → 15 (re-enable)"
    sdo_write "$node" 6040 00 2B "$(hex16le 6)" >/dev/null 2>&1 || true
    sleep 0.1
    sdo_write "$node" 6040 00 2B "$(hex16le 7)" >/dev/null 2>&1 || true
    sleep 0.1
    sdo_write "$node" 6040 00 2B "$(hex16le 15)" >/dev/null 2>&1 || true

    log "Odczyt zwrotny 2017h:03h (oczekiwane 33):"
    if v=$(sdo_read_u16 "$node" 2017 03); then
        log "   2017h:03h = ${v} $([ "$v" = 33 ] && echo '✓' || echo '✗ NIEZGODNE')"
    else
        log "   UWAGA: brak odpowiedzi przy odczycie"
    fi

    log "── node ${node}: zakończono ──────────────────────────────────────────"
}

main() {
    local ids=()
    local iface=""
    while (( $# )); do
        if [[ "$1" =~ ^(can[0-9]+|vcan[0-9]+)$ ]]; then
            iface="$1"
        else
            ids+=("$1")
        fi
        shift
    done
    [[ -n "$iface" ]] && CAN_IF="$iface"
    [[ ${#ids[@]} -eq 0 ]] && ids=(1 2)

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend'"
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump'"

    for node in "${ids[@]}"; do
        configure_node "$node"
    done
    log "Gotowe."
}

main "$@"
