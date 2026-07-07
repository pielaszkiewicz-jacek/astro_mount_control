# Sterowanie osiami przez kontrolery z drukarek 3D

## Analiza integracji bez zmian w kodzie

**Data:** 2026-07-05  
**Cel:** Oceń możliwość wykorzystania płyt głównych/kontrolerów z drukarek 3D (RAMPS, SKR, MKS, Duet) jako zamiennika dla serwonapędów CANopen — bez modyfikacji kodu `astro_mount_control`.

---

## 1. Popularne platformy 3D-printing

| Platforma | MCU | Step/Dir | Enkoder | Komunikacja | Cena |
|-----------|-----|----------|---------|-------------|------|
| **RAMPS 1.4** | ATmega2560 (Arduino Mega) | 5 osi | ❌ | USB-serial | ~50 PLN |
| **MKS Gen-L v2** | ATmega2560 | 5 osi | ❌ | USB-serial | ~80 PLN |
| **SKR Mini E3 v3** | STM32F103 | 4 osi | ❌ | USB-serial, CAN (opcja) | ~120 PLN |
| **SKR Pro v1.2** | STM32F407 | 6 osi | ❌ | USB, CAN, WiFi | ~180 PLN |
| **Duet 3 Mini** | STM32H743 | 5 osi | ❌ | Ethernet, USB, CAN-FD | ~450 PLN |
| **BTT Octopus** | STM32F429 | 8 osi | ❌ | USB, CAN, WiFi | ~250 PLN |
| **FYSETC Spider** | STM32F446 | 8 osi | ❌ | USB, CAN, WiFi | ~200 PLN |

Wszystkie te płyty mają **Step/Direction + Enable** jako natywne wyjścia. Żadna nie ma natywnego CANopen (poza CAN-FD w Duet 3, który wymaga własnego firmware).

---

## 2. Firmware dla astronomii na platformach 3D-printing

### 2.1 OnStep (najlepszy wybór)

**OnStep** to otwarty firmware astronomiczny zaprojektowany specjalnie dla płytek z drukarek 3D.

| Cecha | Wartość |
|-------|--------|
| Sprzęt | RAMPS, MKS Gen-L, STM32, ESP32, Teensy |
| Protokół | **LX200** przez USB-serial (natywny!) |
| Osie | 2 (RA/Dec) + rotator pola |
| Tryby śledzenia | Syderyczny, solarny, lunarny, custom |
| Microstepping | Do 256× (TMC2130/5160) |
| Autoguiding | ST-4, pulse guiding |
| ASCOM/INDI | ✅ Pełne wsparcie |

**Integracja z astro_mount_control:** OnStep mówi natywnie protokołem **LX200 przez port szeregowy**. Oznacza to, że można go podłączyć bezpośrednio przez USB-serial i sterować komendami LX200.

### 2.2 Marlin (drukarki 3D)

Marlin ma tryb "SCARA" i "coreXY", które można by zaadaptować, ale nie ma natywnego wsparcia dla astronomii. Komunikacja przez G-code (`G0`, `G1`). **Niepraktyczne bez ciężkich modyfikacji firmware.**

### 2.3 Klipper

Klipper działa na modelu host (RPi) + MCU (płytka 3D). Można napisać własny moduł kinematyki. Komunikacja przez API Klippera. **Wymaga dedykowanego RPi i własnego kodu Python.**

### 2.4 GRBL

GRBL jest dla CNC (3 osie liniowe), nie dla astronomii. **Niepraktyczne.**

---

## 3. Ścieżki integracji z astro_mount_control

### 3.1 Ścieżka A: OnStep jako "LX200 device" — przez port szeregowy

```
astro_mount_control (Linux)
  └── ICanOpenInterface → LX200Interface (nowy, RS-485/LX200)
        └── /dev/ttyUSB0 → OnStep na MKS Gen-L
              └── Step/Dir → TMC5160 → silnik krokowy RA
              └── Step/Dir → TMC5160 → silnik krokowy Dec
              └── Enkoder (opcjonalnie)
```

**Wymagane zmiany w kodzie:** LX200Interface (już zaprojektowany w [`docs/pl/rs485_lx200_integracja.md`](docs/pl/rs485_lx200_integracja.md)). **Minimalne** — tylko nowa implementacja `ICanOpenInterface`.

OnStep przez port szeregowy rozumie natywnie LX200:
```
:Sr 12:34:56#  → ustaw target RA
:Sd +45:12:34# → ustaw target Dec
:MS#           → wykonaj slew
:GR#           → odczytaj RA
:GD#           → odczytaj Dec
```

### 3.2 Ścieżka B: OnStep jako ASCOM/INDI device — przez sieć

```
astro_mount_control → INDI/ASCOM → OnStep (przez Ethernet/WiFi)
```

OnStep ma własny serwer INDI i sterownik ASCOM. Można by zintegrować na poziomie INDI zamiast CANopen. **Wymaga zmian w architekturze** — przejście z `ICanOpenInterface` na sterowanie przez INDI.

### 3.3 Ścieżka C: Bezpośrednie Step/Dir przez GPIO Linux

```
astro_mount_control (Linux, PREEMPT_RT)
  └── ICanOpenInterface → StepDirInterface (nowy)
        └── GPIO → Step/Dir → TMC5160 → silnik krokowy
```

Linux z jądrem RT może generować impulsy Step/Dir przez GPIO z precyzją ~100 kHz. **Wymaga jądra PREEMPT_RT** i nowej implementacji `ICanOpenInterface`.

### 3.4 Ścieżka D: CANopen przez płytkę CAN + firmware

Niektóre płytki (SKR Pro, Duet 3) mają CAN-FD. Można by napisać firmware emulujące CANopen slave na płytce, ale to **ogromna praca** i niepraktyczne.

---

## 4. Porównanie ścieżek

| Ścieżka | Zmiany w kodzie | Sprzęt | Realność |
|---------|----------------|--------|----------|
| **A: OnStep + LX200Interface** | Małe (tylko LX200Interface) | OnStep na MKS/RAMPS + TMC5160 | ✅ **Najlepsza** |
| B: OnStep + INDI bridge | Duże (zmiana architektury) | Jak A | ⚠️ Utrata kontroli low-level |
| C: Step/Dir przez GPIO RT | Średnie (StepDirInterface) | Bezpośrednie GPIO + TMC5160 | ⚠️ Wymaga PREEMPT_RT |
| D: CANopen na płytce 3D | Ogromne (firmware) | SKR Pro / Duet 3 | ❌ Niepraktyczne |

---

## 5. Rekomendowana konfiguracja: OnStep + LX200Interface

### 5.1 Sprzęt (~300 PLN za 2 osie)

| Komponent | Model | Cena |
|-----------|-------|------|
| Płyta główna | MKS Gen-L v2 | ~80 PLN |
| Sterowniki silników | TMC5160 (×2) | ~120 PLN |
| Silniki krokowe | NEMA 17 0.9° 1.7A (×2) | ~100 PLN |
| Zasilacz | 24V 5A | ~40 PLN |

### 5.2 Firmware

- **OnStep** wgrany na MKS Gen-L przez USB
- Konfiguracja: `Config.h` — ustawienie przełożenia, mikrokroków, limitów
- Komunikacja: LX200 przez `/dev/ttyUSB0` @ 115200 bps

### 5.3 Integracja z astro_mount_control

```json
{
    "lx200": {
        "enabled": true,
        "port": "/dev/ttyUSB0",
        "baud_rate": 115200,
        "timeout_ms": 500,
        "axis_addrs": [0, 1]
    },
    "mount": {
        "type": "equatorial",
        "canopen_interface": "lx200"
    }
}
```

### 5.4 Przepływ komend (przykład śledzenia)

```
astro_mount_control                          OnStep (MKS Gen-L)
       │                                           │
       │ :A0Sr 02:34:56.7#                         │  (set RA target)
       │ ────────────────────────────────────────► │
       │                             1#            │  (OK)
       │ ◄──────────────────────────────────────── │
       │                                           │
       │ :A1Sd +45:12:34.5#                        │  (set Dec target)
       │ ────────────────────────────────────────► │
       │                             1#            │
       │ ◄──────────────────────────────────────── │
       │                                           │
       │ :A0MS#                                    │  (slew RA)
       │ ────────────────────────────────────────► │  → TMC5160 generuje impulsy Step/Dir
       │                             0#            │  (moving...)
       │ ◄──────────────────────────────────────── │
       │                             1#            │  (target reached)
       │ ◄──────────────────────────────────────── │
```

---

## 6. Ograniczenia

### 6.1 Brak sprzężenia zwrotnego (closed-loop)
Płytki 3D bez enkoderów nie mają sprzężenia zwrotnego. Sterownik **nie wie**, czy silnik rzeczywiście wykonał ruch. Przy przeciążeniu silnik gubi kroki — pozycja się rozjeżdża.

**Rozwiązanie:** TMC5160 ma wbudowane wykrywanie przeciążeń (StallGuard). Można też dodać enkoder przez SPI.

### 6.2 Prędkość
Step/Dir ma ograniczenie częstotliwości. Przy 256× mikrokroku i 200 krokach/obrót, 1 obrót = 51200 impulsów. Przy 100 kHz daje to ~2 obr/s silnika. Z przełożeniem 360:1: 2/360 = 0.0056 obr/s osi = 2°/s telescope. **Wystarczające dla śledzenia, za wolne dla szybkiego slewu.**

**Rozwiązanie:** Użyć niższego mikrokroku dla slewu (np. 16×) lub przełączać microstepping dynamicznie.

### 6.3 Brak CANopen
Płytki 3D nie mają natywnego CANopen. LX200Interface tłumaczy komendy na LX200 — tracimy zaawansowane funkcje CiA 402 (PDO, SYNC, heartbeat). W praktyce dla astronomii amatorskiej **LX200 jest wystarczający**.

### 6.4 Jakość sygnału step/dir
Długie kable Step/Dir są podatne na zakłócenia EMI. **Rekomendacja:** trzymać płytkę blisko silników (<30cm) i komunikować się z nią przez USB/LX200 (kabel do 5m) lub RS-485 (do 1200m).

---

## 7. Wnioski

**Najlepsza ścieżka bez zmian w istniejącym kodzie `MountController`:** OnStep + LX200Interface.

1. OnStep działa na płytkach z drukarek 3D (MKS Gen-L, RAMPS, SKR) — sprzęt za ~200-300 PLN
2. OnStep natywnie mówi LX200 przez port szeregowy
3. LX200Interface (już zaprojektowany) implementuje `ICanOpenInterface` — zero zmian w `MountController`
4. Całość wymaga tylko: nowego pliku `lx200_interface.cpp` + konfiguracji JSON

**Koszt całkowity (sprzęt + kod):**
- Sprzęt: ~300 PLN (MKS Gen-L + 2× TMC5160 + 2× NEMA17 + zasilacz)
- Kod: ~4h implementacji LX200Interface (już zaprojektowany w raporcie)
- **Zero zmian** w istniejącym `MountController`, `CanOpenFactory`, logice śledzenia
