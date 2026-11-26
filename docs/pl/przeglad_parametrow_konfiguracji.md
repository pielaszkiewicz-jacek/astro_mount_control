# Przegląd parametrów konfiguracji i UI — stan na 2026-11-22

Celem dokumentu jest inwentaryzacja parametrów dostępnych w pliku konfiguracyjnym
(`config/mount/canopen_stm42m.json`) oraz w Web UI, wskazanie nieużywanych,
określenie które działają od ręki (runtime) a które wymagają restartu, oraz
które poprawnie zapisują się na dysk.

Legenda:
- **Używany**: TAK / NIE / CZĘŚCIOWO (np. tylko przechowywany, nie liczony).
- **Runtime**: zmiana działa bez restartu (parametr czytany na bieżąco).
- **Restart**: zmiana wymaga ponownego uruchomienia kontrolera.
- **Zapis**: czy zapis do pliku JSON jest poprawny (round-trip).

> Uwaga ogólna: `updateConfiguration()` nadpisuje całą strukturę `config_` w
> pamięci i **tylko** log level / console_output są aplikowane natychmiast
> ([`updateConfiguration()`](../../src/controllers/mount_controller.cpp:7323)).
> Pozostałe pola są stosowane dopiero przy następnej operacji (jeśli pętla czyta
> je na bieżąco) albo po restarcie (jeśli są cache'owane przy `initialize()`).

---

## 1. Sekcja `mount`

| Parametr (JSON) | Używany | Runtime/Restart | Zapis |
|---|---|---|---|
| `type` | TAK | Restart | TAK |
| `axis_convention` | TAK (CASUAL) | Restart | TAK |
| `latitude`, `longitude`, `altitude` | TAK | Runtime | TAK |
| `max_slew_rate` | TAK | **Runtime** | TAK |
| `max_tracking_rate` | TAK | **Runtime** | TAK |
| `slew_acceleration` | TAK | **Runtime** | TAK |
| `tracking_acceleration` | TAK | **Runtime** | TAK |
| `mount_height`, `pier_west`, `pier_east` | TAK (TPOINT R5) | Runtime (TPOINT) | TAK |
| `default_temperature/pressure/humidity` | TAK | Runtime | TAK |
| `use_encoders` | TAK | Restart | TAK |
| `encoders_absolute` | TAK | Restart | TAK |
| `encoder_resolution` (mount-level) | **CZĘŚCIOWO** (myli z `hal.axes[].encoder_config.resolution`) | Restart | TAK |
| `position_tolerance`, `rate_tolerance` | TAK | **Runtime** | TAK |
| `slew_verify_tolerance_servo_deg` | TAK | Runtime | TAK |
| `meridian_flip_enabled` | TAK | Runtime | TAK |
| `meridian_flip_delay_minutes` | TAK | Runtime | TAK |
| `meridian_flip_hysteresis_degrees` | TAK | Runtime | TAK |
| `meridian_flip_timeout_seconds` | TAK | Runtime | TAK |
| `soft_limits_enabled` | TAK | Runtime | TAK |
| `soft_limit_axis1/2_min/max` | TAK | Runtime | TAK |
| `soft_limit_warning/deceleration_degrees` | TAK | Runtime | TAK |
| `soft_limit_tracking_rate_factor` | TAK | Runtime | TAK |
| `park_position_axis1/2` | TAK | Runtime | TAK |
| `enable_refraction_correction` | TAK | Runtime | TAK |
| `equatorial_tracking_velocity_mode` | TAK (legacy) | Restart | TAK |
| `tracking_mode` | TAK | Restart | TAK |
| `auto_track_after_goto` | TAK | Runtime | TAK |
| `tracking_pos_velocity_factor` | TAK | Runtime | TAK |
| `tracking_pos_lead_seconds` | TAK | Runtime | TAK |
| `tracking_pos_velocity_margin` | TAK | Runtime | TAK |
| `tracking_pos_update_hz` | TAK | Restart | TAK |
| `tracking_error_retries` | TAK | Runtime | TAK |
| `invert_axis1`, `invert_axis2` | TAK | Restart | TAK |
| `controller_poll_ms` | TAK | Restart | TAK |
| `tracking_update_ms` | TAK | Restart | TAK |
| `calc_update_hz` | TAK | Restart | TAK |
| `io_update_hz` | TAK | Restart | TAK |
| `telemetry_decimation` | TAK | Runtime | TAK |
| `safety_check_decimation` | TAK | Runtime | TAK |
| `split_tracking_loop` | TAK | Restart | TAK |
| `io_realtime`, `io_realtime_priority` | TAK | Restart | TAK |
| `pdo_fast_path` | TAK | Restart | TAK |
| `pdo_velocity_mapping` | TAK | Restart | TAK |
| `sdo_slow_control` | TAK | Restart | TAK |
| `orientation_quaternion` | TAK (CASUAL) | Restart | TAK |
| `ha_axis_params.gear_ratio`, `dec_axis_params.gear_ratio` | TAK | **Restart** | TAK |
| `ha/dec_axis_params.position_counts_per_degree` | TAK (HAL) | Restart | TAK |
| `ha/dec_axis_params.velocity_counts_per_deg_s` | TAK (HAL) | Restart | TAK |
| `ha/dec_axis_params.encoder_resolution` | TAK (HAL) | Restart | TAK |
| `ha/dec_axis_params.encoder_counts_per_arcsec` | CZĘŚCIOWO | Restart | TAK |
| `ha/dec_axis_params.encoder_quantization_error` | CZĘŚCIOWO | Restart | TAK |
| `ha/dec_axis_params.worm_ratio` | CZĘŚCIOWO | Restart | TAK |
| `ha/dec_axis_params.worm_teeth` | **NIE** (tylko przechowywane) | — | TAK |
| `ha/dec_axis_params.worm_wheel_teeth` | **NIE** (tylko przechowywane) | — | TAK |
| `ha/dec_axis_params.cyclic_error_amplitude/period` | CZĘŚCIOWO (TPOINT) | Restart | TAK |
| `ha/dec_axis_params.cyclic_harmonics` | **NIE** (przechowywane, nie liczone) | — | TAK |
| `ha/dec_axis_params.backlash`, `backlash_temp_coeff` | CZĘŚCIOWO (TPOINT) | Restart | TAK |
| `ha/dec_axis_params.axis_stiffness` | TAK (TPOINT) | Restart | TAK |
| `ha/dec_axis_params.torsional_compliance` | CZĘŚCIOWO (TPOINT) | Restart | TAK |
| `ha/dec_axis_params.expansion_coeff`, `temp_gear_error_coeff` | TAK (TPOINT) | Restart | TAK |
| `ha/dec_axis_params.calibration_table` | TAK (TPOINT) | Restart | TAK |
| `ha/dec_axis_params.calibration_temp` | TAK | Restart | TAK |

---

## 2. Sekcja `hal.axes[0/1]`

| Parametr | Używany | Runtime/Restart | Zapis |
|---|---|---|---|
| `can_node_id` | TAK | Restart | TAK |
| `name`, `id` | TAK | Restart | TAK |
| `invert_direction` | TAK | Restart | TAK |
| `motor_config.*` (current_limit, max_velocity, max_acceleration, …) | TAK | Restart | TAK |
| `encoder_config.*` (interface, resolution, counts_per_degree, …) | TAK | Restart | TAK |
| `safety_limits.*` (max_position, max_velocity, max_acceleration, …) | TAK | Restart | TAK |

Uwaga: `motor_config.max_velocity` i `safety_limits.max_velocity` są używane do
klamry `max_slew_rate` (80%) — patrz [`initialize()`](../../src/controllers/mount_controller.cpp:585).

---

## 3. Sekcja `hal.canopen`

| Parametr | Używany | Runtime/Restart | Zapis |
|---|---|---|---|
| `library`, `interface_name`, `bitrate`, `node_id` | TAK | Restart | TAK |
| `accel_mode`, `sdo_timeout_ms`, `pdo_update_rate` | TAK | Restart | TAK |
| `pdo_config_enabled`, `pdo_fast_path`, `pdo_velocity_mapping` | TAK | Restart | TAK |
| `position_rewind_*` | TAK | Restart | TAK |
| `nmt.*`, `speed_pid_schedule`, `speed_pid_adaptation_*` | TAK | Restart | TAK |
| `io_realtime`, `io_realtime_priority` | TAK | Restart | TAK |

Uwaga: `pdo_update_rate` (ms) wyznacza również kadencję próbkowania pozycji, z
której [`CanOpenMotor::updateStatus()`](../../src/hal/canopen_hal/canopen_hal.cpp:378)
estymuje rzeczywistą prędkość telemetryczną (okno kroczące 12 próbek ≈ 1,2 s
przy domyślnych 100 ms). Prędkość nie jest odczytywana przez SDO w ścieżce PDO,
lecz wyznaczana z pozycji — patrz [`hal_layer.md`](hal_layer.md).

---

## 4. Sekcja `hal.gamepad`

| Parametr | Używany | Runtime/Restart | Zapis |
|---|---|---|---|
| `device_path`, `autostart` | TAK | Restart | TAK |
| `axis_mapping`, `button_mapping` | TAK | Restart | TAK |
| `max_velocity_deg_s`, `sensitivity`, `deadzone` | TAK | **Runtime** (gamepad pętla) | TAK |
| `speed_presets` | TAK | Runtime | TAK |

---

## 5. Sekcje rzadziej używane (potencjalnie do usunięcia)

| Sekcja / parametr | Używany | Uwagi |
|---|---|---|
| `telescope.focal_length/aperture/tube_length` | TAK (TPOINT) | camera_model/pixel_size/sensor_* raczej nieużywane przez montaż |
| `telescope.camera_model` | **NIE** (serwis kamery osobno) | kandydat do usunięcia |
| `telescope.pixel_size/sensor_width/sensor_height` | **NIE** w kontrolerze montażu | kandydat |
| `telescope.lx200_enabled/port/baud_rate` | TAK (LX200 serwer) | — |
| `guider.*` (enabled, connection_string, max_correction, aggression, exposure_time_ms, binning) | CZĘŚCIOWO (ST4 osobny proces) | kandydat do przeniesienia/usunięcia |
| `kalman.process_noise/measurement_noise/adaptive_q/adaptive_r/innovation_threshold` | TAK | — |
| `kalman.max_iterations` | **NIE** (filtr używa stałego okna 50) | kandydat do usunięcia |
| `tpoint.enabled_terms/min_measurements/max_residual` | TAK | — |
| `tpoint.auto_calibrate` | CZĘŚCIOWO (wymaga weryfikacji) | — |
| `field_rotation.*` | CZĘŚCIOWO (wartości runtime; `computed_rate`/`applied_correction`/`temperature`/`flexure_correction` nadpisywane) | kandydat do usunięcia z configu |
| `external_services.*` (weather, power, sequencer) | TAK (procesy zewnętrzne) | — |
| `external_services.dome/derotator/focuser/st4_guider/pec` | TAK (in-process) | — |
| `hal.ethernet`, `hal.mf7025v2` | **NIE** przy CANopen STMP42SXI | kandydat |
| `servo_init.*` | TAK | — |

---

## 6. Plan usunięcia (kandydaci NIEUŻYWANI)

1. `mount.ha_axis_params.worm_teeth` / `worm_wheel_teeth` (HA i Dec) — nigdy nie są odczytywane w obliczeniach.
2. `mount.ha_axis_params.cyclic_harmonics` / `dec_axis_params.cyclic_harmonics` — przechowywane, nie liczone.
3. `mount.encoder_resolution` (poziom mount) — mylący duplikat `hal.axes[].encoder_config.resolution`.
4. `telescope.camera_model` / `pixel_size` / `sensor_width` / `sensor_height` — nieużywane przez kontroler montażu (kamera to osobny serwis).
5. `kalman.max_iterations` — nieużywane (stałe okno adaptacji).
6. `field_rotation.computed_rate` / `applied_correction` / `temperature` / `flexure_correction` — wartości runtime, nie konfiguracja.
7. `hal.ethernet` i `hal.mf7025v2` — martwe dla konfiguracji CANopen STMP42SXI (warto zostawić w backupach, usunąć z aktywnego pliku).
8. `guider.*` — do weryfikacji; ST4 guider ma własną konfigurację (`config/st4guider/`).

> Zalecana kolejność: najpierw usunąć pozycje 1–3 i 7 (bez ryzyka), potem 4–6 i 8 po weryfikacji grepowaniem `telescope.camera_model` itd. w `src/`.

---

## 7. Runtime vs Restart — podsumowanie najważniejszych

**Działają od ręki (runtime), bo czytane na bieżąco:**
`max_slew_rate`, `max_tracking_rate`, `slew_acceleration`, `tracking_acceleration`,
`position_tolerance`, `soft_limit_*`, `park_position_*`, `meridian_flip_*`,
`enable_refraction_correction`, `auto_track_after_goto`, `tracking_pos_*`,
`log_level`, `log_console_output`, `gamepad.max_velocity_deg_s`.

**Wymagają restartu (cache'owane przy starcie):**
`type`, `axis_convention`, `gear_ratio` (obie osie), `encoders_absolute`,
`use_encoders`, `split_tracking_loop`, `calc_update_hz`, `io_update_hz`,
`io_realtime*`, `pdo_*`, `sdo_slow_control`, `tracking_mode`,
`equatorial_tracking_velocity_mode`, cała sekcja `hal.*`, `servo_init.*`,
`network.*` (adres/port gRPC), `invert_axis1/2`.

---

## 8. Poprawność zapisu (round-trip)

- `Configuration::saveToFile()` ([`configuration.cpp`](../../src/config/configuration.cpp)) i
  `MountController::saveConfigToFile()` ([`mount_controller.cpp`](../../src/controllers/mount_controller.cpp:7367))
  zapisują większość pól, ale używają strażników **„tylko jeśli nie-zero/nie-puste"**:
  - liczby równe `0` **nie są zapisywane** (nie da się ustawić wartości na 0),
  - puste stringi **nie są zapisywane**,
  - boolean-y używają `setAlways` → zapisują się poprawnie,
  - tablice (`calibration_table`, `cyclic_harmonics`, `orientation_quaternion`) zapisują się tylko gdy niepuste.

**Znany problem**: wyzerowanie parametru (np. `max_tracking_rate = 0` lub
`mount_height = 0`) przez UI nie utrwali się na dysku — po restarcie wróci stara
wartość. Dotyczy to obu ścieżek zapisu.

**Działa poprawnie (round-trip)**: boolean-y, stringi niepuste, liczby niezerowe.

---

## 9. Niewielka liczba pól vs UI

Web UI (settings.js) eksponuje podzbiór: `mount_type`, `max_slew_rate`,
`max_tracking_rate`, `slew_acceleration`, `tracking_acceleration`, adresy
połączeń, gear ratio, itd. Pola HAL (CANopen/PDO/rewind) oraz `servo_init`
**nie są dostępne w UI** — tylko w pliku JSON (wymagają restartu).

Parametry UI Setup (układ tabów, rozmiar czcionki/ikon) to preferencje
przeglądarki zapisywane w **cookies** (nie w konfiguracji kontrolera).
