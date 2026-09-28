#!/usr/bin/env bash
#
# canopen_rotate.sh — obrót silnika NiMotion STM42 o zadany kąt (CANopen/CiA402).
#
# Źródło protokołu: docs/stm42-canopen-protocol.pdf (rozdział 6.3 Profile Position Mode).
#
# Sekwencja:
#   1) tryb CiA402         2002h:01h = 0
#   2) tryb ruchu PP       6060h     = 1
#   3) pozycja docelowa    607Ah     (int32, user units)
#   4) prędkość profilu    6081h     (uint32, user units/s)
#   5) przyspieszenie      6083h     (uint32, user units/s^2)
#   6) hamowanie           6084h     (uint32, user units/s^2)
#   7) NMT -> Operational  000#01<node>
#   8) enable silnika      6040h = 0x06 -> 0x07 -> 0x0F
#   9) wyzwolenie ruchu    6040h = 0x4F -> 0x5F (względny) / 0x0F -> 0x1F (absolutny)
#
# Użycie:
#   ./scripts/canopen_rotate.sh <node_id> <kat_stopni> [interfejs_can] [opcje]
#
# Przykłady:
#   ./scripts/canopen_rotate.sh 1 90 can0       # obrót o +90°
#   ./scripts/canopen_rotate.sh 1 -180 can0     # obrót o -180°
#   ./scripts/canopen_rotate.sh 2 45 can0 --units-per-turn 8000

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_rotate.sh <node_id> <kat_stopni> [interfejs_can] [opcje]

Argumenty pozycyjne:
  node_id          Node-ID kontrolera (1..127)
  kat_stopni       kąt obrotu w stopniach (znak = kierunek), ruch względny
  interfejs_can    interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --angle <deg>           kąt obrotu w stopniach (alternatywnie do argumentu)
  --position <units>      bezpośrednia wartość 607Ah (zamiast kąta)
  --absolute              pozycja absolutna zamiast względnej
  --units-per-turn <n>    jednostek użytkownika na pełny obrót (domyślnie: 131072)
  --velocity <n>          prędkość profilu 6081h (domyślnie: 2000)
  --accel <n>             przyspieszenie 6083h (domyślnie: 8000)
  --decel <n>             hamowanie 6084h (domyślnie: 8000)
  --no-disable            nie wyłączaj silnika przed konfiguracją
  --no-trigger            tylko konfiguracja, bez wyzwalania ruchu
  --no-verify             nie czekaj na osiągnięcie pozycji i nie raportuj delty 6064h

Przykłady:
  ./scripts/canopen_rotate.sh 1 90 can0
  ./scripts/canopen_rotate.sh 1 -180 can0
  ./scripts/canopen_rotate.sh 2 45 can0 --units-per-turn 8000 --velocity 20000
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

is_number() {
    [[ "$1" =~ ^-?[0-9]+$ ]]
}

hex8()    { printf '%02X' $(( $1 & 0xFF )); }
hex16le() { printf '%02X%02X' $(( $1 & 0xFF )) $(( ($1 >> 8) & 0xFF )); }

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

# Zapis SDO (potwierdzenie write OK = 0x60).
sdo_write() {
    local node="$1" idx="$2" sub="$3" cmd="$4" val_hex="$5"
    local lo hi data sdo_id
    cmd="${cmd#0x}"
    lo=$(printf '%02X' $((idx & 0xFF)))
    hi=$(printf '%02X' $(((idx >> 8) & 0xFF)))
    data="${cmd}${lo}${hi}$(printf '%02X' "$((sub))")${val_hex}"
    while (( ${#data} < 16 )); do data+="00"; done
    sdo_id=$(printf '%X' $((0x600 + node)))
    log "TX ${sdo_id}#${data}"
    sdo_exchange "$node" "${sdo_id}#${data}" "60" 1000 >/dev/null
}

# Odczyt status word 6041h (uint16). Wypisuje wartość i zwraca 0, lub 1 przy braku odpowiedzi.
read_status() {
    local node="$1" resp lo hi
    if resp=$(sdo_exchange "$node" "$(printf '%X' $((0x600 + node)))#4041600000000000" "4B" 1000); then
        lo="${resp:8:2}"
        hi="${resp:10:2}"
        echo $((16#${hi}${lo}))
        return 0
    fi
    return 1
}

# Czeka aż status word osiągnie "Operation enabled" (bit0..2=111, bit3=0, bit5=1, bit6=0).
wait_status_enabled() {
    local node="$1" tries=25 st
    while (( tries-- > 0 )); do
        if st=$(read_status "$node"); then
            # Operation enabled: bit0..2=111, bit3=0 (brak Fault), bit5=1, bit6=0.
            if (( (st & 0x6F) == 0x27 )); then
                return 0
            fi
        fi
        sleep 0.2
    done
    return 1
}

# Odczyt pozycji aktualnej 6064h (int32, user units). Wypisuje wartość i zwraca 0/1.
read_position() {
    local node="$1" resp b0 b1 b2 b3 val
    if resp=$(sdo_exchange "$node" "$(printf '%X' $((0x600 + node)))#4064600000000000" "43" 1000); then
        b0="${resp:8:2}"; b1="${resp:10:2}"; b2="${resp:12:2}"; b3="${resp:14:2}"
        val=$((16#${b3}${b2}${b1}${b0}))
        if (( val >= 0x80000000 )); then val=$(( val - 0x100000000 )); fi
        echo "$val"
        return 0
    fi
    return 1
}

# Czeka na osiągnięcie pozycji (bit10=1) lub błąd Fault (bit3). Zwraca 0=osiągnięto, 1=timeout, 2=fault.
wait_target_reached() {
    local node="$1" tries=150 st
    while (( tries-- > 0 )); do
        if st=$(read_status "$node"); then
            if (( st & 0x0008 )); then return 2; fi
            if (( st & 0x0400 )); then return 0; fi
        fi
        sleep 0.2
    done
    return 1
}

main() {
    local node_id="" angle="" iface=""
    local position=""
    local absolute=0
    local units_per_turn=131072
    local velocity=2000
    local accel=8000
    local decel=8000
    local do_disable=1
    local do_trigger=1
    local verify=1

    # Parsowanie argumentów.
    while (( $# )); do
        case "$1" in
            --angle)          angle="$2"; shift 2 ;;
            --position)       position="$2"; shift 2 ;;
            --absolute)       absolute=1; shift ;;
            --units-per-turn) units_per_turn="$2"; shift 2 ;;
            --velocity)       velocity="$2"; shift 2 ;;
            --accel)          accel="$2"; shift 2 ;;
            --decel)          decel="$2"; shift 2 ;;
            --no-disable)     do_disable=0; shift ;;
            --no-trigger)     do_trigger=0; shift ;;
            --no-verify)      verify=0; shift ;;
            --help|-h)        usage; exit 0 ;;
            *)
                if [[ -z "$node_id" ]]; then
                    node_id="$1"
                elif [[ -z "$angle" && -z "$position" ]]; then
                    angle="$1"
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

    # Ustalenie wartości pozycji docelowej 607Ah.
    local target
    if [[ -n "$position" ]]; then
        is_number "$position" || die "Nieprawidlowa wartosc --position: $position"
        target="$position"
    else
        [[ -n "$angle" ]] || { usage; exit 1; }
        is_number "$angle" || die "Nieprawidlowy kat: $angle"
        is_number "$units_per_turn" || die "Nieprawidlowe --units-per-turn: $units_per_turn"
        target=$(awk -v a="$angle" -v u="$units_per_turn" 'BEGIN { printf "%d", a/360.0*u }')
    fi

    is_number "$velocity" || die "Nieprawidlowe --velocity: $velocity"
    is_number "$accel"    || die "Nieprawidlowe --accel: $accel"
    is_number "$decel"    || die "Nieprawidlowe --decel: $decel"

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    log "Obrót: node=${node_id}, kat=${angle:-"(pozycja)"}, target=${target} user units (interfejs: ${CAN_IF})"

    # 0) Wyłączenie silnika przed zmianą trybu (Switch on disabled).
    if (( do_disable )); then
        log "Wyłączenie silnika (6040h = 0x00)"
        sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x0000)" || log "UWAGA: brak potwierdzenia disable"
    fi

    # 1) Tryb CiA402 (2002h:01h = 0).
    log "Tryb CiA402 (2002h:01h = 0)"
    sdo_write "$node_id" 0x2002 0x01 0x2B "$(hex16le 0)" || log "UWAGA: brak potwierdzenia 2002h:01h"

    # 2) Profile Position Mode (6060h = 1).
    log "Tryb Profile Position (6060h = 1)"
    sdo_write "$node_id" 0x6060 0x00 0x2F "$(hex8 1)" || log "UWAGA: brak potwierdzenia 6060h"

    # 3) Pozycja docelowa 607Ah (int32).
    log "Pozycja docelowa 607Ah = ${target}"
    sdo_write "$node_id" 0x607A 0x00 0x23 "$(hex32le "$target")" || log "UWAGA: brak potwierdzenia 607Ah"

    # 4) Prędkość profilu 6081h (uint32).
    log "Prędkość profilu 6081h = ${velocity}"
    sdo_write "$node_id" 0x6081 0x00 0x23 "$(hex32le "$velocity")" || log "UWAGA: brak potwierdzenia 6081h"

    # 5) Przyspieszenie 6083h (uint32).
    log "Przyspieszenie 6083h = ${accel}"
    sdo_write "$node_id" 0x6083 0x00 0x23 "$(hex32le "$accel")" || log "UWAGA: brak potwierdzenia 6083h"

    # 6) Hamowanie 6084h (uint32).
    log "Hamowanie 6084h = ${decel}"
    sdo_write "$node_id" 0x6084 0x00 0x23 "$(hex32le "$decel")" || log "UWAGA: brak potwierdzenia 6084h"

    # 7) NMT: przejście węzła w stan Operational (wymagany do wykonywania ruchu).
    log "NMT start node (Operational)"
    cansend "${CAN_IF}" "000#01$(printf '%02X' "$node_id")"
    sleep 0.3

    # 8) Enable silnika: 0x06 -> 0x07 -> 0x0F (z kontrolą status word 6041h).
    log "Enable silnika (6040h: 0x06 -> 0x07 -> 0x0F)"
    sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x0006)" || log "UWAGA: brak potwierdzenia 0x06"
    sleep 0.2
    sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x0007)" || log "UWAGA: brak potwierdzenia 0x07"
    sleep 0.2
    sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x000F)" || log "UWAGA: brak potwierdzenia 0x0F"

    if wait_status_enabled "$node_id"; then
        log "Status: Operation enabled (6041h)"
    else
        local st
        st=$(read_status "$node_id" || true)
        log "UWAGA: silnik nie osiągnął Operation enabled, status word 6041h = ${st:-'brak odpowiedzi'}"
    fi

    # 9) Wyzwolenie ruchu (narastające zbocze bitu 4 new set-point).
    if (( do_trigger )); then
        local pos_before="" pos_after=""
        if (( verify )); then
            pos_before=$(read_position "$node_id" || true)
            log "Pozycja aktualna 6064h przed ruchem = ${pos_before:-'?'}"
        fi

        if (( absolute )); then
            log "Wyzwolenie ruchu absolutnego (6040h: 0x0F -> 0x1F)"
            sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x000F)"; sleep 0.1
            sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x001F)"
        else
            log "Wyzwolenie ruchu względnego (6040h: 0x4F -> 0x5F)"
            sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x004F)"; sleep 0.1
            sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x005F)"
        fi

        # Odczyt status word po wyzwoleniu (diagnostyka).
        local st
        if st=$(read_status "$node_id"); then
            log "Status word 6041h po wyzwoleniu = 0x$(printf '%04X' "$st")"
        else
            log "UWAGA: brak status word po wyzwoleniu"
        fi

        # Wyczyszczenie bitu 4 (new set-point) — ruch trwa dalej.
        sleep 0.1
        if (( absolute )); then
            sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x000F)"
        else
            sdo_write "$node_id" 0x6040 0x00 0x2B "$(hex16le 0x004F)"
        fi

        # Weryfikacja wykonanego ruchu.
        if (( verify )); then
            local rc
            wait_target_reached "$node_id"; rc=$?
            if (( rc == 0 )); then
                pos_after=$(read_position "$node_id" || true)
                if [[ -n "$pos_before" && -n "$pos_after" ]]; then
                    log "Ruch zakończony. Delta 6064h = $((pos_after - pos_before)) jednostek (${pos_before} -> ${pos_after})"
                else
                    log "Ruch zakończony. Pozycja 6064h = ${pos_after:-'?'}"
                fi
            elif (( rc == 2 )); then
                log "UWAGA: Fault (bit3 status word) — sprawdź obiekt 1003h"
            else
                log "UWAGA: timeout oczekiwania na osiągnięcie pozycji"
            fi
        fi
    fi

    log "Zakończono."
}

main "$@"
