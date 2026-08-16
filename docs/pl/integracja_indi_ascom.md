# Integracja INDI i ASCOM — konfiguracja i użycie

**Projekt:** AstroMountController
**Data:** 2026-08-15
**Dotyczy:** driver INDI (C++, [`indi/`](../indi)) oraz driver ASCOM (C#, [`ascom/`](../ascom)).

---

## Spis treści

1. [Architektura integracji](#1-architektura-integracji)
2. [Wymagania](#2-wymagania)
3. [Wspólny krok: uruchomienie kontrolera](#3-wspólny-krok-uruchomienie-kontrolera)
4. [INDI — budowa, konfiguracja, użycie](#4-indi--budowa-konfiguracja-użycie)
5. [ASCOM — budowa, konfiguracja, użycie](#5-ascom--budowa-konfiguracja-użycie)
6. [Mapowanie funkcji (INDI ↔ ASCOM ↔ gRPC)](#6-mapowanie-funkcji)
7. [Troubleshooting](#7-troubleshooting)
8. [Pliki kluczowe](#8-pliki-kluczowe)

---

## 1. Architektura integracji

```mermaid
flowchart LR
    subgraph Klienci
        K[KStars / Ekos / INDI Control Panel]
        A[NINA / SGP / MaxIm DL / SkyX / PHD2]
    end
    subgraph Sterowniki
        I[astro_mount_indi_driver (C++)]
        AS[AstroMountTelescope.dll (C#)]
    end
    subgraph Komunikacja
        IS[INDI server / indiserver]
        AC[ASCOM Platform / Alpaca]
    end
    subgraph Kontroler
        M[astro_mount_controller<br/>gRPC :50051]
    end
    K --> IS
    IS --> I
    I -->|gRPC| M
    A --> AC
    AC --> AS
    AS -->|gRPC| M
```

- **Oba sterowniki są „cienkie”** — cała logika (astronomia, TPOINT, Kalman, bezpieczeństwo) działa w `astro_mount_controller`; sterowniki tylko tłumaczą wywołania na gRPC.
- **Port gRPC domyślnie:** `50051` (można zmienić; patrz niżej).
- **INDI:** driver rejestruje się w `indiserver`, który dystrybuuje zdarzenia do klientów (KStars/Ekos).
- **ASCOM:** driver to biblioteka COM rejestrowana w ASCOM Platform; klient (NINA itd.) łączy się przez wybór urządzenia w chooserze.

---

## 2. Wymagania

### Dla INDI (zwykle Linux / RPi)
- **INDI SDK ≥ 2.0** (`libindi-dev`): `sudo apt install libindi-dev`
  (na Debian/Ubuntu INDI jest wykrywane przez **pkg-config** — `libindi.pc`; driver używa pkg-config z fallbackiem na `find_package(INDI)` dla buildów 2.x dostarczających `INDIConfig.cmake`)
- **libnova-dev** (zależność nagłówków INDI):
  `sudo apt install libnova-dev`
- **protobuf + gRPC** (dev):
  `sudo apt install protobuf-compiler libprotobuf-dev libgrpc-dev libgrpc++-dev protobuf-compiler-grpc`
- **cmake ≥ 3.16**, kompilator C++17
- **indiserver + klient** (np. KStars/Ekos lub INDI Control Panel)

Kompletna instalacja zależności na Debian/Ubuntu:
```bash
sudo apt install libindi-dev libnova-dev \
  protobuf-compiler libprotobuf-dev libgrpc-dev libgrpc++-dev protobuf-compiler-grpc \
  cmake build-essential
```

### Dla ASCOM (Windows)
- **.NET SDK** (do budowy) oraz **ASCOM Platform ≥ 6** (na maszynie docelowej)
- Dla Mono (opcjonalnie): `mono-devel` + `msbuild`
- Klient: NINA, Sequence Generator Pro, MaxIm DL, SkyX, ASCOM Device Hub itd.

> Uwaga: **rejestracja COM (regasm) jest dostępna tylko na Windows.** Na Linux/Mono kod ASCOM można skompilować, ale nie zarejestrować w ASCOM.

---

## 3. Wspólny krok: uruchomienie kontrolera

Zbuduj i uruchom kontroler (pełny build projektu, patrz [`docs/pl/installation.md`](installation.md)):

```bash
# build (z katalogu projektu)
cmake --build build -j6
# uruchomienie (z katalogu projektu)
./build/bin/astro_mount_controller config/default.json
```

Zweryfikuj, że gRPC nasłuchuje:

```bash
grpc_cli call localhost:50051 CheckHealth "service: 'mount_controller'"
```

> Do testów z HAL-em symulowanym użyj [`config/emulation.json`](../config/emulation.json).

---

## 4. INDI — budowa, konfiguracja, użycie

### 4.1 Budowa

Driver INDI ma **własny** [`CMakeLists.txt`](../indi/CMakeLists.txt) — nie jest częścią głównego builda:

```bash
cd indi
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/usr
make -j$(nproc)
```

Powstaje plik wykonywalny `astro_mount_indi_driver`. Opcjonalnie zainstaluj do katalogu driverów INDI:

```bash
sudo cmake --install .
# kopiuje do ${INDI_DATA_DIR}/drivers (zwykle /usr/share/indi/drivers)
```

### 4.2 Konfiguracja adresu kontrolera

#### a) Wartości domyślne (zmienne środowiskowe)

Driver inicjalizuje host/port gRPC ze **zmiennych środowiskowych** przy starcie ([`astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp:13)):

| Zmienna | Domyślnie | Opis |
|---------|-----------|------|
| `GRPC_HOST` | `localhost` | Host kontrolera gRPC |
| `GRPC_PORT` | `50051` | Port kontrolera gRPC |

```bash
export GRPC_HOST=192.168.1.100
export GRPC_PORT=50051
indiserver astro_mount_indi_driver
```

#### b) Konfiguracja przez UI (właściwości INDI)

Od poprawki „konfiguracja INDI z UI" driver definiuje **właściwości edytowalne w kliencie INDI** (KStars/Ekos/INDI Control Panel), widoczne od razu po załadowaniu (nie wymagają połączenia):

| Właściwość | Typ | Znaczenie |
|------------|-----|-----------|
| **GRPC_CONNECTION** (HOST, PORT) | Text (IP_RW) | Host i port kontrolera gRPC |
| **GRPC_TLS** (ENABLE / DISABLE) | Switch | Włącza TLS/SSL dla połączenia gRPC |
| **GRPC_CONNECTION_STATUS** | Text (IP_RO) | Status połączenia („Connected to host:port” / „Not connected”) |

Ustawienia wpisuje się w **Connection tab** klienta INDI. Zmiany zapisują `m_grpcHost`/`m_grpcPort`/`m_grpcUseSsl` i są stosowane przy następnym **Connect** ([`applyConnectionConfig()`](../indi/astro_mount_driver.cpp:836) przebudowuje klienta gRPC przed `m_grpc->connect()`).

### 4.3 Uruchomienie

Driver eksportuje symbole loaderowe (`ISGetProperties`, `ISNewSwitch`, …) — uruchamia się go **przez indiserver**:

```bash
# lokalnie, z driverem w PATH lub ścieżce:
indiserver -v astro_mount_indi_driver

# jeśli zainstalowany w katalogu driverów INDI:
indiserver -v astro_mount_indi_driver
```

### 4.4 Połączenie z klienta (KStars / Ekos)

1. **KStars → Ekos → Ekos Manager (lub Narzędzia → Urządzenia):**
   - wybierz **AstroMount** jako teleskop (zakładka „Mount”),
   - opcjonalnie: uruchom indiserver przez przycisk **Start** (lokalnie) albo podaj adres zdalnego serwera INDI,
   - kliknij **Connect** — driver nawiąże gRPC do kontrolera.
2. **INDI Control Panel:** połącz się z `localhost:7624` (lub adresem serwera), znajdź `astro_mount_indi_driver`, włącz **CONNECT**.

> **Ważne:** od poprawki N12 `Connect()` faktycznie nawiązuje gRPC (`m_grpc->connect()`). Bez niej przycisk Connect niczego nie łączył.

### 4.5 Dostępne funkcje INDI

| Funkcja | Gdzie w INDI | RPC na kontrolerze |
|---------|--------------|--------------------|
| **Goto (RA/Dec)** | `EQUATORIAL_EOD_COORD` | `SlewToCoordinates` |
| **Sync** | `EQUATORIAL_EOD_COORD` | `AddBootstrapMeasurement` + `RunBootstrapCalibration` |
| **MoveNS / MoveWE** | `TELESCOPE_MOTION_NS/WE` | `ControlAxis` (velocity) / `StopAxis` |
| **Abort** | `TELESCOPE_ABORT_MOTION` | `Stop` |
| **Park / Unpark** | `TELESCOPE_PARK` | `Park` / `Unpark` |
| **Bootstrap** | `BOOTSTRAP_CALIBRATION` (RUN/CLEAR/STATUS) | `AddBootstrapMeasurement`, `RunBootstrapCalibration`, `GetBootstrapStatus` |
| **TPOINT status** | `TPOINT_STATUS` (read-only) | z `GetState().tpoint_params` |
| **Environment** | `ENVIRONMENT` (temp/ciśnienie/wilgotność) | z `GetState()` |
| **Lokalizacja** | `GEOGRAPHIC_COORD` | `GetConfiguration`/`UpdateConfiguration` |

Stan odpytywany co ~1 s (`TimerHit` → `GetState`); RA/Dec przeliczane z pozycji montażu (LST w `IndiPropertyMapper`).

---

## 5. ASCOM — budowa, konfiguracja, użycie

### 5.1 Budowa

Plik projektu: [`AstroMountTelescope.csproj`](../ascom/AstroMountTelescope.csproj) (target `net48`).

```bash
cd ascom
dotnet build -c Release
# albo Mono:
msbuild AstroMountTelescope.csproj -p:Configuration=Release
```

Efekt: `bin/Release/AstroMountTelescope.dll`.

### 5.2 Rejestracja (Windows, administrator)

```bash
regasm /codebase ascom/bin/Release/AstroMountTelescope.dll
```

Driver dostępny pod **ProgId `AstroMount.Telescope`** i nazwą **„AstroMount Telescope Controller”**.

### 5.3 Konfiguracja przez UI (okno Setup)

Od poprawki N13 driver ma **własne okno konfiguracyjne** (Windows Forms, [`SetupDialog.cs`](../ascom/SetupDialog.cs)):

1. W aplikacji ASCOM (NINA, SGP, MaxIm, SkyX, ASCOM Device Hub): wybierz **AstroMount Telescope Controller**.
2. Kliknij **Setup** (otwiera `ActionSetup()` → `SetupDialog`).
3. W oknie ustaw:
   - **gRPC Host** (np. `192.168.1.100` lub `localhost`),
   - **Port** (domyślnie `50051`),
   - **Use TLS (SSL)** — tylko jeśli kontroler ma włączony SSL.
4. Kliknij **Test Connection** — sprawdzi `CheckHealth` i pokaże wynik.
5. **OK** — zapis konfiguracji i przebudowa klienta gRPC.

### 5.4 Konfiguracja przez connection string

Równoważnie (np. w chooserze ASCOM) można ustawić connection string w formacie:

```
host=192.168.1.100;port=50051
```

Opcjonalne pola: `ssl=1` (lub `tls=1`) włącza TLS.

### 5.5 Połączenie i sterowanie

1. **Connect** — driver tworzy kanał gRPC, weryfikuje `CheckHealth` i uruchamia `StateCache` (odpytywanie `GetState()` co 1 s).
2. Sterowanie (interfejs `ITelescopeV3`):

| Funkcja | Właściwość/metoda ASCOM |
|---------|--------------------------|
| **Goto** | `SlewToCoordinatesAsync(ra, dec)` / `SlewToTargetAsync` |
| **Sync** | `SyncToCoordinates(ra, dec)` |
| **Abort** | `AbortSlew()` |
| **Park / Unpark** | `Park()` / `Unpark()` |
| **Prowadzenie** | `PulseGuide(direction, durationMs)` |
| **Pozycja** | `RightAscension`, `Declination` (z cache) |
| **Stan** | `Slewing`, `Tracking`, `SideOfPier` |
| **Alt/Az** | `SlewToAltAz(alt, az)` |
| **Lokalizacja** | `SiteLatitude`, `SiteLongitude`, `SiteElevation` |

---

## 6. Mapowanie funkcji

| Funkcja | gRPC RPC | INDI | ASCOM |
|---------|----------|------|-------|
| Slew RA/Dec | `SlewToCoordinates` | Goto | `SlewToCoordinatesAsync` |
| Slew Alt/Az | `SlewToHorizontal` | — | `SlewToAltAz` |
| Sync / bootstrap | `AddBootstrapMeasurement`+`RunBootstrapCalibration` | Sync / BOOTSTRAP | `SyncToCoordinates` |
| Stop / Abort | `Stop` | Abort | `AbortSlew` |
| Park | `Park` | Park | `Park` |
| Unpark | `Unpark` | Unpark | `Unpark` |
| Ruch osi | `ControlAxis` | MoveNS/MoveWE | — (Faza 3) |
| Prowadzenie | `PulseGuide` (guider) | — | `PulseGuide` |
| Pozycja | `GetState` | `EQUATORIAL_EOD_COORD` | `RightAscension`/`Declination` |
| Konfiguracja | `GetConfiguration`/`UpdateConfiguration` | `UpdateLocation` | `SiteLatitude/...` |

---

## 7. Troubleshooting

### INDI
- **`CMake Error: find_package(INDI)` / „Could not find INDIConfig.cmake”:** INDI nie jest zainstalowane. Driver wykrywa INDI przez **pkg-config** (`libindi.pc`) z fallbackiem na `find_package(INDI)` — po instalacji pakietu konfiguracja przechodzi:
  ```bash
  sudo apt install libindi-dev
  pkg-config --modversion libindi   # musi zwrócić wersję
  ```
  Jeśli INDI jest zainstalowane gdzie indziej: `cmake .. -DCMAKE_PREFIX_PATH=/sciezka/do/indi`.
- **Connect nie działa / brak połączenia gRPC:** upewnij się, że driver jest zbudowany z poprawką `Connect()` (N12) i że kontroler działa. Sprawdź `GRPC_HOST`/`GRPC_PORT` lub właściwości `GRPC_CONNECTION` w UI.
- **Brak drivera na liście w KStars:** `sudo cmake --install build` w katalogu `indi`, potem restart indiserver; sprawdź `indiserver -v` (verbose).
- **Brak INDI SDK przy budowie:** zainstaluj `libindi-dev`; `pkg-config --modversion libindi` musi zwrócić wersję.
- **Pozycja się nie odświeża:** sprawdź logi drivera (`LOG_ERROR` przy `pollController`); możliwy problem z dostępnością portu lub odrzuconym połączeniem.

### ASCOM
- **`regasm` nie znaleziony:** użyj Developer Command Prompt dla Visual Studio, albo ścieżki `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\regasm.exe`.
- **Driver nie pojawia się w chooserze:** sprawdź rejestrację COM i `ProgId AstroMount.Telescope`; `dotnet build -c Release` musi się udać (wymagane NuGet: ASCOM.Tools, Grpc.Core, Google.Protobuf).
- **Test Connection nie przechodzi:** sprawdź host/port oraz czy kontroler gRPC nasłuchuje (gRPC `CheckHealth`).
- **TLS:** `ssl=1` wymaga kontrolera skonfigurowanego z certyfikatami (`certs/`) i `ENABLE_SSL=true` w proxy/kontrolerze; domyślnie użyj `ssl=0`.
- **Linux/Mono:** rejestracja COM niedostępna — użyj na Windows lub przez ASCOM Remote/Alpaca.

---

## 8. Pliki kluczowe

- **INDI:** [`indi/astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp), [`indi/astro_mount_driver.h`](../indi/astro_mount_driver.h), [`indi/MountGrpcClient.cpp`](../indi/MountGrpcClient.cpp), [`indi/IndiPropertyMapper.cpp`](../indi/IndiPropertyMapper.cpp), [`indi/CMakeLists.txt`](../indi/CMakeLists.txt)
- **ASCOM:** [`ascom/AstroMountTelescope.cs`](../ascom/AstroMountTelescope.cs), [`ascom/SetupDialog.cs`](../ascom/SetupDialog.cs), [`ascom/GrpcClient.cs`](../ascom/GrpcClient.cs), [`ascom/StateCache.cs`](../ascom/StateCache.cs), [`ascom/ConversionHelper.cs`](../ascom/ConversionHelper.cs), [`ascom/AstroMountTelescope.csproj`](../ascom/AstroMountTelescope.csproj)
- **Wspólne:** [`proto/mount_controller.proto`](../proto/mount_controller.proto), [`src/main.cpp`](../src/main.cpp) (hosting gRPC 50051)
