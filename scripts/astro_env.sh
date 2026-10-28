#!/usr/bin/env bash
#
# astro_env.sh — Zarządzanie całym środowiskiem Astro Mount Controller.
#
# Umożliwia uruchamianie (start), zatrzymywanie (stop) oraz restartowanie
# (restart) całego środowiska lub wybranych komponentów. Domyślnie sterownik
# montażu uruchamiany jest z konfiguracją STM (config/mount/canopen_stm42m.json).
#
# Komponenty:
#   mount       astro_mount_controller          (gRPC 50051, konfiguracja STM)
#   db          astro_object_database_server    (gRPC 50052)
#   weather     astro_weather_server            (gRPC 50055)
#   power       astro_power_server              (gRPC 50056)
#   sequencer   astro_sequencer_server          (gRPC 50057)
#   derotator   astro_derotator_server          (gRPC 50054, opcjonalny — domyślnie WYŁĄCZONY,
#                                                bo derotator działa w procesie sterownika na 50051)
#   web         proxy WWW (Node.js)             (HTTP 8080)
#
# Użycie:
#   ./scripts/astro_env.sh [opcje] <komenda> [komponenty...]
#
# Komendy:
#   start     Uruchom środowisko (domyślnie wszystkie komponenty)
#   stop      Zatrzymaj środowisko (domyślnie wszystkie komponenty)
#   restart   Zrestartuj środowisko (domyślnie wszystkie komponenty)
#   status    Pokaż status komponentów
#   build     Zbuduj projekt (cmake + make)
#   list      Wyświetl listę komponentów
#
# Przykłady:
#   ./scripts/astro_env.sh start
#   ./scripts/astro_env.sh stop
#   ./scripts/astro_env.sh restart
#   ./scripts/astro_env.sh restart mount weather
#   ./scripts/astro_env.sh start --config config/mount/default.json
#   ./scripts/astro_env.sh start mount db
#   ./scripts/astro_env.sh start derotator      # uruchom wyłącznie derotator
#   ./scripts/astro_env.sh --build start
#
# Katalog lokalny:
#   Środowisko działa domyślnie z katalogu ~/astro-mount-controller
#   (tworzonego przez 'make localuser'). Inny katalog można wskazać zmienną
#   środowiskową ASTRO_MOUNT_HOME (export ASTRO_MOUNT_HOME=/ścieżka/katalogu).
# ============================================================================

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

# Katalog lokalny kontrolera (deployment per-user, tworzony przez 'make localuser').
# Można nadpisać zmienną środowiskową ASTRO_MOUNT_HOME; domyślnie ~/astro-mount-controller.
CONTROLLER_HOME="${ASTRO_MOUNT_HOME:-$HOME/astro-mount-controller}"

# Katalogi runtime'owe (PID, logi) zawsze w katalogu kontrolera.
RUN_DIR="${CONTROLLER_HOME}/run"
LOG_DIR="${CONTROLLER_HOME}/logs"
SYSLOG_DIR="/var/log/astro-mount"

first_dir() {
    local d
    for d in "$@"; do
        [ -d "$d" ] && { echo "$d"; return 0; }
    done
    return 1
}

# BIN_DIR: preferuj wdrożone binarki (make localuser), w razie braku — build/.
if [ -x "${CONTROLLER_HOME}/bin/astro_mount_controller" ]; then
    BIN_DIR="${CONTROLLER_HOME}/bin"
else
    BIN_DIR="${BUILD_DIR}/bin"
fi

# CONFIG_DIR: preferuj konfigurację we wdrożonym katalogu, w razie braku — źródło.
CONFIG_DIR="$(first_dir "${CONTROLLER_HOME}/config" "${PROJECT_DIR}/config" || echo "${PROJECT_DIR}/config")"

# DATA_DIR: preferuj dane we wdrożonym katalogu, w razie braku — źródło.
DATA_DIR="$(first_dir "${CONTROLLER_HOME}/data" "${PROJECT_DIR}/data" || echo "${PROJECT_DIR}/data")"

# Katalog roboczy procesów C++ — musi zawierać podkatalog config/ (main.cpp
# czyta np. config/dome/dome_config.json względem bieżącego katalogu).
RUNTIME_ROOT="$(dirname "${CONFIG_DIR}")"

DEFAULT_MOUNT_CONFIG="${CONFIG_DIR}/mount/canopen_stm42m.json"
MOUNT_CONFIG="${DEFAULT_MOUNT_CONFIG}"
DO_BUILD=false

# Kolejność startu.
COMPONENT_ORDER=(mount db weather power sequencer derotator web)

# Kolejność zatrzymywania — najpierw klienci, potem serwery od których zależą.
# Dzięki temu gRPC Shutdown() serwerów (weather/power) nie czeka na klienta
# sterownika montażu.
STOP_ORDER=(web sequencer mount power weather derotator db)

# Komponenty uruchamiane domyślnie (całe środowisko bez samodzielnego derotatora).
DEFAULT_COMPONENTS=(mount db weather power sequencer web)

# --- Kolory ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}${*}${NC}"; }
ok()    { echo -e "${GREEN}${*}${NC}"; }
warn()  { echo -e "${YELLOW}${*}${NC}" >&2; }
error() { echo -e "${RED}${*}${NC}" >&2; }

usage() {
    sed -n '2,43p' "$0" | sed 's/^# \?//'
    exit 0
}

component_default_enabled() {
    case "$1" in
        derotator) return 1 ;;
        *) return 0 ;;
    esac
}

component_description() {
    case "$1" in
        mount)     echo "sterownik montażu (gRPC 50051)" ;;
        db)        echo "baza obiektów astronomicznych (gRPC 50052)" ;;
        weather)   echo "serwer pogodowy (gRPC 50055)" ;;
        power)     echo "serwer zasilania (gRPC 50056)" ;;
        sequencer) echo "sekwenser obserwacji (gRPC 50057)" ;;
        derotator) echo "derotator — proces samodzielny (gRPC 50054)" ;;
        web)       echo "proxy WWW (HTTP 8080)" ;;
        *)         echo "" ;;
    esac
}

is_known_component() {
    for c in "${COMPONENT_ORDER[@]}"; do
        [[ "$c" == "$1" ]] && return 0
    done
    return 1
}

resolve_mount_config() {
    local val="$1"
    case "$val" in
        stm|stm42m|canopen_stm42m) echo "${CONFIG_DIR}/mount/canopen_stm42m.json" ;;
        default)                   echo "${CONFIG_DIR}/mount/default.json" ;;
        *)
            if [[ "$val" == /* ]] && [ -f "$val" ]; then
                echo "$val"
            elif [ -f "$val" ]; then
                echo "$val"
            elif [ -f "${CONFIG_DIR}/$val" ]; then
                echo "${CONFIG_DIR}/$val"
            elif [ -f "${CONFIG_DIR}/mount/$val" ]; then
                echo "${CONFIG_DIR}/mount/$val"
            elif [ -f "${PROJECT_DIR}/config/$val" ]; then
                echo "${PROJECT_DIR}/config/$val"
            elif [ -f "${PROJECT_DIR}/config/mount/$val" ]; then
                echo "${PROJECT_DIR}/config/mount/$val"
            else
                echo "$val"
            fi
            ;;
    esac
}

# Resolve listę komponentów. Tryb:
#   default — bez argumentów zwraca komponenty domyślne (DEFAULT_COMPONENTS),
#   all     — bez argumentów zwraca wszystkie znane komponenty (COMPONENT_ORDER).
resolve_component_list() {
    local mode="$1"
    shift
    local -a result=()
    if [ "$#" -eq 0 ]; then
        if [ "$mode" = "all" ]; then
            result=("${COMPONENT_ORDER[@]}")
        else
            result=("${DEFAULT_COMPONENTS[@]}")
        fi
    else
        for name in "$@"; do
            if ! is_known_component "$name"; then
                error "Nieznany komponent: ${name}"
                error "Dostępne: ${COMPONENT_ORDER[*]}"
                return 1
            fi
            result+=("$name")
        done
    fi
    printf '%s\n' "${result[@]}"
}

# Komponenty do uruchomienia (domyślnie środowisko podstawowe).
resolve_components() { resolve_component_list default "$@"; }

# Komponenty do zatrzymania (domyślnie wszystkie znane — również opcjonalne).
resolve_stop_components() { resolve_component_list all "$@"; }

pid_file() { echo "${RUN_DIR}/$1.pid"; }
log_file() { echo "${LOG_DIR}/$1.log"; }

is_running() {
    local pidfile pid
    pidfile="$(pid_file "$1")"
    [ -f "$pidfile" ] || return 1
    pid="$(cat "$pidfile" 2>/dev/null)"
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null
}

get_pid() {
    local pidfile pid
    pidfile="$(pid_file "$1")"
    pid="$(cat "$pidfile" 2>/dev/null)"
    echo "${pid:-—}"
}

ensure_dirs() {
    mkdir -p "$RUN_DIR" "$LOG_DIR" "$DATA_DIR"
    if [ ! -d "$SYSLOG_DIR" ]; then
        mkdir -p "$SYSLOG_DIR" 2>/dev/null \
            || sudo -n mkdir -p "$SYSLOG_DIR" 2>/dev/null \
            || warn "Nie udało się utworzyć ${SYSLOG_DIR} — sterownik może nie mieć gdzie zapisać logów."
    fi
}

start_component() {
    local name="$1"
    local pidfile pid
    pidfile="$(pid_file "$name")"

    if is_running "$name"; then
        pid="$(get_pid "$name")"
        info "  [pominięto] ${name} już działa (PID ${pid})"
        return 0
    fi

    local cwd="${RUNTIME_ROOT}"
    local -a cmd=()

    case "$name" in
        mount)
            cmd=("${BIN_DIR}/astro_mount_controller" "${MOUNT_CONFIG}")
            ;;
        db)
            cmd=("${BIN_DIR}/astro_object_database_server"
                 --address "0.0.0.0:50052"
                 --db "${DATA_DIR}/astronomy_objects.db")
            ;;
        weather)
            cmd=("${BIN_DIR}/astro_weather_server"
                 --address "0.0.0.0:50055"
                 --config "${CONFIG_DIR}/weather/weather_config.json")
            ;;
        power)
            cmd=("${BIN_DIR}/astro_power_server"
                 --address "0.0.0.0:50056"
                 --config "${CONFIG_DIR}/power/power_config.json")
            ;;
        sequencer)
            cmd=("${BIN_DIR}/astro_sequencer_server"
                 --address "0.0.0.0:50057"
                 --config "${CONFIG_DIR}/sequencer/sequencer_config.json")
            ;;
        derotator)
            cmd=("${BIN_DIR}/astro_derotator_server"
                 --address "0.0.0.0:50054"
                 --config "${CONFIG_DIR}/derotator/derotator_config.json")
            ;;
        web)
            cwd="${CONTROLLER_HOME}/web/proxy"
            [ -d "$cwd" ] || cwd="${PROJECT_DIR}/web/proxy"
            cmd=(node server.js)
            ;;
        *)
            error "Nieznany komponent: ${name}"
            return 1
            ;;
    esac

    if [ "$name" != "web" ] && [ ! -x "${cmd[0]}" ]; then
        error "  Brak pliku binarnego: ${cmd[0]}"
        error "  Uruchom: $0 build"
        return 1
    fi

    if [ "$name" = "web" ]; then
        if ! command -v node >/dev/null 2>&1; then
            error "  Nie znaleziono polecenia 'node' w PATH."
            return 1
        fi
        if [ ! -d "${cwd}/node_modules" ]; then
            error "  Brak zależności web/proxy (node_modules)."
            error "  Uruchom: (cd ${cwd} && npm install)"
            return 1
        fi
    fi

    mkdir -p "$RUN_DIR" "$LOG_DIR"
    info "  [start] ${name} (log: $(log_file "$name"))"

    (
        cd "$cwd" || exit 1
        nohup "${cmd[@]}" >>"$(log_file "$name")" 2>&1 &
        echo $! >"$pidfile"
    )

    sleep 1

    pid="$(get_pid "$name")"
    if kill -0 "$pid" 2>/dev/null; then
        ok "  [ok] ${name} uruchomiony (PID ${pid})"
        return 0
    else
        error "  [błąd] ${name} zakończył działanie zaraz po starcie — sprawdź $(log_file "$name")"
        rm -f "$pidfile"
        return 1
    fi
}

stop_component() {
    local name="$1"
    local pidfile pid i
    pidfile="$(pid_file "$name")"

    if ! is_running "$name"; then
        rm -f "$pidfile"
        info "  [pominięto] ${name} nie działa"
        return 0
    fi

    pid="$(get_pid "$name")"
    info "  [stop] ${name} (PID ${pid})"

    # Serwery gRPC potrzebują kilku sekund na Shutdown() (oczekiwanie na
    # zakończenie RPC). Sterownik montażu dodatkowo zamyka pętlę gamepada
    # i magistralę CANopen, więc dostaje najdłuższy limit.
    local attempts=50
    [ "$name" = "mount" ] && attempts=75

    kill -TERM "$pid" 2>/dev/null || true
    for i in $(seq 1 "$attempts"); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.2
    done

    if kill -0 "$pid" 2>/dev/null; then
        warn "  [wymuszam] ${name} — SIGKILL"
        kill -KILL "$pid" 2>/dev/null || true
        sleep 0.2
    fi

    rm -f "$pidfile"
    ok "  [ok] ${name} zatrzymany"
}

# Zatrzymaj komponenty z przekazanej listy w kolejności STOP_ORDER.
stop_selected() {
    local -n list="$1"
    local name c
    for name in "${STOP_ORDER[@]}"; do
        for c in "${list[@]}"; do
            if [[ "$c" == "$name" ]]; then
                stop_component "$name"
                break
            fi
        done
    done
}

print_status() {
    local name pid state color desc
    printf '\n%-12s %-8s %-8s %s\n' "KOMPONENT" "STAN" "PID" "OPIS"
    printf '%-12s %-8s %-8s %s\n' "---------" "----" "---" "----"
    for name in "${COMPONENT_ORDER[@]}"; do
        desc="$(component_description "$name")"
        if is_running "$name"; then
            pid="$(get_pid "$name")"
            printf "${GREEN}%-12s %-8s %-8s %s${NC}\n" "$name" "DZIAŁA" "$pid" "$desc"
        else
            if component_default_enabled "$name"; then
                printf "${YELLOW}%-12s %-8s %-8s %s${NC}\n" "$name" "STOP" "—" "$desc"
            else
                printf "${CYAN}%-12s %-8s %-8s %s (opcjonalny)${NC}\n" "$name" "STOP" "—" "$desc"
            fi
        fi
    done
    echo ""
}

do_build() {
    info "Budowanie projektu..."
    mkdir -p "$BUILD_DIR"
    (
        cd "$BUILD_DIR" || exit 1
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j"$(nproc)"
    ) || { error "Budowanie nie powiodło się."; return 1; }
    ok "Budowanie zakończone. Binarki: ${BUILD_DIR}/bin"
    info "Aby wdrożyć lokalnie (${CONTROLLER_HOME}), uruchom: make localuser"
}

# ============================================================ Parsowanie opcji

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            shift
            [ $# -gt 0 ] || { error "--config wymaga argumentu"; exit 1; }
            MOUNT_CONFIG="$(resolve_mount_config "$1")"
            ;;
        --build)
            DO_BUILD=true
            ;;
        --help|-h)
            usage
            ;;
        *)
            break
            ;;
    esac
    shift
done

COMMAND="${1:-start}"
shift || true
COMPONENT_ARGS=("$@")

# ============================================================ Wykonanie

case "$COMMAND" in
    start|stop|restart|status)
        # Status obsługuje też filtr komponentów, ale domyślnie pokazuje wszystkie.
        ;;
    build)
        do_build
        exit 0
        ;;
    list)
        echo -e "${CYAN}Dostępne komponenty:${NC}"
        for name in "${COMPONENT_ORDER[@]}"; do
            desc="$(component_description "$name")"
            if component_default_enabled "$name"; then
                printf '  %-12s %s\n' "$name" "$desc"
            else
                printf "  ${YELLOW}%-12s %s (opcjonalny, domyślnie wyłączony)${NC}\n" "$name" "$desc"
            fi
        done
        exit 0
        ;;
    *)
        error "Nieznana komenda: ${COMMAND}"
        usage
        ;;
esac

ensure_dirs

if [ "$DO_BUILD" = true ]; then
    do_build || exit 1
fi

# Dla statusu nie wymagamy konfiguracji montażu.
if [ "$COMMAND" != "status" ]; then
    if [ ! -f "$MOUNT_CONFIG" ]; then
        error "Plik konfiguracyjny sterownika nie istnieje: ${MOUNT_CONFIG}"
        error "Podaj właściwą ścieżkę przez --config (domyślnie: config/mount/canopen_stm42m.json)"
        exit 1
    fi
fi

case "$COMMAND" in
    start)
        COMPONENT_LIST="$(resolve_components "${COMPONENT_ARGS[@]}")" || exit 1
        mapfile -t COMPONENTS <<< "$COMPONENT_LIST"
        info "Uruchamianie środowiska (konfiguracja sterownika: ${MOUNT_CONFIG})..."
        for name in "${COMPONENTS[@]}"; do
            start_component "$name"
        done
        echo ""
        ok "Zakończono uruchamianie."
        if is_running "web"; then
            info "Panel WWW: http://localhost:8080"
        fi
        print_status
        ;;
    stop)
        STOP_LIST="$(resolve_stop_components "${COMPONENT_ARGS[@]}")" || exit 1
        mapfile -t COMPONENTS <<< "$STOP_LIST"
        info "Zatrzymywanie środowiska..."
        stop_selected COMPONENTS
        echo ""
        ok "Zakończono zatrzymywanie."
        ;;
    restart)
        STOP_LIST="$(resolve_stop_components "${COMPONENT_ARGS[@]}")" || exit 1
        mapfile -t STOP_COMPONENTS <<< "$STOP_LIST"
        START_LIST="$(resolve_components "${COMPONENT_ARGS[@]}")" || exit 1
        mapfile -t START_COMPONENTS <<< "$START_LIST"
        info "Restart środowiska (konfiguracja sterownika: ${MOUNT_CONFIG})..."
        stop_selected STOP_COMPONENTS
        echo ""
        # Uruchom ponownie wybrane komponenty w kolejności startu.
        for name in "${COMPONENT_ORDER[@]}"; do
            for c in "${START_COMPONENTS[@]}"; do
                if [[ "$c" == "$name" ]]; then
                    start_component "$name"
                    break
                fi
            done
        done
        echo ""
        ok "Zakończono restart."
        if is_running "web"; then
            info "Panel WWW: http://localhost:8080"
        fi
        print_status
        ;;
    status)
        if [ "${#COMPONENT_ARGS[@]}" -gt 0 ]; then
            SHOWN_LIST="$(resolve_components "${COMPONENT_ARGS[@]}")" || exit 1
            mapfile -t SHOWN <<< "$SHOWN_LIST"
            for name in "${COMPONENT_ORDER[@]}"; do
                for c in "${SHOWN[@]}"; do
                    [[ "$c" == "$name" ]] || continue
                    if is_running "$name"; then
                        printf "${GREEN}%-12s %-8s %-8s %s${NC}\n" "$name" "DZIAŁA" "$(get_pid "$name")" "$(component_description "$name")"
                    else
                        printf "${YELLOW}%-12s %-8s %-8s %s${NC}\n" "$name" "STOP" "—" "$(component_description "$name")"
                    fi
                    break
                done
            done
        else
            print_status
        fi
        ;;
esac
