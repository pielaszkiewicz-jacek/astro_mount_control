# LingKong — Ściąga Komend CAN (CAN ID + DATA)

> Źródło: `/home/jacek/Pobrane/20240814163302f.txt` — referencyjne przykłady ramek CAN dla protokołu LingKong V2.36.  
> CAN ID = `0x140 + node_id`. Poniższe przykłady używają node ID = 1 (ID `0x141`), chyba że zaznaczono inaczej.

---

## Zasilanie silnika

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `80 00 00 00 00 00 00 00` | 0x80 | Wyłączenie silnika (Motor Off) |
| 141 | `81 00 00 00 00 00 00 00` | 0x81 | Zatrzymanie silnika (Motor Stop) |
| 141 | `88 00 00 00 00 00 00 00` | 0x88 | Uruchomienie silnika (Motor Run) |

---

## Sterowanie ruchem

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `A0 00 00 00 C8 00 00 00` | 0xA0 | Pętla otwarta (tylko seria MS). powerControl = 0x00C8 = 200 |
| 141 | `A1 00 00 00 64 00 00 00` | 0xA1 | Pętla zamknięta momentu (tylko MF, MG). iqControl = 0x0064 = 100 |
| 141 | `A2 00 00 00 A0 8C 00 00` | 0xA2 | Pętla zamknięta prędkości. speedControl = 0x00008CA0 = 36000 (360,00 dps) |
| 141 | `A3 00 00 00 A0 8C 00 00` | 0xA3 | Pozycja wieloobrotowa 1. angleControl = 0x00008CA0 = 36000 (360,00°) |
| 141 | `A4 00 D0 02 A0 8C 00 00` | 0xA4 | Pozycja wieloobrotowa 2. maxSpeed = 0x02D0 = 720 dps, angleControl = 36000 |
| 141 | `A4 00 78 00 50 46 00 00` | 0xA4 | Pozycja wieloobrotowa 2. prędkość 120 dps, kąt +180° (0x4650 = 18000) |
| 154 | `A4 00 78 00 B0 B9 FF FF` | 0xA4 | Pozycja wieloobrotowa 2, ID#20. prędkość 120 dps, kąt -180° (0xFFFFB9B0 = -18000) |
| 141 | `A5 00 00 00 50 46 00 00` | 0xA5 | Pozycja jednoobrotowa 1. spinDirection=0x00 (CW), angleControl=18000 |
| 141 | `A6 00 D0 02 50 46 00 00` | 0xA6 | Pozycja jednoobrotowa 2. spinDirection=0x00, maxSpeed=720 dps, angleControl=18000 |
| 141 | `A7 00 D0 02 94 11 00 00` | 0xA7 | Przyrost pozycji 1. angleIncrement = 0x00001194 = 4500 (45,00°) |
| 141 | `A8 00 D0 02 B8 0B 00 00` | 0xA8 | Przyrost pozycji 2. prędkość 720 dps, kąt +30° (0x0BB8 = 3000) |
| 141 | `A8 00 D0 02 48 F4 FF FF` | 0xA8 | Przyrost pozycji 2. prędkość 720 dps, kąt -30° (0xFFFFF448 = -3000) |

---

## Parametry PID

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `30 00 00 00 00 00 00 00` | 0x30 | Odczyt PID |
| 141 | `31 00 64 64 32 28 32 32` | 0x31 | Zapis PID do RAM. Kp=100, Ki=100, Kd=50, ... |
| 141 | `32 00 64 64 32 28 32 32` | 0x32 | Zapis PID do ROM. (trwały) |

> **Uwaga:** Komendy `0x30`, `0x31`, `0x32` nie są częścią oficjalnego protokołu V2.36 — mogą być specyficzne dla starszego firmware lub narzędzia konfiguracyjnego.

---

## Akceleracja / Moment obrotowy

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `33 00 00 00 00 00 00 00` | 0x33 | Odczyt przyspieszenia |
| 141 | `34 00 00 00 B8 0B 00 00` | 0x34 | Zapis przyspieszenia do RAM. acceleration = 0x0BB8 = 3000 |
| 141 | `37 00 00 00 00 00 00 00` | 0x37 | Odczyt maksymalnego momentu obrotowego |
| 141 | `38 00 00 00 64 00 00 00` | 0x38 | Zapis maksymalnego momentu do RAM. torque = 0x64 = 100 |

> **Uwaga:** Komendy `0x33`, `0x34`, `0x37`, `0x38` również mogą być spoza oficjalnego protokołu V2.36.

---

## Enkoder

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `90 00 00 00 00 00 00 00` | 0x90 | Odczyt wartości enkodera (encoder, raw, offset) |
| 141 | `91 00 00 00 00 00 00 00` | 0x91 | Zapis punktu zerowego silnika do ROM |
| 141 | `19 00 00 00 00 00 00 00` | 0x19 | Ustawienie bieżącej pozycji jako punktu zerowego (ROM, trwały) |
| 141 | `92 00 00 00 00 00 00 00` | 0x92 | Odczyt kąta wieloobrotowego |
| 141 | `94 00 00 00 00 00 00 00` | 0x94 | Odczyt kąta jednoobrotowego |

---

## Status / Błędy

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `9A 00 00 00 00 00 00 00` | 0x9A | Odczyt znaczników błędów i statusu 1 (temperatura, napięcie, prąd, motorState, errorState) |
| 141 | `9B 00 00 00 00 00 00 00` | 0x9B | Kasowanie znaczników błędów |
| 141 | `9C 00 00 00 00 00 00 00` | 0x9C | Odczyt statusu 2 (temperatura, iq/power, prędkość, enkoder) |
| 141 | `9D 00 00 00 00 00 00 00` | 0x9D | Odczyt statusu 3 (temperatura, prądy fazowe iA, iB, iC) |

---

## Dodatkowe (spoza pliku źródłowego)

| CAN ID | DATA (HEX) | Komenda | Opis |
|--------|------------|---------|------|
| 141 | `18 00 00 00 00 00 00 00` | 0x18 | Kalibracja enkodera (zapis do ROM) |
| 141 | `C0 xx 00 00 00 00 00 00` | 0xC0 | Odczyt parametru sterowania (xx = controlParamID) |
| 141 | `C1 xx ...` | 0xC1 | Zapis parametru sterowania do RAM |
| 141 | `40 xx yy 00 00 00 00 00` | 0x40 | Odczyt parametru konfiguracyjnego |
| 141 | `42 xx yy zz ...` | 0x42 | Zapis parametru konfiguracyjnego |
| 141 | `44 05 FA 00 00 00 00 00` | 0x44 | Zapisanie parametrów konfiguracyjnych do ROM |
| 141 | `07 00 00 00 00 00 00 00` | 0x07 | Restart silnika (brak odpowiedzi) |

---

## Format ogólny

```
cansend can0 <CAN_ID>#<DATA[0]><DATA[1]>...<DATA[7]>
```

**Przykład — uruchomienie silnika na osi 1:**
```bash
cansend can0 141#8800000000000000
```

**Przykład — przesunięcie do 180° z prędkością 120 dps na osi 1:**
```bash
cansend can0 141#A4007800504600000000
```

**Przykład — przesunięcie do -180° z prędkością 120 dps na osi 2 (node ID=2):**
```bash
cansend can0 142#A4007800B0B9FFFF
```

---

> **Tłumaczenie na język polski na potrzeby projektu Astro Mount Control.**
