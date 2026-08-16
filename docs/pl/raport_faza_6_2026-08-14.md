# Raport — Faza 6: Testy i dokumentacja

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 6 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md):
1. Rozszerzenie testów: integracyjne proxy (Faza 1), field-rotation per `mount_type` (Faza 3), TMC5160 position (Faza 3), źródła pogody (mock HTTP, Faza 4).
2. Aktualizacja `docs/pl/` (API, integracja serwisów, porty, stan kompletności) o stan po wdrożeniu.

---

## 1. Rozszerzenie testów

### Podsumowanie pokrycia (wszystkie fazy)

| Obszar | Test | Gdzie | Stan |
|--------|------|-------|:----:|
| **Integracyjne proxy (Faza 1)** | 32 testy: trasy rozszerzone → 503, odroczone → 503 „not implemented”, mount → 503 | [`web/proxy/test/proxy.integration.test.js`](../../web/proxy/test/proxy.integration.test.js) | ✅ |
| **Field-rotation per `mount_type` (Faza 3)** | EQUATORIAL → 0, ALT_AZ → model, CASUAL → kwaternion | [`tests/test_derotator.cpp`](../../tests/test_derotator.cpp) | ✅ |
| **TMC5160 position (Faza 3)** | `setAngle` → pozycja dochodzi do celu; `setRate` → integracja prędkości | [`tests/test_derotator.cpp`](../../tests/test_derotator.cpp) | ✅ |
| **Źródła pogody — parsowanie (Faza 4)** | próbki JSON: openweathermap/imgw/weathergov | [`tests/test_weather.cpp`](../../tests/test_weather.cpp) | ✅ |
| **Źródła pogody — mock HTTP (Faza 4/6)** | **pełny pipeline `httpGet` (libcurl) → JSON → `fetchCurrent`** przeciw lokalnemu serwerowi HTTP | [`tests/test_weather.cpp`](../../tests/test_weather.cpp) — `OpenWeatherMapMockHttpTest` | ✅ **nowość** |
| GPIO „brak czujnika”, kanały powiadomień | 12 testów w `WeatherTest` | [`tests/test_weather.cpp`](../../tests/test_weather.cpp) | ✅ |

### Nowy test w Faza 6 — `OpenWeatherMapMockHttpTest`

Uruchamia **lokalny serwer HTTP** (socket + wątek, jedna odpowiedź), ustawia `base_url_` źródła na `http://127.0.0.1:<port>/onecall` i wywołuje `fetchCurrent()` — weryfikuje cały łańcuch **libcurl HTTP GET → parsowanie JSON → `WeatherData`** (nie tylko parsowanie próbki). Wymaga POSIX-socket; pomijany (`GTEST_SKIP`) poza Linuxem.

### Wyniki

```
C++  : 100% tests passed, 0 failed out of 18
Proxy: 32/32
```

---

## 2. Aktualizacja dokumentacji

### [`docs/pl/konfiguracja_serwisow_zewnetrznych.md`](../../docs/pl/konfiguracja_serwisow_zewnetrznych.md)

Zaktualizowano do stanu po wdrożeniu Fazy 1–2/4:

| Serwis | Hosting | Port | Domyślnie |
|--------|---------|------|:---------:|
| Kopuła | w procesie | 50051 | wył. |
| Derotator | w procesie | 50051 | **wł.** |
| Focuser | w procesie | 50051 | **wł.** |
| **ST4 guider** | w procesie | 50051 | **wł.** |
| **PEC** | w procesie | 50051 | **wł.** |
| Pogoda | samodzielny (`astro_weather_server`) | 50055 | wył. |
| Zasilanie | samodzielny (`astro_power_server`) | 50056 | wył. |
| **Sekwencer** | samodzielny (`astro_sequencer_server`) | 50057 | wył. |

- Dodano `st4_guider`, `pec` (w procesie) i `sequencer` (samodzielny, port 50057).
- Zaktualizowano JSON `external_services` i domyślne włączenia (zgodne z web proxy).

---

## 3. Stan kompletności po wdrożeniu (Fazy 0–6)

| Zakładka UI | Backend | Stan |
|-------------|---------|:----:|
| Status / Control / Calibration / Tracking / Logging / Database / Tests | mount (50051) | ✅ realne |
| Dome | in-process 50051 | ✅ realne |
| Derotator | in-process 50051 | ✅ realne (P5/P6 poprawione) |
| Focuser | in-process 50051 | ✅ realne |
| **Guider** | in-process 50051 (Faza 2) | ✅ realne |
| **PEC** | in-process 50051 (Faza 2) | ✅ realne |
| Weather / Power / Sequencer | osobne procesy (50055/50056/50057) | ✅ trasy podpięte; dane zależą od backendu |
| Camera / Pulley | brak backendu (P2) | ⏸ zakładki ukryte, trasy 503 „not implemented” |
| LX200 | mount | ✅ realne |

**Serwisy hostowane in-process na 50051:** dome, derotator, focuser, ST4 guider, PEC.
**Osobne procesy:** weather (50055), power (50056), sequencer (50057), db (50052).

### Raporty faz
- Faza 0: [`raport_faza_0_2026-08-14.md`](raport_faza_0_2026-08-14.md)
- Faza 1: [`raport_faza_1_2026-08-14.md`](raport_faza_1_2026-08-14.md)
- Faza 2: [`raport_faza_2_2026-08-14.md`](raport_faza_2_2026-08-14.md)
- Faza 3: [`raport_faza_3_2026-08-14.md`](raport_faza_3_2026-08-14.md)
- Faza 4: [`raport_faza_4_2026-08-14.md`](raport_faza_4_2026-08-14.md)
- Faza 5: [`raport_faza_5_2026-08-14.md`](raport_faza_5_2026-08-14.md)

---

## 4. Podsumowanie wdrożonych poprawek

| # | Usterka | Faza | Status |
|---|---------|------|:------:|
| P1 | proxy wywołuje nieistniejące RPC na kliencie mount | 1 | ✅ |
| P2 | brak hostingu Camera/Pulley/Pec/St4Guider | 2 (częściowo) | ✅ guider/pec; ⏸ camera/pulley |
| P3 | brak klienta focuser w proxy | 1 | ✅ |
| P4 | niespójne domyślne włączenia | 1 | ✅ |
| P5 | derotator zawsze model alt-az | 3 | ✅ |
| P6 | TMC5160 brak integracji pozycji | 3 | ✅ |
| P7 | stub `startPHD2` | 2 | ⚠️ częściowo (kalibracja działa; PHD2 zewn.) |
| P8 | źródła pogody/GPIO stuby | 4 | ✅ |
| P9 | kanały powiadomień stuby | 4 | ✅ |
| P10 | ConfigMonitor martwy | 5 | ✅ |
| P11 | brak focuser_config.json | 1 | ✅ (istniał) |
| P12 | power_stub insecure | 5 | ✅ |
| P13 | konkurencja WatchStatus | 5 | ✅ |
| P14 | stuby HAL | 5 | ✅ usunięte |
| P15 | porządki (alt_true, callback, gui/) | 5 | ✅ |
