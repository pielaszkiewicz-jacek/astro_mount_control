#!/usr/bin/env bash
#
# canopen_read_encoder_resolution.sh — odczyt rozdzielczości wbudowanego enkodera
# napędów NiMotion STM42 (CANopen/CiA402) przez SDO.
#
# Źródło protokołu: docs/stm42-canopen-protocol.pdf, rozdział 5.1 (608Fh).
#   - 608Fh:01h = Encoder increments  (liczba inkrementów enkodera na obrót silnika)
#   - 608Fh:02h = Motor revolutions   (liczba obrotów silnika)
#   - Rozdzielczość pozycji = 608Fh:01h / 608Fh:02h  [counts / obrót silnika]
#
# Odczyt SDO (upload expedited, uint32):
#   Żądanie:  COB-ID = 0x600 + node_id, dane: 40 8F 60 <sub> 00 00 00 00
#   Odpowiedź: COB-ID = 0x580 + node_id, dane: 43 8F 60 <sub> <b0> <b1> <b2> <b3>
#   (0x43 = expedited upload response, 4 bajty danych little-endian).
#
# Użycie:
#   ./scripts/canopen_read_encoder_resolution.sh [node_id ...] [interfejs_can]
#
# Przykłady:
#   ./scripts/canopen_read_encoder_resolution.sh 1 2
#   ./scripts/canopen_read_encoder_resolution.sh 1 2 can0
#   CAN_IF=can1 ./scripts/canopen_read_encoder_resolution.sh 1 2

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_read_encoder_resolution.sh [node_id ...] [interfejs_can]

Argumenty:
  node_id          adresy CANopen do sprawdzenia (1..127), domyślnie: 1 2
  interfejs_can    interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Przykłady:
  ./scripts/canopen_read_encoder_resolution.sh 1 2
  ./scripts/canopen_read_encoder_resolution.sh 1 2 can0
  CAN_IF=can1 ./scripts/canopen_read_encoder_resolution.sh 1 2
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

# Odczyt uint32 z obiektu 608Fh:<sub> węzła $1. Wypisuje wartość i zwraca 0/1.
read_608f() {
    local node="$1" sub="$2" resp b0 b1 b2 b3 value
    local frame
    frame="$(printf '%X' $((0x600 + node)))#408F60$(printf '%02X' "$sub")00000000"
    if resp=$(sdo_exchange "$node" "$frame" "43" 1000); then
        b0="${resp:8:2}"
        b1="${resp:10:2}"
        b2="${resp:12:2}"
        b3="${resp:14:2}"
        value=$((16#${b3}${b2}${b1}${b0}))
        echo "$value"
        return 0
    fi
    return 1
}

main() {
    local ids=()
    local iface=""

    for arg in "$@"; do
        case "$arg" in
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
    [[ ${#ids[@]} -eq 0 ]] && ids=(1 2)

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    for node in "${ids[@]}"; do
        is_valid_node_id "$node" || { log "Pominięto nieprawidłowy node_id: $node"; continue; }

        local inc rev
        if ! inc=$(read_608f "$node" 1); then
            log "node ${node}: brak odpowiedzi (608Fh:01h)"
            continue
        fi
        if ! rev=$(read_608f "$node" 2); then
            log "node ${node}: brak odpowiedzi (608Fh:02h)"
            continue
        fi

        local resolution=""
        local cpd=""
        if (( rev > 0 )); then
            resolution=$(( inc / rev ))
            # counts/stopień = resolution / 360, z 3 miejscami po przecinku
            cpd=$(awk -v r="$inc" -v v="$rev" 'BEGIN { printf "%.9f", (r/v)/360.0 }')
        fi

        log "node ${node}: 608Fh:01h (encoder increments) = ${inc}"
        log "node ${node}: 608Fh:02h (motor revolutions)   = ${rev}"
        if [[ -n "$resolution" ]]; then
            log "node ${node}: rozdzielczość = ${resolution} counts/obrót | counts/stopień = ${cpd}"
        else
            log "node ${node}: UWAGA — 608Fh:02h = 0, nie można obliczyć rozdzielczości"
        fi
    done
}

main "$@"
