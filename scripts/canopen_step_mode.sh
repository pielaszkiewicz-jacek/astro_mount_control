#!/usr/bin/env bash
#
# canopen_step_mode.sh — odczyt i zapis parametrów grupy 10.2
# "Manufacturer Defined Parameter Group 2000h Description"
# napędów NiMotion STM42 (CANopen/CiA402) przez SDO.
#
# Źródło: docs/stm42-canopen-protocol.pdf, rozdział 10.2 (tabela 2000h–2021h).
#
# Parametry obejmują m.in.:
#   2000h   RatedVoltage, MaxMotorSpeed (RO)
#   2001h   prądy (Run/Hold/Lock/Over), StepMode (mikrokrok), progi napięć
#   2002h   tryb sterowania (CtrlModeSelec), pętla zamknięta (CloseLoopEn), wersje
#   2003h/2004h/2017h/2021h  konfiguracja wejść/wyjść cyfrowych i wirtualnych DI
#   2005h/2006h  parametry trybu Nimotion (krok/prędkość/rampy)
#   2008h   regulatory PID, filtry, ograniczenia prędkości i prądu
#   200Bh   parametry monitorowane (prędkość, stany DI/DO, napięcie szyny — RO)
#   200Ch   konfiguracja komunikacji (Node-ID, baud-rate, protokół)
#   200Eh   mapowanie kodów błędów (uint32)
#
# Odczyt SDO (upload expedited):
#   Żądanie:   COB-ID = 0x600 + node, dane: 40 <idx_lo> <idx_hi> <sub> 00 00 00 00
#   Odpowiedź: COB-ID = 0x580 + node, dane: 4B/43 <idx_lo> <idx_hi> <sub> <b0..b3>
#   (4B = 2 bajty danych, 43 = 4 bajty danych, little-endian)
#
# Zapis SDO (download expedited):
#   2 bajty: COB-ID = 0x600 + node, dane: 2B <idx_lo> <idx_hi> <sub> <b0> <b1> 00 00
#   4 bajty: COB-ID = 0x600 + node, dane: 23 <idx_lo> <idx_hi> <sub> <b0> <b1> <b2> <b3>
#   Potwierdzenie: 60 <idx_lo> <idx_hi> <sub> 00 00 00 00
#
# Użycie:
#   ./scripts/canopen_step_mode.sh <node_id ...> [interfejs_can]               # odczyt wszystkich
#   ./scripts/canopen_step_mode.sh <node_id> --read <param,...> [iface]        # odczyt wybranych
#   ./scripts/canopen_step_mode.sh <node_id> --write <param>=<wartość> [iface] # zapis
#   ./scripts/canopen_step_mode.sh --list                                     # lista parametrów
#   ./scripts/canopen_step_mode.sh <node_id> --step-mode <n> [iface] [--save]  # alias zapisu
#
# Format <param>: "2001:08", "0x2001:0x08", "2001h:08h", "2001" (cały indeks)
#                 lub nazwa (np. "StepMode", "RunCurrentVal").
#
# Przykłady:
#   ./scripts/canopen_step_mode.sh 1 can0                    # pełny zrzut grupy 2000h
#   ./scripts/canopen_step_mode.sh 1 --read 2001:08 can0     # tylko StepMode
#   ./scripts/canopen_step_mode.sh 1 --read StepMode,200C:02 can0
#   ./scripts/canopen_step_mode.sh 1 --write 2001:08=3 can0  # 1/8 step
#   ./scripts/canopen_step_mode.sh 1 --write StepMode=4 --save can0

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_step_mode.sh <node_id ...> [interfejs_can]
  canopen_step_mode.sh <node_id ...> --read <param,...> [interfejs_can]
  canopen_step_mode.sh <node_id> --write <param>=<wartość> [interfejs_can] [--save]
  canopen_step_mode.sh <node_id> --step-mode <n> [interfejs_can] [--save]
  canopen_step_mode.sh --list

Argumenty:
  node_id            adresy CANopen (1..127). W trybie zapisu dokładnie jeden.
  interfejs_can      interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --list             wypisz wszystkie parametry grupy 2000h (bez dostępu do CAN)
  --read <param,...> odczyt wybranych parametrów (domyślnie: wszystkie)
  --write <p>=<w>    zapis parametru; <p> = "2001:08", "StepMode" itp.,
                     <w> = wartość dziesiętna lub 0x... (hex)
  --step-mode <n>    alias: --write 2001:08=<n> (mikrokrok)
  --save             po zapisie zapisz parametry do EEPROM (1010h:01h = 0x65766173)

Format <param>:
  2001:08            indeks i subindeks (hex)
  0x2001:0x08        jawnie hex
  2001h:08h          notacja dokumentacji
  2001               cały indeks (wszystkie subindeksy)
  StepMode           nazwa (lub fragment nazwy)

Przykłady:
  ./scripts/canopen_step_mode.sh 1 can0
  ./scripts/canopen_step_mode.sh 1 --read 2001:08 can0
  ./scripts/canopen_step_mode.sh 1 --write StepMode=3 can0
  ./scripts/canopen_step_mode.sh 1 --write 0x2001:0x08=4 --save can0
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
# Rejestr parametrów grupy 2000h (10.2).
# Format wiersza: INDEX:SUB|NAME|TYPE|SIZE|ACCESS|UNIT|FACTORY|OPTS
#   TYPE  : uint16|int16|uint32|int32
#   SIZE  : liczba bajtów danych SDO (2 lub 4)
#   ACCESS: RO|RW (lub "-" gdy dokumentacja nie podaje)
#   OPTS  : dopuszczalne wartości / opis (może być puste)
# ─────────────────────────────────────────────────────────────────────────────
declare -a PARAMS

load_registry() {
    PARAMS=()
    local line
    while IFS= read -r line; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        PARAMS+=("$line")
    done <<'REGISTRY_EOF'
# 2000h — parametry informacyjne (RO)
2000:01|RatedVoltage|uint16|2|RO|1V|24|
2000:02|MaxMotorSpeed|uint16|2|RO|1rpm|2000|
# 2001h — napięcia i prądy, mikrokrok
2001:01|BusOverVoltVal|uint16|2|RW|1V|30|10=alarm >10V,30=alarm >30V
2001:02|BusLowVoltVal|uint16|2|RW|1V|10|0=brak alarmu,10=alarm <10V
2001:03|RunCurrentVal|uint16|2|RW|0.001A|500|
2001:04|Reserve1|uint16|2|RW|-|-|
2001:05|Reserve2|uint16|2|RW|-|-|
2001:06|HoldCurrentVal|uint16|2|RW|0.001A|500|
2001:07|OverCurrentVal|uint16|2|RW|0.1A|40|
2001:08|StepMode|uint16|2|RW|-|3|0=full step,1=half step,2=1/4 step,3=1/8 step,4=1/16 step
2001:09|LockCurrentVal|uint16|2|RW|0.001A|800|
# 2002h — tryb sterowania i wersje
2002:01|CtrlModeSelec|uint16|2|RW|-|0|0=CiA402,1=Nimotion position,2=Nimotion velocity
2002:02|CloseLoopEn|uint16|2|RW|-|1|0=open-loop,1=closed-loop
2002:03|IAPVersion|uint32|4|RO|-|-|
2002:04|Reserved|uint32|4|RW|-|-|
2002:05|HardwareVersion|uint32|4|RO|-|-|
# 2003h — wejścia cyfrowe
2003:01|DI1FunSelec|uint16|2|RW|-|0|
2003:02|DI1LogicSelec|uint16|2|RW|-|0|0-4
2003:03|DI2FunSelec|uint16|2|RW|-|0|
2003:04|DI2LogicSelec|uint16|2|RW|-|0|0-4
2003:05|DI3FunSelec|uint16|2|RW|-|0|
2003:06|DI3LogicSelec|uint16|2|RW|-|0|0-4
2003:07|DI4FunSelec|uint16|2|RW|-|0|
2003:08|DI4LogicSelec|uint16|2|RW|-|0|0-4
2003:09|DI5FunSelec|uint16|2|RW|-|0|
2003:0A|DI5LogicSelec|uint16|2|RW|-|0|0-4
2003:0B|IsDISetPullUp|uint16|2|RW|-|0x07|0-7
# 2004h — wyjścia cyfrowe
2004:01|DoStateCommSet|uint16|2|RW|-|0|
2004:02|DO1FunSelec|uint16|2|RW|-|0|0-5
2004:03|DO1LogicSelec|uint16|2|RW|-|1|0-1
2004:04|DO2FunSelec|uint16|2|RW|-|0|0-5
2004:05|DO2LogicSelec|uint16|2|RW|-|1|0-1
# 2005h — tryb Nimotion: krok/prędkość
2005:02|StepAmount|int32|4|RW|-|50|
2005:03|StepSpd|uint32|4|RW|rpm|100|
# 2006h — źródło prędkości i rampy
2006:01|SpeedRefSource|uint16|2|RW|-|0|0-1
2006:02|KeypadSpeedRef|int16|2|RW|-|0|-600..600
2006:03|SpeedRefAccelRampTime|uint16|2|RW|ms|200|
2006:04|SpeedRefDecelRampTime|uint16|2|RW|ms|200|
# 2008h — regulatory i filtry
2008:01|PosLoopGain|uint16|2|RW|0.00001|200|
2008:02|Kpc|uint16|2|RW|0.01|500|
2008:03|CtrlSpdFdBckFilterTime|uint16|2|RW|ms|10|
2008:04|DipSpdFdBckFilterTime|uint16|2|RW|ms|25|
2008:05|LockedSpdErrScale|uint16|2|RW|1%|40|
2008:06|LockedSpdThreshold|uint16|2|RW|rpm|6|
2008:07|LockedTime|uint16|2|RW|ms|2000|
2008:08|Reserve1|uint16|2|RW|-|-|
2008:09|SpdFdFwrGain|uint16|2|RW|0.01|0|
2008:0A|SpdFdFwrFilterCoeff|uint16|2|RW|0.001|100|
2008:0B|SpdFilterCoeff|uint16|2|RW|0.001|500|
2008:0C|SpdLimitThreshold|uint16|2|RW|rpm/ms|200|
2008:0D|CurrentFdFwrGain|uint16|2|RW|0.1|4000|
2008:0E|CurrentFilterCoeff|uint16|2|RW|0.001|1000|
# 200Bh — parametry monitorowane (RO)
200B:01|DriverState|uint16|2|RO|-|-|
200B:02|ActualMotorSpeed|int16|2|RO|rpm|-|
200B:03|MonitoredDiStates|uint16|2|RO|-|-|
200B:04|MonitoredDoStates|uint16|2|RO|-|-|
200B:05|MonitoredVDiStates|uint16|2|RO|-|-|
200B:06|TotalRUNTime|uint32|4|RO|15min|-|
200B:07|PluseInDuty|uint16|2|RO|%|-|
200B:08|BusVoltage|uint16|2|RO|V|-|
# 200Ch — konfiguracja komunikacji
200C:01|CommunicationSelect|uint16|2|RW|-|2|1=EtherCAT,2=CAN,3=serial
200C:02|ServoShaftAddress|uint16|2|RW|-|1|1-127 (Node-ID, restart)
200C:03|SerialPortBaudRate|uint16|2|RW|-|0x08|0=10k,1=20k,2=50k,3=100k,4=125k,5=250k,6=500k,7=800k,8=1M
200C:04|ModbusDataFormat|uint16|2|RW|-|0|0-3
# 200Eh — mapowanie kodów błędów
200E:01|OverCurrent|uint32|4|RW|-|0x2300|
200E:02|OverVoltage|uint32|4|RW|-|0x13110|
200E:03|UnderVoltage|uint32|4|RW|-|0x13120|
200E:04|ThermalWarning|uint32|4|RW|-|0x4310|
200E:05|CAN_ErrPassiveSet|uint32|4|RW|-|0x18120|
200E:06|CAN_Overrun|uint32|4|RW|-|0x18130|
200E:07|CAN_Busoff|uint32|4|RW|-|0x18140|
200E:08|EERROM_Malfunction|uint32|4|RW|-|0x15530|
200E:09|EEPROMSelfCheckError|uint32|4|RW|-|0x300FF04|
200E:0A|Motorlocked|uint32|4|RW|-|0x03017121|
200E:0B|FlashInitError|uint32|4|RW|-|0x15540|
200E:0C|DinSetError|uint32|4|RW|-|0x3016320|
200E:0D|ThermalShutdown|uint32|4|RW|-|0xFF00|
200E:0E|DriverChipWrongCMD|uint32|4|RW|-|0xFF01|
200E:0F|DriverChipNotperfCMD|uint32|4|RW|-|0xFF02|
200E:10|DriverChipSPIError|uint32|4|RW|-|0xFF03|
200E:11|DriverChipSelfCheckError|uint32|4|RW|-|0x1FF07|
200E:12|EncoderSelfCheckError|uint32|4|RW|-|0x1FF06|
200E:13|EncoderFrameError|uint32|4|RW|-|0x1FF08|
200E:14|EncoderCRCError|uint32|4|RW|-|0x1FF09|
200E:15|EncoderCMDError|uint32|4|RW|-|0x1FF0A|
200E:16|EncoderMagTooHighError|uint32|4|RW|-|0x1FF0C|
200E:17|EncoderMagTooLowError|uint32|4|RW|-|0x1FF0D|
200E:18|NegativeLimitSwitch|uint32|4|RW|-|0x301FF0E|
200E:19|PositiveLimitSwitch|uint32|4|RW|-|0x301FF0F|
200E:1A|PosRangLimit|uint32|4|RW|-|0x401FF10|
# 2017h — wirtualne wejścia cyfrowe
2017:01|VDinEn|uint16|2|RW|-|1|0-1
2017:02|VDI_VirtualLevelCommSet|uint16|2|RW|-|0|
2017:03|VDI1FunSelec|uint16|2|RW|-|33|
2017:04|VDI1LogicSelec|uint16|2|RW|-|1|0-1
2017:05|VDI2FunSelec|uint16|2|RW|-|2|
2017:06|VDI2LogicSelec|uint16|2|RW|-|1|0-1
2017:07|VDI3FunSelec|uint16|2|RW|-|38|
2017:08|VDI3LogicSelec|uint16|2|RW|-|1|0-1
2017:09|VDI4FunSelec|uint16|2|RW|-|0|
2017:0A|VDI4LogicSelec|uint16|2|RW|-|0|0-1
2017:0B|VDI5FunSelec|uint16|2|RW|-|0|
2017:0C|VDI5LogicSelec|uint16|2|RW|-|0|0-1
2017:0D|VDI6FunSelec|uint16|2|RW|-|0|
2017:0E|VDI6LogicSelec|uint16|2|RW|-|0|0-1
# 2021h — kierunki linii DX
2021:01|DX1InOutSelec|uint16|2|RW|-|1|0-1
2021:02|DX2InOutSelec|uint16|2|RW|-|1|0-1
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

# Odczyt wartości z obiektu <index>:<sub> (size = 2 lub 4 bajty).
# Wypisuje wartość dziesiętną (ze znakiem dla int*) i zwraca 0/1.
read_sdo_uint() {
    local node="$1" idxh="$2" subh="$3" size="$4" signed="$5"
    local idx idxlo idxhi sub frame resp expected value
    idx=$((16#$idxh))
    idxlo=$(printf '%02X' $(( idx & 0xFF )))
    idxhi=$(printf '%02X' $(( (idx >> 8) & 0xFF )))
    sub=$(printf '%02X' $((16#$subh)))
    frame="$(printf '%X' $((0x600 + node)))#40${idxlo}${idxhi}${sub}00000000"
    expected="4B"
    (( size == 4 )) && expected="43"
    if ! resp=$(sdo_exchange "$node" "$frame" "$expected" 500); then
        return 1
    fi
    if (( size == 4 )); then
        value=$((16#${resp:14:2}${resp:12:2}${resp:10:2}${resp:8:2}))
    else
        value=$((16#${resp:10:2}${resp:8:2}))
    fi
    if [[ "$signed" == "signed" ]]; then
        if (( size == 4 )); then
            (( value & 0x80000000 )) && value=$(( value - 0x100000000 ))
        else
            (( value & 0x8000 )) && value=$(( value - 0x10000 ))
        fi
    fi
    printf '%s' "$value"
    return 0
}

# Zapis wartości do obiektu <index>:<sub> (size = 2 lub 4 bajty). Zwraca 0/1.
write_sdo_uint() {
    local node="$1" idxh="$2" subh="$3" size="$4" value="$5"
    local idx idxlo idxhi sub b0 b1 b2 b3 cmd frame resp
    idx=$((16#$idxh))
    idxlo=$(printf '%02X' $(( idx & 0xFF )))
    idxhi=$(printf '%02X' $(( (idx >> 8) & 0xFF )))
    sub=$(printf '%02X' $((16#$subh)))
    if (( size == 4 )); then
        cmd="23"
        b0=$(printf '%02X' $(( value & 0xFF )))
        b1=$(printf '%02X' $(( (value >> 8) & 0xFF )))
        b2=$(printf '%02X' $(( (value >> 16) & 0xFF )))
        b3=$(printf '%02X' $(( (value >> 24) & 0xFF )))
        frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}${b1}${b2}${b3}"
    else
        cmd="2B"
        b0=$(printf '%02X' $(( value & 0xFF )))
        b1=$(printf '%02X' $(( (value >> 8) & 0xFF )))
        frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}${b1}0000"
    fi
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
        printf '  %4sh:%02sh %-24s = %-12s [%s %s' \
            "${P_KEY%%:*}" "$((16#${P_KEY##*:}))" "$P_NAME" "$value" "$P_TYPE" "$P_SIZE"
        [[ -n "$P_ACCESS" ]] && printf ' %s' "$P_ACCESS"
        [[ -n "$P_UNIT" && "$P_UNIT" != "-" ]] && printf ', jedn. %s' "$P_UNIT"
        [[ -n "$P_FACTORY" && "$P_FACTORY" != "-" ]] && printf ', fabr. %s' "$P_FACTORY"
        printf ']'
        [[ -n "$P_OPTS" ]] && printf '\n    (%s)' "$P_OPTS"
        printf '\n'
        return 0
    fi
    printf '  %4sh:%02sh %-24s : brak odpowiedzi\n' \
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
    printf '%-9s %-24s %-8s %-4s %-6s %-9s %-12s %s\n' \
        "Index" "Nazwa" "Typ" "Bajt" "Dostęp" "Jedn." "Fabrycznie" "Opcje/opis"
    printf '%s\n' "---------------------------------------------------------------------------------------------------------"
    for line in "${PARAMS[@]}"; do
        read_param_line "$line"
        printf '%-9s %-24s %-8s %-4s %-6s %-9s %-12s %s\n' \
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
            --step-mode) writes+=("2001:08=$2"); shift 2 ;;
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
                log "node ${node}: odczyt grupy 2000h (${#PARAMS[@]} parametrów)"
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
