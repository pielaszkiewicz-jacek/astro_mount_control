# Shanghai LingKong Technology (上海瓴控科技)

# Protokół Komunikacyjny CAN Bus Silnika

**Wersja V2.36**

---

## Spis Treści

1. Shanghai LingKong Technology
2. Protokół Komunikacyjny CAN Bus Silnika
3. Zastrzeżenie Prawne
4. Parametry Magistrali CAN
5. Pojedyncze Komendy Silnika
   - 1. Odczyt Statusu Silnika 1 i Znaczników Błędów
   - 2. Kasowanie Znaczników Błędów Silnika
   - 3. Odczyt Statusu Silnika 2
   - 4. Odczyt Statusu Silnika 3
   - 5. Wyłączenie Silnika
   - 6. Uruchomienie Silnika
   - 7. Zatrzymanie Silnika
   - 8. Sterowanie Hamulcem i Odczyt Stanu
   - 9. Sterowanie w Pętli Otwartej (tylko silniki MS)
   - 10. Sterowanie Momentem w Pętli Zamkniętej (tylko MF, MH, MG)
   - 11. Sterowanie Prędkością w Pętli Zamkniętej
   - 12. Sterowanie Pozycją Wieloobrotową w Pętli Zamkniętej 1
   - 13. Sterowanie Pozycją Wieloobrotową w Pętli Zamkniętej 2
   - 14. Sterowanie Pozycją Jednoobrotową w Pętli Zamkniętej 1
   - 15. Sterowanie Pozycją Jednoobrotową w Pętli Zamkniętej 2
   - 16. Sterowanie Przyrostem Pozycji w Pętli Zamkniętej 1
   - 17. Sterowanie Przyrostem Pozycji w Pętli Zamkniętej 2
   - 18. Odczyt Parametrów Sterowania
   - 19. Zapis Parametrów Sterowania
   - 20. Odczyt Danych Enkodera Silnika
   - 21. Kalibracja Enkodera
   - 22. Ustawienie Bieżącej Pozycji Jako Punktu Zerowego Silnika (zapis do ROM, trwały)
   - 23. Odczyt Kąta Wieloobrotowego
   - 24. Odczyt Kąta Jednoobrotowego
   - 25. Ustawienie Bieżącej Pozycji Jako Punktu Zerowego (zapis do RAM)
   - 26. Odczyt Parametrów Konfiguracyjnych
   - 27. Zapis Parametrów Konfiguracyjnych
   - 28. Zapisanie Parametrów Konfiguracyjnych
   - 29. Restart Silnika

---

## Zastrzeżenie Prawne

Dziękujemy za zakup zintegrowanego systemu sterowania napędem silnika firmy Shanghai LingKong Technology Co., Ltd. Przed użyciem prosimy o dokładne zapoznanie się z niniejszym zastrzeżeniem. Rozpoczęcie użytkowania produktu jest równoznaczne z akceptacją wszystkich warunków niniejszego zastrzeżenia. Należy ściśle przestrzegać instrukcji produktu, protokołu sterowania oraz odpowiednich przepisów prawa, polityk i wytycznych podczas instalacji i użytkowania tego produktu. Podczas korzystania z produktu użytkownik zobowiązuje się do ponoszenia odpowiedzialności za swoje działania oraz wszelkie wynikające z nich konsekwencje. Firma LingKong Technology nie ponosi odpowiedzialności prawnej za jakiekolwiek szkody spowodowane niewłaściwym użytkowaniem, instalacją lub modyfikacją produktu.

LingKong Technology jest znakiem towarowym Shanghai LingKong Technology Co., Ltd. i jej podmiotów stowarzyszonych. Nazwy produktów i marek wymienione w niniejszym dokumencie są znakami towarowymi lub zastrzeżonymi znakami towarowymi ich odpowiednich właścicieli.

Niniejszy produkt i instrukcja są własnością intelektualną Shanghai LingKong Technology Co., Ltd. Kopiowanie lub powielanie w jakiejkolwiek formie bez zezwolenia jest zabronione. Ostateczna interpretacja niniejszego zastrzeżenia należy do naszej firmy.

---

## Parametry Magistrali CAN

| Parametr | Wartość |
|----------|---------|
| **Interfejs magistrali** | CAN |
| **Prędkość transmisji (tryb normalny, komendy pojedynczego silnika)** | 1Mbps (domyślnie), 500kbps, 250kbps, 125kbps, 100kbps |
| **Prędkość transmisji (tryb rozgłoszeniowy, komendy wielu silników)** | 1Mbps, 500kbps |

---

## Pojedyncze Komendy Silnika

Na jednej magistrali można podłączyć do 32 sterowników (w zależności od obciążenia magistrali). Aby zapobiec kolizjom na magistrali, każdy sterownik musi mieć ustawiony inny identyfikator (ID).

Sterownik główny wysyła komendę pojedynczego silnika na magistralę. Silnik o odpowiednim ID wykonuje komendę po jej odebraniu i w określonym czasie (w ciągu 0,25ms) wysyła odpowiedź do sterownika głównego. Format ramki komendy i odpowiedzi jest następujący:

| Parametr | Wartość |
|----------|---------|
| **Identyfikator ramki komendy** | 0x140 + ID (1~32) |
| **Identyfikator ramki odpowiedzi** | 0x140 + ID (1~32) |
| **Format ramki** | Ramka danych |
| **Typ ramki** | Ramka standardowa |
| **DLC** | 8 bajtów |

---

### 1. Odczyt Statusu Silnika 1 i Znaczników Błędów

Komenda odczytuje bieżącą temperaturę silnika, napięcie oraz znaczniki stanu błędów.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9A |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Ramka danych zawiera następujące parametry:

1. Temperatura silnika `temperature` (typ int8_t, jednostka 1°C/LSB)
2. Napięcie szyny DC `voltage` (typ int16_t, jednostka 0,01V/LSB)
3. Prąd szyny DC `current` (typ int16_t, jednostka 0,01A/LSB)
4. Stan silnika `motorState` (typ uint8_t, każdy bit reprezentuje inny stan silnika)
5. Znaczniki błędów `errorState` (typ uint8_t, każdy bit reprezentuje inny stan błędu silnika)

**Ramka odpowiedzi:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9A |
| DATA[1] | Temperatura silnika | DATA[1] = *(uint8_t *)(&temperature) |
| DATA[2] | Napięcie szyny DC - młodszy bajt | DATA[2] = *(uint8_t *)(&voltage) |
| DATA[3] | Napięcie szyny DC - starszy bajt | DATA[3] = *((uint8_t *)(&voltage)+1) |
| DATA[4] | Prąd szyny DC - młodszy bajt | DATA[4] = *(uint8_t *)(&current) |
| DATA[5] | Prąd szyny DC - starszy bajt | DATA[5] = *((uint8_t *)(&current)+1) |
| DATA[6] | Bajt stanu silnika | DATA[6] = motorState |
| DATA[7] | Bajt stanu błędów | DATA[7] = errorState |

**Uwagi:**

1. `motorState = 0x00` — silnik w stanie włączonym; `motorState = 0x10` — silnik w stanie wyłączonym.

2. Tabela stanów poszczególnych bitów `errorState`:

| Bit errorState | Opis stanu | 0 | 1 |
|----------------|------------|---|---|
| 0 | Stan niskiego napięcia | Normalny | Zabezpieczenie podnapięciowe |
| 1 | Stan wysokiego napięcia | Normalny | Zabezpieczenie nadnapięciowe |
| 2 | Stan temperatury sterownika | Normalny | Przegrzanie sterownika |
| 3 | Stan temperatury silnika | Normalny | Przegrzanie silnika |
| 4 | Stan prądu silnika | Normalny | Nadprąd silnika |
| 5 | Stan zwarcia silnika | Normalny | Zwarcie silnika |
| 6 | Stan blokady wirnika | Normalny | Blokada wirnika silnika |
| 7 | Stan sygnału wejściowego | Normalny | Przekroczenie czasu utraty sygnału wejściowego |

---

### 2. Kasowanie Znaczników Błędów Silnika

Komenda kasuje bieżący stan błędu silnika. Silnik odpowiada po odebraniu komendy.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9B |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi są takie same jak w komendzie odczytu statusu silnika 1 i znaczników błędów (różni się tylko bajt komendy DATA[0], tutaj 0x9B).

**Uwagi:**

1. Jeśli stan silnika nie wrócił do normy, znaczniki błędów nie mogą zostać skasowane.

---

### 3. Odczyt Statusu Silnika 2

Komenda odczytuje bieżącą temperaturę silnika, prąd momentu obrotowego silnika (MF, MG) / moc wyjściową silnika (MS), prędkość obrotową oraz pozycję enkodera.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9C |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Ramka danych zawiera następujące parametry:

1. Temperatura silnika `temperature` (typ int8_t, 1°C/LSB)
2. Wartość prądu momentu obrotowego `iq` dla silników MF, MG lub wartość mocy wyjściowej `power` dla silników MS, typ int16_t. Rozdzielczość iq dla silników MG: (66/4096 A) / LSB; rozdzielczość iq dla silników MF: (33/4096 A) / LSB. Zakres mocy `power` dla silników MS: -1000~1000.
3. Prędkość obrotowa silnika `speed` (typ int16_t, 1dps/LSB)
4. Wartość enkodera `encoder` (typ uint16_t, zakres wartości enkodera 14-bitowego: 0~16383, enkodera 15-bitowego: 0~32767, enkodera 16-bitowego: 0~65535)

**Ramka odpowiedzi:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9C |
| DATA[1] | Temperatura silnika | DATA[1] = *(uint8_t *)(&temperature) |
| DATA[2] | Prąd momentu - młodszy bajt | DATA[2] = *(uint8_t *)(&iq) |
| | Moc wyjściowa - młodszy bajt (seria MS) | DATA[2] = *(uint8_t *)(&power) |
| DATA[3] | Prąd momentu - starszy bajt | DATA[3] = *((uint8_t *)(&iq)+1) |
| | Moc wyjściowa - starszy bajt (seria MS) | DATA[3] = *((uint8_t *)(&power)+1) |
| DATA[4] | Prędkość silnika - młodszy bajt | DATA[4] = *(uint8_t *)(&speed) |
| DATA[5] | Prędkość silnika - starszy bajt | DATA[5] = *((uint8_t *)(&speed)+1) |
| DATA[6] | Pozycja enkodera - młodszy bajt | DATA[6] = *(uint8_t *)(&encoder) |
| DATA[7] | Pozycja enkodera - starszy bajt | DATA[7] = *((uint8_t *)(&encoder)+1) |

---

### 4. Odczyt Statusu Silnika 3

Ponieważ silniki MS nie mają pomiaru prądu fazowego, ta komenda nie działa na silnikach MS.

Komenda odczytuje bieżącą temperaturę silnika oraz dane prądu 3-fazowego.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9D |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Ramka danych zawiera następujące dane:

1. Temperatura silnika `temperature` (typ int8_t, 1°C/LSB)
2. Dane prądu fazowego iA, iB, iC, typ danych int16_t. Rozdzielczość prądu fazowego dla silników MG: (66/4096 A) / LSB; rozdzielczość prądu fazowego dla silników MF: (33/4096 A) / LSB.

**Ramka odpowiedzi:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x9D |
| DATA[1] | Temperatura silnika | DATA[1] = *(uint8_t *)(&temperature) |
| DATA[2] | Prąd fazy A - młodszy bajt | DATA[2] = *(uint8_t *)(&iA) |
| DATA[3] | Prąd fazy A - starszy bajt | DATA[3] = *((uint8_t *)(&iA)+1) |
| DATA[4] | Prąd fazy B - młodszy bajt | DATA[4] = *(uint8_t *)(&iB) |
| DATA[5] | Prąd fazy B - starszy bajt | DATA[5] = *((uint8_t *)(&iB)+1) |
| DATA[6] | Prąd fazy C - młodszy bajt | DATA[6] = *(uint8_t *)(&iC) |
| DATA[7] | Prąd fazy C - starszy bajt | DATA[7] = *((uint8_t *)(&iC)+1) |

---

### 5. Wyłączenie Silnika

Przełącza silnik ze stanu włączonego (stan domyślny po włączeniu zasilania) do stanu wyłączonego, kasuje liczbę obrotów silnika oraz wcześniej odebrane instrukcje sterujące. Dioda LED przechodzi ze świecenia ciągłego na wolne miganie. W tym stanie silnik nadal może odpowiadać na komendy sterujące, ale nie wykonuje żadnych akcji.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x80 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Taka sama jak ramka wysłana przez sterownik główny.

---

### 6. Uruchomienie Silnika

Przełącza silnik ze stanu wyłączonego do stanu włączonego. Dioda LED przechodzi z wolnego migania na świecenie ciągłe. Po tej operacji można wysyłać instrukcje sterujące, aby sterować ruchem silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x88 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Taka sama jak ramka wysłana przez sterownik główny.

---

### 7. Zatrzymanie Silnika

Zatrzymuje silnik, ale nie kasuje stanu pracy silnika. Ponowne wysłanie instrukcji sterującej umożliwia sterowanie ruchem silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x81 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika (1 ramka):**

Taka sama jak ramka wysłana przez sterownik główny.

---

### 8. Sterowanie Hamulcem i Odczyt Stanu

Steruje załączaniem/zwalnianiem hamulca lub odczytuje bieżący stan hamulca.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x8C |
| DATA[1] | Bajt sterowania i odczytu stanu hamulca | 0x00: hamulec bez zasilania, hamowanie aktywne |
| | | 0x01: hamulec pod zasilaniem, hamowanie zwolnione |
| | | 0x10: odczyt stanu hamulca |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika (1 ramka):**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x8C |
| DATA[1] | Bajt stanu hamulca | 0x00: hamulec w stanie bez zasilania, hamowanie aktywne |
| | | 0x01: hamulec w stanie pod zasilaniem, hamowanie zwolnione |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

---

### 9. Sterowanie w Pętli Otwartej (tylko silniki MS, dla innych silników nieaktywne)

Sterownik główny wysyła tę komendę, aby sterować napięciem wyjściowym w pętli otwartej do silnika. Wartość sterująca `powerControl` jest typu int16_t, zakres wartości: -850~850 (prąd silnika i moment obrotowy zależą od konkretnego silnika).

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA0 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Wartość sterowania w pętli otwartej - młodszy bajt | DATA[4] = *(uint8_t *)(&powerControl) |
| DATA[5] | Wartość sterowania w pętli otwartej - starszy bajt | DATA[5] = *((uint8_t *)(&powerControl)+1) |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Uwagi:**

1. Wartość sterująca `powerControl` w tej komendzie nie jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika (1 ramka):**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA0).

---

### 10. Sterowanie Momentem w Pętli Zamkniętej (tylko silniki MF, MH, MG)

Sterownik główny wysyła tę komendę, aby sterować wyjściowym prądem momentu obrotowego silnika. Wartość sterująca `iqControl` jest typu int16_t, zakres wartości: -2048~2048. Odpowiada to zakresowi rzeczywistego prądu momentu obrotowego dla silników MF: -16,5A~16,5A, dla silników MG: -33A~33A. Prąd szyny i rzeczywisty moment obrotowy silnika różnią się w zależności od konkretnego silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA1 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Wartość sterowania prądem momentu - młodszy bajt | DATA[4] = *(uint8_t *)(&iqControl) |
| DATA[5] | Wartość sterowania prądem momentu - starszy bajt | DATA[5] = *((uint8_t *)(&iqControl)+1) |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Uwagi:**

1. Wartość sterująca `iqControl` w tej komendzie nie jest ograniczana przez wartość Max Torque Current w sterowniku nadrzędnym.

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA1).

---

### 11. Sterowanie Prędkością w Pętli Zamkniętej

Sterownik główny wysyła tę komendę, aby sterować prędkością silnika z jednoczesnym ograniczeniem momentu obrotowego. Wartość sterująca `speedControl` jest typu int32_t, odpowiada rzeczywistej prędkości obrotowej 0,01dps/LSB. Wartość sterująca `iqControl` jest typu int16_t, zakres wartości: -2048~2048. Odpowiada to zakresowi rzeczywistego prądu momentu obrotowego dla silników MF: -16,5A~16,5A, dla silników MG: -33A~33A. Prąd szyny i rzeczywisty moment obrotowy silnika różnią się w zależności od konkretnego silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA2 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | Wartość ograniczenia prądu momentu - młodszy bajt | DATA[2] = *(uint8_t *)(&iqControl) |
| DATA[3] | Wartość ograniczenia prądu momentu - starszy bajt | DATA[3] = *((uint8_t *)(&iqControl)+1) |
| DATA[4] | Sterowanie prędkością - młodszy bajt | DATA[4] = *(uint8_t *)(&speedControl) |
| DATA[5] | Sterowanie prędkością | DATA[5] = *((uint8_t *)(&speedControl)+1) |
| DATA[6] | Sterowanie prędkością | DATA[6] = *((uint8_t *)(&speedControl)+2) |
| DATA[7] | Sterowanie prędkością - starszy bajt | DATA[7] = *((uint8_t *)(&speedControl)+3) |

**Uwagi:**

1. W tej komendzie `speedControl` silnika jest ograniczane przez wartość Max Speed w sterowniku nadrzędnym.
2. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA2).

---

### 12. Sterowanie Pozycją Wieloobrotową w Pętli Zamkniętej 1

Sterownik główny wysyła tę komendę, aby sterować pozycją silnika (kąt wieloobrotowy). Wartość sterująca `angleControl` jest typu int32_t, odpowiada rzeczywistej pozycji 0,01stopnia/LSB, tzn. 36000 reprezentuje 360°. Kierunek obrotu silnika jest określany przez różnicę między pozycją docelową a bieżącą.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA3 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Sterowanie pozycją - młodszy bajt | DATA[4] = *(uint8_t *)(&angleControl) |
| DATA[5] | Sterowanie pozycją | DATA[5] = *((uint8_t *)(&angleControl)+1) |
| DATA[6] | Sterowanie pozycją | DATA[6] = *((uint8_t *)(&angleControl)+2) |
| DATA[7] | Sterowanie pozycją - starszy bajt | DATA[7] = *((uint8_t *)(&angleControl)+3) |

**Uwagi:**

1. Wartość sterująca `angleControl` w tej komendzie jest ograniczana przez wartość Max Angle w sterowniku nadrzędnym.
2. W tej komendzie maksymalna prędkość silnika jest ograniczana przez wartość Max Speed w sterowniku nadrzędnym.
3. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.
4. W tym trybie sterowania maksymalny prąd momentu obrotowego dla silników MF, MH, MG jest ograniczany przez wartość Max Torque Current w sterowniku nadrzędnym; maksymalna moc dla silników MS jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA3).

---

### 13. Sterowanie Pozycją Wieloobrotową w Pętli Zamkniętej 2

Sterownik główny wysyła tę komendę, aby sterować pozycją silnika (kąt wieloobrotowy).

1. Wartość sterująca `angleControl` jest typu int32_t, odpowiada rzeczywistej pozycji 0,01stopnia/LSB, tzn. 36000 reprezentuje 360°. Kierunek obrotu silnika jest określany przez różnicę między pozycją docelową a bieżącą.
2. Wartość sterująca `maxSpeed` ogranicza maksymalną prędkość obrotową silnika, jest typu uint16_t, odpowiada rzeczywistej prędkości obrotowej 1dps/LSB, tzn. 360 reprezentuje 360dps.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA4 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | Ograniczenie prędkości - młodszy bajt | DATA[2] = *(uint8_t *)(&maxSpeed) |
| DATA[3] | Ograniczenie prędkości - starszy bajt | DATA[3] = *((uint8_t *)(&maxSpeed)+1) |
| DATA[4] | Sterowanie pozycją - młodszy bajt | DATA[4] = *(uint8_t *)(&angleControl) |
| DATA[5] | Sterowanie pozycją | DATA[5] = *((uint8_t *)(&angleControl)+1) |
| DATA[6] | Sterowanie pozycją | DATA[6] = *((uint8_t *)(&angleControl)+2) |
| DATA[7] | Sterowanie pozycją - starszy bajt | DATA[7] = *((uint8_t *)(&angleControl)+3) |

**Uwagi:**

1. Wartość sterująca `angleControl` w tej komendzie jest ograniczana przez wartość Max Angle w sterowniku nadrzędnym.
2. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.
3. W tym trybie sterowania maksymalny prąd momentu obrotowego dla silników MF, MH, MG jest ograniczany przez wartość Max Torque Current w sterowniku nadrzędnym; maksymalna moc dla silników MS jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika (1 ramka):**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA4).

---

### 14. Sterowanie Pozycją Jednoobrotową w Pętli Zamkniętej 1

Sterownik główny wysyła tę komendę, aby sterować pozycją silnika (kąt jednoobrotowy).

1. Wartość sterująca `spinDirection` ustawia kierunek obrotu silnika, jest typu uint8_t, 0x00 oznacza zgodnie z ruchem wskazówek zegara, 0x01 oznacza przeciwnie do ruchu wskazówek zegara.
2. Wartość sterująca `angleControl` jest typu uint32_t, odpowiada rzeczywistej pozycji 0,01stopnia/LSB, tzn. 36000 reprezentuje 360°.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA5 |
| DATA[1] | Bajt kierunku obrotu | DATA[1] = spinDirection |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Sterowanie pozycją - bajt 1 (bit0 : bit7) | DATA[4] = *(uint8_t *)(&angleControl) |
| DATA[5] | Sterowanie pozycją - bajt 2 (bit8 : bit15) | DATA[5] = *((uint8_t *)(&angleControl)+1) |
| DATA[6] | Sterowanie pozycją - bajt 3 (bit16 : bit23) | DATA[6] = *((uint8_t *)(&angleControl)+2) |
| DATA[7] | Sterowanie pozycją - bajt 4 (bit24: bit31) | DATA[7] = *((uint8_t *)(&angleControl)+3) |

**Uwagi:**

1. W tej komendzie maksymalna prędkość silnika jest ograniczana przez wartość Max Speed w sterowniku nadrzędnym.
2. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.
3. W tym trybie sterowania maksymalny prąd momentu obrotowego dla silników MF, MH, MG jest ograniczany przez wartość Max Torque Current w sterowniku nadrzędnym; maksymalna moc dla silników MS jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA5).

---

### 15. Sterowanie Pozycją Jednoobrotową w Pętli Zamkniętej 2

Sterownik główny wysyła tę komendę, aby sterować pozycją silnika (kąt jednoobrotowy).

1. Wartość sterująca `spinDirection` ustawia kierunek obrotu silnika, jest typu uint8_t, 0x00 oznacza zgodnie z ruchem wskazówek zegara, 0x01 oznacza przeciwnie do ruchu wskazówek zegara.
2. `angleControl` jest typu uint32_t, odpowiada rzeczywistej pozycji 0,01stopnia/LSB, tzn. 36000 reprezentuje 360°.
3. Wartość sterowania prędkością `maxSpeed` ogranicza maksymalną prędkość obrotową silnika, jest typu uint16_t, odpowiada rzeczywistej prędkości obrotowej 1dps/LSB, tzn. 360 reprezentuje 360dps.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA6 |
| DATA[1] | Bajt kierunku obrotu | DATA[1] = spinDirection |
| DATA[2] | Ograniczenie prędkości - bajt 1 (bit0 : bit7) | DATA[2] = *(uint8_t *)(&maxSpeed) |
| DATA[3] | Ograniczenie prędkości - bajt 2 (bit8 : bit15) | DATA[3] = *((uint8_t *)(&maxSpeed)+1) |
| DATA[4] | Sterowanie pozycją - bajt 1 (bit0 : bit7) | DATA[4] = *(uint8_t *)(&angleControl) |
| DATA[5] | Sterowanie pozycją - bajt 2 (bit8 : bit15) | DATA[5] = *((uint8_t *)(&angleControl)+1) |
| DATA[6] | Sterowanie pozycją - bajt 3 (bit16 : bit23) | DATA[6] = *((uint8_t *)(&angleControl)+2) |
| DATA[7] | Sterowanie pozycją - bajt 4 (bit24: bit31) | DATA[7] = *((uint8_t *)(&angleControl)+3) |

**Uwagi:**

1. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.
2. W tym trybie sterowania maksymalny prąd momentu obrotowego dla silników MF, MH, MG jest ograniczany przez wartość Max Torque Current w sterowniku nadrzędnym; maksymalna moc dla silników MS jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika (1 ramka):**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA6).

---

### 16. Sterowanie Przyrostem Pozycji w Pętli Zamkniętej 1

Sterownik główny wysyła tę komendę, aby sterować przyrostem pozycji silnika.

Wartość sterująca `angleIncrement` jest typu int32_t, odpowiada rzeczywistej pozycji 0,01stopnia/LSB, tzn. 36000 reprezentuje 360°. Kierunek obrotu silnika jest określany przez znak tego parametru.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA7 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Sterowanie pozycją - młodszy bajt | DATA[4] = *(uint8_t *)(&angleIncrement) |
| DATA[5] | Sterowanie pozycją | DATA[5] = *((uint8_t *)(&angleIncrement)+1) |
| DATA[6] | Sterowanie pozycją | DATA[6] = *((uint8_t *)(&angleIncrement)+2) |
| DATA[7] | Sterowanie pozycją - starszy bajt | DATA[7] = *((uint8_t *)(&angleIncrement)+3) |

**Uwagi:**

1. W tej komendzie maksymalna prędkość silnika jest ograniczana przez wartość Max Speed w sterowniku nadrzędnym.
2. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.
3. W tym trybie sterowania maksymalny prąd momentu obrotowego dla silników MF, MH, MG jest ograniczany przez wartość Max Torque Current w sterowniku nadrzędnym; maksymalna moc dla silników MS jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA7).

---

### 17. Sterowanie Przyrostem Pozycji w Pętli Zamkniętej 2

Sterownik główny wysyła tę komendę, aby sterować przyrostem pozycji silnika.

1. Wartość sterująca `angleIncrement` jest typu int32_t, odpowiada rzeczywistej pozycji 0,01stopnia/LSB, tzn. 36000 reprezentuje 360°. Kierunek obrotu silnika jest określany przez znak tego parametru.
2. Wartość sterująca `maxSpeed` ogranicza maksymalną prędkość obrotową silnika, jest typu uint32_t, odpowiada rzeczywistej prędkości obrotowej 1dps/LSB, tzn. 360 reprezentuje 360dps.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xA8 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | Ograniczenie prędkości - młodszy bajt | DATA[2] = *(uint8_t *)(&maxSpeed) |
| DATA[3] | Ograniczenie prędkości - starszy bajt | DATA[3] = *((uint8_t *)(&maxSpeed)+1) |
| DATA[4] | Sterowanie pozycją - młodszy bajt | DATA[4] = *(uint8_t *)(&angleIncrement) |
| DATA[5] | Sterowanie pozycją | DATA[5] = *((uint8_t *)(&angleIncrement)+1) |
| DATA[6] | Sterowanie pozycją | DATA[6] = *((uint8_t *)(&angleIncrement)+2) |
| DATA[7] | Sterowanie pozycją - starszy bajt | DATA[7] = *((uint8_t *)(&angleIncrement)+3) |

**Uwagi:**

1. W tym trybie sterowania maksymalne przyspieszenie silnika jest ograniczane przez wartość Max Acceleration w sterowniku nadrzędnym.
2. W tym trybie sterowania maksymalny prąd momentu obrotowego dla silników MF, MH, MG jest ograniczany przez wartość Max Torque Current w sterowniku nadrzędnym; maksymalna moc dla silników MS jest ograniczana przez wartość Max Power w sterowniku nadrzędnym.

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane odpowiedzi silnika są takie same jak w komendzie odczytu statusu silnika 2 (różni się tylko bajt komendy DATA[0], tutaj 0xA8).

---

### 18. Odczyt Parametrów Sterowania

Sterownik główny wysyła tę komendę, aby odczytać bieżące parametry sterowania z pamięci RAM. Odczytywany parametr jest określany przez numer `controlParamID` — patrz Tabela Parametrów Sterowania Silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xC0 |
| DATA[1] | Numer parametru sterowania | DATA[1] = controlParamID |
| DATA[2] | NULL | DATA[2] = 0x00 |
| DATA[3] | NULL | DATA[3] = 0x00 |
| DATA[4] | NULL | DATA[4] = 0x00 |
| DATA[5] | NULL | DATA[5] = 0x00 |
| DATA[6] | NULL | DATA[6] = 0x00 |
| DATA[7] | NULL | DATA[7] = 0x00 |

**Odpowiedź sterownika:**

Dane odpowiedzi sterownika zawierają odczytaną wartość parametru — patrz Tabela Parametrów Sterowania Silnika.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xC0 |
| DATA[1] | Numer parametru sterowania | DATA[1] = controlParamID |
| DATA[2] | Bajt parametru sterowania 1 | Parametr sterowania |
| DATA[3] | Bajt parametru sterowania 2 | Control Parameter |
| DATA[4] | Bajt parametru sterowania 3 | |
| DATA[5] | Bajt parametru sterowania 4 | |
| DATA[6] | Bajt parametru sterowania 5 | |
| DATA[7] | Bajt parametru sterowania 6 | |

---

### 19. Zapis Parametrów Sterowania

Sterownik główny wysyła tę komendę, aby zapisać parametry sterowania do pamięci RAM. Parametry obowiązują natychmiast, ale są tracone po wyłączeniu zasilania. Zapisywane parametry sterowania i ich numery — patrz Tabela Parametrów Sterowania Silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xC1 |
| DATA[1] | Numer parametru sterowania | DATA[1] = controlParamID |
| DATA[2] | Bajt parametru sterowania 1 | Parametr sterowania |
| DATA[3] | Bajt parametru sterowania 2 | Control Parameter |
| DATA[4] | Bajt parametru sterowania 3 | |
| DATA[5] | Bajt parametru sterowania 4 | |
| DATA[6] | Bajt parametru sterowania 5 | |
| DATA[7] | Bajt parametru sterowania 6 | |

**Odpowiedź sterownika:**

Dane odpowiedzi sterownika zawierają zapisaną wartość parametru — patrz Tabela Parametrów Sterowania Silnika.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0xC1 |
| DATA[1] | Numer parametru sterowania | DATA[1] = controlParamID |
| DATA[2] | Bajt parametru sterowania 1 | Parametr sterowania |
| DATA[3] | Bajt parametru sterowania 2 | Control Parameter |
| DATA[4] | Bajt parametru sterowania 3 | |
| DATA[5] | Bajt parametru sterowania 4 | |
| DATA[6] | Bajt parametru sterowania 5 | |
| DATA[7] | Bajt parametru sterowania 6 | |

---

### Tabela Parametrów Sterowania Silnika

#### Regulator PID Pętli Pozycji (Position Loop PID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint16 |
| Zakres danych | 0~2000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x0A |
| DATA[2] | Position Loop Kp Bit 7:0 |
| DATA[3] | Position Loop Kp Bit 15:8 |
| DATA[4] | Position Loop Ki Bit 7:0 |
| DATA[5] | Position Loop Ki Bit 15:8 |
| DATA[6] | Position Loop Kd Bit 7:0 |
| DATA[7] | Position Loop Kd Bit 15:8 |

#### Regulator PID Pętli Prędkości (Speed Loop PID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint16 |
| Zakres danych | 0~2000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x0B |
| DATA[2] | Speed Loop Kp Bit 7:0 |
| DATA[3] | Speed Loop Kp Bit 15:8 |
| DATA[4] | Speed Loop Ki Bit 7:0 |
| DATA[5] | Speed Loop Ki Bit 15:8 |
| DATA[6] | Speed Loop Kd Bit 7:0 |
| DATA[7] | Speed Loop Kd Bit 15:8 |

#### Regulator PID Pętli Prądu (Current Loop PID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint16 |
| Zakres danych | 0~2000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x0C |
| DATA[2] | Current Loop Kp Bit 7:0 |
| DATA[3] | Current Loop Kp Bit 15:8 |
| DATA[4] | Current Loop Ki Bit 7:0 |
| DATA[5] | Current Loop Ki Bit 15:8 |
| DATA[6] | Current Loop Kd Bit 7:0 |
| DATA[7] | Current Loop Kd Bit 15:8 |

#### Ograniczenie Prądu Momentu (Torque Limit)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int16 |
| Zakres danych | 0~850 (seria MS); 0~2000 (seria MF, MHF, MG) |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x1E |
| DATA[2] | 0x00 |
| DATA[3] | 0x00 |
| DATA[4] | Torque Limit Bit 7:0 |
| DATA[5] | Torque Limit Bit 15:8 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Ograniczenie Prędkości (Speed Limit)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~600000 |
| Jednostka | 0,01dps |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x20 |
| DATA[2] | 0x00 |
| DATA[3] | 0x00 |
| DATA[4] | Speed Limit Bit 7:0 |
| DATA[5] | Speed Limit Bit 15:8 |
| DATA[6] | Speed Limit Bit 23:16 |
| DATA[7] | Speed Limit Bit 31:24 |

#### Ograniczenie Kąta (Angle Limit)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~(2³¹ – 1) |
| Jednostka | 0,01deg |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x22 |
| DATA[2] | 0x00 |
| DATA[3] | 0x00 |
| DATA[4] | Angle Limit Bit 7:0 |
| DATA[5] | Angle Limit Bit 15:8 |
| DATA[6] | Angle Limit Bit 23:16 |
| DATA[7] | Angle Limit Bit 31:24 |

#### Nachylenie Prądu (Current Ramp)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~30000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x24 |
| DATA[2] | 0x00 |
| DATA[3] | 0x00 |
| DATA[4] | Current Ramp Bit 7:0 |
| DATA[5] | Current Ramp Bit 15:8 |
| DATA[6] | Current Ramp Bit 23:16 |
| DATA[7] | Current Ramp Bit 31:24 |

#### Nachylenie Prędkości (Speed Ramp)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~600000 |
| Jednostka | 1dps/s |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x26 |
| DATA[2] | 0x00 |
| DATA[3] | 0x00 |
| DATA[4] | Speed Ramp Bit 7:0 |
| DATA[5] | Speed Ramp Bit 15:8 |
| DATA[6] | Speed Ramp Bit 23:16 |
| DATA[7] | Speed Ramp Bit 31:24 |

---

### 20. Odczyt Danych Enkodera Silnika

Sterownik główny wysyła tę komendę, aby odczytać bieżącą pozycję enkodera.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x90 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Ramka danych zawiera następujące parametry:

1. Pozycja enkodera `encoder` (typ uint16_t, zakres wartości enkodera 14-bitowego: 0~16383) — jest to wartość po odjęciu offsetu enkodera od wartości surowej.
2. Surowa pozycja enkodera `encoderRaw` (typ uint16_t, zakres wartości enkodera 14-bitowego: 0~16383).
3. Offset enkodera `encoderOffset` (typ uint16_t, zakres wartości enkodera 14-bitowego: 0~16383) — ten punkt służy jako punkt zerowy kąta silnika.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x90 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | Pozycja enkodera - młodszy bajt | DATA[2] = *(uint8_t *)(&encoder) |
| DATA[3] | Pozycja enkodera - starszy bajt | DATA[3] = *((uint8_t *)(&encoder)+1) |
| DATA[4] | Surowa pozycja enkodera - młodszy bajt | DATA[4] = *(uint8_t *)(&encoderRaw) |
| DATA[5] | Surowa pozycja enkodera - starszy bajt | DATA[5] = *((uint8_t *)(&encoderRaw)+1) |
| DATA[6] | Offset enkodera - młodszy bajt | DATA[6] = *(uint8_t *)(&encoderOffset) |
| DATA[7] | Offset enkodera - starszy bajt | DATA[7] = *((uint8_t *)(&encoderOffset)+1) |

---

### 21. Kalibracja Enkodera

Ta komenda służy do kalibracji enkodera. Kalibrację wystarczy przeprowadzić raz, wartość kalibracyjna jest zapisywana w ROM i jest trwale ważna.

**Uwaga:**

1. Ta komenda zapisuje parametry związane z kalibracją do ROM. Wielokrotny zapis wpłynie na żywotność układu — nie zaleca się częstego używania.
2. Podczas kalibracji silnik powinien być nieobciążony lub lekko obciążony.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x18 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik wykonuje kalibrację po odebraniu komendy. Podczas kalibracji silnik obraca się wielokrotnie przez kilka sekund. Po zakończeniu kalibracji silnik odpowiada sterownikowi głównemu. Ramka odpowiedzi zawiera następujące parametry:

1. Wartość kalibracyjna `AlignValue`, dane typu uint32_t.
2. Wartość współczynnika kalibracji `AlignRatio`, dane typu uint16_t. Ta wartość powinna wynosić około 1000; im bliżej 1000, tym lepszy efekt kalibracji.
3. Bajt stanu kalibracji `AlignState`. Bit 4 tego bajtu wskazuje kolejność faz zidentyfikowaną podczas kalibracji (0: normalna; 1: odwrotna), bit 0 wskazuje, czy kalibracja się powiodła (0: niepowodzenie; 1: powodzenie).

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x18 |
| DATA[1] | Wartość kalibracyjna - bajt 1 | DATA[1] = *(uint8_t *)(&AlignValue) |
| DATA[2] | Wartość kalibracyjna - bajt 2 | DATA[2] = *((uint8_t *)(&AlignValue) + 1) |
| DATA[3] | Wartość kalibracyjna - bajt 3 | DATA[3] = *((uint8_t *)(&AlignValue) + 2) |
| DATA[4] | Wartość kalibracyjna - bajt 4 | DATA[4] = *((uint8_t *)(&AlignValue) + 3) |
| DATA[5] | Współczynnik kalibracji - młodszy bajt | DATA[5] = *(uint8_t *)(&AlignRatio) |
| DATA[6] | Współczynnik kalibracji - starszy bajt | DATA[6] = *((uint8_t *)(&AlignRatio) + 1) |
| DATA[7] | Bajt stanu kalibracji | DATA[7] = AlignState |

---

### 22. Ustawienie Bieżącej Pozycji Jako Punktu Zerowego Silnika (zapis do ROM, trwały)

Ustawia surową wartość enkodera w bieżącej pozycji silnika jako początkowy punkt zerowy po włączeniu zasilania silnika.

**Uwaga:**

1. Ta komenda wymaga ponownego włączenia zasilania, aby zaczęła obowiązywać.
2. Ta komenda zapisuje punkt zerowy do ROM sterownika. Wielokrotny zapis wpłynie na żywotność układu — nie zaleca się częstego używania.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x19 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. W danych `encoderOffset` jest ustawioną wartością offsetu.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x19 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Offset enkodera - młodszy bajt 1 | DATA[4] = *(uint8_t *)(&encoderOffset) |
| DATA[5] | Offset enkodera - bajt 2 | DATA[5] = *((uint8_t *)(&encoderOffset)+1) |
| DATA[6] | Offset enkodera - bajt 3 | DATA[6] = *((uint8_t *)(&encoderOffset)+2) |
| DATA[7] | Offset enkodera - starszy bajt 4 | DATA[7] = *((uint8_t *)(&encoderOffset)+3) |

---

### 23. Odczyt Kąta Wieloobrotowego

Sterownik główny wysyła tę komendę, aby odczytać bieżącą wartość kąta bezwzględnego wieloobrotowego silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x92 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Ramka danych zawiera następujące parametry:

1. Kąt silnika `motorAngle`, dane typu int64_t. Wartości dodatnie oznaczają skumulowany kąt zgodnie z ruchem wskazówek zegara, wartości ujemne oznaczają skumulowany kąt przeciwnie do ruchu wskazówek zegara. Jednostka: 0,01°/LSB.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x92 |
| DATA[1] | Kąt - młodszy bajt 1 | DATA[1] = *(uint8_t *)(&motorAngle) |
| DATA[2] | Kąt - bajt 2 | DATA[2] = *((uint8_t *)(&motorAngle)+1) |
| DATA[3] | Kąt - bajt 3 | DATA[3] = *((uint8_t *)(&motorAngle)+2) |
| DATA[4] | Kąt - bajt 4 | DATA[4] = *((uint8_t *)(&motorAngle)+3) |
| DATA[5] | Kąt - bajt 5 | DATA[5] = *((uint8_t *)(&motorAngle)+4) |
| DATA[6] | Kąt - bajt 6 | DATA[6] = *((uint8_t *)(&motorAngle)+5) |
| DATA[7] | Kąt - bajt 7 | DATA[7] = *((uint8_t *)(&motorAngle)+6) |

---

### 24. Odczyt Kąta Jednoobrotowego

Sterownik główny wysyła tę komendę, aby odczytać bieżący kąt jednoobrotowy silnika.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x94 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Ramka danych zawiera następujące parametry:

1. Kąt jednoobrotowy silnika `circleAngle`, dane typu uint32_t. Punktem początkowym jest punkt zerowy enkodera, wartość zwiększa się zgodnie z ruchem wskazówek zegara. Po ponownym osiągnięciu punktu zerowego wartość wraca do 0. Jednostka: 0,01°/LSB. Zakres wartości: 0~36000*przełożenie-1.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x94 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | Kąt jednoobrotowy - młodszy bajt 1 | DATA[4] = *(uint8_t *)(&circleAngle) |
| DATA[5] | Kąt jednoobrotowy - bajt 2 | DATA[5] = *((uint8_t *)(&circleAngle)+1) |
| DATA[6] | Kąt jednoobrotowy - bajt 3 | DATA[6] = *((uint8_t *)(&circleAngle)+2) |
| DATA[7] | Kąt jednoobrotowy - starszy bajt 4 | DATA[7] = *((uint8_t *)(&circleAngle)+3) |

---

### 25. Ustawienie Bieżącej Pozycji Jako Punktu Zerowego (zapis do RAM)

Sterownik główny wysyła tę komendę, aby ustawić bieżącą pozycję silnika jako punkt zerowy zapisany w RAM. Po wysłaniu komendy silnik przejdzie w stan zatrzymania (motor stop). Ten punkt zerowy traci ważność po ponownym włączeniu zasilania.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x95 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Silnik odpowiada sterownikowi głównemu po odebraniu komendy. Dane ramki są takie same jak ramka wysłana przez sterownik główny.

---

### 26. Odczyt Parametrów Konfiguracyjnych

Sterownik główny wysyła tę komendę, aby odczytać parametry konfiguracyjne. Odczytywane parametry konfiguracyjne — patrz Tabela Parametrów Konfiguracyjnych.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x40 |
| DATA[1] | Bajt parametru 1 | DATA[1] |
| DATA[2] | Bajt parametru 2 | DATA[2] |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika:**

Dane odpowiedzi sterownika zawierają odczytane wartości parametrów konfiguracyjnych — patrz Tabela Parametrów Konfiguracyjnych.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x40 |
| DATA[1] | Bajt parametru 1 | DATA[1] |
| DATA[2] | Bajt parametru 2 | DATA[2] |
| DATA[3] | Bajt parametru 3 | DATA[3] |
| DATA[4] | Bajt parametru 4 | DATA[4] |
| DATA[5] | Bajt parametru 5 | DATA[5] |
| DATA[6] | Bajt parametru 6 | DATA[6] |
| DATA[7] | Bajt parametru 7 | DATA[7] |

---

### 27. Zapis Parametrów Konfiguracyjnych

Sterownik główny wysyła tę komendę, aby zapisać parametry konfiguracyjne. Zapisywane parametry konfiguracyjne — patrz Tabela Parametrów Konfiguracyjnych.

**Uwaga:**

a. Po zapisaniu parametrów konfiguracyjnych należy wysłać komendę zapisania parametrów konfiguracyjnych, aby zapisać dane do ROM.
b. Można zapisać wiele parametrów konfiguracyjnych przed wysłaniem komendy zapisania parametrów konfiguracyjnych.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x42 |
| DATA[1] | Bajt parametru 1 | DATA[1] |
| DATA[2] | Bajt parametru 2 | DATA[2] |
| DATA[3] | Bajt parametru 3 | DATA[3] |
| DATA[4] | Bajt parametru 4 | DATA[4] |
| DATA[5] | Bajt parametru 5 | DATA[5] |
| DATA[6] | Bajt parametru 6 | DATA[6] |
| DATA[7] | Bajt parametru 7 | DATA[7] |

**Odpowiedź sterownika:**

Dane odpowiedzi sterownika zawierają zapisane wartości parametrów — patrz Tabela Parametrów Konfiguracyjnych.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x42 |
| DATA[1] | Bajt parametru 1 | DATA[1] |
| DATA[2] | Bajt parametru 2 | DATA[2] |
| DATA[3] | Bajt parametru 3 | DATA[3] |
| DATA[4] | Bajt parametru 4 | DATA[4] |
| DATA[5] | Bajt parametru 5 | DATA[5] |
| DATA[6] | Bajt parametru 6 | DATA[6] |
| DATA[7] | Bajt parametru 7 | DATA[7] |

---

### Tabela Parametrów Konfiguracyjnych

#### Pojedyncze Komendy Parametrów (One Parameter Command)

#### ID Sterownika (Driver ID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint8 |
| Zakres danych | 0~32 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0x0A |
| DATA[3] | 0x00 |
| DATA[4] | Driver ID Bit 7:0 |
| DATA[5] | 0x00 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Typ Magistrali (Bus Type)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint8 |
| Zakres danych | 0~2 |

Wartości:
- 0: None
- 1: RS485
- 2: CAN

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0x0B |
| DATA[3] | 0x00 |
| DATA[4] | Bus Type Bit 7:0 |
| DATA[5] | 0x00 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Prędkość Transmisji RS485 (RS485 Baudrate)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint8 |
| Zakres danych | 0~10 |

Wartości:
- 0: 9600bps
- 1: 19200bps
- 2: 38400bps
- 3: 57600bps
- 4: 115200bps
- 5: 230400bps
- 6: 460800bps
- 7: 921600bps
- 8: 1000000bps
- 9: 2000000bps
- 10: 4000000bps

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0x0C |
| DATA[3] | 0x00 |
| DATA[4] | RS485 Baudrate Bit 7:0 |
| DATA[5] | 0x00 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Prędkość Transmisji CAN (CAN Baudrate)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint8 |
| Zakres danych | 0~4 |

Wartości:
- 0: 100Kbps
- 1: 125Kbps
- 2: 250Kbps
- 3: 500Kbps
- 4: 1Mbps

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0x0D |
| DATA[3] | 0x00 |
| DATA[4] | CAN Baudrate Bit 7:0 |
| DATA[5] | 0x00 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Moc Maksymalna (Max Power)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int16 |
| Zakres danych | 0~850 (seria MS); 0~2000 (seria MF, MHF, MG) |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0xE0 |
| DATA[3] | 0x00 |
| DATA[4] | Max Power Bit 7:0 |
| DATA[5] | Max Power Bit 15:8 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Prędkość Maksymalna (Max Speed)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~600000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0xE2 |
| DATA[3] | 0x00 |
| DATA[4] | Max Speed Bit 7:0 |
| DATA[5] | Max Speed Bit 15:8 |
| DATA[6] | Max Speed Bit 23:16 |
| DATA[7] | Max Speed Bit 31:24 |

#### Kąt Maksymalny (Max Angle)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~(2³¹ – 1) |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0xE4 |
| DATA[3] | 0x00 |
| DATA[4] | Max Angle Bit 7:0 |
| DATA[5] | Max Angle Bit 15:8 |
| DATA[6] | Max Angle Bit 23:16 |
| DATA[7] | Max Angle Bit 31:24 |

#### Nachylenie Prądu (Current Ramp)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int16 |
| Zakres danych | 0~30000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0xEA |
| DATA[3] | 0x00 |
| DATA[4] | Current Ramp Bit 7:0 |
| DATA[5] | Current Ramp Bit 15:8 |
| DATA[6] | 0x00 |
| DATA[7] | 0x00 |

#### Nachylenie Prędkości (Speed Ramp)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | int32 |
| Zakres danych | 0~600000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0x05 |
| DATA[2] | 0xEC |
| DATA[3] | 0x00 |
| DATA[4] | Speed Ramp Bit 7:0 |
| DATA[5] | Speed Ramp Bit 15:8 |
| DATA[6] | Speed Ramp Bit 23:16 |
| DATA[7] | Speed Ramp Bit 31:24 |

---

#### Komendy Wielu Parametrów (Multiple Parameter Command)

#### Regulator PID Pętli Pozycji (Position Loop PID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint16 |
| Zakres danych | 0~2000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0xA0 |
| DATA[2] | Position Loop Kp Bit 7:0 |
| DATA[3] | Position Loop Kp Bit 15:8 |
| DATA[4] | Position Loop Ki Bit 7:0 |
| DATA[5] | Position Loop Ki Bit 15:8 |
| DATA[6] | Position Loop Kd Bit 7:0 |
| DATA[7] | Position Loop Kd Bit 15:8 |

#### Regulator PID Pętli Prędkości (Speed Loop PID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint16 |
| Zakres danych | 0~2000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0xA4 |
| DATA[2] | Speed Loop Kp Bit 7:0 |
| DATA[3] | Speed Loop Kp Bit 15:8 |
| DATA[4] | Speed Loop Ki Bit 7:0 |
| DATA[5] | Speed Loop Ki Bit 15:8 |
| DATA[6] | Speed Loop Kd Bit 7:0 |
| DATA[7] | Speed Loop Kd Bit 15:8 |

#### Regulator PID Pętli Prądu (Current Loop PID)

| Właściwość | Wartość |
|------------|---------|
| Typ danych | uint16 |
| Zakres danych | 0~2000 |

| Pole danych | Znaczenie |
|-------------|-----------|
| DATA[1] | 0xA8 |
| DATA[2] | Current Loop Kp Bit 7:0 |
| DATA[3] | Current Loop Kp Bit 15:8 |
| DATA[4] | Current Loop Ki Bit 7:0 |
| DATA[5] | Current Loop Ki Bit 15:8 |
| DATA[6] | Current Loop Kd Bit 7:0 |
| DATA[7] | Current Loop Kd Bit 15:8 |

---

### 28. Zapisanie Parametrów Konfiguracyjnych

Sterownik główny wysyła tę komendę, aby zapisać parametry konfiguracyjne do ROM. Po zapisaniu należy ponownie włączyć zasilanie lub wysłać komendę restartu.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x44 |
| DATA[1] | Wartość stała | DATA[1] = 0x05 |
| DATA[2] | Wartość stała | DATA[2] = 0xFA |
| DATA[3] | NULL | DATA[3] = 0x00 |
| DATA[4] | NULL | DATA[4] = 0x00 |
| DATA[5] | NULL | DATA[5] = 0x00 |
| DATA[6] | NULL | DATA[6] = 0x00 |
| DATA[7] | NULL | DATA[7] = 0x00 |

**Odpowiedź sterownika:**

Dane odpowiedzi sterownika zawierają stan zapisania.

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x44 |
| DATA[1] | Wartość stała | DATA[1] = 0x05 |
| DATA[2] | Znacznik zapisu | DATA[2] = 0x01 — zapis parametrów udany |
| | | DATA[2] = 0x00 — zapis parametrów nieudany |
| DATA[3] | NULL | DATA[3] = 0x00 |
| DATA[4] | NULL | DATA[4] = 0x00 |
| DATA[5] | NULL | DATA[5] = 0x00 |
| DATA[6] | NULL | DATA[6] = 0x00 |
| DATA[7] | NULL | DATA[7] = 0x00 |

---

### 29. Restart Silnika

Restartuje silnik, co jest równoważne ponownemu włączeniu zasilania silnika. Po odebraniu tej komendy silnik nie odpowiada danymi.

**Ramka komendy:**

| Pole danych | Opis | Dane |
|-------------|------|------|
| DATA[0] | Bajt komendy | 0x07 |
| DATA[1] | NULL | 0x00 |
| DATA[2] | NULL | 0x00 |
| DATA[3] | NULL | 0x00 |
| DATA[4] | NULL | 0x00 |
| DATA[5] | NULL | 0x00 |
| DATA[6] | NULL | 0x00 |
| DATA[7] | NULL | 0x00 |

**Odpowiedź sterownika (1 ramka):**

Brak.

---

> **Źródło:** Shanghai LingKong Technology (上海瓴控科技) — Motor CAN Bus Communication Protocol V2.36  
> **Tłumaczenie na język polski na potrzeby projektu Astro Mount Control.**
