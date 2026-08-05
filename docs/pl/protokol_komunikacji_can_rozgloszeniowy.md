# LingKong Technology — Protokół Rozgłoszeniowy CAN Bus Silnika

**Wersja V2.35**

---

## 1. Parametry Magistrali CAN w Trybie Rozgłoszeniowym

| Parametr | Wartość |
|----------|---------|
| **Interfejs magistrali** | CAN |
| **Prędkość transmisji** | 500Kbps, 1Mbps |
| **Format ramki** | Ramka danych |
| **Typ ramki** | Ramka standardowa |
| **DLC** | 8 bajtów |

---

## 2. Komendy Trybu Rozgłoszeniowego

Komendy opisane w tym protokole służą do szybkiej komunikacji z silnikami i sterowania rozgłoszeniowego. Jedna komenda może jednocześnie sterować maksymalnie 4 silnikami.

- Należy włączyć tryb rozgłoszeniowy w sterowniku nadrzędnym (上位机) i ustawić prędkość transmisji CAN na co najmniej 500Kbps.
- Długość DLC ramki komendy wysyłanej przez sterownik główny wynosi 8 bajtów.
- Aby zapobiec kolizjom na magistrali, każdy sterownik musi mieć ustawiony inny identyfikator (ID) — odpowiednio 1, 2, 3, 4 (przy mniej niż 4 silnikach można pominąć nieużywane ID). ID można wybrać za pomocą przełączników DIP na płytce sterownika lub ustawić za pomocą oprogramowania nadrzędnego.
- Sterownik główny wysyła komendę w formie rozgłoszeniowej. Każda płytka sterownika po odebraniu komendy wykonuje ją, a następnie po określonym czasie odpowiada sterownikowi głównemu sekwencyjnie według ID (niższe ID odpowiada jako pierwsze).
- W trybie rozgłoszeniowym obsługiwane komendy są rozróżniane na podstawie ID ramki. Aktualnie obsługiwane komendy:

| Nr | Komenda | ID Standardowej Ramki Danych |
|----|---------|------------------------------|
| 1 | Sterowanie momentem / w pętli otwartej | 0x280 |
| 2 | Sterowanie prędkością | 0x281 |
| 3 | Sterowanie pozycją | 0x282 |
| 4 | Komenda mieszana | 0x288 |

---

### 2.1 Komenda Sterowania Momentem / w Pętli Otwartej

Zawiera jednocześnie wartości sterowania prądem momentu obrotowego (seria MF, MG) lub wartości sterowania napięciem w pętli otwartej (seria MS) dla 4 silników. Wartość sterująca `torqueValue` jest 16-bitową daną całkowitą. Dla silników MF, MG zakres danych wynosi **-2000 ~ +2000**; dla silników MS zakres danych wynosi **-850 ~ +850**.

| Pole | Opis | Uwagi |
|------|------|-------|
| **ID ramki** | 0x280 | |
| data[0] | Silnik #1 `torqueValue` — młodszy bajt | |
| data[1] | Silnik #1 `torqueValue` — starszy bajt | |
| data[2] | Silnik #2 `torqueValue` — młodszy bajt | |
| data[3] | Silnik #2 `torqueValue` — starszy bajt | |
| data[4] | Silnik #3 `torqueValue` — młodszy bajt | |
| data[5] | Silnik #3 `torqueValue` — starszy bajt | |
| data[6] | Silnik #4 `torqueValue` — młodszy bajt | |
| data[7] | Silnik #4 `torqueValue` — starszy bajt | |

**Przykład:** Sterownik główny wysyła do silnika #1 prąd momentu 100, a do silnika #3 prąd momentu -100. Dane komendy (HEX):

```
64  00  00  00  9C  FF  00  00
```

**Odpowiedź sterownika dla komendy sterowania momentem / w pętli otwartej:** Taka sama jak odpowiedź dla pojedynczej komendy sterowania momentem.

---

### 2.2 Komenda Sterowania Prędkością

Zawiera jednocześnie wartości sterowania prędkością dla 4 silników. Wartość sterująca `speedValue` jest 16-bitową daną całkowitą. Rozdzielczość wynosi **1dps / LSB**. Ze względu na ograniczenie długości danych, zakres prędkości `speedValue` wynosi **-32768 ~ 32767dps**.

| Pole | Opis | Uwagi |
|------|------|-------|
| **ID ramki** | 0x281 | |
| data[0] | Silnik #1 `speedValue` — młodszy bajt | |
| data[1] | Silnik #1 `speedValue` — starszy bajt | |
| data[2] | Silnik #2 `speedValue` — młodszy bajt | |
| data[3] | Silnik #2 `speedValue` — starszy bajt | |
| data[4] | Silnik #3 `speedValue` — młodszy bajt | |
| data[5] | Silnik #3 `speedValue` — starszy bajt | |
| data[6] | Silnik #4 `speedValue` — młodszy bajt | |
| data[7] | Silnik #4 `speedValue` — starszy bajt | |

**Przykład:** Sterownik główny wysyła do silnika #2 prędkość 360dps, a do silnika #4 prędkość -720dps. Dane komendy (HEX):

```
00  00  68  01  00  00  30  FD
```

**Odpowiedź sterownika dla komendy sterowania prędkością:** Taka sama jak odpowiedź dla pojedynczej komendy sterowania prędkością.

---

### 2.3 Komenda Sterowania Pozycją

Zawiera jednocześnie wartości sterowania pozycją bezwzględną dla 4 silników. Wartość sterująca `angleValue` jest 16-bitową daną całkowitą. Rozdzielczość wynosi **0,01stopnia / LSB**. Ze względu na ograniczenie długości danych, zakres kąta `angleValue` wynosi **-327,68° ~ 327,67°**.

| Pole | Opis | Uwagi |
|------|------|-------|
| **ID ramki** | 0x282 | |
| data[0] | Silnik #1 `angleValue` — młodszy bajt | |
| data[1] | Silnik #1 `angleValue` — starszy bajt | |
| data[2] | Silnik #2 `angleValue` — młodszy bajt | |
| data[3] | Silnik #2 `angleValue` — starszy bajt | |
| data[4] | Silnik #3 `angleValue` — młodszy bajt | |
| data[5] | Silnik #3 `angleValue` — starszy bajt | |
| data[6] | Silnik #4 `angleValue` — młodszy bajt | |
| data[7] | Silnik #4 `angleValue` — starszy bajt | |

**Przykład:** Sterownik główny wysyła do silnika #1 kąt 180°, a do silnika #4 kąt -90°. Dane komendy (HEX):

```
50  46  00  00  00  00  D8  DC
```

**Odpowiedź sterownika dla komendy sterowania pozycją:** Taka sama jak odpowiedź dla pojedynczej komendy sterowania pozycją 1.

---

### 2.4 Komenda Mieszana

Zawiera jednocześnie różne komendy dla 4 silników. Komenda wykonywana przez każdy silnik jest określana na podstawie bajtu `motorCmd`.

| Pole | Opis | Uwagi |
|------|------|-------|
| **ID ramki** | 0x288 | |
| data[0] | Silnik #1 — bajt `motorCmd` | |
| data[1] | 0x00 | |
| data[2] | Silnik #2 — bajt `motorCmd` | |
| data[3] | 0x00 | |
| data[4] | Silnik #3 — bajt `motorCmd` | |
| data[5] | 0x00 | |
| data[6] | Silnik #4 — bajt `motorCmd` | |
| data[7] | 0x00 | |

**Komendy obsługiwane przez `motorCmd`:**

| Nr | Komenda | Bajt `motorCmd` |
|----|---------|------------------|
| 1 | Odczyt statusu silnika 1 i znaczników błędów | 0x9A |
| 2 | Kasowanie znaczników błędów silnika | 0x9B |
| 3 | Odczyt statusu silnika 2 | 0x9C |
| 4 | Wyłączenie silnika | 0x80 |
| 5 | Włączenie silnika | 0x88 |
| 6 | Zatrzymanie silnika | 0x81 |

**Przykład:** Sterownik główny wysyła do silnika #1 komendę odczytu statusu 2, a do silnika #4 komendę zatrzymania. Dane komendy (HEX):

```
9C  00  00  00  00  00  81  00
```

**Odpowiedź sterownika dla komendy mieszanej:** Taka sama jak odpowiedź dla odpowiadającej pojedynczej komendy silnika.

---

## 3. Inne

Ze względu na ograniczenia prędkości magistrali i taktowania:

- Przy prędkości transmisji **500Kbps** maksymalna częstotliwość wysyłania komend przy jednoczesnym sterowaniu 4 silnikami wynosi około **600Hz**.
- Przy prędkości transmisji **1Mbps** maksymalna częstotliwość wysyłania komend przy jednoczesnym sterowaniu 4 silnikami wynosi około **1,2KHz**.

---

> **Źródło:** LingKong Technology (瓴控科技) — Motor CAN Broadcast Communication Protocol V2.35  
> **Tłumaczenie na język polski na potrzeby projektu Astro Mount Control.**
