#!/usr/bin/env bash
#
# canopen_params_6000h.sh — odczyt i zapis parametrów grupy 10.3
# "Sub-protocol Definition Parameter Group 6000h Description" (CiA 402)
# napędów NiMotion STM42 (CANopen/CiA402) przez SDO.
#
# Źródło: docs/stm42-canopen-protocol.pdf, rozdział 10.3 (tabela 603Fh–60FFh).
#
# Grupa obejmuje obiekty CiA 402: słowo sterowania/stanu, tryby pracy,
# parametry profilu pozycji/prędkości (PP/PV), homing, jednostki (608Fh/6091h),
# okna pozycji/prędkości (6065h–6070h), ograniczenia (607Bh/607Dh), jerk
# (60A3h/60A4h), interpolację (60C1h/60C2h) i prędkość docelową (60FFh).
#
# Obsługiwane typy: uint8/int8 (1 bajt), uint16/int16 (2 bajty),
#                   uint32/int32 (4 bajty).
#
# Odczyt SDO (upload expedited):
#   Żądanie:   COB-ID = 0x600 + node, dane: 40 <idx_lo> <idx_hi> <sub> 00 00 00 00
#   Odpowiedź: COB-ID = 0x580 + node, dane: 4F/4B/43 <idx_lo> <idx_hi> <sub> <b0..b3>
#   (4F = 1 bajt, 4B = 2 bajty, 43 = 4 bajty danych, little-endian)
#
# Zapis SDO (download expedited):
#   1 bajt:  COB-ID = 0x600 + node, dane: 2F <idx_lo> <idx_hi> <sub> <b0> 00 00 00
#   2 bajty: COB-ID = 0x600 + node, dane: 2B <idx_lo> <idx_hi> <sub> <b0> <b1> 00 00
#   4 bajty: COB-ID = 0x600 + node, dane: 23 <idx_lo> <idx_hi> <sub> <b0> <b1> <b2> <b3>
#   Potwierdzenie: 60 <idx_lo> <idx_hi> <sub> 00 00 00 00
#
# Użycie:
#   ./scripts/canopen_params_6000h.sh <node_id ...> [interfejs_can]           # odczyt wszystkich
#   ./scripts/canopen_params_6000h.sh <node_id> --read <param,...> [iface]    # odczyt wybranych
#   ./scripts/canopen_params_6000h.sh <node_id> --write <param>=<w> [iface]   # zapis
#   ./scripts/canopen_params_6000h.sh <node_id> --mode <n> [iface] [--save]   # alias 6060h:00h
#   ./scripts/canopen_params_6000h.sh --list                                 # lista parametrów
#
# Format <param>: "6060:00", "0x6060:0x00", "6060h:00h", "6060" (cały indeks)
#                 lub nazwa (np. "TargetPosition", "ModesOfOperation").
#
# Przykłady:
#   ./scripts/canopen_params_6000h.sh 1 can0                    # pełny zrzut grupy 6000h
#   ./scripts/canopen_params_6000h.sh 1 --read 6060:00 can0     # tylko tryb pracy
#   ./scripts/canopen_params_6000h.sh 1 --write 607A:00=1000 can0
#   ./scripts/canopen_params_6000h.sh 1 --mode 1 can0           # Profile Position

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_params_6000h.sh <node_id ...> [interfejs_can]
  canopen_params_6000h.sh <node_id ...> --read <param,...> [interfejs_can]
  canopen_params_6000h.sh <node_id> --write <param>=<wartość> [interfejs_can] [--save]
  canopen_params_6000h.sh <node_id> --mode <n> [interfejs_can] [--save]
  canopen_params_6000h.sh --list

Argumenty:
  node_id            adresy CANopen (1..127). W trybie zapisu dokładnie jeden.
  interfejs_can      interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --list             wypisz wszystkie parametry grupy 6000h (bez dostępu do CAN)
  --read <param,...> odczyt wybranych parametrów (domyślnie: wszystkie)
  --write <p>=<w>    zapis parametru; <p> = "607A:00", "TargetPosition" itp.,
                     <w> = wartość dziesiętna lub 0x... (hex)
  --mode <n>         alias: --write 6060:00=<n> (tryb pracy)
  --save             po zapisie zapisz parametry do EEPROM (1010h:01h = 0x65766173)

Format <param>:
  607A:00            indeks i subindeks (hex)
  0x607A:0x00        jawnie hex
  607Ah:00h          notacja dokumentacji
  607A               cały indeks (wszystkie subindeksy)
  TargetPosition     nazwa (lub fragment nazwy)

Przykłady:
  ./scripts/canopen_params_6000h.sh 1 can0
  ./scripts/canopen_params_6000h.sh 1 --read 6060:00 can0
  ./scripts/canopen_params_6000h.sh 1 --write TargetPosition=1000 can0
  ./scripts/canopen_params_6000h.sh 1 --mode 1 --save can0
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die()  { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

hex32le() {
    local v="$1" h
    h=$(printf '%016X' "$v")
    h=${h:8:8}
    printf '%s%s%s%s' "${h:6:2}" "${h:4:2}" "${h:2:2}" "${h:0:2}"
}

# ─────────────────────────────────────────────────────────────────────────────
# Rejestr parametrów grupy 6000h (10.3).
# Format wiersza: INDEX:SUB|NAME|TYPE|SIZE|ACCESS|UNIT|FACTORY|OPTS
#   TYPE  : uint8|int8|uint16|int16|uint32|int32
#   SIZE  : liczba bajtów danych SDO (1, 2 lub 4)
#   ACCESS: RO|RW
# ─────────────────────────────────────────────────────────────────────────────
declare -a PARAMS

load_registry() {
    PARAMS=()
    local line
    while IFS= read -r line; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        PARAMS+=("$line")
    done <<'REGISTRY_EOF'
# Sterowanie / stan
603F:00|ErrorCode|uint16|2|RO|-|0|
6040:00|Controlword|uint16|2|RW|1|0|6=shutdown,7=switch on,15=enable
6041:00|Statusword|uint16|2|RO|1|0|
# Tryb prędkości (Velocity Mode, VM)
6042:00|VITargetVelocity|int16|2|RW|1rpm|0|-3000..3000
6043:00|VIVelocityDemand|int16|2|RO|1rpm|0|-3000..3000
6046:01|VIVelocityMinAmount|uint32|4|RW|1rpm|0|0..3000
6046:02|VIVelocityMaxAmount|uint32|4|RW|1rpm|300|0..3000
6048:01|VAccelDeltaSpeed|uint32|4|RW|1rpm|500|0..300000
6048:02|VAccelDeltaTime|uint16|2|RW|1s|1|1..65535
6049:01|VDecelDeltaSpeed|uint32|4|RW|1rpm|500|0..300000
6049:02|VDecelDeltaTime|uint16|2|RW|1s|1|1..65535
604A:01|VBrakeDeltaSpeed|uint32|4|RW|1rpm|1000|0..300000
604A:02|VBrakeDeltaTime|uint16|2|RW|1s|1|1..65535
604C:01|VlDimensionFactorNumerator|int32|4|RW|1|0|
604C:02|VlDimensionFactorDenominator|int32|4|RW|1|0|
# Kody opcji (CiA 402)
605A:00|QuickStopOptionCode|int16|2|RW|1|2|0-65535
605B:00|ShutdownOptionCode|int16|2|RW|1|0|0-65535
605C:00|DisableOperationOptionCode|int16|2|RW|1|0|0-65535
605D:00|HaltOptionCode|int16|2|RW|1|1|0-2
605E:00|FaultReactionOptionCode|int16|2|RO|1|0|0-4
# Tryb pracy
6060:00|ModesOfOperation|int8|1|RW|1|1|0-10 (1=PP,3=PV,6=HM)
6061:00|ModesOfOperationDisplay|int8|1|RO|1|1|0-10
# Pozycja
6062:00|PositionDemandValue|int32|4|RO|UserUnit|0|
6063:00|PositionActualEncoderValue|int32|4|RO|EncoderUnit|0|
6064:00|PositionActualUserValue|int32|4|RO|UserUnit|0|
6065:00|FollowingErrorWindow|uint32|4|RW|UserUnit|50|
6066:00|FollowingErrorTimeout|uint16|2|RW|1ms|10000|
6067:00|PositionWindow|uint32|4|RW|UserUnit|10|
6068:00|PositionWindowTime|uint16|2|RW|1ms|5|
# Prędkość
606B:00|VelocityDemandValue|int32|4|RO|UserUnit/s|0|
606C:00|VelocityActualValue|int32|4|RO|rpm|0|
606D:00|VelocityWindow|uint16|2|RW|rpm|100|
606E:00|VelocityWindowTime|uint16|2|RW|ms|5|
606F:00|VelocityThreshold|uint16|2|RW|UserUnit/s|5|
6070:00|VelocityThresholdTime|uint16|2|RW|ms|5|
# Pozycja docelowa i ograniczenia
607A:00|TargetPosition|int32|4|RW|UserUnit|0|
607B:01|MinPosRangLimit|int32|4|RW|UserUnit|-60000000|
607B:02|MaxPosRangLimit|int32|4|RW|UserUnit|60000000|
607C:00|HomeOffset|int32|4|RW|UserUnit|0|aktywne gdy 6041h bit15=1
607D:01|MinSoftwarePositionLimit|int32|4|RW|InstrUnit|-60000|
607D:02|MaxSoftwarePositionLimit|int32|4|RW|InstrUnit|60000|
607E:00|Polarity|uint8|1|RW|1|0|0-255
# Profil ruchu
607F:00|MaxProfileVelocity|uint32|4|RW|UserUnit/s|40000|CL=40000,OL=16000
6080:00|MaxMotorSpeed|uint32|4|RW|rpm|600|0-500
6081:00|ProfileVelocity|uint32|4|RW|UserUnit/s|12000|CL=12000,OL=4800
6082:00|EndVelocity|uint32|4|RW|UserUnit/s|0|
6083:00|ProfileAcceleration|uint32|4|RW|UserUnit/s2|40000|CL=40000,OL=16000
6084:00|ProfileDeceleration|uint32|4|RW|UserUnit/s2|120000|CL=120000,OL=48000
6085:00|QuickStopDeceleration|uint32|4|RW|UserUnit/s2|400000|CL=400000,OL=160000
6086:00|MotionProfileType|int16|2|RW|1|0|0=linear,3=S-curve
# Jednostki (encoder / gear)
608F:01|EncoderIncrements|uint32|4|RW|1|4000|CL=4000,OL=1600
608F:02|MotorRevolutions|uint32|4|RW|1|1|
6091:01|GearMotorRevolutions|uint32|4|RW|1|1|
6091:02|GearShaftRevolutions|uint32|4|RW|1|1|
# Homing
6098:00|HomingMethod|int8|1|RW|1|24|17-30
6099:01|HomingSearchSwitchSpeed|uint32|4|RW|UserUnit/s|12000|
6099:02|HomingSearchZeroSpeed|int32|4|RW|UserUnit/s|4000|
609A:00|HomingAcceleration|uint32|4|RW|UserUnit/s|80000|
# Jerk
60A3:00|ProfileJerkUse|uint8|1|RW|1|2|tylko 2 (60A4h:01h)
60A4:01|ProfileJerk1|uint32|4|RW|UserUnit/s3|15000|
60A4:02|ProfileJerk2|uint32|4|RW|UserUnit/s3|30000|
# Offsety
60B0:00|PositionOffset|int32|4|RW|UserUnit|0|
60B1:00|VelocityOffset|int32|4|RW|UserUnit|0|
# Interpolacja
60C1:01|InterpolationDataRecord|int32|4|RW|InstrUnit|0|
60C2:01|InterpolationTimePeriodValue|uint8|1|RW|1|20|0-255
60C2:02|InterpolationTimeIndex|int8|1|RW|10ns|-3|-128..127
# Maksymalne przyspieszenie/hamowanie
60C5:00|MaxAcceleration|uint32|4|RW|UserUnit/s2|500000|
60C6:00|MaxDeceleration|uint32|4|RW|UserUnit/s2|500000|
# Opcja pozycjonowania i wartości zwrotne
60F2:00|PositioningOptionCode|uint16|2|RW|1|0|0-2
60F4:00|FollowingErrorActualValue|int32|4|RO|UserUnit|0|
60FC:00|PositionDemandInternalValue|int32|4|RO|UserUnit|0|
60FF:00|TargetVelocity|int32|4|RW|UserUnit/s|0|
REGISTRY_EOF
}

# Parsowanie wiersza rejestru do zmiennych globalnych P_*.
read_param_line() {
    IFS='|' read -r P_KEY P_NAME P_TYPE P_SIZE P_ACCESS P_UNIT P_FACTORY P_OPTS <<<"$1"
}

# Czy zapytanie wygląda na "indeks[:subindeks]" (hex, opcjonalnie 0x i h)?
looks_numeric() {
    local q="$1"
    q="${q//0[xX]/}"
    q="${q//[hH]/}"
    q="${q//:/}"
    [[ "$q" =~ ^[0-9A-Fa-f]{4,6}$ ]]
}

# Wyszukuje parametr(y) w rejestrze. Wypisuje pasujące wiersze.
find_param() {
    local q="$1" nq idx sub line li ls
    if looks_numeric "$q"; then
        nq="${q^^}"
        nq="${nq//0X/}"
        nq="${nq//[H]/}"
        idx="${nq%%:*}"
        sub="${nq##*:}"
        [[ "$sub" == "$nq" ]] && sub=""
        for line in "${PARAMS[@]}"; do
            read_param_line "$line"
            li="${P_KEY%%:*}"
            ls="${P_KEY##*:}"
            if (( 16#$li == 16#$idx )); then
                if [[ -z "$sub" ]] || (( 16#$ls == 16#$sub )); then
                    printf '%s\n' "$line"
                fi
            fi
        done
    else
        local pat="${q,,}" exact="" line
        for line in "${PARAMS[@]}"; do
            read_param_line "$line"
            if [[ "${P_NAME,,}" == "$pat" ]]; then
                exact+="$line"$'\n'
            fi
        done
        if [[ -n "$exact" ]]; then
            printf '%s' "$exact"
        else
            for line in "${PARAMS[@]}"; do
                read_param_line "$line"
                if [[ "${P_NAME,,}" == *"$pat"* ]]; then
                    printf '%s\n' "$line"
                fi
            done
        fi
    fi
}

# Wypisuje dokładnie jeden wiersz. Zwraca 1 gdy brak, 2 gdy niejednoznaczne.
resolve_one() {
    local query="$1" matches n
    matches=$(find_param "$query")
    n=$(grep -c . <<<"$matches" || true)
    if (( n == 0 )); then
        return 1
    elif (( n > 1 )); then
        return 2
    fi
    printf '%s\n' "$matches"
    return 0
}

# Wysyła ramkę CAN i czeka na odpowiedź SDO. Nasłuch uruchamiany jest PRZED
# wysłaniem ramki. Wypisuje na stdout dane odpowiedzi (hex) i zwraca 0, gdy
# jej pierwszy bajt zgadza się z oczekiwanym.
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

# Odczyt wartości z obiektu <index>:<sub> (size = 1, 2 lub 4 bajty).
# Wypisuje wartość dziesiętną (ze znakiem dla int*) i zwraca 0/1.
read_sdo_uint() {
    local node="$1" idxh="$2" subh="$3" size="$4" signed="$5"
    local idx idxlo idxhi sub frame resp expected value
    idx=$((16#$idxh))
    idxlo=$(printf '%02X' $(( idx & 0xFF )))
    idxhi=$(printf '%02X' $(( (idx >> 8) & 0xFF )))
    sub=$(printf '%02X' $((16#$subh)))
    frame="$(printf '%X' $((0x600 + node)))#40${idxlo}${idxhi}${sub}00000000"
    case "$size" in
        1) expected="4F" ;;
        2) expected="4B" ;;
        4) expected="43" ;;
        *) return 1 ;;
    esac
    if ! resp=$(sdo_exchange "$node" "$frame" "$expected" 500); then
        return 1
    fi
    case "$size" in
        1) value=$((16#${resp:8:2})) ;;
        2) value=$((16#${resp:10:2}${resp:8:2})) ;;
        4) value=$((16#${resp:14:2}${resp:12:2}${resp:10:2}${resp:8:2})) ;;
    esac
    if [[ "$signed" == "signed" ]]; then
        case "$size" in
            1) (( value & 0x80 )) && value=$(( value - 0x100 )) ;;
            2) (( value & 0x8000 )) && value=$(( value - 0x10000 )) ;;
            4) (( value & 0x80000000 )) && value=$(( value - 0x100000000 )) ;;
        esac
    fi
    printf '%s' "$value"
    return 0
}

# Zapis wartości do obiektu <index>:<sub> (size = 1, 2 lub 4 bajty). Zwraca 0/1.
write_sdo_uint() {
    local node="$1" idxh="$2" subh="$3" size="$4" value="$5"
    local idx idxlo idxhi sub b0 b1 b2 b3 cmd frame resp
    idx=$((16#$idxh))
    idxlo=$(printf '%02X' $(( idx & 0xFF )))
    idxhi=$(printf '%02X' $(( (idx >> 8) & 0xFF )))
    sub=$(printf '%02X' $((16#$subh)))
    case "$size" in
        1)
            cmd="2F"
            b0=$(printf '%02X' $(( value & 0xFF )))
            frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}000000"
            ;;
        2)
            cmd="2B"
            b0=$(printf '%02X' $(( value & 0xFF )))
            b1=$(printf '%02X' $(( (value >> 8) & 0xFF )))
            frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}${b1}0000"
            ;;
        4)
            cmd="23"
            b0=$(printf '%02X' $(( value & 0xFF )))
            b1=$(printf '%02X' $(( (value >> 8) & 0xFF )))
            b2=$(printf '%02X' $(( (value >> 16) & 0xFF )))
            b3=$(printf '%02X' $(( (value >> 24) & 0xFF )))
            frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}${b1}${b2}${b3}"
            ;;
        *) return 1 ;;
    esac
    log "TX ${frame}"
    if resp=$(sdo_exchange "$node" "$frame" "60" 1000); then
        return 0
    fi
    if [[ "${resp:0:2}" == "80" ]]; then
        log "Abort SDO: ${resp}"
    else
        log "Brak potwierdzenia zapisu"
    fi
    return 1
}

# Konwersja wartości podanej przez użytkownika (dec lub 0x..) na liczbę.
parse_value() {
    local v="$1"
    if [[ "$v" =~ ^0[xX][0-9A-Fa-f]+$ ]]; then
        printf '%s' $((16#${v:2}))
        return 0
    elif [[ "$v" =~ ^-?[0-9]+$ ]]; then
        printf '%s' "$v"
        return 0
    fi
    return 1
}

value_in_range() {
    local type="$1" v="$2"
    case "$type" in
        uint8)  (( v >= 0 && v <= 255 )) ;;
        int8)   (( v >= -128 && v <= 127 )) ;;
        uint16) (( v >= 0 && v <= 65535 )) ;;
        int16)  (( v >= -32768 && v <= 32767 )) ;;
        uint32) (( v >= 0 && v <= 4294967295 )) ;;
        int32)  (( v >= -2147483648 && v <= 2147483647 )) ;;
        *) return 1 ;;
    esac
}

# Wypisuje linię odczytu jednego parametru.
read_param() {
    local node="$1" line="$2"
    read_param_line "$line"
    local signed="unsigned" value
    [[ "$P_TYPE" == int* ]] && signed="signed"
    if value=$(read_sdo_uint "$node" "${P_KEY%%:*}" "${P_KEY##*:}" "$P_SIZE" "$signed"); then
        printf '  %4sh:%02sh %-28s = %-12s [%s %s' \
            "${P_KEY%%:*}" "$((16#${P_KEY##*:}))" "$P_NAME" "$value" "$P_TYPE" "$P_SIZE"
        [[ -n "$P_ACCESS" ]] && printf ' %s' "$P_ACCESS"
        [[ -n "$P_UNIT" && "$P_UNIT" != "-" ]] && printf ', jedn. %s' "$P_UNIT"
        [[ -n "$P_FACTORY" && "$P_FACTORY" != "-" ]] && printf ', fabr. %s' "$P_FACTORY"
        printf ']'
        [[ -n "$P_OPTS" ]] && printf '\n    (%s)' "$P_OPTS"
        printf '\n'
        return 0
    fi
    printf '  %4sh:%02sh %-28s : brak odpowiedzi\n' \
        "${P_KEY%%:*}" "$((16#${P_KEY##*:}))" "$P_NAME"
    return 1
}

# Zapis jednego parametru z walidacją zakresu i dostępu.
write_param() {
    local node="$1" line="$2" value="$3"
    read_param_line "$line"
    if [[ "$P_ACCESS" == "RO" ]]; then
        log "${P_KEY} (${P_NAME}): parametr tylko do odczytu — pomijam zapis"
        return 1
    fi
    if ! value_in_range "$P_TYPE" "$value"; then
        log "${P_KEY} (${P_NAME}): wartość ${value} poza zakresem typu ${P_TYPE}"
        return 1
    fi
    log "${P_KEY} ${P_NAME} <- ${value} (${P_TYPE})"
    if write_sdo_uint "$node" "${P_KEY%%:*}" "${P_KEY##*:}" "$P_SIZE" "$value"; then
        log "Zapis ${P_KEY} ${P_NAME} = ${value}: OK"
        return 0
    fi
    return 1
}

# Weryfikacja zapisu przez odczyt zwrotny.
verify_param() {
    local node="$1" line="$2" expected="$3"
    read_param_line "$line"
    local signed="unsigned" value
    [[ "$P_TYPE" == int* ]] && signed="signed"
    if value=$(read_sdo_uint "$node" "${P_KEY%%:*}" "${P_KEY##*:}" "$P_SIZE" "$signed"); then
        if (( value == expected )); then
            log "Weryfikacja ${P_KEY} ${P_NAME} = ${value}: OK"
            return 0
        fi
        log "Weryfikacja ${P_KEY} NIEZGODNA: zapisano ${expected}, odczytano ${value}"
        return 1
    fi
    log "Brak odpowiedzi przy weryfikacji ${P_KEY}"
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

list_registry() {
    local line
    printf '%-9s %-28s %-8s %-4s %-6s %-10s %-12s %s\n' \
        "Index" "Nazwa" "Typ" "Bajt" "Dostęp" "Jedn." "Fabrycznie" "Opcje/opis"
    printf '%s\n' "----------------------------------------------------------------------------------------------------------------"
    for line in "${PARAMS[@]}"; do
        read_param_line "$line"
        printf '%-9s %-28s %-8s %-4s %-6s %-10s %-12s %s\n' \
            "${P_KEY%%:*}h:${P_KEY##*:}h" "$P_NAME" "$P_TYPE" "$P_SIZE" "$P_ACCESS" "$P_UNIT" "$P_FACTORY" "$P_OPTS"
    done
    printf '\nŁącznie: %d parametrów.\n' "${#PARAMS[@]}"
}

main() {
    load_registry

    local ids=()
    local iface=""
    local read_sel=""
    local writes=()
    local do_save=0
    local do_list=0
    local node

    while (( $# )); do
        case "$1" in
            --help|-h) usage; exit 0 ;;
            --list)     do_list=1; shift ;;
            --read)     read_sel+=" $2"; shift 2 ;;
            --write)    writes+=("$2"); shift 2 ;;
            --mode)     writes+=("6060:00=$2"); shift 2 ;;
            --save)     do_save=1; shift ;;
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

    if (( do_list )); then
        list_registry
        exit 0
    fi

    [[ -n "$iface" ]] && CAN_IF="$iface"
    [[ ${#ids[@]} -eq 0 ]] && ids=(1 2)

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    if (( ${#writes[@]} > 0 )); then
        # ── Tryb zapisu ──────────────────────────────────────────────
        [[ ${#ids[@]} -eq 1 ]] || die "Tryb zapisu wymaga dokładnie jednego node_id."
        node="${ids[0]}"
        is_valid_node_id "$node" || die "Nieprawidlowy node_id: $node (dopuszczalne 1..127)"

        local wr_lines=()
        local wr_values=()
        local spec param value value_dec line
        for spec in "${writes[@]}"; do
            [[ "$spec" == *"="* ]] || die "Format zapisu: --write <parametr>=<wartość> (brak '=' w: ${spec})"
            param="${spec%%=*}"
            value="${spec#*=}"
            if ! line=$(resolve_one "$param"); then
                die "Nie znaleziono jednoznacznego parametru: $param"
            fi
            if ! value_dec=$(parse_value "$value"); then
                die "Nieprawidłowa wartość: $value (dla parametru $param)"
            fi
            wr_lines+=("$line")
            wr_values+=("$value_dec")
            write_param "$node" "$line" "$value_dec"
        done

        (( do_save )) && save_eeprom "$node"

        log "Stan po zapisie (node ${node}):"
        local i
        for i in "${!wr_lines[@]}"; do
            verify_param "$node" "${wr_lines[$i]}" "${wr_values[$i]}" || true
        done
    else
        # ── Tryb odczytu ─────────────────────────────────────────────
        read_sel="${read_sel//,/ }"
        local query matches line
        for node in "${ids[@]}"; do
            is_valid_node_id "$node" || { log "Pominięto nieprawidłowy node_id: $node"; continue; }
            if [[ -z "$read_sel" ]]; then
                log "node ${node}: odczyt grupy 6000h (${#PARAMS[@]} parametrów)"
                for line in "${PARAMS[@]}"; do
                    read_param "$node" "$line" || true
                done
            else
                log "node ${node}: odczyt wybranych parametrów"
                for query in $read_sel; do
                    matches=$(find_param "$query")
                    if [[ -z "$matches" ]]; then
                        log "Nie znaleziono parametru: $query"
                        continue
                    fi
                    while IFS= read -r line; do
                        read_param "$node" "$line" || true
                    done <<<"$matches"
                done
            fi
        done
    fi
}

main "$@"
