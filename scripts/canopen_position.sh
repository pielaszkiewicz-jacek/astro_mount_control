#!/usr/bin/env bash
#
# canopen_position.sh — odczyt i zapis pozycji napędów NiMotion STM42
# (CANopen/CiA402) przez SDO: 607Ah (Target position) i 6064h (Position actual).
#
# Jednostki pozycji (user units) zależą od napędu:
#   - STM42: 4000 jednostek / obrót silnika (mikrokroki),
#   - enkoder 608Fh = 131072 counts/obrót to OSOBNA skala (feedback).
#   Przelicznik na stopnie wału: kat = jednostki / units_per_turn * 360
#
# Odczyt SDO (upload expedited, int32):
#   Żądanie:   COB-ID = 0x600 + node, dane: 40 <idx_lo> <idx_hi> <sub> 00 00 00 00
#   Odpowiedź: COB-ID = 0x580 + node, dane: 43 <idx_lo> <idx_hi> <sub> <b0> <b1> <b2> <b3>
#
# Zapis SDO (download expedited, int32, command specifier 0x23):
#   Żądanie:       COB-ID = 0x600 + node, dane: 23 <idx_lo> <idx_hi> <sub> <b0> <b1> <b2> <b3>
#   Potwierdzenie: COB-ID = 0x580 + node, dane: 60 <idx_lo> <idx_hi> <sub> 00 00 00 00
#
# UWAGA: 6064h (Position actual value) jest zwykle tylko do odczytu — próba
# zapisu może skończyć się abortem SDO (0x80...), a skrypt to zgłosi.
#
# Użycie:
#   ./scripts/canopen_position.sh <node_id> [interfejs_can]
#   ./scripts/canopen_position.sh <node_id> --target <v> [interfejs_can]
#   ./scripts/canopen_position.sh <node_id> --actual <v> [interfejs_can]
#   ./scripts/canopen_position.sh <node_id> --angle <deg> [--units-per-turn <n>] [interfejs_can]
#
# Przykłady:
#   ./scripts/canopen_position.sh 1 can0                    # odczyt 607Ah i 6064h
#   ./scripts/canopen_position.sh 1 --target 4000 can0      # zapis 607Ah = 4000 (1 obrót przy 4000/obr)
#   ./scripts/canopen_position.sh 1 --target -4000 can0     # zapis ujemny
#   ./scripts/canopen_position.sh 1 --actual 0 can0         # próba wyzerowania 6064h (często read-only)
#   ./scripts/canopen_position.sh 1 --angle 360 can0        # 607Ah = 360° (przy 4000/obr = 4000)
#   CAN_IF=can1 ./scripts/canopen_position.sh 1

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

IDX_TARGET=0x607A   # Target position (int32)
IDX_ACTUAL=0x6064   # Position actual value (int32)
IDX_POS_INTERNAL=0x6063  # Position actual internal value (int32, encoder increments)
IDX_GEAR=0x6091          # Gear ratio (uint32: 01 = motor shaft revs, 02 = driving shaft revs)
IDX_FEED=0x6092          # Feed constant (uint32: 01 = feed, 02 = driving shaft revs)

usage() {
    cat <<'EOF'
Użycie:
  canopen_position.sh <node_id> [interfejs_can]
  canopen_position.sh <node_id> --target <v> [interfejs_can]
  canopen_position.sh <node_id> --actual <v> [interfejs_can]
  canopen_position.sh <node_id> --angle <deg> [--units-per-turn <n>] [interfejs_can]

Argumenty:
  node_id            adres CANopen (1..127)
  interfejs_can      interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --target <n>       zapisz 607Ah:00h = Target position (int32)
  --actual <n>       zapisz 6064h:00h = Position actual value (int32, często read-only)
  --angle <deg>      kąt wału silnika w stopniach -> zapis 607Ah
                     (target = deg / 360 * units_per_turn)
  --units-per-turn <n>  jednostek na pełny obrót silnika (domyślnie: 4000)
  --help|-h          pomoc

Przykłady:
  ./scripts/canopen_position.sh 1 can0
  ./scripts/canopen_position.sh 1 --target 4000 can0
  ./scripts/canopen_position.sh 1 --angle 360 can0
  ./scripts/canopen_position.sh 2 --angle -180 --units-per-turn 8000 can0
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die()  { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

is_i32() {
    [[ "$1" =~ ^-?[0-9]+$ ]] && (( $1 >= -2147483648 && $1 <= 2147483647 ))
}

is_u32() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 0xFFFFFFFF ))
}

hex8()    { printf '%02X' $(( $1 & 0xFF )); }

hex32le() {
    local v="$1" h
    h=$(printf '%016X' "$v")
    h=${h:8:8}
    printf '%s%s%s%s' "${h:6:2}" "${h:4:2}" "${h:2:2}" "${h:0:2}"
}

# Wysyła ramkę CAN i czeka na odpowiedź SDO. Nasłuch uruchamiany PRZED wysłaniem.
# Wypisuje na stdout dane odpowiedzi (hex) i zwraca 0, gdy pierwszy bajt
# zgadza się z oczekiwanym.
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

# Odczyt int32 z obiektu <idx>:<sub> węzła $1. Wypisuje wartość i zwraca 0/1.
read_int32() {
    local node="$1" idx="$2" sub="$3" resp b0 b1 b2 b3 value
    local ilo ihi frame
    ilo=$(printf '%02X' $((idx & 0xFF)))
    ihi=$(printf '%02X' $(((idx >> 8) & 0xFF)))
    frame="$(printf '%X' $((0x600 + node)))#40${ilo}${ihi}$(printf '%02X' "$sub")00000000"
    if resp=$(sdo_exchange "$node" "$frame" "43" 1000); then
        b0="${resp:8:2}"
        b1="${resp:10:2}"
        b2="${resp:12:2}"
        b3="${resp:14:2}"
        value=$((16#${b3}${b2}${b1}${b0}))
        if (( value >= 0x80000000 )); then value=$(( value - 0x100000000 )); fi
        echo "$value"
        return 0
    fi
    return 1
}

# Zapis int32 do obiektu <idx>:<sub> (command specifier 0x23). Zwraca 0/1.
# Po zapisie weryfikuje wartość przez odczyt zwrotny.
write_int32() {
    local node="$1" idx="$2" sub="$3" value="$4"
    local ilo ihi frame resp verify
    ilo=$(printf '%02X' $((idx & 0xFF)))
    ihi=$(printf '%02X' $(((idx >> 8) & 0xFF)))
    frame="$(printf '%X' $((0x600 + node)))#23${ilo}${ihi}$(printf '%02X' "$sub")$(hex32le "$value")"
    log "TX ${frame}"
    if resp=$(sdo_exchange "$node" "$frame" "60" 1000); then
        log "Potwierdzenie zapisu $(printf '%04Xh' "$idx"):$(printf '%02Xh' "$sub") = ${value}: OK"
    else
        if [[ "${resp:0:2}" == "80" ]]; then
            log "Abort SDO: ${resp} ($(printf '%04Xh' "$idx"):$(printf '%02Xh' "$sub") — obiekt może być tylko do odczytu)"
        else
            log "Brak potwierdzenia zapisu $(printf '%04Xh' "$idx"):$(printf '%02Xh' "$sub")"
        fi
        return 1
    fi
    if verify=$(read_int32 "$node" "$idx" "$sub"); then
        if (( verify == value )); then
            log "Weryfikacja odczytu $(printf '%04Xh' "$idx"):$(printf '%02Xh' "$sub") = ${verify}: OK"
            return 0
        fi
        log "Weryfikacja NIEZGODNA: zapisano ${value}, odczytano ${verify}"
        return 1
    fi
    log "Brak odpowiedzi przy weryfikacji odczytu"
    return 1
}

# Raport odczytu 607Ah i 6064h w jednostkach i stopniach wału.
read_report() {
    local node="$1" units_per_turn="$2" actual target deg
    if target=$(read_int32 "$node" "$IDX_TARGET" 0); then
        deg=$(awk -v u="$target" -v n="$units_per_turn" 'BEGIN { printf "%.6f", u/n*360.0 }')
        log "node ${node}: 607Ah:00h (Target position)  = ${target} | ${deg}° (${units_per_turn} j./obrót)"
    else
        log "node ${node}: brak odpowiedzi (607Ah:00h)"
        return 1
    fi
    if actual=$(read_int32 "$node" "$IDX_ACTUAL" 0); then
        deg=$(awk -v u="$actual" -v n="$units_per_turn" 'BEGIN { printf "%.6f", u/n*360.0 }')
        log "node ${node}: 6064h:00h (Position actual)   = ${actual} | ${deg}° (${units_per_turn} j./obrót)"
    else
        log "node ${node}: brak odpowiedzi (6064h:00h)"
        return 1
    fi
}

# Diagnostyka skalowania pozycji: odczyt 608Fh/6091h/6092h/6063h/6064h
# i wyliczenie liczby jednostek użytkownika na pełny obrót silnika.
diagnose() {
    local node="$1" enc_inc enc_rev gear_num gear_den feed_num feed_den
    local pos_internal pos_user upr cpd

    enc_inc=$(read_int32 "$node" 0x608F 1) || { log "node ${node}: brak odpowiedzi (608Fh:01h)"; return 1; }
    enc_rev=$(read_int32 "$node" 0x608F 2) || enc_rev=1
    gear_num=$(read_int32 "$node" "$IDX_GEAR" 1) || gear_num=0
    gear_den=$(read_int32 "$node" "$IDX_GEAR" 2) || gear_den=0
    feed_num=$(read_int32 "$node" "$IDX_FEED" 1) || feed_num=0
    feed_den=$(read_int32 "$node" "$IDX_FEED" 2) || feed_den=0
    pos_internal=$(read_int32 "$node" "$IDX_POS_INTERNAL" 0) || pos_internal="?"
    pos_user=$(read_int32 "$node" "$IDX_ACTUAL" 0) || pos_user="?"

    log "node ${node}: 608Fh:01h (encoder increments) = ${enc_inc}"
    log "node ${node}: 608Fh:02h (motor revolutions)   = ${enc_rev}"
    log "node ${node}: 6091h (gear ratio)              = ${gear_num} / ${gear_den}"
    log "node ${node}: 6092h (feed constant)           = ${feed_num} / ${feed_den}"
    log "node ${node}: 6063h (position internal)       = ${pos_internal}"
    log "node ${node}: 6064h (position user)           = ${pos_user}"

    if (( enc_rev > 0 )); then
        upr=$(awk -v e="${enc_inc}" -v r="${enc_rev}" \
                   -v gn="${gear_num}" -v gd="${gear_den}" \
                   -v fn="${feed_num}" -v fd="${feed_den}" \
                   'BEGIN {
                        g = (gd > 0) ? gn/gd : 0.0;
                        f = (fd > 0) ? fn/fd : 0.0;
                        printf "%.6f", (e/r) * g * f;
                   }')
        cpd=$(awk -v u="$upr" 'BEGIN { printf "%.9f", u/360.0 }')
        log "node ${node}: jednostki/obrót (obliczone) = ${upr} | counts/stopień = ${cpd}"
    else
        log "node ${node}: UWAGA — 608Fh:02h = 0, nie można wyliczyć skali"
    fi
}

main() {
    local node_id="" iface=""
    local target="" actual="" angle=""
    local units_per_turn=4000
    local diagnose_mode=0

    while (( $# )); do
        case "$1" in
            --target)          target="$2"; shift 2 ;;
            --actual)          actual="$2"; shift 2 ;;
            --angle)           angle="$2"; shift 2 ;;
            --units-per-turn)  units_per_turn="$2"; shift 2 ;;
            --diagnose)        diagnose_mode=1; shift ;;
            --help|-h)         usage; exit 0 ;;
            *)
                if [[ -z "$node_id" ]]; then
                    node_id="$1"
                elif [[ -z "$iface" ]] && [[ "$1" =~ ^(can[0-9]+|vcan[0-9]+)$ ]]; then
                    iface="$1"
                else
                    die "Nierozpoznany argument: $1"
                fi
                shift
                ;;
        esac
    done

    [[ -n "$iface" ]] && CAN_IF="$iface"
    [[ -n "$node_id" ]] || { usage; exit 1; }
    is_valid_node_id "$node_id" || die "Nieprawidlowy node_id: $node_id (1..127)"
    is_u32 "$units_per_turn" || die "Nieprawidlowe --units-per-turn: $units_per_turn"

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    # Zapisy (wzajemnie wykluczające się — sprawdź przed wykonaniem).
    if [[ -n "$angle" ]]; then
        [[ -z "$target" && -z "$actual" ]] || die "--angle nie może występować razem z --target/--actual"
        is_i32 "$angle" || die "Nieprawidlowy kąt: $angle"
        target=$(awk -v a="$angle" -v u="$units_per_turn" 'BEGIN { printf "%d", a/360.0*u }')
        log "node ${node_id}: kąt ${angle}° -> 607Ah = ${target} jednostek (${units_per_turn} j./obrót)"
    fi

    if (( diagnose_mode )); then
        [[ -z "$target" && -z "$actual" && -z "$angle" ]] || die "--diagnose nie można łączyć z zapisami"
        diagnose "$node_id"
    elif [[ -n "$target" ]]; then
        is_i32 "$target" || die "Nieprawidlowa wartość --target: $target"
        write_int32 "$node_id" "$IDX_TARGET" 0 "$target"
    elif [[ -n "$actual" ]]; then
        is_i32 "$actual" || die "Nieprawidlowa wartość --actual: $actual"
        write_int32 "$node_id" "$IDX_ACTUAL" 0 "$actual"
    else
        read_report "$node_id" "$units_per_turn"
    fi
}

main "$@"
