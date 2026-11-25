#!/usr/bin/env bash
#
# canopen_disable_limits.sh — trwałe wyłączenie krańcówek (limit switches)
# oraz poszerzenie programowego limitu pozycji dla napędów NiMotion STMP42SXI.
#
# Montaż nie ma podłączonych fizycznych wyłączników krańcowych do wejść
# cyfrowych DI1..DI3. Jeżeli któreś wejście pozostaje zmapowane na funkcję
# krańcówki (2003h: 14 = forward overtravel, 15 = reverse overtravel), pływające
# wejście może być odczytywane jako "limit aktywny" i ustawiać bit 11 słowa
# statusu ("internal limit active") — objaw: "Drive fault during slew" / alarm
# 0x3018614 "limit switch is incorrect".
#
# Skrypt wykonuje dla każdego węzła:
#   1) 6040h:00h = 0        — wyłączenie napędu (zmiany wejść są stop-effective)
#   2) 2003h:03h = 0        — DI1 funkcja = "No definition"
#   3) 2003h:05h = 0        — DI2 funkcja = "No definition"
#   4) 2003h:07h = 0        — DI3 funkcja = "No definition"
#   5) 2003h:04h/06h/08h=0  — logika DI1/DI2/DI3 = low-level-valid (neutralne)
#   6) 607Dh:01h = -2e9     — programowy limit pozycji min (jak 607Bh)
#   7) 607Dh:02h = +2e9     — programowy limit pozycji max (jak 607Bh)
#   8) 1010h:01h = save     — zapis do EEPROM (0x65766173)
#   9) NMT reset 0x81       — przeładowanie konfiguracji (opcja --no-reset pomija)
#
# Użycie:
#   ./scripts/canopen_disable_limits.sh [node_id ...] [interfejs_can]
#   ./scripts/canopen_disable_limits.sh --no-reset [node_id ...] [interfejs_can]
#   CAN_IF=can1 ./scripts/canopen_disable_limits.sh 1 2

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_disable_limits.sh [node_id ...] [interfejs_can]
  canopen_disable_limits.sh --no-reset [node_id ...] [interfejs_can]

Argumenty:
  node_id          adresy CANopen (1..127), domyślnie: 1 2
  interfejs_can    interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --no-reset       nie wysyłaj NMT reset node (0x81) po zapisie
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
    local node="$1" do_reset="$2"

    log "── node ${node}: wyłączanie krańcówek / limity pozycji ─────────────"

    log "6040h:00h = 0 (disable)"
    sdo_write "$node" 6040 00 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia (kontynuuję)"
    sleep 0.3

    log "2003h:03h = 0 (DI1 = No definition)"
    sdo_write "$node" 2003 03 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"
    log "2003h:05h = 0 (DI2 = No definition)"
    sdo_write "$node" 2003 05 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"
    log "2003h:07h = 0 (DI3 = No definition)"
    sdo_write "$node" 2003 07 2B "$(hex16le 0)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    log "2003h:04h/06h/08h = 0 (logika DI1/DI2/DI3)"
    sdo_write "$node" 2003 04 2B "$(hex16le 0)" >/dev/null 2>&1 || true
    sdo_write "$node" 2003 06 2B "$(hex16le 0)" >/dev/null 2>&1 || true
    sdo_write "$node" 2003 08 2B "$(hex16le 0)" >/dev/null 2>&1 || true
    log "   OK"

    log "607Dh:01h = -2000000000 (min software position limit)"
    sdo_write "$node" 607D 01 23 "$(hex32le -2000000000)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"
    log "607Dh:02h = 2000000000 (max software position limit)"
    sdo_write "$node" 607D 02 23 "$(hex32le 2000000000)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia"

    log "1010h:01h = 0x65766173 (save EEPROM)"
    sdo_write "$node" 1010 01 23 "$(hex32le 0x65766173)" >/dev/null 2>&1 \
        && log "   OK" || log "   UWAGA: brak potwierdzenia zapisu EEPROM"

    if (( do_reset )); then
        log "NMT reset 0x81"
        cansend "${CAN_IF}" "000#810${node}" >/dev/null 2>&1 \
            && log "   OK" || log "   UWAGA: brak potwierdzenia resetu"
        sleep 1
    fi

    log "Odczyt zwrotny:"
    if v=$(sdo_read_u16 "$node" 2003 03); then
        log "   2003h:03h (DI1 funkcja) = ${v} $([ "$v" = 0 ] && echo '✓' || echo '✗ NIEZGODNE')"
    else
        log "   UWAGA: brak odpowiedzi przy odczycie 2003h:03h"
    fi

    log "── node ${node}: zakończono ─────────────────────────────────────────"
}

main() {
    local ids=()
    local iface=""
    local do_reset=1
    while (( $# )); do
        case "$1" in
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

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend'"
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump'"

    for node in "${ids[@]}"; do
        is_valid_node_id "$node" || die "Niepoprawny node_id: ${node}"
        configure_node "$node" "$do_reset"
    done
}

main "$@"
