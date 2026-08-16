# Raport — Faza 1: Naprawa integracji web-proxy (P1, P3, P4)

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 1 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md):
- P1 — trasy serwisów rozszerzonych wywołują RPC nieistniejące na `MountControllerService` (502/dane symulowane),
- P3 — focuser in-process na 50051 bez klienta w proxy,
- P4 — niespójność domyślnych włączeń serwisów (config ↔ proxy),
- P11 — brak `focuser_config.json`.

Faza 0 (baseline: 16/16 testów C++ + 30 asercji proxy potwierdzających 502/symulowane) wykonana w [`raport_faza_0_2026-08-14.md`](raport_faza_0_2026-08-14.md).

---

## 1. Co zostało zmienione

### 1.1 Dedykowane klienty gRPC — [`web/proxy/grpc/client.js`](../../web/proxy/grpc/client.js)

Dodano generyczną fabrykę `makeServiceClient(name, protoPath, serviceName, address)` tworzącą klienty dla serwisów rozszerzonych + bezpieczny wrapper wywołania:

| Klient | Proto | Serwis | Adres (domyślny) |
|--------|-------|--------|------------------|
| Weather | [`proto/weather.proto`](../../proto/weather.proto) | `WeatherService` | 127.0.0.1:50055 |
| Power | [`proto/power.proto`](../../proto/power.proto) | `PowerService` | 127.0.0.1:50056 |
| Sequencer | [`proto/sequencer.proto`](../../proto/sequencer.proto) | `SequencerService` | 127.0.0.1:50057 |
| Focuser | [`proto/focuser.proto`](../../proto/focuser.proto) | `FocuserService` | 127.0.0.1:50051 (in-process) |

Porty zweryfikowane w źródłach serwerów: weather 50055, power 50056, sequencer **50057** (dokumentacja w raporcie sugerowała 50054 — faktyczny domyślny to 50057).

Eksporty: `create/get/<nazwa>GrpcCall` dla każdego serwisu (weather, power, sequencer, focuser).

### 1.2 Konfiguracja adresów — [`web/proxy/config.js`](../../web/proxy/config.js)

Dodano sekcje `weather`/`power`/`sequencer`/`focuser` (host/port, zmienne env `*_GRPC_HOST`/`*_GRPC_PORT`), wzorem `dome`/`derotator`.

### 1.3 Startup/shutdown — [`web/proxy/server.js`](../../web/proxy/server.js)

Tworzenie nowych klientów przy starcie (guard `require.main === module`), zamykanie przy SIGINT/SIGTERM, rozszerzony banner.

### 1.4 Przełączone trasy (realny backend, bez danych symulowanych)

| Trasa | Klient | Zmiana |
|-------|--------|--------|
| [`routes/weather.js`](../../web/proxy/routes/weather.js) | `weatherGrpcCall` | GET `/status` → realne dane; catch → **503** (zamiast symulacji 15.5°C) |
| [`routes/power.js`](../../web/proxy/routes/power.js) | `powerGrpcCall` | `/status`, `/output`, `/history` → **503** (zamiast symulacji 12.5V) |
| [`routes/sequencer.js`](../../web/proxy/routes/sequencer.js) | `sequencerGrpcCall` | `/start`,`/stop`,`/pause`,`/resume`,`/load`,`/status` → **503** (zamiast stanu `IDLE`) |
| [`routes/focuser.js`](../../web/proxy/routes/focuser.js) | `focuserGrpcCall` + strumień | `/move`,`/halt`,`/status` → **503**; `/autofocus` obsługuje **server-streaming** `RunAutoFocus` (kolekcja postępu do `complete`) |
| [`routes/dome.js`](../../web/proxy/routes/dome.js) | `domeGrpcCall` | GET `/status` — usunięty symulowany fallback → **503** |
| [`routes/derotator.js`](../../web/proxy/routes/derotator.js) | `derotatorGrpcCall` | GET `/status`, `/field-rotation` — usunięte symulowane fallbacki → **503** |

### 1.5 Trasy odroczone do Fazy 2 (explicite „not implemented”)

[`routes/camera.js`](../../web/proxy/routes/camera.js), [`routes/pulley.js`](../../web/proxy/routes/pulley.js), [`routes/pec.js`](../../web/proxy/routes/pec.js), [`routes/guider.js`](../../web/proxy/routes/guider.js) — wszystkie endpointy zwracają **HTTP 503** z komunikatem „…Service is not implemented yet (deferred to Phase 2)”. **Nigdy** danych symulowanych.

### 1.6 Poprawiony `grpcCall` (i odpowiedniki)

- `grpcCall` (mount) oraz `makeServiceClient.call` walidują istnienie metody na kliencie — brak metody → czytelny błąd „Method 'X' does not exist on …” zamiast opakowanego `TypeError`.
- `call` jest teraz `async`, więc brak zainicjalizowanego klienta odrzuca czysto (Express 4 nie łapie synchronicznych rzutów w handlerze — zapobiega „wiszącemu” żądaniu).

### 1.7 UI — [`web/public/js/app.js`](../../web/public/js/app.js) i komponenty

- Nowa stała `DEFERRED_TABS` (camera/pulley/pec/guider) — zakładki **zawsze ukrywane** (backend dopiero w Fazie 2).
- `applyExternalServicesVisibility()` dodatkowo ukrywa zakładki odroczone.
- Nowa funkcja `App.showServiceUnavailable(panelId, serviceName)` — wstrzykuje widoczny baner „⚠️ Service unavailable” do panelu.
- Komponenty weather/power/sequencer/focuser/camera/pulley/pec/guider wołają `App.showServiceUnavailable(...)` w `.catch()` zamiast cichego `() => {}`.

### 1.8 Konfiguracja (P4/P11) — [`config/default.json`](../../config/default.json)

Sekcja `external_services` ujednolicona z proxy (`config.js`):

| Serwis | default.json | proxy (config.js) |
|--------|:------------:|:-----------------:|
| dome | false | false |
| **derotator** | **true** | **true** (domyślnie) |
| weather | false | false |
| power | false | false |
| sequencer | false | false |
| **focuser** | **true** | **true** (domyślnie) |

Dodano sekcje `weather`/`power`/`sequencer` z kluczem `address` (parsowane przez [`configuration.cpp`](../../src/config/configuration.cpp:648) — `getExternalIntegrationConfig()`). `focuser_config.json` już istnieje → **P11** potwierdzone jako zrealizowane.

---

## 2. Testy

### C++ (regresja po zmianie `default.json`)

```
100% tests passed, 0 tests failed out of 16   (ConfigurationTest 26/26 OK)
```

### Proxy — zaktualizowany baseline ([`web/proxy/test/proxy.integration.test.js`](../../web/proxy/test/proxy.integration.test.js))

```
# tests 27
# pass 27
# fail 0
```

| Grupa | Zachowanie | Liczba |
|-------|------------|:------:|
| Trasy rozszerzone (weather/power/sequencer/focuser/dome/derotator), GET+POST | **HTTP 503** z komunikatem (nigdy dane symulowane) | 15 |
| Trasy odroczone (camera/pulley/pec/guider), GET+POST | **HTTP 503 „not implemented”** | 12 |
| Kontrola: trasa montażu `/api/status` | **HTTP 503** | 1 |

### Test ręczny (uruchomiony serwer, brak backendów)

- Boot: 8 klientów gRPC (mount/db/dome/derotator/weather/power/sequencer/focuser).
- `GET /api/weather/status` → `503 {"error":"Weather service unavailable", ...}` (ECONNREFUSED 50055).
- `GET /api/camera/info` → `503 {"error":"CameraService is not implemented yet (deferred to Phase 2)"}`.
- `POST /api/focuser/move` → `503 {"error":"Focuser service unavailable"}`.
- `GET /api/config/external-services` → `{"dome":false,"derotator":true,"weather":false,"power":false,"sequencer":false,"focuser":true}` (zgodne z default.json — P4).

---

## 3. Status Fazy 1

| Usterka | Status |
|---------|:------:|
| **P1** — proxy wywołuje nieistniejące RPC na kliencie mount | ✅ naprawione (dedykowane klienty + jawny 503) |
| **P3** — brak klienta focuser w proxy | ✅ naprawione (klient in-process 50051) |
| **P4** — niespójne domyślne włączenia serwisów | ✅ ujednolicone (default.json ↔ proxy) |
| **P11** — brak `focuser_config.json` | ✅ plik istnieje (potwierdzone) |

**Uwaga:** trasy camera/pulley/pec/guider zwracają 503 „not implemented” do czasu Fazy 2 (P2 — hosting `CameraService`/`PulleyService`/`PecService`/`St4GuiderService`), a ich zakładki są ukryte w UI.
