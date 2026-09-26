# NiMotion STM42 (CANopen) — ustawianie Node-ID kontrolera

> Źródło: [`docs/stm42-canopen-protocol.pdf`](../../stm42-canopen-protocol.pdf)
> (sekcja **3.6.2 Node ID**, obiekt **200Ch:02h**).

## Zasada działania

- Node-ID jest zapisywany w obiekcie **200Ch:02h** (`ServoShaftAddress`), typ `uint16`, zakres **1–127**.
- Zmiana aktywuje się **po restarcie** kontrolera (`Restart to take effect`).
- Zapis SDO:
  - COB-ID nadawcy = `0x600 + aktualny_node_id`
  - COB-ID odpowiedzi = `0x580 + aktualny_node_id`
  - dane little-endian
  - zapis 2 bajtów (uint16) → command specifier `0x2B`
  - zapis 4 bajtów (uint32) → command specifier `0x23`
- Zapis do EEPROM: obiekt `1010h:01h` = `0x65766173` (ASCII `save`).
- Restart węzła: NMT COB-ID `0x000`, dane `81 <node_id>`.

## Format ramki SDO (write uint16)

```
CAN-ID          DATA[0..7]
0x600+ID        2B 0C 20 02 <lo> <hi> 00 00 00
                │  └─┴─ index 0x200C
                │        └─ sub-index 0x02
                └─ command specifier 0x2B (write 2 bytes)
```

## Komendy `cansend` — ID = 1

Urządzenie domyślnie ma Node-ID = 1, więc programuje się je pod adresem `0x601`.

```bash
# 1. Zapisz nowy Node-ID = 1 do 200Ch:02h
cansend can0 601#2B0C20020100000000

# 2. Zapisz parametry do EEPROM (1010h:01h = "save")
cansend can0 601#2310100173617665

# 3. Zrestartuj węzeł (NMT reset node, ID=1)
cansend can0 000#8101
```

## Komendy `cansend` — ID = 2

Drugie urządzenie programuje się **jedno na raz** — gdy oba mają fabryczne ID=1,
należy je podłączać osobno (albo najpierw nadać pierwszemu ID=1, odłączyć je i
podłączyć drugie do magistrali).

```bash
# 1. Zapisz nowy Node-ID = 2 do 200Ch:02h (bieżący adres nadal 0x601)
cansend can0 601#2B0C20020200000000

# 2. Zapisz parametry do EEPROM (1010h:01h = "save")
cansend can0 601#2310100173617665

# 3. Zrestartuj węzeł (NMT reset node, ID=1 — nowe ID aktywuje się po restarcie)
cansend can0 000#8101
```

## Weryfikacja

Po restarcie kontroler zgłasza boot-up pod nowym adresem `0x700 + nowy_node_id`.
Nowe ID można odczytać z obiektu `200Ch:02h`:

```bash
# Odczyt 200Ch:02h — ID=1 (odpowiedź oczekiwana na 0x581)
cansend can0 601#400C200200000000

# Odczyt 200Ch:02h — ID=2 (odpowiedź oczekiwana na 0x582)
cansend can0 602#400C200200000000
```

Interpretacja odpowiedzi SDO (odczyt 2 bajtów, uint16):

```
can0  601   [8]  40 0C 20 02 00 00 00 00   <- zapytanie (upload request)
can0  581   [8]  4B 0C 20 02 01 00 00 00   <- odpowiedź: Node-ID = 0x0001 = 1
                  │  └─┴─ index 200Ch
                  │        └─ sub-index 02
                  └─ 0x4B = expedited upload response, 2 bajty danych

bajty [4..5] = Node-ID little-endian
  01 00  -> ID = 1
  02 00  -> ID = 2
```

## CLI — automatyczna konfiguracja

Skrypt [`scripts/canopen_set_node_id.sh`](../../scripts/canopen_set_node_id.sh)
wykonuje całą sekwencję (zapis ID, EEPROM save, reset, weryfikacja):

```bash
# Ustawienie ID = 1
./scripts/canopen_set_node_id.sh 1 1 can0

# Ustawienie ID = 2
./scripts/canopen_set_node_id.sh 1 2 can0
```

Wariant z innym interfejsem CAN:

```bash
CAN_IF=can1 ./scripts/canopen_set_node_id.sh 1 2
```

> **Uwaga:** oba kontrolery nie mogą mieć jednocześnie tego samego Node-ID na
> jednej magistrali — programuj je pojedynczo.
