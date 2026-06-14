# Plik Konfiguracyjny Sterownika Montażu Astronomicznego

## 1. Wprowadzenie

Plik konfiguracyjny w formacie JSON definiuje wszystkie parametry pracy sterownika montażu astronomicznego. 
Domyślna lokalizacja: [`config/default.json`](config/default.json). 
Można również przekazać alternatywną ścieżkę jako pierwszy argument wywołania programu (`argv[1]`).

Ładowanie konfiguracji odbywa się w [`src/config/configuration.cpp`](src/config/configuration.cpp) poprzez klasę 
`Configuration::Impl::loadFromFile()`. System wspiera hot-reload poprzez [`ConfigMonitor`](src/config/config_monitor.cpp), 
który monitoruje plik pod kątem zmian czasu modyfikacji i automatycznie przeładowuje konfigurację.

---

## 2. Struktura główna pliku

```json
{
  "logging": { ... },
  "network": { ... },
  "canopen": { ... },
  "mount": { ... },
  "telescope": { ... },
  "guider": { ... },
  "kalman": { ... },
  "tpoint": { ... },
  "derotator": { ... },
  "field_rotation": { ... },
  "hal": { ... }
}
```

Wszystkie sekcje są opcjonalne. W przypadku braku sekcji stosowane są wartości domyślne zdefiniowane w 
[`Configuration::Impl::initializeDefaults()`](src/config/configuration.cpp:891).

---

## 3. Sekcja `logging` — Konfiguracja logowania

Definiuje sposób zapisu logów systemowych.

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `level` | string | `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`, `"CRITICAL"` | `"INFO"` | Poziom szczegółowości logów |
| `directory` | string | dowolna ścieżka bezwzględna | `"/var/log/astro-mount"` | Katalog docelowy plików logów |
| `rotation_days` | integer | ≥ 1 | `7` | Rotacja logów po N dniach (nieużywane — patrz uwagi poniżej) |
| `max_file_size_mb` | integer | ≥ 1 | `100` | Maksymalny rozmiar pliku logu (MB) (nieużywane — patrz uwagi poniżej) |
| `console_output` | boolean | `true`, `false` | `true` | Czy wypisywać logi również na konsolę |

Walidacja poziomów logowania: [`Configuration::Impl::isValidLogLevel()`](src/config/configuration.cpp:1055).

### Szczegóły implementacyjne (stan na czerwiec 2025)

System logowania oparty jest na bibliotece **spdlog**. Inicjalizacja odbywa się w [`Logger::initProgrammatic()`](src/logging/logger.cpp:68), wywoływanej z [`main.cpp`](src/main.cpp:48).

#### Architektura

- **Sink plikowy**: [`basic_file_sink_mt`](src/logging/logger.cpp:90) — zapis w trybie append (`"ab"`, bez rotacji). Rotacja plików logów obsługiwana jest **zewnętrznie** (np. przez `logrotate`). Parametry `rotation_days` i `max_file_size_mb` są zachowane w konfiguracji dla kompatybilności wstecznej, ale nie są funkcjonalnie wykorzystywane.
- **Sink konsolowy**: [`stdout_color_sink_mt`](src/logging/logger.cpp:97) — kolorowe wyjście na stdout.
- **Sink syslog** (opcjonalny): [`syslog_sink_mt`](src/logging/logger.cpp:104).
- **Rejestracja w globalnym rejestrze spdlog**: Logger-y są rejestrowane przez [`spdlog::register_logger()`](src/logging/logger.cpp:384-386), co umożliwia działanie `spdlog::flush_every()`.
- **Periodiczny flush**: [`spdlog::flush_every(5s)`](src/logging/logger.cpp:119) — bufor stdio jest opróżniany na dysk co 5 sekund.

#### Format logu

```
[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v
```

Przykładowy wpis:
```
[2025-06-01 22:30:15.123] [INFO] [mount] System initialized
```

#### Logger-y komponentowe

Przy starcie tworzonych jest 13 predefiniowanych logger-ów ([`createDefaultLoggers()`](src/logging/logger.cpp:368)):

| Nazwa | Przeznaczenie |
|---|---|
| `mount` | Główny kontroler montażu |
| `api` | API i gRPC |
| `canopen` | Magistrala CANopen |
| `tpoint` | Model TPoint |
| `kalman` | Filtr Kalmana |
| `config` | Zarządzanie konfiguracją |
| `safety` | System bezpieczeństwa |
| `structured` | Dane strukturalne |
| `audit` | Audyt operacji |
| `performance` | Wydajność i timing |
| `error` | Błędy i wyjątki |
| `calibration` | Kalibracja |
| `health` | Stan systemu |

Dodatkowe logger-y mogą być tworzone na żądanie przez [`Logger::get(name)`](src/logging/logger.cpp:166-185) — każdy nowy logger jest automatycznie rejestrowany w globalnym rejestrze spdlog i dziedziczy te same sink-i.

#### Plik logu

Domyślnie: `{directory}/astro-mount.log` (np. `/var/log/astro-mount/astro-mount.log`).

---

## 4. Sekcja `network` — Konfiguracja sieciowa (gRPC)

Definiuje serwer gRPC do komunikacji zewnętrznej.

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `grpc_address` | string | prawidłowy adres IPv4 | `"0.0.0.0"` | Adres nasłuchiwania serwera gRPC |
| `grpc_port` | integer | 1–65535 | `50051` | Port serwera gRPC |
| `max_connections` | integer | 1–1000 | `10` | Maksymalna liczba równoczesnych połączeń |
| `enable_ssl` | boolean | `true`, `false` | `false` | Czy włączyć szyfrowanie TLS |
| `ssl_cert_path` | string | ścieżka do pliku .pem/.crt | `""` (pusty) | Ścieżka certyfikatu SSL |
| `ssl_key_path` | string | ścieżka do pliku .key | `""` (pusty) | Ścieżka klucza prywatnego SSL |

Implementacja: [`NetworkConfig`](include/config/configuration.h:30).

---

## 5. Sekcja `canopen` — Konfiguracja magistrali CANopen

Dotyczy warstwy CANopen — interfejsu magistrali CAN.

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `interface` | string | nazwa interfejsu SocketCAN | `"can0"` | Nazwa interfejsu CAN (np. can0, can1, vcan0) |
| `node_id` | integer | 1–127 (CiA 301) | `1` | CANopen Node ID sterownika (własny adres) |
| `baud_rate` | integer | 10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000 | `1000000` | Szybkość transmisji magistrali CAN (1 Mbit/s) |
| `enable_sync` | boolean | `true`, `false` | `true` | Czy włączyć cykliczny SYNC (CiA 301 §7.2.4) |
| `sync_interval_ms` | integer | 10–10000 | `100` | Interwał SYNC w milisekundach |
| `accel_mode` | string | `"time"`, `"rate"` | `"time"` | Sposób interpretacji przyspieszenia przez napęd. `"time"` – napęd traktuje wartość 0x6083/0x6084 jako czas rampy (mniejsza wartość = szybsza rampa). `"rate"` – napęd traktuje jako tempo (°/s², większa wartość = szybsza rampa). |

**Uwaga:** `node_id` w tej sekcji to adres samego sterownika na magistrali CAN, **nie** adresy serwonapędów.
Adresy napędów konfiguruje się w sekcji [`hal.axes[].can_node_id`](#811-can-node-id).

Implementacja: [`CanOpenConfig`](include/config/configuration.h:39),
odczyt: [`Configuration::getCanOpenConfig()`](src/config/configuration.cpp:302).
Konwersja: [`canopen_interface.cpp::setVelocityTarget()`](src/controllers/canopen_interface.cpp:527).

---

## 6. Sekcja `mount` — Konfiguracja montażu

### 6.1 Parametry globalne montażu

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `type` | string | `"equatorial"`, `"altazimuth"` | `"equatorial"` | Typ montażu: paralaktyczny (EQ) lub azymutalny (Alt-Az) |
| `latitude` | float | -90.0 do 90.0 | `52.0` | Szerokość geograficzna obserwatorium (stopnie) |
| `longitude` | float | -180.0 do 180.0 | `21.0` | Długość geograficzna (stopnie, +E) |
| `altitude` | float | 0.0 do 10000.0 | `100.0` | Wysokość n.p.m. (metry) |
| `mount_height` | float | 0.0 do 10.0 | `1.5` | Wysokość osi montażu nad poziomem gruntu (m) |
| `pier_west` | float | -180.0 do 180.0 | `0.0` | Offset filaru dla zachodniej strony montażu (deg) |
| `pier_east` | float | -180.0 do 180.0 | `0.0` | Offset filaru dla wschodniej strony montażu (deg) |
| `default_temperature` | float | -50.0 do 60.0 | `15.0` | Domyślna temperatura (°C) dla refrakcji |
| `default_pressure` | float | 500.0 do 1100.0 | `1013.25` | Domyślne ciśnienie (hPa) dla refrakcji |
| `default_humidity` | float | 0.0 do 1.0 | `0.5` | Domyślna wilgotność (0.0–1.0) dla refrakcji |
| `use_encoders` | boolean | `true`, `false` | `true` | Czy używać enkoderów do sprzężenia zwrotnego |
| `encoders_absolute` | boolean | `true`, `false` | `true` | Czy enkodery są absolutne (true) czy inkrementalne (false) |
| `encoder_resolution` | integer | 1–2³² | `16384` | Rozdzielczość enkoderów (counts/obrót) |

### 6.2 Parametry ruchu

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `max_slew_rate` | float | 0.1 do 20.0 | `5.0` | Maksymalna prędkość przewijania (deg/s) |
| `max_tracking_rate` | float | 0.0 do 0.1 | `0.004178` | Maksymalna prędkość trackingu (~1× sidereal, deg/s) |
| `slew_acceleration` | float | 0.01 do 10.0 | `1.0` | Przyspieszenie przewijania (deg/s²) |
| `tracking_acceleration` | float | 0.0001 do 0.1 | `0.001` | Przyspieszenie trackingu (deg/s²) |
| `position_tolerance` | float | 0.001 do 10.0 | `0.1` | Tolerancja pozycji dla uznania targetu za osiągnięty (arcsec) |
| `rate_tolerance` | float | 0.0001 do 1.0 | `0.01` | Tolerancja prędkości (deg/s) |

### 6.3 Przejścia przez południk (Meridian Flip)

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `meridian_flip_enabled` | boolean | `true`, `false` | `true` | Czy włączyć automatyczne flipowanie |
| `meridian_flip_delay_minutes` | float | 0.0 do 60.0 | `5.0` | Opóźnienie flipa po osiągnięciu południka (min) |
| `meridian_flip_hysteresis_degrees` | float | 0.0 do 10.0 | `0.5` | Histereza flipa (deg) — zapobiega oscylacjom |
| `meridian_flip_timeout_seconds` | float | 10.0 do 600.0 | `120.0` | Maksymalny czas trwania flipa (s) |

### 6.4 Limity miękkie (Soft Limits)

| Parametr | Typ | Zakres / Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `soft_limits_enabled` | boolean | `true`, `false` | `true` | Czy włączyć miękkie limity pozycji |
| `soft_limit_axis1_min` | float | -360.0 do 360.0 | `-270.0` | Minimalna pozycja osi 1 (HA/Azm, deg) |
| `soft_limit_axis1_max` | float | -360.0 do 360.0 | `270.0` | Maksymalna pozycja osi 1 (HA/Azm, deg) |
| `soft_limit_axis2_min` | float | -360.0 do 360.0 | `-5.0` | Minimalna pozycja osi 2 (Dec/Alt, deg) |
| `soft_limit_axis2_max` | float | -360.0 do 360.0 | `185.0` | Maksymalna pozycja osi 2 (Dec/Alt, deg) |
| `soft_limit_warning_degrees` | float | 0.0 do 90.0 | `10.0` | Strefa ostrzeżenia przed limitem (deg) |
| `soft_limit_deceleration_degrees` | float | 0.0 do 90.0 | `5.0` | Strefa hamowania przed limitem (deg) |
| `soft_limit_tracking_rate_factor` | float | 0.0 do 1.0 | `0.1` | Współczynnik redukcji prędkości w strefie ostrzeżenia |

### 6.5 Pozycja parkowania

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `park_position_axis1` | float | -360.0 do 360.0 | `0.0` | Pozycja parkowania osi 1 (deg) |
| `park_position_axis2` | float | -360.0 do 360.0 | `90.0` | Pozycja parkowania osi 2 (deg) |

### 6.6 Refrakcja atmosferyczna

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `enable_refraction_correction` | boolean | `true`, `false` | `true` | Czy włączyć korekcję refrakcji atmosferycznej |
| `orientation_quaternion` | float[4] | wektor jednostkowy | `[1,0,0,0]` | Kwaternion orientacji montażu (x, y, z, w) |

### 6.7 Parametry fizyczne osi (`axis_physical_parameters`)

Definiuje charakterystykę mechaniczną każdej osi montażu.

#### 6.7.1 `ha_axis` / `dec_axis`

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `encoder_resolution` | integer | 1–2³² | `16384` | Rozdzielczość enkodera (counts/obrót) |
| `encoder_counts_per_arcsec` | float | > 0 | `0.0126` | Liczba impulsów enkodera na sekundę kątową |
| `encoder_quantization_error` | float | > 0 | `39.6` | Błąd kwantyzacji enkodera (mas) |
| `gear_ratio` | float | 0.1 do 10000.0 | `360.0` | Całkowite przełożenie przekładni |
| `worm_ratio` | float | 0.1 do 1000.0 | `180.0` | Przełożenie ślimaka |
| `worm_teeth` | integer | 1–100 | `1` | Ilość zębów ślimaka (zazwyczaj 1) |
| `worm_wheel_teeth` | integer | 1–1000 | `180` | Ilość zębów koła ślimacznicy |
| `cyclic_error_amplitude` | float | 0.0 do 100.0 | `15.2` (HA) / `12.8` (Dec) | Amplituda błędu cyklicznego (arcsec) |
| `cyclic_error_period` | float | 0.0 do 360.0 | `360.0` | Okres błędu cyklicznego (deg) |
| `cyclic_harmonics` | float[8] | dowolne | zależne od osi | Współczynniki harmonicznych błędu cyklicznego |
| `backlash` | float | 0.0 do 100.0 | `8.5` (HA) / `6.3` (Dec) | Luz mechaniczny (arcsec) |
| `backlash_temp_coeff` | float | 0.0 do 1.0 | `0.02` (HA) / `0.015` (Dec) | Współczynnik temperaturowy luzu (arcsec/°C) |
| `axis_stiffness` | float | 0.0 do 10.0 | `0.5` (HA) / `0.6` (Dec) | Sztywność osi |
| `torsional_compliance` | float | > 0 | `1e-6` (HA) / `1.2e-6` (Dec) | Podatność skrętna (rad/Nm) |
| `expansion_coeff` | float | > 0 | `11.0e-6` | Współczynnik rozszerzalności cieplnej (1/°C) |
| `temp_gear_error_coeff` | float | > 0 | `0.05` (HA) / `0.04` (Dec) | Współczynnik błędu przekładni od temperatury (arcsec/°C) |
| `calibration_temp` | float | -50.0 do 60.0 | `20.0` | Temperatura kalibracji (°C) |
| `calibration_table` | array | lista [pozycja, korekcja] | `[]` | Tabela kalibracyjna (para deg → arcsec) |

Implementacja: [`MountConfig`](include/config/configuration.h:86), 
[`AxisPhysicalParameters`](include/config/configuration.h:47),
odczyt: [`Configuration::getMountConfig()`](src/config/configuration.cpp:315).

---

## 7. Sekcje `telescope`, `guider`, `kalman`, `tpoint`

### 7.1 `telescope` — Parametry teleskopu

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `focal_length` | float | 10.0 do 50000.0 | `2000.0` | Ogniskowa teleskopu (mm) |
| `aperture` | float | 10.0 do 5000.0 | `200.0` | Apertura/średnica teleskopu (mm) |
| `tube_length` | float | 100.0 do 10000.0 | `1800.0` | Długość tubusa (mm) |
| `camera_model` | string | dowolny | `"ASI1600"` | Model kamery |
| `pixel_size` | float | 0.1 do 100.0 | `3.8` | Rozmiar piksela (µm) |
| `sensor_width` | integer | 1–20000 | `4656` | Szerokość matrycy (piksele) |
| `sensor_height` | integer | 1–20000 | `3520` | Wysokość matrycy (piksele) |

### 7.2 `guider` — Konfiguracja autoguidera

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `enabled` | boolean | `true`, `false` | `false` | Czy włączyć autoguider |
| `connection_string` | string | dowolny | `""` | Ciąg połączenia (zależny od protokołu) |
| `max_correction` | float | 0.0 do 100.0 | `10.0` | Maksymalna korekcja (arcsec) |
| `aggression` | float | 0.0 do 1.0 | `0.5` | Agresywność korekcji (0=none, 1=full) |
| `exposure_time_ms` | integer | 100–60000 | `2000` | Czas ekspozycji guidera (ms) |
| `binning` | integer | 1–4 | `2` | Binning (1=no binning) |

### 7.3 `kalman` — Filtr Kalmana

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `process_noise` | float | 1e-10 do 1.0 | `0.01` | Szum procesu (Q) — im wyższy, tym szybsza adaptacja |
| `measurement_noise` | float | 1e-10 do 1.0 | `1.0` | Szum pomiaru (R) — im wyższy, tym większe wygładzenie |
| `adaptive_q` | boolean | `true`, `false` | `true` | Adaptacyjne Q (dostosowuje się do warunków) |
| `adaptive_r` | boolean | `true`, `false` | `false` | Adaptacyjne R |
| `innovation_threshold` | float | 0.1 do 10.0 | `3.0` | Próg innowacji (odrzucanie outlierów w sigma) |
| `max_iterations` | integer | 1–100 | `10` | Maksymalna liczba iteracji korekcji |

### 7.4 `tpoint` — Model TPoint

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `enabled_terms` | integer | 0–65535 | `65535` | Maska bitowa włączonych wyrazów TPoint |
| `min_measurements` | integer | 3–1000 | `10` | Minimalna liczba pomiarów do kalibracji |
| `max_residual` | float | 0.1 do 100.0 | `30.0` | Maksymalny dopuszczalny residuum (arcsec) |
| `auto_calibrate` | boolean | `true`, `false` | `true` | Automatyczna kalibracja po osiągnięciu min. pomiarów |

Implementacja: [`TelescopeConfig`](include/config/configuration.h:142), 
[`GuiderConfig`](include/config/configuration.h:152),
[`KalmanConfig`](include/config/configuration.h:161),
[`TPointConfig`](include/config/configuration.h:170).

---

## 8. Sekcja `hal` — Warstwa abstrakcji sprzętowej (HAL)

Sekcja `hal` definiuje konfigurację warstwy sprzętowej. Parsowana przez 
[`HALConfig::fromJson()`](include/hal/hal_config.h:222).

```json
"hal": {
  "type": "...",
  "name": "...",
  "canopen": { ... },
  "axes": [ ... ],
  "gamepad": { ... },
  "pid_params": { ... },
  "safety": { ... }
}
```

### 8.1 Parametry ogólne HAL

| Parametr | Typ | Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `type` | string | `"canopen"`, `"simulated"`, `"ethernet"`, `"serial"`, `"gamepad"` | `"canopen"` | Typ backendu HAL |
| `name` | string | dowolny | `""` | Nazwa instancji HAL (dla logów/debug) |

### 8.2 `hal.canopen` — Konfiguracja CANopen w warstwie HAL

Niezależna od sekcji głównej [`canopen`](#5-sekcja-canopen--konfiguracja-magistrali-canopen). 
Dotyczy szczegółów integracji z biblioteką CANopen.

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `library` | string | `"canopensocket"`, `"libcanopen"` | `"canopensocket"` | Biblioteka CANopen |
| `interface_name` | string | nazwa interfejsu | `"can0"` | Interfejs SocketCAN |
| `bitrate` | integer | 10000–1000000 | `1000000` | Szybkość transmisji (bps) |
| `node_id` | integer | 1–127 | `1` | Lokalny Node ID sterownika |
| `use_sync` | boolean | `true`, `false` | `true` | Czy używać SYNC |
| `sync_period_ms` | integer | 10–10000 | `100` | Okres SYNC (ms) |
| `sdo_timeout_ms` | integer | 100–10000 | `1000` | Timeout SDO (ms) |
| `pdo_update_rate` | integer | 1–1000 | `100` | Częstotliwość aktualizacji PDO (Hz) |

#### 8.2.1 `hal.canopen.nmt` — NMT (Network Management)

Zgodnie z CiA 301 (DS-301).

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `enable_nmt` | boolean | `true`, `false` | `true` | Czy włączyć monitorowanie NMT |
| `heartbeat_period_ms` | integer | 10–10000 | `100` | Okres oczekiwania na heartbeat (ms) |
| `heartbeat_timeout_ms` | integer | 50–30000 | `500` | Timeout heartbeatu (ms) |
| `max_missed_heartbeats` | integer | 1–100 | `3` | Maks. pominiętych heartbeatów przed reakcją |
| `enable_bootup_check` | boolean | `true`, `false` | `true` | Czy sprawdzać bootup węzłów |
| `bootup_timeout_ms` | integer | 100–30000 | `5000` | Timeout bootupu (ms) |
| `enable_auto_recovery` | boolean | `true`, `false` | `true` | Automatyczne przywracanie po utracie komunikacji |
| `recovery_interval_s` | integer | 1–300 | `5` | Minimalny odstęp między próbami recovery (s) — zabezpieczenie anti-flapping |
| `enable_node_guarding` | boolean | `true`, `false` | `false` | Node Guarding (CiA 301 §7.2.6.2) zamiast Heartbeat |
| `node_guarding_period_ms` | integer | 50–10000 | `1000` | Okres Node Guarding (ms) |

Implementacja NMT: [`CanOpenHAL::nmtMonitoringThread()`](src/hal/canopen_hal/canopen_hal.cpp:1397) — 
pętla 100 Hz z 6 fazami:
1. **Bootup** — oczekiwanie na potwierdzenie inicjalizacji węzła
2. **Heartbeat** — cykliczne sprawdzanie stanu węzłów
3. **Utrata komunikacji** — reakcja po `MAX_MISSED_HB` pominiętych heartbeatach (→ PRE-OPERATIONAL)
4. **Auto-recovery** — automatyczne przywracanie do OPERATIONAL po odzyskaniu komunikacji
5. **Inicjalne przejście** — pierwsze przejście PRE-OPERATIONAL → OPERATIONAL
6. **Raportowanie** — okresowe logowanie stanu sieci (~1s)

### 8.3 `hal.axes[]` — Konfiguracja osi

Tablica obiektów, każdy definiuje jedną oś napędową.

#### 8.3.1 Parametry ogólne osi

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `id` | integer | 0–N | `0` | Indeks osi (0 = HA/Azm, 1 = Dec/Alt, 2 = Derotator) |
| `name` | string | dowolny | `"Axis_0"` | Nazwa osi (dla logów/debug) |
| [`can_node_id`](include/hal/hal_config.h:135) | integer | 0–127 | `0` (auto) | CANopen Node ID napędu. **`0` oznacza automatyczne mapowanie: `axis_id + 1`** |

**Ważne:** `can_node_id` to adres serwonapędu na magistrali CAN. Wartość `0` (domyślna) zachowuje 
kompatybilność wsteczną: oś 0 → node 1, oś 1 → node 2, oś 2 → node 3. 
Dla niestandardowych adresów (np. 5, 6) należy jawnie ustawić tę wartość.

Implementacja w [`canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:1450):
```cpp
auto getNodeId = [this](int axis_index) -> uint8_t {
    if (axis_index < config_.axes.size() && config_.axes[axis_index].can_node_id > 0)
        return config_.axes[axis_index].can_node_id;
    return static_cast<uint8_t>(axis_index + 1);  // fallback
};
```

#### 8.3.2 `motor_config` — Konfiguracja silnika

| Parametr | Typ | Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `type` | string | `"STEPPER"`, `"SERVO"`, `"BRUSHED_DC"`, `"BRUSHLESS_DC"`, `"CANOPEN_SERVO"`, `"VIRTUAL"` | `"STEPPER"` | Typ silnika |
| `default_mode` | string | `"POSITION"`, `"VELOCITY"`, `"TORQUE"`, `"TRAJECTORY"`, `"OPEN_LOOP"` | `"POSITION"` | Domyślny tryb sterowania |
| `max_velocity` | float | 0.01 do 100.0 | `2.0` | Maksymalna prędkość (deg/s) |
| `max_acceleration` | float | 0.01 do 50.0 | `0.5` | Maksymalne przyspieszenie (deg/s²) |
| `max_torque` | float | 0.1 do 500.0 | `100.0` | Maksymalny moment obrotowy (Nm lub %) |
| `encoder_counts_per_degree` | float | 1.0 do 1e9 | `10000.0` | Liczba impulsów enkodera na stopień |
| `gear_ratio` | float | 0.1 do 10000.0 | `1.0` | Przełożenie przekładni |
| `enable_current_limit` | boolean | `true`, `false` | `true` | Czy włączyć ogranicznik prądu |
| `current_limit` | float | 0.1 do 100.0 | `5.0` | Limit prądu (A) |
| `enable_temperature_protection` | boolean | `true`, `false` | `true` | Czy włączyć ochronę temperaturową |
| `max_temperature` | float | 20.0 do 120.0 | `80.0` | Maksymalna temperatura silnika (°C) |

#### 8.3.3 `encoder_config` — Konfiguracja enkodera

| Parametr | Typ | Dozwolone wartości | Wartość domyślna | Opis |
|---|---|---|---|---|
| `type` | string | `"ABSOLUTE"`, `"INCREMENTAL"`, `"RESOLVER"`, `"HALL_SENSOR"` | `"ABSOLUTE"` | Typ enkodera |
| `interface` | string | `"SSI"`, `"QUADRATURE"`, `"BISS"`, `"ENDAT"`, `"CANOPEN"` | `"SSI"` | Interfejs enkodera |
| `resolution` | integer | 1–2³² | `16384` | Rozdzielczość (counts/obrót) |
| `counts_per_degree` | float | 1.0 do 1e9 | `10000.0` | Liczba impulsów na stopień kątowy |
| `use_index_pulse` | boolean | `true`, `false` | `true` | Czy używać impulsu indeksowego (zerowanie) |
| `use_direction_signal` | boolean | `true`, `false` | `true` | Czy używać sygnału kierunku |
| `max_velocity` | float | 1.0 do 10000.0 | `1000.0` | Maksymalna prędkość odczytu enkodera (deg/s) |
| `enable_error_detection` | boolean | `true`, `false` | `true` | Czy włączyć detekcję błędów enkodera |
| `error_threshold` | integer | 1–1000 | `10` | Próg błędów przed alarmem |
| `calibration_offset` | float | -360.0 do 360.0 | `0.0` | Offset kalibracyjny (deg) |

#### 8.3.4 `safety_limits` — Limity bezpieczeństwa osi

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `min_position` | float | -360.0 do 360.0 | `-270.0` | Minimalna pozycja (deg) |
| `max_position` | float | -360.0 do 360.0 | `270.0` | Maksymalna pozycja (deg) |
| `max_velocity` | float | 0.1 do 100.0 | `5.0` | Maksymalna prędkość (deg/s) |
| `max_acceleration` | float | 0.01 do 50.0 | `2.0` | Maksymalne przyspieszenie (deg/s²) |
| `max_current` | float | 0.1 do 100.0 | `10.0` | Maksymalny prąd (A) |
| `max_temperature` | float | 20.0 do 120.0 | `80.0` | Maksymalna temperatura (°C) |

### 8.4 `hal.gamepad` — Konfiguracja gamepada

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `device_path` | string | ścieżka /dev/input/* | `""` | Ścieżka urządzenia wejściowego |
| `deadzone` | float | 0.0 do 1.0 | `0.15` | Strefa martwa analogowych osi |
| `sensitivity` | float | 0.1 do 10.0 | `1.0` | Czutość osi |
| `max_velocity_deg_s` | float | 0.1 do 20.0 | `5.0` | Maksymalna prędkość ręcznego sterowania |
| `invert_axis1` | boolean | `true`, `false` | `false` | Inwersja osi 1 |
| `invert_axis2` | boolean | `true`, `false` | `false` | Inwersja osi 2 |
| `speed_presets` | float[] | lista prędkości | `[]` | Presety prędkości (np. [0.5, 1.0, 2.0, 5.0]) |
| `update_rate_hz` | float | 10.0 do 200.0 | `50.0` | Częstotliwość odczytu gamepada (Hz) |

#### 8.4.1 `button_mapping` — Mapowanie przycisków

Mapa `"numer_przycisku": "akcja"`.

Dozwolone akcje:
- `"home"` — powrót do pozycji domowej
- `"stop"` — zatrzymanie ruchu
- `"park"` — parkowanie montażu
- `"emergency_stop"` — zatrzymanie awaryjne
- `"speed_down"` — zmniejszenie prędkości
- `"speed_up"` — zwiększenie prędkości
- `"manual_toggle"` — przełączanie trybu ręcznego/automatycznego
- `"none"` — brak akcji

#### 8.4.2 `axis_mapping` — Mapowanie osi analogowych

Mapa `"numer_osi": "funkcja"`.

Dozwolone funkcje:
- `"lx"`, `"ly"` — lewa gałka (X/Y)
- `"rx"`, `"ry"` — prawa gałka (X/Y)
- `"trigger_l"`, `"trigger_r"` — spusty
- `"pov_x"`, `"pov_y"` — POV hat switch

### 8.5 `hal.pid_params` — Parametry regulatora PID

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `kp` | float | 0.0 do 1000.0 | `1.5` | Wzmocnienie proporcjonalne |
| `ki` | float | 0.0 do 100.0 | `0.2` | Wzmocnienie całkujące |
| `kd` | float | 0.0 do 100.0 | `0.05` | Wzmocnienie różniczkujące |
| `integral_limit` | float | 0.0 do 1e6 | `1000.0` | Limit członu całkującego (anti-windup) |
| `output_limit` | float | 0.0 do 1e6 | `100.0` | Limit wyjścia PID |
| `anti_windup_gain` | float | 0.0 do 1.0 | `0.1` | Wzmocnienie anti-windup (back-calculation) |
| `enable_anti_windup` | boolean | `true`, `false` | `true` | Czy włączyć anti-windup |

Implementacja w [`include/hal/hal_config.h:152`](include/hal/hal_config.h:152).

### 8.6 `hal.safety` — Globalne bezpieczeństwo

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `enable_limits` | boolean | `true`, `false` | `true` | Czy włączyć limity bezpieczeństwa |
| `enable_emergency_stop` | boolean | `true`, `false` | `true` | Czy włączyć przycisk awaryjny |
| `emergency_stop_timeout_ms` | integer | 10–10000 | `100` | Czas reakcji na E-stop (ms) |
| `enable_temperature_monitoring` | boolean | `true`, `false` | `true` | Monitorowanie temperatury |
| `enable_current_monitoring` | boolean | `true`, `false` | `true` | Monitorowanie prądu |
| `enable_voltage_monitoring` | boolean | `true`, `false` | `true` | Monitorowanie napięcia |
| `min_voltage` | float | 0.0 do 50.0 | `20.0` | Minimalne napięcie zasilania (V) |
| `max_voltage` | float | 0.0 do 50.0 | `30.0` | Maksymalne napięcie zasilania (V) |
| `monitoring_rate` | integer | 1–1000 | `10` | Częstotliwość monitorowania (Hz) |

---

## 9. Sekcje `derotator` i `field_rotation`

### 9.1 `derotator` — Derotator pola

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `type` | string | `"CANOPEN"`, `"STEPPER"`, `"SERVO"`, `"NONE"` | `"CANOPEN"` | Typ derotatora |
| `enabled` | boolean | `true`, `false` | `false` | Czy włączony |
| `connection_string` | string | dowolny | `""` | Ciąg połączenia |
| `gear_ratio` | float | 0.1 do 1000.0 | `10.0` | Przełożenie derotatora |
| `max_speed` | float | 0.1 do 50.0 | `5.0` | Max prędkość (deg/s) |
| `max_acceleration` | float | 0.01 do 20.0 | `2.0` | Max przyspieszenie (deg/s²) |
| `backlash` | float | 0.0 do 50.0 | `2.0` | Luz derotatora (arcsec) |
| `absolute_encoder` | boolean | `true`, `false` | `true` | Enkoder absolutny |
| `encoder_resolution` | integer | 1–2³² | `16384` | Rozdzielczość enkodera |
| `homing_offset` | float | -360.0 do 360.0 | `0.0` | Offset pozycji domowej (deg) |
| `calibration_table` | array | lista korekcji | `[]` | Tabela kalibracyjna |

### 9.2 `field_rotation` — Rotacja pola (obliczona)

| Parametr | Typ | Zakres | Wartość domyślna | Opis |
|---|---|---|---|---|
| `enabled` | boolean | `true`, `false` | `false` | Czy włączona korekcja rotacji |
| `latitude` | float | -90.0 do 90.0 | `52.0` | Szerokość geograficzna |
| `altitude` | float | 0.0 do 90.0 | `0.0` | Altitude celu |
| `azimuth` | float | 0.0 do 360.0 | `0.0` | Azymut celu |
| `computed_rate` | float | dowolna | `0.0` | Obliczona prędkość rotacji (deg/s) |
| `applied_correction` | float | dowolna | `0.0` | Zastosowana korekcja (deg) |
| `temperature` | float | -50.0 do 60.0 | `15.0` | Temperatura (°C) |
| `flexure_correction` | float | dowolna | `0.0` | Korekcja flexury (deg) |

---

## 10. Przykładowe konfiguracje

### 10.1 Dwa serwonapędy CANopen na adresach 5 i 6 (montaż paralaktyczny)

Plik: [`config/dual_servo_config.json`](config/dual_servo_config.json)

```json
{
  "hal": {
    "type": "canopen",
    "axes": [
      {
        "id": 0,
        "name": "HA_Axis",
        "can_node_id": 5,
        "motor_config": { "type": "CANOPEN_SERVO", ... },
        "encoder_config": { "interface": "CANOPEN", ... },
        "safety_limits": { ... }
      },
      {
        "id": 1,
        "name": "Dec_Axis",
        "can_node_id": 6,
        "motor_config": { "type": "CANOPEN_SERVO", ... },
        "encoder_config": { "interface": "CANOPEN", ... },
        "safety_limits": { ... }
      }
    ]
  }
}
```

### 10.2 Domyślna konfiguracja (3 osie, adresy 1, 2, 3)

Plik: [`config/default.json`](config/default.json)

W domyślnej konfiguracji brak sekcji `hal.axes[]` — system używa wartości domyślnych, 
gdzie `can_node_id = 0` (auto) co daje mapowanie: oś 0 → node 1, oś 1 → node 2, oś 2 → node 3.

---

## 11. Diagram przepływu ładowania konfiguracji

```
argv[1] (ścieżka pliku)
    │
    ├── podana → Configuration::loadFromFile(argv[1])
    │               │
    │               ├── sukces → gotowe
    │               └── błąd  → fallback do default.json
    │
    └── brak    → Configuration::loadFromFile("config/default.json")
                    │
                    ├── sukces → gotowe
                    └── błąd  → Configuration::initializeDefaults()
                                  (wartości domyślne w kodzie)
```

Po załadowaniu:
1. [`ConfigMonitor`](src/config/config_monitor.cpp) monitoruje plik pod kątem zmian
2. [`ConfigNotifier`](src/config/config_monitor.cpp:205) powiadamia subskrybentów o zmianach
3. [`ConfigManager`](src/config/config_monitor.cpp:266) zarządza cyklem życia konfiguracji

---

## 12. Walidacja konfiguracji

Walidacja odbywa się w [`Configuration::Impl::validate()`](src/config/configuration.cpp:61). 
Sprawdzane są m.in.:

- Poprawność poziomu logowania
- Zakres portu gRPC (1–65535)
- Zakres Node ID CANopen (1–127)
- Poprawność współrzędnych geograficznych
- Spójność konfiguracji HAL

W przypadku błędów krytycznych program loguje ostrzeżenie i stosuje wartości domyślne.
