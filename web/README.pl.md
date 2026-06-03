# Astronomical Mount Controller — Interfejs Webowy

## Przegląd

Interfejs webowy zapewnia zdalne sterowanie kontrolerem montażu astronomicznego za pośrednictwem przeglądarki, używając proxy HTTP/JSON komunikującego się z backendem gRPC. Jest to aplikacja jednostronicowa (SPA) zbudowana z modułów czystego JavaScript, bez zależności frameworkowych.

### Architektura

```
┌──────────────┐    HTTP/JSON     ┌────────────────────┐    gRPC     ┌─────────────────────┐
│  Przeglądarka │ ◄──────────────► │  Proxy Express     │ ◄────────► │  Kontroler Montażu  │
│  (SPA)        │   port 8080      │  web/proxy/server.js│  port 50051│  (C++ gRPC server)  │
│               │                  │                     │            │                     │
│  index.html   │                  │  Pliki statyczne:   │            │  Slew, Track,       │
│  app.js       │                  │  ./public/          │            │  Park, Kalibruj...  │
│  api.js       │                  │                     │            └─────────────────────┘
│  components/  │                  │  CORS                │
│               │                  │  Konfiguracja .env   │            ┌─────────────────────┐
└──────────────┘                  │                     │    gRPC    │  Baza Obiektów      │
                                   │                     │ ◄────────► │  (C++ gRPC server)  │
                                   │                     │  port 50052│                     │
                                   └─────────────────────┘            │  CRUD katalogów,     │
                                                                      │  Wyszukiwanie, Import│
                                                                      └─────────────────────┘
```

### Struktura katalogów

```
web/
├── proxy/
│   ├── .env.example        # Szablon konfiguracji środowiska
│   ├── package.json        # Zależności Node.js
│   ├── package-lock.json
│   └── server.js           # Serwer proxy Express (HTTP/JSON → gRPC)
├── public/
│   ├── index.html          # Punkt wejścia SPA (cała struktura DOM)
│   ├── css/
│   │   └── style.css       # Pełne style aplikacji
│   └── js/
│       ├── app.js          # Główny moduł aplikacji (zakładki, polling, motywy)
│       ├── api.js          # Klient API HTTP/JSON
│       ├── utils.js        # Wspólne narzędzia (formatowanie, helpery DOM)
│       ├── logger.js       # Logowanie po stronie klienta
│       └── components/
│           ├── mountControl.js   # Zakładka sterowania montażem (slew, panel osi, stan)
│           ├── mountStatus.js    # Zakładka statusu montażu (wyświetlanie w czasie rzeczywistym)
│           ├── database.js       # Zakładka bazy obiektów (CRUD, wyszukiwanie, import)
│           ├── calibration.js    # Zakładka kalibracji (Bootstrap + TPOINT)
│           ├── tracking.js       # Zakładka śledzenia efemeryd (obiekty ruchome)
│           └── settings.js       # Zakładka ustawień (18 grup konfiguracyjnych, import/eksport)
└── README.pl.md            # Ten plik
```

---

## Szybki start

### Wymagania wstępne

- **Node.js** ≥ 18.0.0
- Serwer gRPC **Kontrolera Montażu** uruchomiony (domyślnie `localhost:50051`)
- Serwer gRPC **Bazy Obiektów** uruchomiony (domyślnie `localhost:50052`)

### Instalacja

```bash
# Przejdź do katalogu proxy
cd web/proxy

# Zainstaluj zależności
npm install

# Skonfiguruj środowisko (edytuj według potrzeb)
cp .env.example .env
```

### Konfiguracja (`.env`)

| Zmienna | Wartość domyślna | Opis |
|---------|---------|-------------|
| `GRPC_HOST` | `localhost` | Host gRPC kontrolera montażu |
| `GRPC_PORT` | `50051` | Port gRPC kontrolera montażu |
| `DB_GRPC_HOST` | `localhost` | Host gRPC bazy obiektów |
| `DB_GRPC_PORT` | `50052` | Port gRPC bazy obiektów |
| `PROXY_HOST` | `0.0.0.0` | Adres bindowania serwera proxy |
| `PROXY_PORT` | `8080` | Port HTTP serwera proxy |
| `ENABLE_SSL` | `false` | Włącz TLS dla połączeń gRPC |
| `SSL_CERT_PATH` | `` | Ścieżka do certyfikatu SSL |
| `SSL_KEY_PATH` | `` | Ścieżka do klucza SSL |
| `CORS_ORIGINS` | `http://localhost:8080` | Dozwolone źródła CORS (oddzielone przecinkami) |
| `LOG_LEVEL` | `dev` | Format loggera Morgan (`dev`, `combined`, `tiny`) |

### Uruchomienie

```bash
# Uruchom serwer proxy
cd web/proxy
npm start

# Lub do developmentu z automatycznym przeładowaniem przy zmianach:
npm run dev
```

Serwer proxy udostępnia SPA pod adresem `http://localhost:8080` oraz API HTTP/JSON pod tym samym originem.

---

## Interfejs użytkownika — opis zakładek

### Zakładka Status

Panel statusu montażu w czasie rzeczywistym z czterema kartami informacyjnymi:

- **Status Montażu** — bieżący stan (IDLE/SLEWING/TRACKING/PARKED/ERROR), status enkoderów, status guidera, strona pylonu, status flipu południkowego, czas do południka
- **Pozycja** — pozycje osi 1/2 (stopnie), szybkości śledzenia w RA/Dec (łuksek/s)
- **Środowisko** — temperatura (°C), ciśnienie (hPa), wilgotność (%)
- **Śledzenie** — nazwa śledzonego obiektu, RA/Dec (format hms/dms), błąd śledzenia (łuksek), wydajność śledzenia (%), błąd wskazywania (łuksek)

Gdy nazwa obiektu jest obecna w danych śledzenia, komponent statusu automatycznie wykonuje zapytanie do bazy danych, aby wyświetlić dodatkowe szczegóły obiektu (typ, katalog, jasność, gwiazdozbiór, typ widmowy, odległość).

**Znaczniki:**
- Wskaźnik połączenia (zielony = połączony, czerwony = rozłączony)
- Wskaźnik połączenia bazy danych (zielony = połączony, czerwony = offline)

### Zakładka Sterowanie

Panel sterowania montażem z czterema sekcjami funkcyjnymi:

#### Slew do współrzędnych
- Wpisz Rektascensję (0–24 h) i Deklinację (–90° do +90°), a następnie kliknij **Slew**
- Wysyła `POST /slew` z `{ ra, dec }`

#### Szybkie akcje
- **Stop** — Zatrzymaj cały ruch montażu
- **Park** — Zaparkuj montaż w skonfigurowanej pozycji parkingowej
- **Unpark** — Przywróć montaż do stanu operacyjnego
- **Clear Errors** — Wyczyść flagi błędów

#### Panel sterowania osiami
Czterokierunkowy panel sterowania z dwoma trybami pracy:

- **Tryb prędkości** (domyślny): Przytrzymaj przycisk kierunku, aby poruszać się w sposób ciągły. Zwolnij, aby zatrzymać. Suwak prędkości (0.1–5.0 °/s) kontroluje prędkość ruchu.
  - Gdy montaż jest **skalibrowany** (tryb astronomiczny): Naciśnięcia używają offsetów współrzędnych — RA+/RA– dla osi 1, Dec+/Dec– dla osi 2 w montażu równikowym, lub Alt+/Az+ w montażu azymutalnym
  - Gdy **nieskalibrowany** (tryb niskopoziomowy): Bezpośrednie sterowanie prędkością osi przez `POST /axis/move`

- **Tryb krokowy**: Pojedyncze kliknięcie przemieszcza o skonfigurowany rozmiar kroku (0.01–90°). W trybie skalibrowanym używa offsetów współrzędnych. W trybie nieskalibrowanym porusza się z bieżącą prędkością przez czas obliczony z rozmiaru kroku / prędkości.

- **EMERGENCY STOP** — Natychmiast zatrzymuje cały ruch osi

#### Stan Montażu
- **Save State** — Zapisz bieżący stan kontrolera montażu (pozycje, kalibracja, orientacja) do pliku na serwerze. Wpisz ścieżkę katalogu i nazwę pliku, lub kliknij **Now**, aby automatycznie wygenerować `mount_state_YYYY-MM-DD_HHMMSS.json`
- **Load State** — Przywróć stan montażu z wcześniej zapisanego pliku na serwerze
- **Upload & Load** — Wybierz plik stanu z lokalnego komputera, aby przesłać i załadować

### Zakładka Ustawienia

Zarządzanie konfiguracją z trzema sekcjami:

#### Informacje o serwerze
Wyświetla port proxy, interwał pollingu i informacje o wersji.

#### Adresy połączeń
- Skonfiguruj host/port gRPC dla Kontrolera Montażu i Bazy Obiektów
- Kliknij **Save & Reconnect**, aby zastosować zmiany

#### Grupy konfiguracyjne
Konfiguracja jest podzielona na 23 zwijane grupy, każda z przyciskami **Save** i **Restore Defaults**:

| # | Grupa | Kluczowe pola |
|---|-------|-------------|
| 1 | Logowanie | poziom, katalog, rotacja_dni, max_rozmiar_pliku_mb, wyjście_konsolowe |
| 2 | Sieć | adres_grpc, port_grpc, max_połączeń, włącz_ssl, ścieżka_certyfikatu_ssl, ścieżka_klucza_ssl |
| 3 | CANopen | interfejs, id_węzła, prędkość_transmisji (100k/250k/500k/1M), włącz_sync, interwał_sync_ms |
| 4 | Lokalizacja Montażu | szerokość_geograficzna, długość_geograficzna, wysokość_n.p.m., wysokość_montażu |
| 5 | Montaż Ogólne | typ_montażu (RÓWNIKOWY/AZYMUTALNY/DORAŹNY/NIEZNANY), max_prędkość_slewu, max_prędkość_śledzenia, przyspieszenie_slewu, przyspieszenie_śledzenia |
| 6 | Montaż Środowiskowe | domyślna_temperatura, domyślne_ciśnienie, domyślna_wilgotność |
| 7 | Enkodery Montażu | używaj_enkoderów, enkodery_absolutne, konfiguracja_rozdzielczości_enkodera |
| 8 | Tolerancje Montażu | tolerancja_pozycji, tolerancja_szybkości |
| 9 | Przejście Południkowe | włącz, opóźnienie_minuty, histereza_stopnie, limit_czasu_sekundy |
| 10 | Ograniczenia Miękkie | włączone, min/max osi1, min/max osi2, stopnie_ostrzeżenia, stopnie_hamowania, współczynnik_szybkości_śledzenia |
| 11 | Pozycja Parkowania | oś1, oś2 |
| 12 | Korekcja Atmosferyczna | włącz_korekcję_refrakcji |
| 13 | Orientacja Montażu | kwaternion (qx, qy, qz, qw) |
| 14 | Parametry Fizyczne Osi HA | Silnik, Enkoder, Przekładnia, Błąd Cykliczny, Luz, Sztywność & Termika (6 podgrup) |
| 15 | Parametry Fizyczne Osi Dec | Silnik, Enkoder, Przekładnia, Błąd Cykliczny, Luz, Sztywność & Termika (6 podgrup) |
| 16 | Teleskop | ogniskowa, apertura, długość_tuby, model_kamery, rozmiar_piksela, szerokość_sensora, wysokość_sensora |
| 17 | Guider | włączony, ciąg_połączenia, max_korekcja, agresja, czas_ekspozycji_ms, binning |
| 18 | Filtr Kalmana | szum_procesu, szum_pomiaru, adaptacyjne_q, adaptacyjne_r, próg_innowacji, max_iteracji |
| 19 | Kalibracja TPOINT | maska_włączonych_wyrazów, min_pomiarów, max_residuum, auto_kalibruj |
| 20 | Derotator | typ, włączony, ciąg_połączenia, przełożenie, max_prędkość, max_przyspieszenie, luz, enkoder_absolutny, rozdzielczość_enkodera, offset_dojazdu |
| 21 | Rotacja Pola | włączona, szerokość_geograficzna, wysokość, azymut, obliczona_szybkość, zastosowana_korekcja, temperatura, korekcja_ugięcia |
| 22 | HAL | typ_interfejsu, interfejs_can, id_węzła_can, prędkość_can, interwał_heartbeat_ms, tryb_mapowania_pdo |
| 23 | HAL - Gamepad | ścieżka_urządzenia, strefa_neutralna, czułość, interwał_odpytywania_ms |

**Akcje globalne:**
- **Export Config** — Pobierz całą konfigurację jako plik JSON (`mount-config-YYYYMMDDTHHMMSS.json`)
- **Import Config** — Prześlij plik JSON konfiguracji, aby zastosować ustawienia
- **Reset All to Defaults** — Zresetuj całą konfigurację do ustawień domyślnych (ukryty domyślnie, pojawia się po kliknięciu w karcie)

### Zakładka Kalibracja

Dwuetapowy przepływ kalibracji z wyszukiwaniem obiektów referencyjnych z bazy danych.

#### Kalibracja Bootstrap (Wstępne Wyrównanie)
Używana do początkowego wyrównania. Stany: `NOT_CALIBRATED` → `MEASUREMENTS_COLLECTING` → `CALIBRATING` → `CALIBRATED` / `ERROR`

1. Wyszukaj gwiazdę/obiekt referencyjny w bazie danych
2. Wybierz go, aby wypełnić oczekiwane współrzędne
3. Kliknij **Slew and Measure**, aby wykonać slew i automatycznie dodać pomiar (po 2s opóźnieniu)
4. Lub kliknij **Add Measurement**, aby użyć bieżącej pozycji montażu
5. Powtórz dla ≥3 pomiarów rozmieszczonych na niebie
6. Kliknij **Run Bootstrap Calibration**, aby obliczyć wyrównanie
7. **Clear Measurements**, aby odrzucić wszystkie dane bootstrap

#### Kalibracja TPOINT (Precyzyjny Model Wskazywania)
Dostraja geometrię montażu za pomocą modelu 21-parametrowego. Wymaga, aby montaż był w stanie skalibrowanym.

1. Wyszukaj obiekt referencyjny w bazie danych
2. Wybierz i wykonaj slew do niego, a następnie dodaj pomiar
3. Powtórz dla ≥10 pomiarów rozmieszczonych na sferze niebieskiej
4. Kliknij **Run TPOINT Calibration**, aby obliczyć model wskazywania
5. Wyniki: RMS residuum, max residuum, chi-kwadrat, liczba pomiarów, znacznik czasu ostatniej aktualizacji

#### Szybka pomoc
Przełącz kartę pomocy kalibracji, aby wyświetlić szczegółowe instrukcje.

### Zakładka Baza Danych

Pełne zarządzanie bazą obiektów astronomicznych z backendem SQLite.

#### Przeglądanie i Wyszukiwanie
- **Przeglądaj**: Stronicowana lista wszystkich obiektów (domyślnie 20 na stronę)
- **Szukaj**: Filtruj według nazwy, typu, gwiazdozbioru, katalogów, zakresu jasności, tylko ulubione
- **Sortuj**: Sortuj według nazwy, RA, dec, jasności, typu, katalogu
- **Szczegóły obiektu**: Kliknij dowolny obiekt, aby wyświetlić pełne szczegóły — podstawowe informacje, współrzędne (J2000 RA/Dec, ruch własny, paralaksa, odległość), jasności (V/B/J/H/K), właściwości fizyczne (typ widmowy, jasność, temperatura, masa, promień), notatki

#### Tworzenie i Edycja
- **Utwórz obiekt**: Pełny formularz ze wszystkimi polami zgodnymi ze schematem protobuf
- **Edytuj obiekt**: Wstępne wypełnienie formularza edycji z istniejących danych obiektu
- **Usuń obiekt**: Dialog potwierdzenia przed usunięciem

#### Ulubione
- Przełącz status ulubionego za pomocą przycisku gwiazdki na liście lub w widoku szczegółów
- Filtruj, aby pokazać tylko ulubione

#### Slew do obiektu
Z widoku szczegółów kliknij **Slew to Object**, aby wypełnić pola RA/Dec w zakładce Sterowanie i przełączyć zakładki.

#### Statystyki bazy danych
Zwijany panel pokazujący: całkowitą liczbę obiektów, ulubione, obiekty dodane przez użytkownika, średnią jasność, podział według typu (top 8) i katalogu (top 5), znacznik czasu ostatniej aktualizacji.

#### Import katalogów
Trzy metody importu:

- **Import pliku**: Wybierz lokalny plik, wybierz format (CSV/JSON) i nazwę katalogu, opcjonalnie nadpisz istniejące
- **Import URL**: Pobierz dane katalogu z adresu URL
- **Import presetów**: Import jednym kliknięciem wbudowanych katalogów:
  - `messier` — 110 obiektów Messiera
  - `ngc` — 7,840 obiektów NGC (z OpenNGC)
  - `ic` — 5,386 obiektów IC (z OpenNGC)
  - `caldwell` — 109 obiektów Caldwella
  - `hyg` — ~120,000 gwiazd HIPPARCOS/Tycho (HYG Database v3)
  - `bright_stars` — Jasne gwiazdy z HYG (jasność < 6.5)
  - `sao` — Gwiazdy z katalogu SAO

Wyniki importu: liczba zaimportowanych, pominiętych, zaktualizowanych obiektów, czas importu oraz ewentualne błędy (zwijane).

### Zakładka Śledzenie

Śledzenie efemeryd dla obiektów ruchomych (satelity, komety, asteroidy, planety).

#### Stany śledzenia
`IDLE` → `SLEWING_TO_START` → `WAITING_AT_START` → `TRACKING` → `PREDICTING` → `ENDED` → `ERROR`

Każdy stan jest wyświetlany z kolorowym znacznikiem.

#### Rozpoczęcie śledzenia
Trzy metody:

- **By Object ID**: Wpisz identyfikator obiektu i kliknij **Start Tracking**
- **With Data**: Wklej dane efemeryd JSON i rozpocznij śledzenie natychmiast
- **Upload Ephemeris**: Prześlij dane efemeryd JSON na serwer

#### Zatrzymanie śledzenia i czyszczenie cache
- **Stop Tracking** — Zakończ bieżącą sesję śledzenia
- **Clear Cache** — Wyczyść zapisane dane efemeryd na serwerze

#### Karta statusu
Wyświetla: Nazwa/ID obiektu, bieżące RA/Dec, docelowe RA/Dec, błąd pozycji (łuksek), szybkości RA/Dec (łuksek/s), pozostały czas (h/m/s), status korekcji rotacji Ziemi, komunikaty błędów/ostrzeżeń.

#### Karta metryk
Wyświetla: Nazwa obiektu, typ, całkowity czas śledzenia, średni/maksymalny błąd pozycji, średni błąd szybkości, liczba predykcji, dokładność predykcji, zastosowana rotacja Ziemi.

### Zakładka Logowanie

Przeglądarka logów w czasie rzeczywistym z dwoma panelami do monitorowania logów kontrolera montażu oraz aplikacji przeglądarkowej.

#### Logi kontrolera (strumień SSE)

Wyświetla wpisy logów przesyłane strumieniowo w czasie rzeczywistym z procesu kontrolera montażu poprzez **Server-Sent Events (SSE)**.

**Funkcje:**
- **Strumieniowanie w czasie rzeczywistym** — Nowe wpisy pojawiają się automatycznie przez endpoint SSE `/api/logs/stream`
- **Ładowanie historii przy połączeniu** — Ostatnie 100 wpisów logów jest wysyłane natychmiast jako `event: init` przy pierwszym połączeniu
- **Filtr poziomu** — Filtrowanie według ALL, DEBUG, INFO, WARN, ERROR
- **Wyszukiwanie tekstu** — Dowolne wyszukiwanie w polach znacznika czasu i wiadomości
- **Auto-scroll** — Włącz/wyłącz automatyczne przewijanie do najnowszego wpisu
- **Przeładowanie** — Ręczne przeładowanie ostatnich 500 wpisów przez `GET /api/logs?lines=500`
- **Wyczyść** — Usuń wszystkie wyświetlane wpisy logów kontrolera
- **Licznik wpisów** — Pokazuje całkowitą liczbę załadowanych wpisów (maks. bufor 5000)

Każdy wpis logu wyświetla:
- `timestamp` — Znacznik czasu w formacie ISO z dokładnością do milisekund
- `level` — Poziom ważności kodowany kolorem (debug, info, warn, error)
- `message` — Treść komunikatu logu (escape'owany HTML)

#### Logi aplikacji

Przechwytuje całe wyjście `console.log/warn/error/info/debug` z przeglądarki do wewnętrznego bufora cyklicznego (maks. 1000 wpisów).

- **Wyczyść** — Odrzuć wszystkie przechowywane wpisy logów przeglądarki
- Wpisy wyświetlają czas (HH:MM:SS), poziom i treść komunikatu

---

## Referencja API HTTP/JSON

Wszystkie endpointy są udostępniane przez proxy pod adresem `http://<host>:<port>` (domyślnie `http://localhost:8080`).

### Status

#### `GET /status`
Pobiera bieżący stan kontrolera montażu.

**Odpowiedź:**
```json
{
  "status": "IDLE|SLEWING|TRACKING|PARKED|ERROR",
  "position": { "axis1": 0.0, "axis2": 0.0 },
  "encoders_enabled": true,
  "guider_active": false,
  "pier_side": "EAST|WEST",
  "meridian_flipped": false,
  "time_to_meridian": 2.5,
  "tracking_rate_ra": 15.041,
  "tracking_rate_dec": 0.0,
  "tracked_object": {
    "name": "M31",
    "ra": 0.842,
    "dec": 41.269,
    "tracking_error_ra": 0.5,
    "tracking_error_dec": 0.3
  },
  "tracking_performance": 98.5,
  "pointing_error": 1.2,
  "temperature": 15.0,
  "pressure": 1013.25,
  "humidity": 0.5,
  "error_message": ""
}
```

#### `GET /db/health`
Sprawdza łączność z bazą obiektów.

**Odpowiedź:** `{ "connected": true }` lub błąd 503 z `{ "error": "message" }`

### Sterowanie Montażem

#### `POST /slew`
Slew do współrzędnych równikowych.

**Żądanie:** `{ "ra": 18.6156, "dec": 38.7836 }`

#### `POST /axis/slew-horizontal`
Slew do współrzędnych horyzontalnych.

**Żądanie:** `{ "altitude": 45.0, "azimuth": 180.0 }`

#### `POST /stop`
Zatrzymuje cały ruch montażu.

#### `POST /park`
Parkuje montaż.

#### `POST /unpark`
Odparkowuje montaż.

#### `POST /errors/clear`
Czyści flagi błędów.

### Sterowanie Osią

#### `POST /axis/move`
Przesuwa oś z określoną prędkością.

**Żądanie:** `{ "axis_id": 0, "velocity": 1.5 }`

#### `POST /axis/stop`
Zatrzymuje określoną oś.

**Żądanie:** `{ "axis_id": 0 }`

#### `POST /axis/emergency-stop`
Natychmiastowe zatrzymanie awaryjne wszystkich osi.

**Żądanie:** `{ "axis_id": 0 }` lub pusty body

### Zarządzanie Stanem

#### `POST /state/save`
Zapisuje stan montażu do pliku po stronie serwera.

**Żądanie:** `{ "file_path": "data/mount_state.json" }`

**Odpowiedź:** `{ "success": true, "file_path": "...", "file_size": 1234 }`

#### `POST /state/load`
Ładuje stan montażu z pliku po stronie serwera.

**Żądanie:** `{ "file_path": "data/mount_state.json" }`

#### `POST /state/upload-and-load`
Przesyła plik stanu z przeglądarki i ładuje go natychmiast.

**Żądanie:** `{ "file_content": "{...json...}", "file_name": "mount_state.json" }`

Plik jest zapisywany w `data/uploads/` i ładowany przez gRPC, a następnie usuwany.

### Konfiguracja

#### `GET /config`
Pobiera pełną konfigurację.

**Odpowiedź:** Pełny obiekt konfiguracji zgodny ze strukturą `config/default.json`.

#### `PUT /config`
Aktualizuje konfigurację.

**Żądanie:** Częściowy lub pełny obiekt konfiguracji.

#### `POST /config/reset`
Resetuje całą konfigurację do ustawień domyślnych.

#### `POST /config/reset-group`
Resetuje pojedynczą grupę konfiguracyjną.

**Żądanie:** `{ "group": "logging" }`

#### `GET /config/addresses`
Pobiera bieżące adresy połączeń gRPC.

**Odpowiedź:**
```json
{
  "addresses": {
    "controller": { "host": "localhost", "port": 50051 },
    "database": { "host": "localhost", "port": 50052 }
  }
}
```

#### `POST /config/addresses`
Ustawia i ponownie łączy z nowymi adresami gRPC.

**Żądanie:**
```json
{
  "controller": { "host": "192.168.1.100", "port": 50051 },
  "database": { "host": "192.168.1.100", "port": 50052 }
}
```

### Kalibracja — Bootstrap

#### `GET /calibration/bootstrap`
Pobiera status kalibracji bootstrap.

**Odpowiedź:**
```json
{
  "state": "MEASUREMENTS_COLLECTING",
  "measurements_count": 3,
  "alignment_error": null,
  "message": ""
}
```

#### `POST /calibration/bootstrap/measurements`
Dodaje pomiar bootstrap.

**Żądanie:**
```json
{
  "object_id": "HIP12345",
  "object_name": "Vega",
  "expected": { "ra": 18.6156, "dec": 38.7836 }
}
```

#### `POST /calibration/bootstrap/run`
Uruchamia kalibrację bootstrap.

**Odpowiedź:**
```json
{
  "alignment_error": 0.5,
  "measurements_used": 5,
  "calibration_matrix": "..."
}
```

#### `POST /calibration/bootstrap/clear`
Czyści wszystkie pomiary bootstrap.

### Kalibracja — TPOINT

#### `GET /calibration/tpoint`
Pobiera parametry kalibracji TPOINT.

**Odpowiedź:**
```json
{
  "calibrated": true,
  "measurements_count": 15,
  "residual_rms": 1.2,
  "max_residual": 3.5,
  "chi_squared": 0.8,
  "last_update": "2026-05-31T12:00:00Z"
}
```

#### `POST /calibration/tpoint/measurements`
Dodaje pomiar TPOINT.

**Żądanie:** Taki sam format jak pomiar bootstrap.

#### `POST /calibration/tpoint/run`
Uruchamia kalibrację TPOINT.

**Odpowiedź:**
```json
{
  "residual_rms": 1.2,
  "measurements_used": 15,
  "parameters": { "IA": 0.5, "IE": -0.3, ... }
}
```

#### `POST /calibration/tpoint/clear`
Czyści wszystkie pomiary TPOINT.

### Śledzenie (Efemerydy)

#### `GET /tracking/status`
Pobiera status śledzenia efemeryd.

**Odpowiedź:**
```json
{
  "state": "TRACKING|IDLE|ERROR",
  "object_id": "SAT123",
  "current_ra": 10.5,
  "current_dec": 20.0,
  "target_ra": 10.52,
  "target_dec": 19.98,
  "position_error": 0.8,
  "ra_rate": 0.05,
  "dec_rate": -0.02,
  "time_remaining": 3600.0,
  "earth_rotation_corrected": true,
  "error_message": ""
}
```

#### `GET /tracking/metrics`
Pobiera metryki wydajności śledzenia.

**Odpowiedź:**
```json
{
  "object": "SAT123",
  "type": "SATELLITE",
  "total_track_time": 7200.0,
  "avg_position_error": 0.5,
  "max_position_error": 2.1,
  "avg_rate_error": 0.01,
  "predictions_count": 144,
  "prediction_accuracy": 98.2,
  "earth_rotation_applied": true
}
```

#### `POST /tracking/start`
Rozpoczyna śledzenie według ID obiektu.

**Żądanie:** `{ "object_id": "SAT123" }`

#### `POST /tracking/start-with-data`
Rozpoczyna śledzenie z danymi efemeryd inline.

**Żądanie:** Pełny obiekt żądania śledzenia z punktami trasy.

#### `POST /tracking/upload`
Przesyła dane efemeryd na serwer.

**Żądanie:** Surowe dane efemeryd JSON.

#### `POST /tracking/stop`
Zatrzymuje bieżącą sesję śledzenia.

#### `POST /tracking/clear-cache`
Czyści zapisane dane efemeryd na serwerze.

### Baza Obiektów — CRUD

#### `GET /db/objects`
Lista obiektów z paginacją.

**Parametry zapytania:** `page`, `page_size` (domyślnie 20)

**Odpowiedź:**
```json
{
  "objects": [...],
  "total_count": 1000,
  "page": 1,
  "page_size": 20,
  "total_pages": 50
}
```

#### `GET /db/objects/search`
Wyszukiwanie i filtrowanie obiektów.

**Parametry zapytania:** `query`, `type`, `magnitude_min`, `magnitude_max`, `sort_by`, `sort_desc`, `favorites_only`, `visible_only`, `catalogs`, `constellation`, `page`, `page_size`

#### `GET /db/objects/:id`
Pobiera pojedynczy obiekt według ID.

#### `POST /db/objects`
Tworzy nowy obiekt astronomiczny.

**Żądanie:** Pełne dane obiektu zgodne z wiadomością protobuf `AstronomicalObject`.

#### `PUT /db/objects/:id`
Aktualizuje istniejący obiekt.

**Żądanie:** Pełne lub częściowe dane obiektu.

#### `DELETE /db/objects/:id`
Usuwa obiekt.

### Baza Danych — Ulubione

#### `GET /db/favorites`
Pobiera wszystkie ulubione obiekty.

#### `POST /db/favorites`
Dodaje obiekt do ulubionych.

**Żądanie:** `{ "object_id": "some-id" }`

#### `DELETE /db/favorites/:objectId`
Usuwa obiekt z ulubionych.

### Baza Danych — Kategorie

#### `GET /db/categories`
Lista wszystkich kategorii.

#### `POST /db/categories`
Tworzy nową kategorię.

**Żądanie:** `{ "name": "My Catalog" }`

### Baza Danych — Narzędzia

#### `GET /db/stats`
Pobiera statystyki bazy danych (sumy, podziały, ostatnia aktualizacja).

#### `GET /db/tonight-best`
Pobiera najlepsze obiekty na bieżącą noc.

**Parametry zapytania:** `latitude`, `longitude`, `limit`, `min_altitude`, `max_magnitude`

### Baza Danych — Import/Eksport

#### `POST /db/import`
Importuje dane katalogu.

**Żądanie:**
```json
{
  "format": "csv|json",
  "data": "...surowy string danych...",
  "options": {
    "catalog_name": "MyCatalog",
    "overwrite": false
  }
}
```

#### `POST /db/import/url`
Importuje katalog z adresu URL.

**Żądanie:**
```json
{
  "url": "https://example.com/catalog.csv",
  "format": "csv",
  "options": {
    "catalog_name": "RemoteCatalog",
    "overwrite": false
  }
}
```

#### `GET /db/import/presets`
Pobiera listę dostępnych presetów katalogów.

**Odpowiedź:**
```json
[
  { "name": "messier", "label": "Messier Catalog", "size": 110, "type": "OpenNGC" },
  { "name": "ngc", "label": "NGC Catalog", "size": 7840, "type": "OpenNGC" },
  { "name": "ic", "label": "IC Catalog", "size": 5386, "type": "OpenNGC" },
  { "name": "caldwell", "label": "Caldwell Catalog", "size": 109, "type": "HYG" },
  { "name": "hyg", "label": "HYG Database v3", "size": 120000, "type": "HYG" },
  { "name": "bright_stars", "label": "Bright Stars", "size": 5000, "type": "HYG" },
  { "name": "sao", "label": "SAO Catalog", "size": 10000, "type": "HYG" }
]
```

#### `POST /db/import/preset/:name`
Importuje wbudowany preset katalogu.

**Żądanie:** `{ "options": { "overwrite": false } }`

---

## Motyw i Układ

### Nocny motyw czerwony (Night-Vision Red)
Przełącz nocny motyw czerwony za pomocą przycisku ikony księżyca w nagłówku. Preferencja jest przechowywana w `localStorage` pod kluczem `red-theme`. Po aktywacji wszystkie elementy interfejsu są renderowane w niskiej intensywności czerwonych tonach, odpowiednich dla widzenia przystosowanego do ciemności.

### Tryb mobilny
Przełącz układ mobilny za pomocą przycisku ikony telefonu. Po aktywacji interfejs przełącza się na układ jednokolumnowy dla małych ekranów. Preferencja jest przechowywana w `localStorage` pod kluczem `mobile-mode`.

### Powiadomienia Toast
Aplikacja wyświetla nieblokujące powiadomienia toast dla komunikatów sukcesu, błędu i informacji. Toasty automatycznie znikają po konfigurowalnym czasie (domyślnie 3 sekundy).

### Panel logów
Zwijany panel logów na dole strony wyświetla komunikaty logów po stronie klienta do debugowania.

---

## Rozwiązywanie problemów

| Objaw | Prawdopodobna przyczyna | Rozwiązanie |
|---------|-------------|-----------|
| Znacznik połączenia czerwony "Disconnected" | Serwer gRPC kontrolera montażu nie działa | Uruchom `astro-mount-controller` na porcie 50051 |
| Znacznik bazy "DB Off" | Serwer gRPC bazy obiektów nie działa | Uruchom `astro-mount-db` na porcie 50052 |
| Proxy uruchamia się, ale natychmiast kończy | Port 8080 już w użyciu | Zmień `PROXY_PORT` w `.env` |
| Błąd CORS w konsoli przeglądarki | `CORS_ORIGINS` nie zgadza się | Ustaw `CORS_ORIGINS=*` lub dopasuj swój origin |
| Błąd "Cannot find module" | Zależności nie zainstalowane | Uruchom `npm install` w `web/proxy/` |
| Polecenia Slew zwracają błędy | Montaż w stanie ERROR lub PARKED | Sprawdź zakładkę statusu, wyczyść błędy, odparkuj |
| Baza danych nie pokazuje obiektów | Nie zaimportowano żadnych katalogów | Użyj zakładki Baza Danych → Import → Preset, aby zaimportować katalog |
| Kalibracja nie powiodła się | Zbyt mało pomiarów | Dodaj więcej pomiarów rozmieszczonych na niebie |
| Śledzenie pokazuje "ENDED" natychmiast | Brak przesłanych danych efemeryd | Najpierw prześlij dane efemeryd |
| Zmiany w ustawieniach nie są stosowane | Grupa nie została zapisana | Kliknij **Save** w odpowiedniej grupie konfiguracyjnej |
| Strona nie znaleziona (404) | Zły URL | Upewnij się, że uzyskujesz dostęp do portu 8080, a nie 50051 |

---

## Rozwój

### Konwencje plików

- Wszystkie moduły JavaScript używają wzorca **IIFE (Immediately Invoked Function Expression)** z `'use strict'`
- Moduły udostępniają publiczne metody poprzez zwracany obiekt
- ID DOM są zgodne z konwencją: `btn-*` dla przycisków, `card-*` dla kart, `panel-*` dla paneli zakładek
- Wszystkie procedury obsługi zdarzeń używają `addEventListener`, bez inline'owych handlerów
- HTML escaping: tekst dostarczony przez użytkownika jest dezynfekowany przez `escapeHtml()` używając wzorca `textContent` → `innerHTML`

### Dodawanie nowego komponentu

1. Utwórz nowy plik w `web/public/js/components/`
2. Opakuj w IIFE zwracającą obiekt publicznego API z co najmniej metodą `init()` i opcjonalnie `startPolling()`/`stopPolling()`
3. Dodaj sekcje HTML komponentu do `web/public/index.html`
4. Zarejestruj zakładkę w `app.js` w metodzie `initTabs()`
5. Dodaj tag `<script>` do `index.html`
6. Dodaj nowe metody API do `api.js`

### Serwer proxy

Proxy (`web/proxy/server.js`) używa frameworka Express i dynamicznie ładuje definicje usług gRPC z katalogu `proto/` projektu. Obsługuje:

- Walidację żądań i transformację błędów
- Konwersję typów gRPC ↔ JSON (Timestampy, enumy, zagnieżdżone wiadomości)
- Wzbogacanie i filtrowanie danych importu katalogów
- Przesyłanie plików i tymczasowe przechowywanie stanu montażu
- Nagłówki CORS dla żądań cross-origin
- Serwowanie plików statycznych z nagłówkami no-cache dla SPA

---

## Zobacz także

- [Architektura systemu](../docs/pl/architecture.md) — Pełna dokumentacja architektury systemu
- [Referencja API gRPC](../docs/pl/api.md) — Pełna dokumentacja API gRPC
- [Przewodnik instalacji](../docs/pl/installation.md) — Budowanie i konfiguracja kontrolera montażu
- [Przewodnik dla dewelopera](../docs/pl/przewodnik_dla_dewelopera.md) — Przewodnik dla nowych deweloperów
