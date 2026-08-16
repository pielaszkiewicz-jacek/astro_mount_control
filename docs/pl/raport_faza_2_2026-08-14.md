# Raport — Faza 2: Uzupełnienie brakujących serwisów (P2)

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 2 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md):
1. **St4GuiderService** — implementacja `St4GuiderServiceImpl` oparta o `St4Guider` + `St4Calibration`, zarejestrowana in-process na 50051, z podpiętym klientem proxy.
2. **PecService** — wrapper wokół `PECModel` (enable/train/status), zarejestrowany in-process, z podpiętym klientem proxy.
3. **CameraService / PulleyService** — odroczone (brak HAL); zakładki ukryte (zrealizowane w Fazie 1).

Faza 1 (P1/P3/P4): [`raport_faza_1_2026-08-14.md`](raport_faza_1_2026-08-14.md).

---

## 1. Nowe serwisy C++ (in-process, port 50051)

### 1.1 St4GuiderServiceImpl — [`st4guider/`](../../st4guider)

| Plik | Opis |
|------|------|
| [`st4guider/include/st4_guider_service_impl.h`](../../st4guider/include/st4_guider_service_impl.h) | Nagłówek `astro_st4guider::St4GuiderServiceImpl` (`St4GuiderService::Service`) |
| [`st4guider/src/st4_guider_service_impl.cpp`](../../st4guider/src/st4_guider_service_impl.cpp) | Implementacja |

- Oparty o [`St4Guider`](../../src/controllers/st4_guider.cpp) + [`St4Calibration`](../../src/controllers/st4_calibration.cpp) (kontroler istniejący od wcześniej).
- HAL ST4 z configu: `simulated` (domyślnie) lub `gpio_sysfs` (piny z configu); fallback do symulacji.
- RPC: `StartGuiding`, `StopGuiding`, `GetStatus`, `PulseGuide`, `Calibrate` (**server-streaming** — strumieniuje postęp, kończy `complete` z `calibration_arcsec_per_ms`; bez sondy używa teoretycznych współczynników syderealnych 15.041 ″/s).

### 1.2 PecServiceImpl — [`pec/`](../../pec)

| Plik | Opis |
|------|------|
| [`pec/include/pec_service_impl.h`](../../pec/include/pec_service_impl.h) | Nagłówek `astro_pec::PecServiceImpl` (`PECService::Service`) |
| [`pec/src/pec_service_impl.cpp`](../../pec/src/pec_service_impl.cpp) | Implementacja |

- Wrapper wokół [`PECModel`](../../src/models/pec_model.cpp) (pełna ścieżka: próbki → DFT per-cykl → rekonstrukcja harmonicznych).
- RPC: `StartTraining` (**server-streaming** — 120 ramek postępu + `complete` z peak/RMS), `StopTraining`, `GetPECStatus`, `SetPECEnabled`, `SavePECData`/`LoadPECData` (persystencja do `config/pec_data.json`).
- Trening używa **syntetycznych** próbek błędu okresowego (podstawowa + 2. harmoniczna) — brak zewnętrznego źródła enkoderów in-process; pełna demonstracja pipeline'u PEC.

### 1.3 Rejestracja i konfiguracja

- [`src/main.cpp`](../../src/main.cpp): inkluzje, globals, rejestracja `registerService()` dla `St4GuiderServiceImpl` i `PecServiceImpl`, teardown (ścieżka normalna i wyjątków).
- [`CMakeLists.txt`](../../CMakeLists.txt): nowe źródła w `MERGED_SERVICE_SOURCES`.
- [`include/config/configuration.h`](../../include/config/configuration.h) + [`src/config/configuration.cpp`](../../src/config/configuration.cpp): pola `st4_guider_enabled`, `pec_enabled` w `ExternalIntegrationConfig` (parsowane z `external_services`).
- [`config/default.json`](../../config/default.json) i [`config/emulation.json`](../../config/emulation.json): `st4_guider` i `pec` włączone domyślnie.
- Nowe pliki config: [`config/st4_guider_config.json`](../../config/st4_guider_config.json), [`config/pec_config.json`](../../config/pec_config.json).

## 2. Proxy i UI

- [`web/proxy/grpc/client.js`](../../web/proxy/grpc/client.js): klienty `St4GuiderService` i `PECService` (fabryka `makeServiceClient`) → `guiderGrpcCall` / `pecGrpcCall`.
- [`web/proxy/config.js`](../../web/proxy/config.js): adresy `guider`/`pec` (in-process 50051) + `external_services` (domyślnie włączone).
- [`web/proxy/server.js`](../../web/proxy/server.js): tworzenie/zamykanie klientów guider/pec.
- [`web/proxy/routes/guider.js`](../../web/proxy/routes/guider.js) i [`web/proxy/routes/pec.js`](../../web/proxy/routes/pec.js): podpięte do realnych serwisów; `Calibrate`/`StartTraining` obsługują **server-streaming** (kolekcja postępu). Bez backendu → jawny **503** (nigdy dane symulowane).
- [`web/public/js/app.js`](../../web/public/js/app.js): guider i pec przeniesione z `DEFERRED_TABS` (zawsze ukryte) do `EXT_SERVICE_TABS` (widoczne, sterowane configiem proxy). Camera/pulley pozostają ukryte (odroczone).

## 3. Testy i weryfikacja

### C++ — `ctest`
```
100% tests passed, 0 tests failed out of 16
```

### Proxy — [`web/proxy/test/proxy.integration.test.js`](../../web/proxy/test/proxy.integration.test.js)
```
# tests 32
# pass 32
# fail 0
```
Guider/pec przeniesione z grupy „not implemented” do grupy tras rozszerzonych (503 przy braku backendu); camera/pulley pozostały odroczone.

### Test end-to-end (tryb emulacji — brak sprzętu)
Kontroler uruchomiony z [`config/emulation.json`](../../config/emulation.json) (SimulatedHAL) + proxy; zweryfikowano realne dane przez HTTP:

| Endpoint | Wynik |
|----------|-------|
| `GET /api/guider/status` | `guiding:false, connected:true, calibrated:false` |
| `POST /api/guider/start` | `success:true` |
| `POST /api/guider/calibrate` (stream) | `complete`, `calibration_arcsec_per_ms:0.015041` (teoretyczny syderealny) |
| `GET /api/guider/status` | `calibrated:true` |
| `POST /api/pec/train/start` (stream) | 120 ramek + `complete`, `peak:9.56″`, `rms:6.04″` |
| `GET /api/pec/status` | `trained:true, num_harmonics:8, correction_arcsec:0.077` |

## 4. Status Fazy 2

| Usterka / element | Status |
|-------------------|:------:|
| **P2** — `St4GuiderService` | ✅ zaimplementowany i hostowany in-process (50051) + klient proxy |
| **P2** — `PecService` | ✅ zaimplementowany i hostowany in-process (50051) + klient proxy |
| **P2** — `CameraService` / `PulleyService` | ⏸ odroczone (brak HAL kamery/silnika); zakładki ukryte |
| **P7** — stub `startPHD2` | ⚠️ pozostał (integruje się z zewnętrznym PHD2 w późniejszej fazie); `GetStatus`/kalibracja działają |

**Uwaga:** `St4GuiderService::Calibrate` bez sondy (kamera przewodnia) używa współczynników teoretycznych — pomiarowa kalibracja wymaga zewnętrznego źródła przemieszczenia (Faza późniejsza).
