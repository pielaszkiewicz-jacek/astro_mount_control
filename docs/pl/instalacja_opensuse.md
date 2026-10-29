# Instalacja i konfiguracja na openSUSE

Instrukcja dotyczy openSUSE Leap 15.4+ oraz openSUSE Tumbleweed. Obejmuje zarówno
budowę kontrolera montażu (`astro_mount_controller`), jak i sterowników INDI
(montaż + kamera CCD), w tym nowego sterownika `astro_mount_ccd`.

## 1. Repozytoria i zależności

Część pakietów (INDI, cfitsio) znajduje się w repozytoriach społecznościowych —
**Packman** lub **devel:libraries**. Dodaj je przed instalacją:

```bash
# Packman (multimedia i dodatkowe biblioteki)
sudo zypper addrepo -cfp 90 https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Leap_$releasever/ packman

# Repozytorium devel:libraries (jeśli brakuje pakietów INDI)
sudo zypper addrepo -cfp 90 https://download.opensuse.org/repositories/devel:/libraries/openSUSE_Leap_$releasever/devel:libraries.repo

sudo zypper refresh
```

> Na Tumbleweed podmień adres repozytoriów na wariant `openSUSE_Tumbleweed`.
> Nazwy pakietów możesz potwierdzić przez `zypper search <nazwa>`.

### Zależności budowy kontrolera

```bash
sudo zypper install -y git cmake gcc-c++ make \
    grpc-devel protobuf-devel \
    nlohmann_json-devel eigen3-devel \
    libnova-devel cfitsio-devel \
    libcurl-devel openssl-devel \
    gtest
```

### Zależności sterowników INDI (dodatkowo)

```bash
sudo zypper install -y indilib-devel
```

`indilib-devel` dostarcza `libindi` (pkg-config `libindi.pc`) oraz nagłówki
`inditelescope.h` / `indiccd.h`. `cfitsio-devel` jest wymagany przez `indiccd.h`
(nagłówek `<fitsio.h>`).

## 2. Budowa kontrolera montażu

```bash
git clone https://github.com/your-org/astro-mount-controller.git
cd astro-mount-controller

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Wynikowe binarki lądują w `build/bin/`, m.in.:

- `astro_mount_controller` — główny proces (gRPC port 50051, CameraService
  hostowana w-procesie),
- serwery pomocnicze (dome, focuser, weather, power, sequencer, derotator).

Opcjonalna instalacja systemowa:

```bash
sudo cmake --install build
```

## 3. Budowa sterowników INDI

Sterowniki INDI to osobny projekt CMake w katalogu [`indi`](../indi):

```bash
cmake -S indi -B indi/build
cmake --build indi/build -j"$(nproc)"
```

Powstają:

- `astro_mount_indi_driver` — sterownik montażu (Telescope),
- `astro_mount_ccd` — sterownik kamery (CCD, nowy).

### Instalacja i rejestracja w KStars

```bash
sudo cmake --install indi/build
```

Instalacja:

1. kopiuje binarki do `/usr/bin`,
2. kopiuje pliki XML ([`astro_mount_indi_driver.xml`](../indi/astro_mount_indi_driver.xml),
   [`astro_mount_ccd.xml`](../indi/astro_mount_ccd.xml)) do `/usr/share/indi`,
3. uruchamia [`register_driver.cmake`](../indi/register_driver.cmake.in), który
   idempotentnie dopisuje wpisy **AstroMount** (Telescopes) oraz
   **AstroMount CCD** (CCDs) do `/usr/share/indi/drivers.xml`.

## 4. Konfiguracja kontrolera

### Plik konfiguracyjny

Domyślna konfiguracja to [`config/mount/default.json`](../config/mount/default.json). Na
openSUSE najważniejsze sekcje do ustawienia:

```json
{
  "mount": {
    "type": "equatorial",
    "latitude": 52.0,
    "longitude": 21.0,
    "max_slew_rate": 1000.0,
    "max_tracking_rate": 1.504
  },
  "network": {
    "grpc_port": 50051
  },
  "hal": {
    "type": "canopen"
  }
}
```

Uruchomienie z konkretnym plikiem:

```bash
./build/bin/astro_mount_controller config/mount/canopen_stm42m.json
```

### SocketCAN (jeśli używasz CANopen)

```bash
sudo ip link set can0 type can bitrate 1000000
sudo ip link set up can0
```

## 5. Konfiguracja sterowników INDI

Endpoint gRPC sterowniki czytają ze zmiennych środowiskowych (domyślnie
`localhost:50051`):

- `GRPC_HOST` — host kontrolera montażu,
- `GRPC_PORT` — port gRPC.

W KStars/Ekos ustaw te zmienne w polu argumentów drivera w profilu, np.:

```
GRPC_HOST=192.168.1.50 GRPC_PORT=50051
```

Następnie w profilu dodaj:

- **Telescope** → „AstroMount”,
- **CCD** → „AstroMount CCD”.

Po połączeniu sterownik CCD pobiera parametry czujnika z `CameraService`
(`GetCameraInfo`) i wystawia standardowe właściwości CCD: ekspozycję, binning,
subframe, frame type, upload, temperaturę oraz `CCD_GAIN`, `CCD_OFFSET`,
`FILTER_SLOT` / `FILTER_NAME`.

> Kamera jest obecnie **symulowana** („Simulated Camera”). Sterownik INDI
> poprawnie odbiera obraz generowany przez usługę, ale do obsługi fizycznego
> sprzętu ZWO/QHY potrzebna jest dokończona warstwa HAL w `CameraService`.

## 6. Rozwiązywanie problemów

| Objaw | Rozwiązanie |
|-------|-------------|
| `fitsio.h: No such file` | `sudo zypper install cfitsio-devel` |
| `libindi not found` przy cmake | `sudo zypper install indilib-devel` (sprawdź Packman / devel:libraries) |
| Brak drivera na liście KStars | `sudo cmake --install indi/build`, potem restart KStars |
| `execlp: No such file` przy starcie drivera | binarka musi być w `/usr/bin` (domyślny prefiks `/usr`) |
| `grpc_cpp_plugin` nie znaleziony | `sudo zypper install grpc-devel` i sprawdź `which grpc_cpp_plugin` |
