# Ocena ODrive w zastosowaniach astronomicznych

**Data analizy**: 2026-07-05
**Kontekst**: Integracja z projektem [`astro_mount_control`](.)

---

## 1. Czym jest ODrive?

ODrive to otwartoźródłowy (open-source hardware + firmware) kontroler serwonapędów BLDC (bezszczotkowych silników prądu stałego), rozwijany przez ODrive Robotics. Dostępny w kilku wersjach sprzętowych:

| Model | Napięcie | Prąd ciągły / szczytowy | Interfejsy | Cena orientacyjna |
|-------|----------|--------------------------|------------|-------------------|
| ODrive v3.6 (EOL) | 12–56V | 40A / 120A | USB, UART, CAN, Step/Dir | ~$150 |
| ODrive S1 | 12–56V | 40A / 120A | USB-C, CAN, Step/Dir, UART | ~$119 |
| ODrive Pro | 12–56V | 40A / 120A | USB, CAN FD, UART, SPI, Step/Dir, PWM | ~$199 |

---

## 2. Porównanie z obecną architekturą CANopen/CiA 402

Projekt [`astro_mount_control`](.) obecnie opiera się na napędach **CANopen/CiA 402** obsługiwanych przez [`CanOpenHAL`](src/hal/canopen_hal/canopen_hal.cpp). Poniżej zestawienie kluczowych różnic:

### 2.1 Architektura sterowania

| Cecha | CANopen/CiA 402 (obecny) | ODrive |
|-------|--------------------------|--------|
| **Protokół** | CANopen (SDO, PDO, NMT, SYNC, Heartbeat) | Niestandardowy protokół CAN (ASCII/binary) lub USB-Serial |
| **Warstwa aplikacyjna** | Standard CiA 402 — rozpoznawalny przez każde narzędzie CANopen | Własny zestaw komend — wymaga dedykowanego klienta |
| **Zgodność ze standardem** | ISO 11898 + CiA 301/402 | Brak — firmware własny |
| **Topologia** | Multi-drop CAN bus, wiele węzłów na jednej magistrali | CAN również multi-drop, ale bez NMT/heartbeat |
| **Konfiguracja** | Obiektowy słownik (Object Dictionary) dla każdego węzła | JSON przez USB lub komendy CAN |

### 2.2 Możliwości sterowania ruchem

| Cecha | CANopen/CiA 402 | ODrive |
|-------|-----------------|--------|
| **Tryby sterowania** | PP (Profile Position), PV (Profile Velocity), TQ (Torque), CSP, CSV, CST, HM (Homing) | Position (trapezoidal), Velocity, Torque, Trajectory Feedforward |
| **Generowanie trajektorii** | Zdefiniowane w CiA 402 — profile prędkości, przyspieszenia, jerk | Własny generator trajektorii z feedforward (przyspieszenie, prędkość, opóźnienie) |
| **Homing** | 35 predefiniowanych metod homingu (CiA 402) | Homing przez `AXIS_STATE_HOMING` + konfigurację `homing` w JSON |
| **Tryb śledzenia (tracking)** | CSP (Cyclic Synchronous Position) — idealny do śledzenia astronomicznego | Brak natywnego CSP — wejście Step/Dir lub aktualizacja pozycji przez CAN |
| **Synchronizacja wieloosiowa** | SYNC + PDO mapping — precyzyjna koordynacja wielu osi | Brak wbudowanej — wymaga zewnętrznego wyzwalania |
| **Interpolacja trajektorii** | IP (Interpolated Position Mode) — płynne przejścia między punktami | Feedforward trajectory — dobre, ale mniej precyzyjne |

### 2.3 Jakość sterowania (kluczowe dla astronomii)

| Cecha | CANopen/CiA 402 | ODrive |
|-------|-----------------|--------|
| **PID / cascaded control** | Zależne od producenta napędu — zazwyczaj 3-pętlowe PID (prąd/prędkość/pozycja) | 3-pętlowe kaskadowe PID (prąd → prędkość → pozycja) z anty-windup, gain scheduling |
| **Feedforward** | Zależne od producenta | Tak — acceleration + velocity feedforward |
| **Anti-cogging** | Zależne od producenta | Wbudowana kompensacja coggingu (liniowa/interpolowana) |
| **Notch filters** | Zależne od producenta | 4 notch filters + low-pass filter |
| **Observer (Luenberger)** | Zależne od producenta | Wbudowany observer prędkości |
| **Autotuning PID** | Zależne od producenta | Wbudowany `odrivetool` — automatyczne strojenie PID |
| **Rozdzielczość sterowania** | Zależne od enkodera (typowo 17-24 bit) | Zależne od enkodera (do 18-bit przez SPI, wyższa z zewnętrznym) |

---

## 3. Zalety ODrive dla montażu astronomicznego

### 3.1 ✅ Precyzja sterowania

- **Anti-cogging** — kluczowa cecha dla astrofotografii. Eliminuje okresowe błędy wynikające z magnetycznego zaczepiania (cogging torque) w silnikach BLDC. Przekłada się to bezpośrednio na mniejsze błędy okresowe (PE — Periodic Error).
- **Observer prędkości** — poprawia estymację prędkości przy niskich obrotach, co jest krytyczne przy śledzeniu syderycznym (~0.004°/s).
- **Notch filters** — pozwalają wytłumić rezonanse mechaniczne, które w montażach astronomicznych są szczególnie problematyczne przy dużych przełożeniach.

### 3.2 ✅ Otwarte oprogramowanie

- Pełny dostęp do firmware (MIT license) — możliwość modyfikacji pętli sterowania pod kątem specyficznych wymagań astronomicznych.
- Możliwość implementacji własnych algorytmów (np. kompensacja PEC — Periodic Error Correction bezpośrednio w firmware).
- Duża społeczność i aktywne wsparcie (GitHub, Discord).

### 3.3 ✅ Stosunek jakości do ceny

- $119–199 za kontroler zdolny sterować dwoma silnikami BLDC jednocześnie (ODrive obsługuje 2 osie na jednym boardzie).
- Porównywalne napędy CANopen/CiA 402 kosztują $300–1000+ za oś.
- Brak kosztów licencyjnych za CANopen stack.

### 3.4 ✅ Łatwość konfiguracji

- `odrivetool` — interaktywne narzędzie Python do konfiguracji, diagnostyki i tuningu.
- Autotuning PID — automatycznie dobiera wzmocnienia.
- Web GUI — graficzny interfejs przez przeglądarkę.

---

## 4. Wady ODrive dla montażu astronomicznego

### 4.1 ❌ Brak standardu CANopen

- **Brak Object Dictionary'ego** — konfiguracja nie jest zgodna z CiA 301/402, co uniemożliwia użycie standardowych narzędzi CANopen (np. CANopen Monitor, CANopen Magic).
- **Brak NMT (Network Management)** — nie ma automatycznego monitorowania stanu węzłów, heartbeatów, boot-upów. W obecnym projekcie [`CanOpenHAL`](src/hal/canopen_hal/canopen_hal.cpp) ma rozbudowaną warstwę NMT (detekcja awarii, auto-recovery).
- **Brak PDO (Process Data Objects)** — ODrive nie wspiera mapowania PDO, które w CANopen umożliwia synchroniczną wymianę danych z precyzyjnym timingiem.

### 4.2 ❌ Brak CSP (Cyclic Synchronous Position)

Tryb CSP jest idealny do śledzenia astronomicznego, ponieważ:
- Host wysyła pozycję docelową cyklicznie (np. co 10ms) zsynchronizowaną z SYNC.
- Napęd interpoluje między kolejnymi zadanymi pozycjami.
- Eliminuje to opóźnienia komunikacyjne i zapewnia płynny ruch.

ODrive nie ma natywnego CSP. Najbliższym odpowiednikiem jest wejście **Step/Dir**, które może symulować CSP z zewnętrznym generatorem impulsów, ale to dodaje złożoności.

### 4.3 ❌ Ograniczona rozdzielczość enkodera SPI

- Wbudowany interfejs enkodera SPI obsługuje do 18 bitów.
- Dla montażu z przełożeniem 360:1 daje to ~0.14" na krok enkodera — do przyjęcia, ale nie znakomite.
- CANopen napędy profesjonalne (Elmo, Maxon, Copley) obsługują enkodery 24+ bitowe przez BiSS/EnDat/SSI.

### 4.4 ❌ Brak wsparcia dla enkoderów absolutnych przez SSI/BiSS/EnDat

- ODrive natywnie wspiera enkodery inkrementalne (AB, ABZ), Hall sensors i SPI.
- Enkodery absolutne BiSS/EnDat/SSI — powszechne w profesjonalnych montażach — nie są wspierane.
- Wymagałyby zewnętrznego konwertera lub osobnego odczytu przez mikrokontroler.

### 4.5 ❌ Ograniczenia CAN

- ODrive CAN używa niestandardowego protokołu binarnego, nie CANopen.
- CAN na ODrive v3.6/S1 to CAN 2.0B (8 bajtów na ramkę, 1 Mbps).
- CAN FD na ODrive Pro oferuje 64 bajty na ramkę, ale nadal bez CANopen.
- Brak wsparcia dla CANopen over EtherCAT (CoE), który jest coraz popularniejszy w profesjonalnych montażach.

### 4.6 ❌ Ryzyko EMC/RFI

Silniki BLDC z driverami PWM (ODrive używa ~40kHz PWM) generują zakłócenia elektromagnetyczne. W astrofotografii długie ekspozycje są wrażliwe na RFI — szczególnie przy kamerach CCD/CMOS. Profesjonalne napędy CANopen często mają certyfikację EMC (CE/FCC Class B). ODrive — jako open-source hardware — nie przechodzi certyfikacji.

---

## 5. Analiza integracji z projektem astro_mount_control

### 5.1 Ścieżki integracji

Istnieją trzy realne ścieżki integracji ODrive z obecną architekturą HAL:

#### Ścieżka A: Nowa implementacja HAL (`OdriveHAL`)

```
HALInterface → OdriveHAL → ODrive CAN protocol
```

**Wymagane prace:**
- Nowa klasa [`OdriveHAL`](src/hal/ethernet_hal/ethernet_hal.h) analogiczna do [`CanOpenHAL`](src/hal/canopen_hal/canopen_hal.h)
- Implementacja `MotorControl` przez protokół CAN ODrive
- Mapowanie `MotorConfig` na parametry ODrive (PID, limity, tryby)
- Rozszerzenie `HALType` o `ODRIVE`
- Rozszerzenie `HALFactory` o tworzenie `OdriveHAL`
- Szacowany nakład: **~800-1200 LOC**

#### Ścieżka B: ODrive jako CANopen slave z zewnętrznym mostkiem

```
HALInterface → CanOpenHAL → CANopen → CAN-to-ODrive bridge (RPi/ESP32)
```

**Wymagane prace:**
- Zewnętrzny mikrokontroler tłumaczący CANopen ↔ ODrive CAN
- Implementacja Object Dictionary (CiA 402 profile) na mikrokontrolerze
- Szacowany nakład: **~2000-3000 LOC + dodatkowy hardware**

#### Ścieżka C: ODrive przez USB/UART jako `SerialHAL`

```
HALInterface → SerialHAL → USB/UART → ODrive JSON protocol
```

**Wymagane prace:**
- Rozszerzenie [`SerialHAL`](src/hal/serial_hal/serial_hal.h) o protokół ODrive JSON
- Szacowany nakład: **~500-800 LOC**

### 5.2 Rekomendowana ścieżka: A (OdriveHAL)

Najczystsza architektonicznie — ODrive dostaje własną implementację HAL, bez udawania że jest CANopen. Pozwala to wykorzystać specyficzne funkcje ODrive (anti-cogging, observer) bez ograniczeń standardu CiA 402.

### 5.3 Wymagane rozszerzenia w `MotorType`

Obecny [`MotorType`](include/hal/motor_control.h:9) nie zawiera dedykowanego typu dla ODrive. Należałoby dodać:

```cpp
enum class MotorType {
    // ... istniejące ...
    ODRIVE_BLDC,    // Silnik BLDC sterowany przez ODrive
};
```

### 5.4 Wymagane rozszerzenia w `HALConfig`

```cpp
struct HALConfig {
    // ... istniejące ...
    struct {
        std::string connection_type{"can"};  // "can", "usb", "uart"
        std::string can_interface{"can0"};
        uint32_t can_bitrate{500000};        // ODrive domyślnie 250kbps
        uint8_t axis0_node_id{0};            // CAN node ID osi 0
        uint8_t axis1_node_id{1};            // CAN node ID osi 1
        std::string usb_serial_path;         // np. /dev/ttyACM0
        bool enable_anticogging{true};
        bool enable_observer{true};
        uint32_t control_loop_hz{1000};      // Częstotliwość pętli sterowania (domyślnie 8kHz w ODrive)
    } odrive;
};
```

---

## 6. Porównanie z alternatywami

| Kryterium | ODrive S1/Pro | CANopen Servo (Elmo/Maxon) | StepStick (TMC) | SimpleFOC |
|-----------|:---:|:---:|:---:|:---:|
| Cena za oś | $60–100 | $300–1500 | $10–30 | $20–50 |
| Protokół | Własny CAN/USB | CANopen CiA 402 | Step/Dir | Własny / CAN |
| CSP / PV | ❌ | ✅ | ❌ (Step/Dir) | ❌ |
| Anti-cogging | ✅ | ✅ (zależne) | ❌ | ❌ |
| Autotuning | ✅ | ✅ (zależne) | ❌ | ❌ |
| Enkodery absolutne | ❌ | ✅ (BiSS/EnDat/SSI) | ❌ | ✅ (SPI/I2C) |
| Wieloosiowość | 2 osie/board | 1 oś/napęd | 1 oś/board | 1 oś/MCU |
| Synchronizacja międzyosiowa | ❌ | ✅ (SYNC) | ❌ | ❌ |
| Otwarte oprogramowanie | ✅ | ❌ | ✅ | ✅ |
| Dojrzałość | Średnia | Wysoka | Wysoka | Niska |

---

## 7. Wnioski i rekomendacje

### 7.1 Dla jakich zastosowań ODrive się nadaje?

| Zastosowanie | Ocena | Uwagi |
|-------------|:-----:|-------|
| **Montaż amatorski / DIY** | ⭐⭐⭐⭐⭐ | Idealny stosunek jakości do ceny; anti-cogging kluczowy |
| **Montaż semi-profesjonalny** | ⭐⭐⭐⭐ | Dobry, ale brak BiSS/EnDat ogranicza precyzję absolutną |
| **Profesjonalny montaż obserwacyjny** | ⭐⭐⭐ | Brak CANopen/CiA 402 utrudnia integrację z istniejącą infrastrukturą |
| **Derotator pola** | ⭐⭐⭐⭐ | Jedna oś, niska moc — ODrive dobrze się sprawdza |
| **Focuser / filter wheel** | ⭐⭐⭐⭐⭐ | Proste zastosowanie, ODrive to overkill, ale działa |
| **Montaż z enkoderami absolutnymi** | ⭐⭐ | Ograniczone wsparcie enkoderów |
| **System wieloosiowy (>3 osie)** | ⭐⭐ | Brak NMT, heartbeat utrudnia zarządzanie wieloma węzłami |

### 7.2 Rekomendacja dla projektu astro_mount_control

**ODrive jest wartościowym dodatkiem** do obsługiwanych typów HAL, ale **nie powinien zastępować CANopen/CiA 402** jako głównej ścieżki sterowania. Rekomenduję:

1. **Dodać typ HAL `ODRIVE`** do [`HALType`](include/hal/hal_config.h:14) i odpowiadającą implementację `OdriveHAL`.
2. **Zachować CANopen jako primary target** dla profesjonalnych instalacji.
3. **Użyć ODrive jako opcji ekonomicznej** dla amatorów i prototypowania.
4. **Rozważyć ODrive dla derotatora** — osobna oś, nie wymaga CSP, anti-cogging jest atutem.

### 7.3 Ryzyka

- **Vendor lock-in**: ODrive jest produktem jednej firmy; CANopen/CiA 402 to standard obsługiwany przez dziesiątki producentów.
- **EMC/RFI**: Przetestować w warunkach astrofotograficznych przed wdrożeniem produkcyjnym.
- **Dojrzałość firmware**: ODrive firmware przechodzi aktywne zmiany; API może być niestabilne między wersjami.

---

## 8. Referencje

- [ODrive Robotics — oficjalna strona](https://odriverobotics.com/)
- [ODrive Firmware — GitHub](https://github.com/odriverobotics/ODrive)
- [ODrive CAN Protocol](https://docs.odriverobotics.com/v/latest/manual/can-protocol.html)
- [CiA 402 — CAN in Automation](https://www.can-cia.org/can-knowledge/canopen/cia402/)
- [Projekt astro_mount_control — dokumentacja CANopen](docs/pl/hal_layer.md)
