#!/usr/bin/env bash
#
# canopen_encoder_resolution.sh — odczyt rozdzielczości enkodera pozycji (608Fh)
# napędów NiMotion STMP42SXI (CANopen/CiA402) przez SDO.
#
# Obiekt 608Fh (Position encoder resolution):
#   - 608Fh:01h = Encoder increments  (uint32) — liczba inkrementów enkodera na obrót silnika
#   - 608Fh:02h = Motor revolutions   (uint32) — liczba obrotów silnika
#   - Rozdzielczość pozycji = 608Fh:01h / 608Fh:02h  [counts / obrót silnika]
#   - STMP42SXI: 608Fh jest rw; enkoder 17-bitowy zgłasza 131072 imp/obr.
#
# Odczyt SDO (upload expedited, uint32):
#   Żądanie:   COB-ID = 0x600 + node_id, dane: 40 8F 60 <sub> 00 00 00 00
#   Odpowiedź: COB-ID = 0x580 + node_id, dane: 43 8F 60 <sub> <b0> <b1> <b2> <b3>
#
# Użycie:
#   ./scripts/canopen_encoder_resolution.sh <node_id ...> [interfejs_can]
#   ./scripts/canopen_encoder_resolution.sh <node_id> --increments <n> [interfejs_can] [--save]
#   ./scripts/canopen_encoder_resolution.sh <node_id> --revolutions <n> [interfejs_can] [--save]
#
# Przykłady:
#   ./scripts/canopen_encoder_resolution.sh 1                          # odczyt node 1
#   ./scripts/canopen_encoder_resolution.sh 1 2 can0                   # odczyt node 1 i 2
#   ./scripts/canopen_encoder_resolution.sh 1 --increments 131072 can0 # zapis 608Fh:01h

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_encoder_resolution.sh <node_id ...> [interfejs_can]
  canopen_encoder_resolution.sh <node_id> --increments <n>  [interfejs_can] [--save]
  canopen_encoder_resolution.sh <node_id> --revolutions <n> [interfejs_can] [--save]

Argumenty:
  node_id           adresy CANopen (1..127). W trybie zapisu dokładnie jeden.
  interfejs_can     interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --increments <n>  zapisz 608Fh:01h = Encoder increments (uint32, 17-bit = 131072)
  --revolutions <n> zapisz 608Fh:02h = Motor revolutions (uint32)
  --save            zapisz parametry do EEPROM (1010h:01h = 0x65766173)

Przykłady:
  ./scripts/canopen_encoder_resolution.sh 1
  ./scripts/canopen_encoder_resolution.sh 1 2 can0
  ./scripts/canopen_encoder_resolution.sh 1 --increments 131072 can0
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

is_u32() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 0 && $1 <= 0xFFFFFFFF ))
}

hex32le() {
    local v="$1" h
    h=$(printf '%016X' "$v")
    h=${h:8:8}
    printf '%s%s%s%s' "${h:6:2}" "${h:4:2}" "${h:2:2}" "${h:0:2}"
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

# Zapis uint32 do obiektu 608Fh:<sub> (command specifier 0x23). Zwraca 0/1.
# Po zapisie weryfikuje wartość przez odczyt zwrotny.
write_608f() {
    local node="$1" sub="$2" value="$3"
    local frame resp verify
    frame="$(printf '%X' $((0x600 + node)))#238F60$(printf '%02X' "$sub")$(hex32le "$value")"
    log "TX ${frame}"
    if resp=$(sdo_exchange "$node" "$frame" "60" 1000); then
        log "Potwierdzenie zapisu 608Fh:$(printf '%02Xh' "$sub") = ${value}: OK"
    else
        if [[ "${resp:0:2}" == "80" ]]; then
            log "Abort SDO: ${resp} (608Fh:$(printf '%02Xh' "$sub") — obiekt może być tylko do odczytu)"
        else
            log "Brak potwierdzenia zapisu 608Fh:$(printf '%02Xh' "$sub")"
        fi
        return 1
    fi
    if verify=$(read_608f "$node" "$sub"); then
        if (( verify == value )); then
            log "Weryfikacja odczytu 608Fh:$(printf '%02Xh' "$sub") = ${verify}: OK"
            return 0
        fi
        log "Weryfikacja NIEZGODNA: zapisano ${value}, odczytano ${verify}"
        return 1
    fi
    log "Brak odpowiedzi przy weryfikacji odczytu"
    return 1
}

# Zapis parametrów do EEPROM (1010h:01h = 0x65766173).
save_eeprom() {
    local node="$1" frame
    frame="$(printf '%X' $((0x600 + node)))#23101001$(hex32le 0x65766173)"
    log "TX ${frame} (save EEPROM)"
    if sdo_exchange "$node" "$frame" "60" 1000 >/dev/null; then
        log "Zapis do EEPROM: OK"
    else
        log "UWAGA: brak potwierdzenia zapisu do EEPROM"
    fi
}

# Odczyt obu podobiektów 608Fh i raport rozdzielczości.
read_and_report() {
    local node="$1" inc rev resolution cpd
    if ! inc=$(read_608f "$node" 1); then
        log "node ${node}: brak odpowiedzi (608Fh:01h)"
        return 1
    fi
    if ! rev=$(read_608f "$node" 2); then
        log "node ${node}: brak odpowiedzi (608Fh:02h)"
        return 1
    fi

    log "node ${node}: 608Fh:01h (encoder increments) = ${inc}"
    log "node ${node}: 608Fh:02h (motor revolutions)   = ${rev}"
    if (( rev > 0 )); then
        resolution=$(( inc / rev ))
        cpd=$(awk -v r="$inc" -v v="$rev" 'BEGIN { printf "%.9f", (r/v)/360.0 }')
        log "node ${node}: rozdzielczość = ${resolution} counts/obrót | counts/stopień = ${cpd}"
    else
        log "node ${node}: UWAGA — 608Fh:02h = 0, nie można obliczyć rozdzielczości"
    fi
}

main() {
    local ids=()
    local iface=""
    local inc="" rev=""
    local do_save=0
    local node

    while (( $# )); do
        case "$1" in
            --help|-h) usage; exit 0 ;;
            --increments)  inc="$2"; shift 2 ;;
            --revolutions) rev="$2"; shift 2 ;;
            --save)        do_save=1; shift ;;
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

    if [[ -n "$inc" || -n "$rev" ]]; then
        # ── Tryb zapisu ──────────────────────────────────────────────
        [[ ${#ids[@]} -eq 1 ]] || die "Tryb zapisu wymaga dokładnie jednego node_id."
        node="${ids[0]}"
        is_valid_node_id "$node" || die "Nieprawidlowy node_id: $node (dopuszczalne 1..127)"
        [[ -z "$inc" ]] || is_u32 "$inc" || die "Nieprawidłowa wartość --increments: $inc (uint32)"
        [[ -z "$rev" ]] || is_u32 "$rev" || die "Nieprawidłowa wartość --revolutions: $rev (uint32)"

        [[ -z "$inc" ]] || write_608f "$node" 1 "$inc"
        [[ -z "$rev" ]] || write_608f "$node" 2 "$rev"
        (( do_save )) && save_eeprom "$node"

        log "Stan po zapisie (node ${node}):"
        read_and_report "$node" || true
    else
        # ── Tryb odczytu ─────────────────────────────────────────────
        for node in "${ids[@]}"; do
            is_valid_node_id "$node" || { log "Pominięto nieprawidłowy node_id: $node"; continue; }
            read_and_report "$node" || true
        done
    fi
}

main "$@"
