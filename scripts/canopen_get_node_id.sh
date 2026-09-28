#!/usr/bin/env bash
#
# canopen_get_node_id.sh — odczyt adresu osi (Node-ID) kontrolerów NiMotion STMP42SXI
# (CANopen/CiA402).
#
# Źródło protokołu: docs/1787619745915-ymnq3q.pdf (rozdział 10.2, 200Ch).
#   - Adres osi jest zapisany w obiekcie 200Ch:02h ("驱动器轴地址", uint16).
#   - Odczyt SDO: COB-ID = 0x600 + node_id, dane: 40 0C 20 02 00 00 00 00
#     (0x40 = initiate upload request).
#   - Odpowiedź SDO na 0x580 + node_id: 4B 0C 20 02 <lo> <hi> 00 00
#     (0x4B = expedited upload response, 2 bajty danych; <lo><hi> = Node-ID LE).
#
# Użycie:
#   ./scripts/canopen_get_node_id.sh [node_id ...] [--scan] [interfejs_can]
#
# Przykłady:
#   ./scripts/canopen_get_node_id.sh 1 2          # odczyt ID dla adresów 1 i 2
#   ./scripts/canopen_get_node_id.sh 1 2 can0     # jawnie podany interfejs
#   ./scripts/canopen_get_node_id.sh --scan can0  # skanowanie 1..127
#   CAN_IF=can1 ./scripts/canopen_get_node_id.sh 1 2

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_get_node_id.sh [node_id ...] [--scan] [interfejs_can]

Argumenty:
  node_id          adresy CANopen do sprawdzenia (1..127), domyślnie: 1 2
  --scan           przeszukaj wszystkie adresy 1..127
  interfejs_can    interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Przykłady:
  ./scripts/canopen_get_node_id.sh 1 2
  ./scripts/canopen_get_node_id.sh --scan can0
  CAN_IF=can1 ./scripts/canopen_get_node_id.sh 1 2
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

# Wysyła ramkę CAN i czeka na odpowiedź SDO. Nasłuch uruchamiany jest PRZED
# wysłaniem ramki (odpowiedź kontrolera przychodzi szybciej niż start candump).
# Wypisuje na stdout dane odpowiedzi (hex) i zwraca 0, gdy jej pierwszy bajt
# zgadza się z oczekiwanym, w przeciwnym razie zwraca 1.
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

# Odczyt Node-ID z węzła o adresie $1. Wypisuje wartość i zwraca 0, lub 1 przy braku odpowiedzi.
read_node_id() {
    local node="$1" resp lo hi value
    if resp=$(sdo_exchange "$node" "$(printf '%X' $((0x600 + node)))#400C200200000000" "4B" 1000); then
        lo="${resp:8:2}"
        hi="${resp:10:2}"
        value=$((16#${hi}${lo}))
        echo "$value"
        return 0
    fi
    return 1
}

main() {
    local ids=()
    local scan=0
    local iface=""

    for arg in "$@"; do
        case "$arg" in
            --scan) scan=1 ;;
            --help|-h) usage; exit 0 ;;
            *)
                if [[ -z "$iface" ]] && [[ "$arg" =~ ^(can[0-9]+|vcan[0-9]+)$ ]]; then
                    iface="$arg"
                else
                    ids+=("$arg")
                fi
                ;;
        esac
    done

    [[ -n "$iface" ]] && CAN_IF="$iface"

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    if (( scan )); then
        ids=($(seq 1 127))
    fi
    (( ${#ids[@]} > 0 )) || ids=(1 2)

    local id value
    log "Odczyt Node-ID z obiektu 200Ch:02h (interfejs: ${CAN_IF})"
    for id in "${ids[@]}"; do
        if ! is_valid_node_id "$id"; then
            log "Pomijam nieprawidlowy adres: $id"
            continue
        fi
        if value=$(read_node_id "$id"); then
            printf '  adres=%-3s -> Node-ID = %s\n' "$id" "$value"
        else
            printf '  adres=%-3s -> brak odpowiedzi\n' "$id"
        fi
    done
}

main "$@"
