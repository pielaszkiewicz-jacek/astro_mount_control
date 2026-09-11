# Instrukcja kalibracji regulatorów PID

Niniejszy dokument opisuje, jak przeprowadzić automatyczną kalibrację regulatorów
PID silników osi montażu z poziomu interfejsu WEB.

Kalibracja polega na tym, że dla każdej kombinacji współczynników sterownik
zadaje prędkość obrotową, mierzy rzeczywistą prędkość przez zadany czas i
wyznacza:

- **średnią arytmetyczną** zmierzonej prędkości,
- **odchylenie standardowe** zmierzonej prędkości.

Jako optymalny zestaw wybierana jest kombinacja, dla której średnia prędkość jest
**najbliższa prędkości zadanej**, a odchylenie standardowe jest **najmniejsze**
(minimalizowana jest wartość `|średnia − zadana| + odchylenie`).

Nastawy PID są zapisywane wyłącznie do **pamięci RAM** napędu — po wyłączeniu
zasilania wracają wartości domyślne. Kalibracja nie zapisuje niczego do ROM.

---

## 1. Wymagane usługi

Do przeprowadzenia kalibracji muszą być uruchomione dwa procesy:

| Usługa | Polecenie | Port | Rola |
|--------|-----------|------|------|
| Kontroler montażu (C++) | `./build/bin/astro_mount_controller config/default.json` | gRPC `50051` | Hostuje `PidCalibrationService` in-process oraz wykonuje zapisy do napędu |
| Web proxy (Node.js) | `cd web/proxy && npm start` | HTTP `8080` | Most HTTP/JSON → gRPC, serwuje interfejs WEB |

Usługa bazy obiektów (`astro_object_database_server`) **nie jest wymagana** do
kalibracji PID.

### 1.1 Kontroler montażu

```bash
cd /home/jacek/development/astro_mount_control
./build/bin/astro_mount_controller config/default.json
```

Jeśli sterownik ma pracować z napędami MF7025v2 (LingKong), plik konfiguracyjny
musi mieć typ HAL `MF7025V2` oraz poprawnie skonfigurowany interfejs CAN:

- [`config/mf7025v2.json`](../config/mf7025v2.json) — przykładowa konfiguracja MF7025v2,
- [`docs/pl/wsparcie_dla_mf7025v2.md`](wsparcie_dla_mf7025v2.md) — szczegóły HAL.

Przed startem kontrolera interfejs CAN musi być włączony, np.:

```bash
sudo ip link set can0 up type can bitrate 1000000
```

### 1.2 Web proxy

```bash
cd web/proxy
npm install        # tylko przy pierwszym uruchomieniu
npm start
```

Po starcie proxy interfejs WEB jest dostępny pod adresem
`http://localhost:8080`.

---

## 2. Krok po kroku — kalibracja w WEB UI

1. Otwórz interfejs: `http://localhost:8080`.
2. Sprawdź w prawym górnym rogu, że wskaźnik połączenia pokazuje **Connected**.
3. Przejdź do zakładki **PID**.
4. W sekcji **Calibration Parameters** ustaw:

   | Pole | Opis |
   |------|------|
   | **Axis** | Oś do kalibracji (`Axis 1` / `Axis 2`) |
   | **Loop** | Pętla do kalibracji: `Speed` (prędkości), `Position` (pozycji), `Current` (prądowa) |
   | **Coefficient** | Który współczynnik stroić: `Kp`, `Ki`, `Kd` lub `All (Kp + Ki + Kd)` |
   | **Min/Max speed (°/s)** | Zakres prędkości zadawanych podczas kalibracji |
   | **Speed step (°/s)** | Krok zwiększania prędkości między kolejnymi punktami |
   | **Kp/Ki/Kd min / max / step** | Zakres i krok przeszukiwania każdego współczynnika |
   | **Kp/Ki/Kd base** | Wartości stałe dla współczynników, które **nie** są strojone (ignorowane w trybie `All`) |
   | **Measurement time (s)** | Czas pomiaru prędkości dla jednej kombinacji |
   | **Settle time (s)** | Czas oczekiwania na ustabilizowanie prędkości przed pomiarem |

5. Kliknij **▶ Start**.
6. Obserwuj postęp w polach **State**, **Progress**, **Tested** oraz **Current Speed**.
7. W każdej chwili możesz przerwać sesję przyciskiem **⏹ Stop**.
8. Po zakończeniu w sekcji **Results** pojawią się:
   - **Overall winner** — najlepsza kombinacja całej sesji,
   - tabela wszystkich przetestowanych kombinacji (prędkość zadana, Kp/Ki/Kd, średnia, odchylenie, score, liczba próbek).
9. Aby zapisać wyniki do pliku JSON, uzupełnij opcjonalnie **Save path** (domyślnie
   `config/pid_calibration_results.json`) i kliknij **💾 Save**.

---

## 3. Tryby kalibracji

### 3.1 Pojedynczy współczynnik (Kp / Ki / Kd)

Strojony jest tylko wybrany współczynnik w podanym zakresie i kroku. Pozostałe dwa
współczynniki przyjmują wartości z pól **base**. Jest to najszybszy tryb i
pozwala korygować jeden parametr bez zmiany pozostałych.

### 3.2 Wszystkie współczynniki (`All`)

Przeszukiwana jest pełna kombinacja `Kp × Ki × Kd` (iloczyn kartezjański trzech
zakresów). Tryb znajduje najlepszą trójkę nastaw za jednym razem, ale liczba
kombinacji rośnie wykładniczo:

```
liczba kombinacji = liczba_prędkości × liczba_Kp × liczba_Ki × liczba_Kd
```

Przykład: 3 prędkości × 10 Kp × 10 Ki × 5 Kd = 1500 pomiarów. Przy czasie pomiaru
1 s i osiadaniu 0.3 s daje to ok. 32 minuty. Zaczynaj od rzadkich kroków i
zawężaj zakresy w kolejnych iteracjach.

---

## 4. Ważne uwagi

- **Bezpieczeństwo:** upewnij się, że oś ma przestrzeń na ruch podczas kalibracji
  (szczególnie dla pętli pozycyjnej, która wykonuje ruch względny). Nie uruchamiaj
  kalibracji, gdy montaż jest zaparkowany przy mechanicznym ograniczniku.
- **Zapis RAM:** nastawy są ulotne. Po akceptacji wyników przenieś je na stałe do
  konfiguracji lub zapisz do ROM napędu odpowiednią komendą (kalibracja tego nie
  robi automatycznie).
- **Pętla prądowa:** zaleca się ostrożność przy zmianie nastaw pętli prądowej —
  zbyt agresywne wartości mogą powodować przegrzanie silnika.
- **Kd nie jest zapisywane:** napędy MF7025v2 przyjmują nastawy PID do RAM przez
  komendę `0x31` (kombinowany zapis trzech pętli), która przenosi tylko `Kp` i
  `Ki` (wartości 8-bitowe `0..255`). Człon `Kd` nie jest obsługiwany — kalibracja
  `Kd` nie zmieni nastaw napędu (w logu pojawi się ostrzeżenie).
- **Zakresy Kp/Ki:** dla napędów MF7025v2 (komenda `0x31`) wartości są 8-bitowe w
  zakresie `0..255`; wartości spoza zakresu są przycinane przez HAL.
