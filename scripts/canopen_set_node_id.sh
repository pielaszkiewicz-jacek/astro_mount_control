#!/usr/bin/env bash
#
# canopen_set_node_id.sh — ustawia adres osi (Node-ID) kontrolera NiMotion STMP42SXI
# (CANopen/CiA402).
#
# Źródło protokołu: docs/1787619745915-ymnq3q.pdf (rozdział 10.2, 200Ch).
#   - Adres osi jest zapisywany w obiekcie 200Ch:02h ("驱动器轴地址", uint16, 1..247).
#   - Dla CANopen obowiązuje zakres Node-ID 1..127 (COB-ID 7-bitowe).
#   - Aktywacja następuje po ponownym włączeniu zasilania ("再次通电").
#   - SDO write: COB-ID = 0x600 + biezacy_node_id, dane little-endian.
#   - Zapis 2 bajtow -> command specifier 0x2B.
#   - Zapis parametrow do EEPROM: 1010h:01h = 0x65766173 ("save").
#   - Restart wezla: NMT COB-ID 0x000, data: 81 <node_id>.
#
# Użycie:
#   ./scripts/canopen_set_node_id.sh <biezacy_id> <nowy_id> [interfejs_can] [--no-save] [--no-reset]
#
# Przykłady:
#   ./scripts/canopen_set_node_id.sh 1 1 can0     # ustaw ID=1
#   ./scripts/canopen_set_node_id.sh 1 2 can0     # ustaw ID=2
#   CAN_IF=can1 ./scripts/canopen_set_node_id.sh 1 2   # przez zmienna srodowiskowa

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_set_node_id.sh <biezacy_id> <nowy_id> [interfejs_can] [opcje]

Argumenty:
  biezacy_id       aktualny Node-ID kontrolera (CANopen: 1..127)
  nowy_id          nowy Node-ID do ustawienia (CANopen: 1..127)
  interfejs_can    interfejs CAN (domyslnie: can0 lub zmienna CAN_IF)

Opcje:
  --no-save        nie zapisuj parametrów do EEPROM (1010h:01h)
  --no-reset       nie wysyłaj NMT reset node (0x81)

Przykłady:
  ./scripts/canopen_set_node_id.sh 1 1 can0
  ./scripts/canopen_set_node_id.sh 1 2 can0
  CAN_IF=can1 ./scripts/canopen_set_node_id.sh 1 2
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

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

main() {
    local current_id=""
    local new_id=""
    local iface=""
    local do_save=1
    local do_reset=1

    for arg in "$@"; do
        case "$arg" in
            --no-save)  do_save=0 ;;
            --no-reset) do_reset=0 ;;
            *)
                if [[ -z "$current_id" ]]; then
                    current_id="$arg"
                elif [[ -z "$new_id" ]]; then
                    new_id="$arg"
                elif [[ -z "$iface" ]]; then
                    iface="$arg"
                else
                    die "Nierozpoznany argument: $arg"
                fi
                ;;
        esac
    done

    [[ -n "$current_id" && -n "$new_id" ]] || { usage; exit 1; }
    [[ -n "$iface" ]] && CAN_IF="$iface"

    is_valid_node_id "$current_id" || die "Nieprawidlowy biezacy_id: $current_id (dopuszczalne 1..127)"
    is_valid_node_id "$new_id"      || die "Nieprawidlowy nowy_id: $new_id (dopuszczalne 1..127)"

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    log "Konfiguracja Node-ID: $current_id -> $new_id (interfejs: ${CAN_IF})"

    # 1) Zapis Node-ID do 200Ch:02h (uint16, little-endian).
    log "Zapis Node-ID do obiektu 200Ch:02h"
    sdo_write "$current_id" 0x200C 0x02 0x2B "$(hex16le "$new_id")" \
        && log "Potwierdzenie zapisu Node-ID: OK" \
        || log "UWAGA: brak potwierdzenia zapisu Node-ID"

    # 2) Zapis parametrow do EEPROM (1010h:01h = 0x65766173).
    if (( do_save )); then
        log "Zapis parametrow do EEPROM (1010h:01h)"
        sdo_write "$current_id" 0x1010 0x01 0x23 "$(hex32le 0x65766173)" \
            && log "Zapis do EEPROM: OK" \
            || log "UWAGA: brak potwierdzenia zapisu EEPROM"
    fi

    # 3) Restart wezla — nowe ID wejdzie w zycie po restarcie.
    if (( do_reset )); then
        log "NMT reset node (0x81) dla ID=${current_id}"
        cansend "${CAN_IF}" "000#81$(printf '%02X' "$current_id")"
        sleep 1
    fi

    # 4) Weryfikacja — odczyt 200Ch:02h z nowego COB-ID.
    if (( do_reset )); then
        log "Weryfikacja: odczyt 200Ch:02h z nowego adresu"
        local resp lo hi value
        if resp=$(sdo_exchange "$new_id" "$(printf '%X' $((0x600 + new_id)))#400C200200000000" "4B" 1000); then
            lo="${resp:8:2}"
            hi="${resp:10:2}"
            value=$((16#${hi}${lo}))
            log "Weryfikacja OK: kontroler raportuje Node-ID=${value}"
        else
            log "UWAGA: brak odpowiedzi z nowego Node-ID=${new_id} (wykonaj reset recznie)"
        fi
    fi

    log "Zakonczono. Nowy Node-ID=${new_id} bedzie aktywny po restarcie kontrolera."
}

main "$@"
