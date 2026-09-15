# Instrukcja obsługi sekwencjonera obserwacji

Sekwencjoner (`SequencerService`) automatyzuje nocne sesje obserwacyjne:
przelot (slew) na obiekt, rozpoczęcie śledzenia, opcjonalny autofokus,
wykonanie zaplanowanych ekspozycji, a na końcu zaparkowanie montażu.
Ten dokument opisuje, jak skonfigurować, uruchomić i obsługiwać sekwencjoner
z poziomu interfejsu WWW oraz API.

---

## 1. Architektura

Sekwencjoner działa jako **osobny proces** `astro_sequencer_server`
(domyślnie port `50057`). Komunikacja przebiega następująco:

```
Przeglądarka (SPA)
   │  HTTP/JSON
   ▼
Proxy Node.js (web/proxy)
   │  gRPC
   ▼
astro_sequencer_server (SequencerService)
   │  gRPC (tylko w trybie rzeczywistym)
   ▼
astro_mount_controller (MountControllerService :50051)
```

Najważniejsze pliki:

- [`proto/sequencer.proto`](../../proto/sequencer.proto:6) — definicja serwisu
  [`SequencerService`](../../proto/sequencer.proto:6) i komunikatów planu/statusu.
- [`sequencer/src/sequencer_service_impl.cpp`](../../sequencer/src/sequencer_service_impl.cpp:129) —
  implementacja RPC (ładowanie planu, start/stop/pauza, status).
- [`src/sequencer/observation_sequencer.cpp`](../../src/sequencer/observation_sequencer.cpp:85) —
  pętla sekwencji i maszyna stanów.
- [`web/proxy/routes/sequencer.js`](../../web/proxy/routes/sequencer.js:20) —
  trasy REST proxy.
- [`web/public/js/components/sequencer.js`](../../web/public/js/components/sequencer.js:6) —
  panel UI.

---

## 2. Konfiguracja

Główny plik konfiguracyjny sekwencjonera:
[`config/sequencer_config.json`](../../config/sequencer_config.json:1).

| Klucz            | Typ    | Domyślnie           | Opis                                                                 |
|------------------|--------|---------------------|----------------------------------------------------------------------|
| `enabled`        | bool   | `true`              | Włącza sekwencjoner (informacyjnie).                                 |
| `simulated`      | bool   | `true`              | Tryb symulowany (`true`) lub rzeczywisty (`false`).                  |
| `mount_address`  | string | `127.0.0.1:50051`   | Adres gRPC kontrolera montażu (używany gdy `simulated=false`).       |
| `camera_address` | string | `""`                | Zarezerwowane dla kamery (obecnie nieużywane).                       |
| `focuser_address`| string | `""`                | Zarezerwowane dla fokusera (obecnie nieużywane).                     |

Przykład (tryb rzeczywisty):

```json
{
    "enabled": true,
    "simulated": false,
    "mount_address": "127.0.0.1:50051",
    "camera_address": "",
    "focuser_address": ""
}
```

### Widoczność zakładki w interfejsie WWW

Aby zakładka **Sequencer** była widoczna w SPA, serwis musi być włączony
w konfiguracji zewnętrznej. W
[`config/default.json`](../../config/default.json:488) ustaw:

```json
"sequencer": {
    "enabled": true,
    "address": "127.0.0.1:50057",
    "poll_interval_ms": 5000
}
```

Alternatywnie w proxy można wymusić widoczność zmienną środowiskową:

```bash
EXT_SERVICE_SEQUENCER=true node web/proxy/server.js
```

---

## 3. Budowanie i uruchamianie

Budowanie:

```bash
cmake --build build --target astro_sequencer_server -j$(nproc)
```

Uruchamianie:

```bash
./build/bin/astro_sequencer_server \
    --address 0.0.0.0:50057 \
    --config config/sequencer_config.json
```

Dostępne opcje wiersza poleceń
([`sequencer/src/main.cpp`](../../sequencer/src/main.cpp:30)):

| Opcja                  | Opis                                                        |
|------------------------|-------------------------------------------------------------|
| `--address HOST:PORT`  | Adres nasłuchu (domyślnie `0.0.0.0:50057`).                 |
| `--config PATH`        | Ścieżka pliku konfiguracyjnego.                             |
| `--ssl CERT KEY`       | Włączenie TLS (certyfikat i klucz PEM).                     |
| `--help`               | Pomoc.                                                      |

---

## 4. Tryby pracy

### Tryb symulowany (`simulated=true`, domyślny)

Wszystkie operacje zewnętrzne są zastępowane wywołaniami zwrotnymi, które
tylko logują postęp i symulują opóźnienia
([`sequencer/src/sequencer_service_impl.cpp`](../../sequencer/src/sequencer_service_impl.cpp:73)):

- slew — log + 2 s przerwy,
- tracking — log,
- autofokus — log + 3 s przerwy,
- ekspozycja — log + 100 ms na każdą sekundę ekspozycji,
- parkowanie — log.

Tryb ten służy do testowania planów i interfejsu bez sprzętu.

### Tryb rzeczywisty (`simulated=false`)

Sekwencjoner łączy się z montażem przez gRPC pod adresem `mount_address`
i wykonuje rzeczywiste operacje
[`slew`](../../sequencer/src/sequencer_service_impl.cpp:104),
[`track`](../../sequencer/src/sequencer_service_impl.cpp:111) oraz
[`park`](../../sequencer/src/sequencer_service_impl.cpp:118)
poprzez [`MountControllerService`](../../proto/mount_controller.proto:328).

> **Uwaga:** w trybie rzeczywistym zaimplementowane są obecnie wyłącznie
> wywołania montażu (slew/track/park). Autofokus i ekspozycje kamery nie mają
> jeszcze podpiętych wywołań zwrotnych — te etapy są pomijane.

---

## 5. Interfejs WWW

1. Upewnij się, że zakładka **Sequencer** jest widoczna (patrz §2).
2. Przejdź do zakładki **Sequencer**.
3. Panel zawiera:
   - **State** — bieżący stan maszyny (`IDLE`, `SLEWING`, `TRACKING`,
     `FOCUSING`, `EXPOSING`, `PARKING`, `COMPLETED`, ...),
   - **Target** — indeks bieżącego obiektu,
   - **Exposure** — indeks bieżącej ekspozycji,
   - **Progress** — pasek postępu w procentach,
   - przyciski **▶ Start**, **⏹ Stop**, **⏸ Pause**, **▶ Resume**,
   - **Target List** — lista obiektów dodawanych ręcznie,
   - formularz dodawania obiektu (nazwa, RA w godzinach, Dec w stopniach),
   - przycisk **📂 Load Observation Plan**,
   - **Session Log** — log sesji.

Typowy przebieg z UI:

1. Dodaj obiekty w sekcji formularza (Nazwa, RA, Dec) przyciskiem **+ Add**.
2. Kliknij **📂 Load Observation Plan**, aby załadować plan.
3. Kliknij **▶ Start**, aby uruchomić sekwencję.
4. Monitoruj postęp i w razie potrzeby użyj **⏸ Pause** / **▶ Resume** / **⏹ Stop**.

---

## 6. API gRPC

Serwis [`SequencerService`](../../proto/sequencer.proto:6):

| RPC                     | Wejście                    | Wyjście             | Opis                                      |
|-------------------------|----------------------------|---------------------|-------------------------------------------|
| `LoadPlan`              | [`ObservationPlanProto`](../../proto/sequencer.proto:29) | [`SequencerResult`](../../proto/sequencer.proto:40) | Ładuje plan obserwacji. |
| `StartSequencer`        | `Empty`                    | [`SequencerResult`](../../proto/sequencer.proto:40) | Uruchamia sekwencję. |
| `StopSequencer`         | `Empty`                    | [`SequencerResult`](../../proto/sequencer.proto:40) | Zatrzymuje sekwencję. |
| `PauseSequencer`        | `Empty`                    | [`SequencerResult`](../../proto/sequencer.proto:40) | Wstrzymuje sekwencję. |
| `GetSequencerStatus`    | `Empty`                    | [`SequencerStatus`](../../proto/sequencer.proto:45) | Zwraca bieżący status. |

---

## 7. REST API proxy

Trasy dostępne pod prefixem `/api/sequencer`
([`web/proxy/routes/sequencer.js`](../../web/proxy/routes/sequencer.js:20)):

| Metoda | Ścieżka                   | Body                                   | Opis                          |
|--------|---------------------------|----------------------------------------|-------------------------------|
| POST   | `/api/sequencer/start`    | —                                      | Start sekwencji.              |
| POST   | `/api/sequencer/stop`     | —                                      | Stop sekwencji.               |
| POST   | `/api/sequencer/pause`    | —                                      | Pauza sekwencji.              |
| POST   | `/api/sequencer/resume`   | —                                      | Wznowienie sekwencji.         |
| POST   | `/api/sequencer/load`     | Plan JSON (`ObservationPlanProto`)     | Ładowanie planu.              |
| GET    | `/api/sequencer/status`   | —                                      | Bieżący status.               |

Przykład załadowania planu przez REST:

```bash
curl -X POST http://127.0.0.1:8080/api/sequencer/load \
  -H 'Content-Type: application/json' \
  -d '{
        "name": "Testowa sesja",
        "observer": "Jan",
        "notes": "Kalibracja sekwencjonera",
        "auto_focus": true,
        "auto_guide": false,
        "dither": false,
        "focus_interval": 2,
        "targets": [
          {
            "name": "M42",
            "catalog_name": "M",
            "ra": 5.588,
            "dec": -5.391,
            "exposure_time_s": 60,
            "gain": 0,
            "binning": 1,
            "filter_position": 0,
            "filter_name": "L",
            "exposure_count": 3
          }
        ]
      }'
```

Następnie start:

```bash
curl -X POST http://127.0.0.1:8080/api/sequencer/start
curl http://127.0.0.1:8080/api/sequencer/status
```

---

## 8. Format planu obserwacji

### [`ObservationPlanProto`](../../proto/sequencer.proto:29)

| Pole            | Typ                | Opis                                                            |
|-----------------|--------------------|-----------------------------------------------------------------|
| `name`          | string             | Nazwa planu.                                                     |
| `observer`      | string             | Obserwator.                                                      |
| `notes`         | string             | Notatki.                                                         |
| `targets`       | repeated `TargetProto` | Lista obiektów do sfotografowania.                            |
| `auto_focus`    | bool               | Automatyczny fokus między obiektami.                             |
| `auto_guide`    | bool               | Automatyczne prowadzenie (obecnie nieużywane w pętli).           |
| `dither`        | bool               | Dithering (obecnie nieużywane w pętli).                          |
| `focus_interval`| int32              | Co ile obiektów wykonać autofokus (gdy `auto_focus=true`).       |

### [`TargetProto`](../../proto/sequencer.proto:14)

| Pole                | Typ    | Opis                                              |
|---------------------|--------|---------------------------------------------------|
| `name`              | string | Nazwa obiektu.                                     |
| `catalog_name`      | string | Nazwa katalogu (np. `M`, `NGC`).                   |
| `ra`                | double | Rektascensja w **godzinach**.                      |
| `dec`               | double | Deklinacja w **stopniach**.                        |
| `magnitude`         | double | Jasność (opcjonalnie).                             |
| `type`              | string | Typ obiektu (opcjonalnie).                         |
| `exposure_time_s`   | double | Czas pojedynczej ekspozycji w sekundach.           |
| `gain`              | int32  | Wzmocnienie kamery.                                |
| `binning`           | int32  | Binning.                                           |
| `filter_position`   | int32  | Pozycja filtra.                                    |
| `filter_name`       | string | Nazwa filtra.                                      |
| `exposure_count`    | int32  | Liczba ekspozycji dla tego obiektu.                |

---

## 9. Przebieg sekwencji (maszyna stanów)

Pętla [`sequencerLoop()`](../../src/sequencer/observation_sequencer.cpp:85)
dla każdego obiektu wykonuje:

1. `SLEWING` — przelot na współrzędne obiektu,
2. `TRACKING` — rozpoczęcie śledzenia,
3. `FOCUSING` — autofokus, jeśli `auto_focus=true`
   oraz `indeks_obiektu % focus_interval == 0`,
4. `EXPOSING` — kolejne ekspozycje zgodnie z planem,
5. po wszystkich obiektach `PARKING` → `COMPLETED`.

Stan `PARKED` pojawia się, gdy sekwencja zostanie przerwana alertem pogodowym
([`onWeatherAlert()`](../../src/sequencer/observation_sequencer.cpp:75)).

---

## 10. Znane ograniczenia

- `auto_guide` i `dither` są zapisywane w planie, ale pętla sekwencji
  jeszcze ich nie wykorzystuje.
- W trybie rzeczywistym podpięte są tylko operacje montażu
  (slew/track/park); autofokus i ekspozycje kamery nie są wykonywane.
- `PauseSequencer` wstrzymuje pętlę **między obiektami** — nie przerywa
  trwającej ekspozycji.
- Endpoint `/api/sequencer/resume` mapuje się na `StartSequencer` i w bieżącej
  implementacji nie kasuje flagi pauzy; wznawianie po pauzie może wymagać
  uzupełnienia o dedykowany RPC `ResumeSequencer`.
- Formularz w UI dodaje obiekty wyłącznie do podglądu listy; przycisk
  **Load Observation Plan** wysyła puste body. W praktyce plan najlepiej
  ładować bezpośrednio przez REST (patrz §7) lub gRPC.
- `GetSequencerStatus` wypełnia obecnie tylko `state`,
  `current_target_index`, `current_exposure` i `progress_percent`;
  pozostałe pola statusu pozostają na wartościach domyślnych.
