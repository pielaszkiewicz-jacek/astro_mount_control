#!/bin/bash
#
# run.sh — Uruchom sterownik montażu (astro-mount-controller)
#           bez instalacji systemowej biblioteki CANopen.
#
# Działanie:
#   1. Buduje projekt (jeśli brak pliku binarnego)
#   2. Tworzy katalog dziennika (/var/log/astro-mount)
#   3. Uruchamia kontroler z lokalnego katalogu build/
#
# Użycie:
#   ./scripts/run.sh [--config ścieżka/do/config.json] [--build]
#
# Opcje:
#   --config PATH   Ścieżka do pliku konfiguracyjnego JSON
#                   (domyślnie: config/default.json z katalogu projektu)
#   --build         Wymuś przebudowanie przed uruchomieniem
#   --help          Wyświetl tę pomoc
#
# Przykłady:
#   ./scripts/run.sh
#   ./scripts/run.sh --build
#   ./scripts/run.sh --config /home/jacek/mount_config.json
#
# UWAGA: Biblioteka libcanopen.so jest ładowana przez RPATH wpisany w
#        plik binarny — nie wymaga LD_LIBRARY_PATH ani instalacji systemowej.
# ============================================================================

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
BINARY="${BUILD_DIR}/bin/astro_mount_controller"
CONFIG_FILE="${PROJECT_DIR}/config/default.json"
LOG_DIR="/var/log/astro-mount"
FORCE_BUILD=false

# --- Kolory ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# --- Pomoc ---
usage() {
    sed -n '3,27p' "$0" | sed 's/^# \?//'
    exit 0
}

# --- Parsowanie argumentów ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            shift
            CONFIG_FILE="$1"
            ;;
        --build)
            FORCE_BUILD=true
            ;;
        --help)
            usage
            ;;
        *)
            echo -e "${RED}Nieznana opcja: $1${NC}"
            usage
            ;;
    esac
    shift
done

# --- 1. Budowanie (jeśli potrzeba) ---
if [ "$FORCE_BUILD" = true ] || [ ! -f "$BINARY" ]; then
    echo -e "${YELLOW}→ Budowanie projektu...${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j"$(nproc)"
    cd "$PROJECT_DIR"
    echo -e "${GREEN}✓ Budowanie zakończone${NC}"
else
    echo -e "${CYAN}→ Binarka istnieje, pomijam budowanie. Użyj --build by wymusić.${NC}"
fi

# --- 2. Przygotowanie katalogu dziennika ---
if [ ! -d "$LOG_DIR" ]; then
    echo -e "${YELLOW}→ Tworzę katalog dziennika: ${LOG_DIR}${NC}"
    sudo mkdir -p "$LOG_DIR"
    sudo chmod 755 "$LOG_DIR"
    echo -e "${GREEN}✓ Katalog dziennika gotowy${NC}"
fi

# --- 3. Weryfikacja pliku konfiguracyjnego ---
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}✗ Plik konfiguracyjny nie istnieje: ${CONFIG_FILE}${NC}"
    echo -e "${YELLOW}  Podaj poprawną ścieżkę przez --config${NC}"
    exit 1
fi

# --- 4. Weryfikacja interfejsu CAN (opcjonalnie) ---
if ip link show can0 &>/dev/null; then
    CAN_STATE=$(ip -s link show can0 | grep -oP 'state \K\w+')
    echo -e "${CYAN}→ can0: ${CAN_STATE}${NC}"
else
    echo -e "${YELLOW}⚠ Interfejs can0 nie został znaleziony.${NC}"
    echo -e "${YELLOW}  Uruchom: sudo ip link set can0 type can bitrate 1000000 && sudo ip link set can0 up${NC}"
fi

# --- 5. Uruchomienie ---
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Uruchamianie sterownika montażu                 ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  Binary:  ${BINARY}  ║${NC}"
echo -e "${GREEN}║  Config:  ${CONFIG_FILE}  ║${NC}"
echo -e "${GREEN}║  Log:     ${LOG_DIR}  ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════╝${NC}"
echo ""

# Sprawdzenie czy biblioteka CANopen jest dostępna (RPATH)
if LD_SHOW_AUXV=1 ldd "$BINARY" 2>/dev/null | grep -q libcanopen; then
    CANOPEN_PATH=$(ldd "$BINARY" | grep libcanopen | awk '{print $3}')
    echo -e "${CYAN}→ libcanopen.so: ${CANOPEN_PATH}${NC}"
fi

echo -e "${YELLOW}→ Ctrl+C aby zatrzymać${NC}"
echo ""

exec "$BINARY" "$CONFIG_FILE"
