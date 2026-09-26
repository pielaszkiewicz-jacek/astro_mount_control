# Audyt: zgodność specyfikacji STMP42SXI z implementacją CANopen

Data: 2026-09-26
Źródło specyfikacji: `docs/1787619745915-ymnq3q.pdf`
(„PMM60L、STM-M、BLM、TSM 系列一体化电机 CANopen 通信（闭环）用户手册” —
STMP42SXI-LIE, silnik serwo BLDC/PMSM z zamkniętą pętlą).

## 1. Wniosek

Specyfikacja **nie jest zgodna** z obecną implementacją. Dokument dotyczy innej
rodziny produktów niż ta, dla której napisano kod i skrypty:

| | Implementacja | Specyfikacja STMP42SXI |
|---|---|---|
| Rodzina produktów | NiMotion **STM42 / STM42M** (silnik krokowy, CiA 402) | **STMP42SXI** — PMM60L/STM-M/BLM/TSM (BLDC/PMSM, pętla zamknięta) |
| Źródło implementacji | `docs/stm42-canopen-protocol.pdf` | `docs/1787619745915-ymnq3q.pdf` |
| Grupa 2000h | parametry napędu krokowego (prądy, StepMode, …) | parametry silnika PMSM (napięcie, moc, moment, indukcyjność, …) |
| Mikrokrok `2001h:08h` | obecny (StepMode) | **nie istnieje** |

Najważniejsza konsekwencja: skrypty `canopen_step_mode.sh` (grupa 2000h) oraz
`canopen_params_6000h.sh` (grupa 6000h) zawierają rejestry obiektów zbudowane
na podstawie manuala STM42 i **nie odwzorowują** słownika STMP42SXI.

---

## 2. Grupa 2000h — zupełnie inny układ obiektów

Specyfikacja STMP42SXI definiuje grupę 2000h jako parametry **silnika PMSM/BLDC**:

- `2000h` (parametry silnika): 06h typ enkodera magistrali, 07h napięcie znamionowe,
  08h moc znamionowa, 09h prąd znamionowy, 0Ah prąd maksymalny, 0Bh czas prądu
  maksymalnego, 0Ch moment znamionowy, 0Dh moment maksymalny, 0Eh prędkość
  znamionowa, 0Fh prędkość maksymalna, 10h moment bezwładności, 11h liczba par
  biegunów, 12h rezystancja stojana, 13h/14h indukcyjności Lq/Ld, 16h stała momentu
  Kt, 19h liczba bitów enkodera, 1Ah liczba biegunów enkodera, 1Ch wybór czujnika
  położenia, 1Dh/1Eh kąty elektryczne, 20h kalibracja, Reserved3 (ograniczenie mocy).

Implementacja STM42 definiuje:

- `2000h`: tylko 01h RatedVoltage (RO), 02h MaxMotorSpeed (RO).
- `2001h`: 01h/02h progi napięć, 03h RunCurrentVal, 06h HoldCurrentVal,
  07h OverCurrentVal, **08h StepMode (mikrokrok)**, 09h LockCurrentVal.
- `2002h`: 01h CtrlModeSelec (0=CiA402, 1=NiMotion pos, 2=NiMotion vel),
  02h CloseLoopEn, 03h IAPVersion, 05h HardwareVersion.

Różnice:

| Aspekt | STM42 (implementacja) | STMP42SXI (specyfikacja) |
|---|---|---|
| `2001h:08h` StepMode | mikrokrok 0–4 | **brak** (silnik PMSM nie ma mikrokroku) |
| `2000h:01h/02h` | RatedVoltage / MaxMotorSpeed (RO) | 06h–1Eh — parametry silnika (RW) |
| `2002h:01h` CtrlModeSelec | 0–2 | **0–5** (dodane: 3 moment, 4 open-loop, 5 identyfikacja silnika) |
| `2005h` | 02h StepAmount (int32), 03h StepSpd | 01h źródło pozycji, 05h StepAmount (int16, inc), 18h/1Ch/1Dh |
| `2006h` | 01h/02h/03h/04h (rampy) | 01h/02h/03h źródła prędkości, 04h/06h/07h/08h/09h/0Ah/0Bh/12h/21h |
| `2008h` | PID stepper (PosLoopGain, Kpc, …) | regulatory prędkości/pozycji serwo (01h–14h, 2 grupy gain) |
| `200Bh` | monitor stepper | 01h stan, 02h/2Dh prędkość, 04h moment, 10h/11h AI, 12h–14h fazy, 15h napięcie, 16h temperatura |
| `200Ch:01h` CommunicationSelect | 1=EtherCAT, 2=CAN, 3=serial (fabr. 2) | 0–2 (fabr. 1) |
| `200Ch:02h` Node-ID | 1–127 | **1–247** |
| `200Ch:07h` | — (brak) | VDI enable (0/1) |
| `200Eh` kody błędów | 26 kodów (0x2300, 0x13110, …) | inny zestaw (67183377, 8978, 78352, 16855584, …) |
| `2011h` / `2012h` | — (brak) | **wielosegmentowa pozycja / prędkość** (16 segmentów) |
| `2017h` VDI | VDI1–VDI6 + VDinEn (01h) + CommSet (02h) | VDI1–VDI16 (01h–20h), bez VDinEn |
| `2021h` DX | obecny | **brak** |
| `2031h` | — (brak) | VDI virtual level / DO state (RW) |

---

## 3. Grupa 6000h (CiA 402) — wspólna podstawa, ale inne domyślne i brakujące obiekty

Grupa 6000h to standard CiA 402, więc większość obiektów się pokrywa, jednak
specyfikacja STMP42SXI:

### 3.1 Obiekty obecne w STMP42SXI, a brakujące w implementacji

| Obiekt | Opis w STMP42SXI |
|---|---|
| `6044h:00h` | VM — rzeczywista prędkość (RO) |
| `6069h:00h` | wartość sprzężenia czujnika prędkości (RO) |
| `6071h:00h` | moment docelowy (int16, 0.1%) |
| `6072h:00h` | moment maksymalny |
| `6073h:00h` | prąd maksymalny |
| `6074h:00h` | żądany moment (RO) |
| `6075h:00h` | prąd znamionowy |
| `6076h:00h` | moment znamionowy |
| `6077h:00h` | sprzężenie momentu (RO) |
| `6078h:00h` | sprzężenie prądu (RO) |
| `6087h:00h` | rampa momentu |
| `6088h:00h` | typ rampy momentu |
| `60B2h:00h` | offset momentu |
| `60FAh:00h` | wyjście regulatora (RO) |
| `60FDh:00h` | monitor sygnałów wejściowych DI |
| `60FEh:01h/02h` | monitor wyjść fizycznych DO |

### 3.2 Różnice dostępu (RW/RO)

| Obiekt | Implementacja (STM42) | STMP42SXI |
|---|---|---|
| `608Fh:01h/02h` (enkoder) | **RW**, 4000 / 1 | **RO**, 10000 / 1 |

Jest to istotne: `config/canopen_stm42m.json` oraz `servo_init` zapisują
`608Fh:01h = 4000`; w STMP42SXI obiekt ten jest tylko do odczytu (rozdzielczość
enkodera jest stała, 10000 imp/obr).

### 3.3 Różnice wartości fabrycznych

| Obiekt | Implementacja (STM42) | STMP42SXI |
|---|---|---|
| `6046h:01h/02h` | 0 / 300 (0–3000) | **10 / 3000** (0–6000) |
| `604Ah:01h` | 1000 | **800** |
| `6066h:00h` | 10000 ms | **30000 ms** |
| `606Dh:00h` | 100 rpm | **500 rpm** |
| `607Bh:01h/02h` | ±60000000 | **±1048576** |
| `607Dh:01h/02h` | ±60000 | **±65535** |
| `607Fh:00h` | 40000 | **500000** |
| `6080h:00h` | 600 rpm | **4000 rpm** (0–6000) |
| `6081h:00h` | 12000 | **500000** |
| `6083h/6084h` | 40000 / 120000 | **409600 / 409600** |
| `6085h:00h` | 400000 | **500000** |
| `6086h:00h` | 0 (linear) | **3 (S-curve)** |
| `6098h:00h` | 24 | **20** |
| `6099h:01h/02h` | 12000 / 4000 | **10000 / 2730** |
| `609Ah:00h` | 80000 | **409600** |
| `60A4h:01h/02h` | 15000 / 30000 | **50000 / 50000** |
| `60C2h:02h` | -3 | **-1** |

### 3.4 Różnice zakresów

- `6046h`: 0–6000 (nie 0–3000)
- `6048h/6049h/604Ah`: 0–4294967295 (nie 0–300000)
- `607Fh/6081h/6082h`: 0–1000000 (nie 0–500000)
- `6080h`: 0–6000 (nie 0–500)
- `605Dh`: 1–2; `605Eh`: 0–2; `605Ah`: 0–2; `605Bh/605Ch`: 0–1

---

## 4. Wpływ na konkretne pliki implementacji

| Plik | Problem |
|---|---|
| [`scripts/canopen_step_mode.sh`](scripts/canopen_step_mode.sh:1) | rejestr 2000h zbudowany wg STM42; obiekty STMP42SXI mają inne znaczenie; `--step-mode` (2001h:08h) nie istnieje w STMP42SXI |
| [`scripts/canopen_params_6000h.sh`](scripts/canopen_params_6000h.sh:1) | brak 16 obiektów momentu/monitoringu; złe wartości fabryczne i zakresy |
| [`scripts/canopen_set_node_id.sh`](scripts/canopen_set_node_id.sh:1) | zakres Node-ID 1–127 (STMP42SXI dopuszcza 1–247 dla „轴地址”) |
| [`scripts/canopen_encoder_resolution.sh`](scripts/canopen_encoder_resolution.sh:1) | zapisuje 608Fh (w STMP42SXI obiekt RO, 10000 imp/obr) |
| [`config/canopen_stm42m.json`](config/canopen_stm42m.json:1) | `encoder_resolution: 4000`, zapis `608Fh:01h=4000`, `2002h:01h=0`, `6060h=1` — wymaga korekty pod STMP42SXI (10000 imp/obr, 608Fh RO) |

---

## 5. Rekomendacje

1. **Nie używać** obecnych skryptów ani `config/canopen_stm42m.json` do STMP42SXI —
   znaczenia obiektów 2000h są inne, a część obiektów 6000h ma inne domyślne/dostęp.
2. Dla STMP42SXI utworzyć osobne rejestry/konfigurację:
   - nowa grupa 2000h (parametry silnika PMSM),
   - rozszerzona grupa 6000h (moment 6071h–6078h, 6087h/6088h, monitory 6069h/60FAh/60FDh/60FEh),
   - `608Fh` traktować jako RO (10000 imp/obr),
   - Node-ID 1–247,
   - domyślne profile wg sekcji 3.3.
3. Wspólną logikę SDO (ramki 0x40/0x2F/0x2B/0x23, parsowanie) można zachować —
   zmienia się tylko rejestr obiektów i walidacja zakresów.
