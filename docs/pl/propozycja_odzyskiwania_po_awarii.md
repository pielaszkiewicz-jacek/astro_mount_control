# Propozycja: mechanizm odzyskiwania po awarii / wznowienie z punktu kontrolnego (checkpoint) dla kontrolera montażu

> **Status:** propozycja projektowa — bez zmian w kodzie.
> Zakres: cykliczne zapisywanie stanu kontrolera (punkty kontrolne) oraz możliwość
> wznowienia pracy kontrolera od momentu awarii na podstawie zapisanego stanu.
> Wdrożenie może nastąpić w późniejszym etapie.

## Spis treści

1. [Istniejące podstawy](#1-istniejące-podstawy)
2. [Scenariusze awarii do obsłużenia](#2-scenariusze-awarii-do-obsłużenia)
3. [Proponowana architektura](#3-proponowana-architektura)
4. [Procedura wznowienia (bezpieczeństwo przede wszystkim)](#4-procedura-wznowienia)
5. [Nowa konfiguracja](#5-nowa-konfiguracja)
6. [Integracja API / UI](#6-integracja-api--ui)
7. [Przypadki brzegowe](#7-przypadki-brzegowe)
8. [Sugerowane fazy implementacji](#8-sugerowane-fazy-implementacji)

---

## 1. Istniejące podstawy

Projekt opiera się na tym, co już istnieje w repozytorium, aby późniejsza
implementacja była przyrostowa i nie wymyślała na nowo istniejących mechanizmów.

- [`MountController::saveState/loadState`](../../src/controllers/mount_controller.cpp:4749)
  już zapisuje podstawowy stan wykonawczy:
  - pozycje osi (`axis1_position`, `axis2_position`),
  - offsety home (`home_offset_axis1/2`),
  - cele osi (`axis1_target`, `axis2_target`),
  - wyliczeniowy stan (`state`),
  - flagi: enkoderów, guidera, TPoint, bootstrappingu,
  - parametry środowiskowe (temperatura, ciśnienie, wilgotność),
  - stronę filaru (`pier_side`) i stan meridian flip,
  - model TPoint (plik towarzyszący `.tpoint`),
  - połączenie guidera, błędy śledzenia,
  - kwaternion orientacji montażu,
  - znacznik czasu (`timestamp`).
- Obecnie jest wywoływany tylko ręcznie:
  - przez gRPC `SaveState` ([`service_impl.cpp`](../../src/api/service_impl.cpp:230)),
  - po kalibracji z gamepada ([`mount_controller.cpp`](../../src/controllers/mount_controller.cpp:7127),
    [`…:7151`](../../src/controllers/mount_controller.cpp:7151)).
- Istnieje już [`Watchdog`](../../include/safety/watchdog.h:87) ze zdarzeniami:
  `ERROR_THRESHOLD_EXCEEDED`, `RECOVERY_*`, `SYSTEM_RESUMED`, `EMERGENCY_STOP_TRIGGERED`.
- Pętla śledzenia ma już zabezpieczenie przed zawieszeniem (dt > 5 s → `ERROR`).
- Istnieje ścieżka „miękkiego restartu” ([`mount_controller.cpp`](../../src/controllers/mount_controller.cpp:6315))
  oraz mechanizm `config_file_path_` ([`main.cpp`](../../src/main.cpp:239)).

---

## 2. Scenariusze awarii do obsłużenia

| Scenariusz | Sprzęt po awarii | Cel odzyskania |
|---|---|---|
| Awaria procesu kontrolera (silniki nadal zasilane) | Montaż może się nadal poruszać | Wznowienie śledzenia/slewu z punktu kontrolnego, uzgodnienie pozycji |
| Zanik zasilania (wyłączenie całej płytki) | Montaż zatrzymany (lub dryf bez prowadzenia) | Przywrócenie kalibracji + ostatniej aktywności; ponowny slew/track po włączeniu |
| Nagły wypadek watchdog / stan ERROR | Montaż zatrzymany przez bezpieczniki | Zapisanie ostatniego dobrego stanu, zaproponowanie wznowienia |
| Awaria w trakcie zapisu punktu kontrolnego | — | Nigdy nie uszkodzić ostatniego dobrego punktu (atomowa zmiana nazwy) |

---

## 3. Proponowana architektura

Podsystem „Odzyskiwanie/Punkty kontrolne” składający się z trzech współpracujących
elementów wewnątrz (lub obok) `MountController::Impl`:

1. **CheckpointWriter** — okresowe + wyzwalane zdarzeniami, atomowe migawki stanu.
2. **CrashDetector** — znacznik działania + flaga czystego zamknięcia, pozwalające
   stwierdzić, że doszło do awarii.
3. **ResumeOrchestrator** — decyzja przy starcie + bezpieczne przywrócenie wcześniejszej
   aktywności.

### 3.1 Okresowe zapisywanie punktów kontrolnych

- Osobny wątek w tle (lub istniejąca pętla w tle) zapisuje stan co
  `recovery.interval_seconds` (domyślnie ~5 s).
- **Częstotliwość adaptacyjna:**
  - 1–2 s podczas SLEW / TRACK / MERIDIAN_FLIP / kalibracji,
  - 10–30 s gdy IDLE / PARKED.
- **Zapis wyzwalany zdarzeniami** przy:
  - zmianach stanu (start/stop śledzenia, zakończenie slewu, start/koniec flipa, park/unpark),
  - zakończeniu kalibracji,
  - zmianie konfiguracji,
  - **awaryjny punkt kontrolny** — natychmiast po wykryciu błędu przez watchdog
    lub pętlę śledzenia.

### 3.2 Trwały, atomowy, rotacyjny zapis

- Zapis do pliku tymczasowego w tym samym katalogu → `fsync` → `rename` →
  `fsync(katalog)`. Dzięki temu awaria w trakcie zapisu nigdy nie uszkodzi
  poprzedniego dobrego punktu.
- **Rotacja:** `checkpoint.latest.json` + N generacji + mały manifest
  z monotonicznymi numerami sekwencji — przy uszkodzonym/niedokończonym najnowszym
  punkcie można wrócić do poprzedniej generacji.
- **Integralność:** wersja schematu + suma kontrolna; przechowywanie skrótu
  aktywnej konfiguracji, aby zmiana konfiguracji między sesjami unieważniała
  nieaktualne punkty.
- **Osobny katalog stanu** (`data/state/`), odrębny od pliku konfiguracyjnego,
  aby punkty kontrolne nigdy nie nadpisywały `config/default.json`.

### 3.3 Rozszerzony stan do zapisu (ponad obecne `saveState`)

- Tryb śledzenia (SIDEREAL / SOLAR / LUNAR / CUSTOM) + `tracking_active` +
  cel RA/Dec (cele są już zapisywane, ale nie tryb/flagę aktywności).
- Polecenia prędkości `axis1_rate_` / `axis2_rate_`.
- Niezastosowane jeszcze korekcje guidera.
- Sesja śledzenia efemeryd (id trackera, id obiektu, bieżący indeks interpolacji
  / okno czasowe) — umożliwia wznowienie śledzenia obiektów ruchomych.
- Stan filtru Kalmana (opcjonalnie, dla ciągłości).
- Kontekst oczekującego meridian flip (cel flipa, przejście strony filaru).
- Ostatnia **pozycja zweryfikowana przez HAL** + źródło pozycji
  (enkoder absolutny / inkrementalny / kinematyczny) — kluczowe dla uzgodnienia pozycji.
- Metadane: znacznik czasu, uptime, sekwencja, powód zapisu oraz flaga
  „bezpieczny do automatycznego wznowienia”.

---

## 4. Procedura wznowienia

Bezpieczeństwo przede wszystkim. `recovery.auto_resume` domyślnie **false** —
po awarii operator potwierdza przez UI/API; przy wartości `true` wznowienie działa
automatycznie (praca bez nadzoru / headless).

Przy starcie `ResumeOrchestrator`:

1. **Brak ważnego/aktualnego punktu kontrolnego** → normalny zimny start (IDLE/PARKED).
2. **Weryfikacja integralności + wieku** (`max_age_seconds`); jeśli punkt nieaktualny
   lub konfiguracja niezgodna → zimny start + powiadomienie.
3. **Przywrócenie zapisanego stanu** przez rozszerzone `loadState`.
4. **Uzgodnienie pozycji fizycznej z HAL:**
   - enkodery absolutne → ufać im,
   - enkodery inkrementalne / nieznane → wymagać Home (lub użyć pozycji z punktu
     kontrolnego za zgodą operatora).
   - **Nigdy nie ruszać się „na ślepo”.**
5. **Przywrócenie wcześniejszej aktywności:**
   - stan `TRACKING` → przeliczenie bieżącego HA z zapisanego RA/Dec (stały cel
     syderyczny) i ponowne uruchomienie pętli śledzenia; jeśli niezgodność pozycji
     przekracza tolerancję → najpierw ponowny slew do celu (lub czekanie na potwierdzenie),
   - w trakcie `SLEW` → ponowne wysłanie slewu do zapisanego celu,
   - stan `MERIDIAN_FLIP` → najbezpieczniej dokończyć do zapisanego celu po flipie,
     w przeciwnym razie przejście do IDLE + powiadomienie,
   - stan `PARKED` / `IDLE` → pozostać w miejscu,
   - w trakcie kalibracji → **nigdy nie wznawiać automatycznie**; wymagać ponownego
     wykonania kalibracji.
6. **Emisja `SYSTEM_RESUMED`** przez watchdog/status callback oraz pokazanie
   wznowionego stanu w UI.

Każdy niejednoznaczny przypadek kończy się **bezpiecznym PARKED/IDLE z monitem
dla operatora**, a nie nieoczekiwanym ruchem.

---

## 5. Nowa konfiguracja

```json
"recovery": {
  "enabled": false,
  "auto_resume": false,
  "state_dir": "data/state",
  "interval_seconds": 5,
  "max_age_seconds": 3600,
  "keep_generations": 3
}
```

---

## 6. Integracja API / UI

- **gRPC:**
  - `GetRecoveryStatus` — status odzyskiwania,
  - `SaveCheckpoint` — ręczny zapis punktu kontrolnego,
  - `ResumeFromCheckpoint` — ręczne wznowienie z punktu,
  - `ClearCheckpoints` — czyszczenie punktów.
- **Web UI:**
  - wskaźnik odzyskiwania (czas / sekwencja / stan ostatniego punktu),
  - przycisk **Wznów** z potwierdzeniem,
  - baner, gdy przy starcie wykryto punkt po awarii.

---

## 7. Przypadki brzegowe

- Awaria w trakcie zapisu → atomowy `rename` zachowuje ostatni dobry punkt.
- Sprzęt poruszył się w czasie przestoju → uzgodnienie pozycji z HAL; bez ruchów „na ślepo”.
- Utrata referencji enkoderów inkrementalnych → wymagane Home; bez automatycznego
  wznowienia śledzenia.
- Zmiana konfiguracji między sesjami → niezgodność skrótu → odrzucenie punktu.
- Punkt zbyt stary → zimny start.
- Równolegli zapisujący → wątek pojedynczego zapisującego + blokada pliku.

---

## 8. Sugerowane fazy implementacji

1. **CheckpointWriter** — atomowe/rotacyjne/rozszerzone zapisy okresowe + wyzwalane
   (bez zmiany zachowania).
2. **CrashDetector** — znacznik działania + obsługa czystego zamknięcia.
3. **ResumeOrchestrator** — uzgodnienie pozycji + przywrócenie aktywności;
   najpierw ręczne wznowienie przez API/UI, potem auto-wznawianie za flagą.
4. **Wznawianie efemeryd / sekwencera** + test integracyjny odzyskiwania po awarii.

---

## Kluczowe zasady projektu

- **Atomowość** — nigdy nie uszkadzać ostatniego dobrego punktu.
- **Prawda o pozycji z HAL** — nigdy nie zakładać, gdzie jest montaż.
- **Bezpieczny fallback** — każda niejednoznaczność → PARKED/IDLE + monit operatora.
