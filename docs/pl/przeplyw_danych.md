# Diagramy Przepływu Danych Systemu

Ten dokument opisuje przepływ danych między wszystkimi głównymi komponentami Astronomicznego Kontrolera Montażu przy użyciu diagramów Mermaid.js.

## 1. Architektura Systemu Wysokiego Poziomu

```mermaid
flowchart TD
    classDef ext fill:#2196F3,stroke:#1565c0,stroke-width:2px,color:#fff
    classDef core fill:#4CAF50,stroke:#2e7d32,stroke-width:2px,color:#fff
    classDef model fill:#795548,stroke:#4e342e,stroke-width:2px,color:#fff
    classDef hal fill:#FF5722,stroke:#bf360c,stroke-width:2px,color:#fff
    classDef mock fill:#FF9800,stroke:#e65100,stroke-width:2px,color:#fff
    classDef real fill:#F44336,stroke:#b71c1c,stroke-width:2px,color:#fff
    classDef hw fill:#607D8B,stroke:#37474f,stroke-width:2px,color:#fff
    classDef data fill:#9E9E9E,stroke:#616161,stroke-width:2px,color:#fff

    subgraph External["🌐 Systemy Zewnętrzne"]
        GUI["💻 Klient GUI<br/>(gRPC)"]:::ext
        SCRIPT["🤖 Skrypt/Automatyzacja<br/>(gRPC)"]:::ext
        GUIDER["📡 Gajder<br/>(TCP socket)"]:::ext
        OBS_DB["🗄️ Baza Obiektów<br/>(gRPC)"]:::ext
    end

    subgraph Core["⚙️ Rdzeń Kontrolera Montażu"]
        API["📨 Serwer API gRPC<br/>src/api/grpc_server.cpp"]:::core
        MC["🎯 Kontroler Montażu<br/>src/controllers/mount_controller.cpp"]:::core
        
        subgraph Models["🧮 Modele Obliczeniowe"]
            TP["📐 Model TPOINT<br/>src/models/tpoint_model.cpp"]:::model
            KF["🔢 Filtr Kalmana<br/>src/models/kalman_filter.cpp"]:::model
            ET["🛰️ Tracker Efemeryd<br/>src/models/ephemeris_tracker.cpp"]:::model
            AC["🌌 Obliczenia Astronomiczne<br/>src/core/astronomical_calculations.cpp"]:::model
        end
        
        CFG["⚙️ Konfiguracja<br/>src/config/configuration.cpp"]:::data
        LOG["📋 Logger<br/>src/logging/logger.cpp"]:::data
    end

    subgraph HAL["🔧 Warstwa Abstrakcji Sprzętu"]
        CAN["🔌 ICanOpenInterface<br/>include/controllers/icanopen_interface.h"]:::hal
        MOCK["🧪 Mock CANopen<br/>(tryb symulacji)"]:::mock
        REAL["🔴 Prawdziwy CANopen<br/>(CANopenSocket/EDSS)"]:::real
    end

    subgraph Hardware["🖥️ Sprzęt Fizyczny"]
        DRV1["⚡ Napęd Osi 1<br/>(RA/Azymut)"]:::hw
        DRV2["⚡ Napęd Osi 2<br/>(Deklinacja/Alt)"]:::hw
        ENC1["📏 Enkoder Osi 1"]:::hw
        ENC2["📏 Enkoder Osi 2"]:::hw
        DEROT["🔄 Derotator"]:::hw
    end

    GUI -->|"slewToEquatorial()<br/>startTracking()<br/>park()"| API
    GUI -->|"getStatus()<br/>getTPointParameters()"| API
    SCRIPT -->|"komendy automatyzacji"| API
    OBS_DB -->|"dane efemeryd"| API
    
    API -->|"dyspozycja komend"| MC
    MC -->|"aktualizacje statusu"| API
    
    MC -->|"transformacje współrzędnych"| AC
    MC -->|"błędy wskazywania"| TP
    MC -->|"estymacja stanu"| KF
    MC -->|"śledzenie obiektów ruchomych"| ET
    
    MC -->|"enableDrive()<br/>setPositionTarget()<br/>setVelocityTarget()"| CAN
    CAN -->|"getDriveStatus()<br/>getPositionData()<br/>getEncoderData()"| MC
    
    CAN -->|"mock CAN bus"| MOCK
    CAN -->|"prawdziwy CAN bus"| REAL
    
    MOCK -->|"symulowane sprzężenie"| CAN
    REAL -->|"ramki CAN<br/>(PDO/SDO/NMT)"| DRV1
    REAL -->|"ramki CAN"| DRV2
    DRV1 -->|"sprzężenie enkodera"| ENC1
    DRV2 -->|"sprzężenie enkodera"| ENC2
    MC -->|"rotacja pola"| DEROT
    
    GUIDER -->|"applyGuiderCorrection()<br/>(TCP socket)"| MC
    
    CFG -->|"config.json"| MC
    MC -->|"zdarzenia logowania"| LOG
```

## 2. Przepływ Sterowania w Pętli Nawigacji

```mermaid
sequenceDiagram
    participant Client as Klient gRPC
    participant API as Serwer API gRPC
    participant MC as Kontroler Montażu
    participant CAN as Interfejs CANopen
    participant HW as Sprzęt
    
    Client->>API: slewToEquatorial(ra, dec)
    API->>MC: komenda + współrzędne
    MC->>MC: validateState()
    
    alt Stan Nieprawidłowy
        MC-->>API: return false
        API-->>Client: odpowiedź błędu
    else Stan Prawidłowy
        MC->>MC: convertToMountCoords(ra, dec)
        MC->>AC: equatorialToHorizontal() / haToEquatorial()
        AC-->>MC: (axis1_target, axis2_target)
        
        MC->>MC: applyTPointCorrections()
        MC->>TP: applyCorrections()
        TP-->>MC: (corrected_ha, corrected_dec)
        
        MC->>MC: launchAsyncSlewThread()
        
        par Pętla Nawigacji (wątek asynchroniczny)
            loop co POLL_MS (100ms)
                CAN->>HW: setPositionTarget(axis, pos, vel)
                HW-->>CAN: potwierdzenie napędu
                CAN->>HW: getPositionData()
                HW-->>CAN: actual_position, actual_velocity
                CAN-->>MC: sprzężenie pozycji
                
                MC->>MC: computeFollowingError()
                alt Błąd > Tolerancja
                    MC->>MC: adjustVelocityProfile()
                else Błąd < Tolerancja
                    MC->>MC: markAxisReached()
                end
            end
        end
        
        MC-->>API: return true (komenda przyjęta)
        API-->>Client: potwierdzenie
        
        Note over MC: Nawigacja kontynuuje w tle
        MC->>MC: obie osie osiągnęły cel?
        MC->>MC: setState(IDLE)
        MC->>Client: callback statusu (opcjonalny)
    end
```

## 3. Pętla Śledzenia (Tracking)

```mermaid
sequenceDiagram
    participant Client as Klient gRPC
    participant MC as Kontroler Montażu
    participant AC as Obliczenia Astron.
    participant CAN as CANopen
    participant GUID as Gajder
    
    Client->>MC: startTracking(ra, dec, SIDEREAL)
    MC->>MC: setState(TRACKING)
    MC->>MC: launchAsyncTrackingThread()
    
    loop co POLL_MS
        MC->>AC: calculateApparentPlace()
        AC-->>MC: apparent (ra, dec)
        
        MC->>MC: computeVelocityCommands()
        MC->>CAN: setVelocityTarget(0, ra_rate)
        MC->>CAN: setVelocityTarget(1, dec_rate)
        CAN-->>MC: status napędu
        
        MC->>MC: updateEncoders()
        MC->>KF: update(position, velocity)
        KF-->>MC: estymowany stan
        
        alt Gajder Aktywny
            GUID->>MC: applyGuiderCorrection(delta_ra, delta_dec)
            MC->>MC: clampCorrection()
            MC->>MC: adjustTrackingRates()
        end
        
        MC->>MC: checkTrackingError()
        alt Błąd > Próg
            MC->>MC: performRecovery()
        end
        
        MC->>MC: updateFieldRotation()
        MC->>DEROT: setRotationRate()
    end
```

## 4. Przepływ Komunikacji CANopen

```mermaid
sequenceDiagram
    participant MC as Kontroler Montażu
    participant CAN as ICanOpenInterface
    participant MOCK as Mock CANopen
    participant REAL as Real CANopen
    participant BUS as Mag. CAN
    participant DRIVE as Napęd Serwo
    
    MC->>CAN: initialize(config)
    
    alt config.interface == "" or "mock"
        CAN->>MOCK: useMockImplementation()
        MOCK-->>CAN: MockInterface utworzony
    else config.interface == "can0" or "vcan0"
        CAN->>REAL: useRealImplementation()
        REAL->>BUS: connect(socketCAN)
        BUS-->>REAL: połączono
    end
    
    CAN-->>MC: interfejs gotowy
    
    MC->>CAN: configureDrive(0, config_string)
    
    CAN->>DRIVE: SDO: 0x6060 (Tryb Pracy)
    DRIVE-->>CAN: OK
    CAN->>DRIVE: SDO: 0x607A (Pozycja Docelowa)  
    DRIVE-->>CAN: OK
    CAN->>DRIVE: SDO: 0x6081 (Prędkość Profilu)
    DRIVE-->>CAN: OK
    
    MC->>CAN: enableDrive(0)
    CAN->>DRIVE: NMT: Start Remote Node
    CAN->>DRIVE: SDO: 0x6040 (Słowo Sterujące) = 0x0006
    CAN->>DRIVE: SDO: 0x6040 (Słowo Sterujące) = 0x0007
    CAN->>DRIVE: SDO: 0x6040 (Słowo Sterujące) = 0x000F
    DRIVE-->>CAN: Statusword = 0x0037 (Operacja Włączona)
    
    loop Cykl PDO (co okres SYNC)
        CAN->>DRIVE: RPDO: Pozycja/Prędkość Docelowa
        DRIVE-->>CAN: TPDO: Rzeczywista Pozycja/Status
        CAN-->>MC: callback PositionData
        CAN-->>MC: callback DriveStatus
    end
    
    MC->>CAN: stopAxis(0)
    CAN->>DRIVE: SDO: 0x6040 (Słowo Sterujące) = 0x0002
    DRIVE-->>CAN: Szybkie zatrzymanie wykonane
```

## 5. Przepływ Śledzenia Efemeryd

```mermaid
sequenceDiagram
    participant Client as Klient gRPC
    participle ODB as Baza Obiektów
    participant API as Serwer API gRPC
    participant MC as Kontroler Montażu  
    participle EPM as EphemerisModel
    participant EPT as EphemerisTracker
    participle EPI as EphemerisInterpolator
    
    Client->>ODB: queryEphemeris(object_id, time_range)
    ODB-->>Client: EphemerisData (punkty[])
    
    Client->>API: uploadEphemeris(data)
    API->>MC: uploadEphemeris(object_id, points)
    
    MC->>EPM: EphemerisModel(data, config)
    EPM->>EPI: EphemerisInterpolator(points, order)
    EPI-->>EPM: interpolator gotowy
    EPM-->>MC: model gotowy
    
    MC->>EPT: EphemerisTracker(model, lat, lon, alt)
    EPT-->>MC: tracker utworzony
    
    Client->>API: startEphemerisTracking(object_id, time)
    API->>MC: startEphemerisTracking()
    
    MC->>EPT: startTracking(start_time, config)
    
    loop Pętla Śledzenia (10 Hz)
        EPT->>EPM: getApparentPosition(time)
        EPM->>EPI: getPositionAtTime(time)
        
        alt czas w zakresie efemeryd
            EPI-->>EPM: (ra, dec, ra_rate, dec_rate)
            EPM->>EPM: applyEarthRotation()
            EPM->>EPM: applyAtmosphericRefraction()
            EPM->>EPM: applyTPointCorrections()
            EPM-->>EPT: apparent_position
        else czas poza zakresem + ekstrapolacja
            EPI->>EPI: predictPosition(time, max_extrap)
            EPI-->>EPM: predicted_position
            EPM-->>EPT: przewidziane + korekcje
        end
        
        EPT->>MC: updateTargetPosition(ra, dec, rates)
        MC->>CAN: setVelocityTarget(axes, rates)
    end
    
    Client->>API: stopEphemerisTracking(tracker_id)
    API->>MC: stopEphemerisTracking()
    MC->>EPT: stopTracking()
    EPT-->>MC: śledzenie zatrzymane, statystyki
    MC-->>API: sukces
    API-->>Client: potwierdzenie
```

## 6. Przepływ Korekcji Gajdera

```mermaid
flowchart LR
    subgraph External
        G[Oprogramowanie Gajdera<br/>PHD2 / Ekos / ASCOM]
    end
    
    subgraph MountController
        direction TB
        GC[Połączenie Gajdera<br/>TCP socket]
        GR[Regulator Gajdera]
        TR[Pętla Śledzenia<br/>Kalkulator prędkości]
        CL[Ogranicznik + Agresywność]
        MC[Główny Kontroler]
    end
    
    subgraph HAL
        CAN2[Interfejs CANopen]
    end
    
    subgraph Hardware
        D1[Napęd Osi 1]
        D2[Napęd Osi 2]
    end
    
    G -->|"połączenie TCP<br/>socket://ip:port"| GC
    GC -->|"korekcja<br/>(ra_arcsec, dec_arcsec)"| GR
    
    GR -->|"surowa korekcja"| CL
    CL -->|"ogranicz do ±max_correction<br/>zastosuj współczynnik agresji"| TR
    
    MC -->|"prędkość gwiazdowa/słoneczna/księżycowa"| TR
    TR -->|"prędkość końcowa = bazowa + Δ"| MC
    
    MC -->|"setVelocityTarget(axis, rate)"| CAN2
    CAN2 -->|"tryb prędkości"| D1
    CAN2 -->|"tryb prędkości"| D2
    
    D1 -->|"rzeczywista pozycja"| CAN2
    D2 -->|"rzeczywista pozycja"| CAN2
    CAN2 -->|"getPositionData()"| MC
    MC -->|"aktualizacja statusu<br/>(guider_active, tracking_error)"| GC
    
    style G fill:#FF9800,color:#fff
    style GC fill:#4CAF50,color:#fff
    style GR fill:#4CAF50,color:#fff
    style CL fill:#FF5722,color:#fff
    style TR fill:#4CAF50,color:#fff
    style MC fill:#4CAF50,color:#fff
    style CAN2 fill:#FF5722,color:#fff
    style D1 fill:#607D8B,color:#fff
    style D2 fill:#607D8B,color:#fff
```

## 7. Przepływ Kalibracji TPOINT

```mermaid
flowchart TB
    subgraph UserInput["Komendy Użytkownika/Klienta"]
        direction TB
        A1["addTPointMeasurement()<br/>zaobserwowane (RA, Dec)<br/>oczekiwane (RA, Dec)<br/>montaż (HA, Dec)<br/>otoczenie (T, P, H)"]
        A2["runTPointCalibration()"]
        A3["getTPointParameters()"]
    end
    
    subgraph Storage["Magazyn Pomiarów"]
        B["tpoint_measurements[]<br/>std::vector<Measurement>"]
    end
    
    subgraph Solver["Rozwiązywacz TPOINT"]
        direction TB
        C1["Zbuduj wektor reszt<br/>b = observed - expected"]
        C2["Zbuduj macierz projektu<br/>A = pochodne cząstkowe<br/>dla włączonych członów"]
        C3["Rozwiązanie najmniejszych kwadratów<br/>p = QR.solve(Aᵀ · b)"]
        C4["Miary jakości<br/>χ², RMS, DOF"]
    end
    
    subgraph Correction["Aplikacja Korekcji"]
        direction TB
        D1["applyCorrections()<br/>ΔRA = IA + CA·cos(HA) + ..."]
        D2["RA_corrected = RA - ΔRA<br/>Dec_corrected = Dec - ΔDec"]
    end
    
    subgraph Prediction["Predykcja Pozycji Montażu"]
        direction TB
        E1["predictMountPosition()<br/>Rozwiązywacz Newtona-Raphsona"]
        E2["Jacobian przez różnice centralne<br/>J = [∂f/∂HA, ∂f/∂Dec]"]
        E3["Iteruj do zbieżności<br/>||Δx|| < ε"]
    end
    
    A1 --> B
    A2 --> B
    B --> C1
    C1 --> C2
    C2 --> C3
    C3 --> C4
    C4 -->|"p (parametry TPOINT)"| D1
    D1 --> D2
    D2 -->|"używane w śledzeniu"| Prediction
    Prediction -->|"wymagane kąty montażu"| CANopen
    
    A3 --> C4
    C4 -->|"parametry JSON"| Klient
    
    style UserInput fill:#2196F3,color:#fff
    style Storage fill:#9E9E9E,color:#fff
    style Solver fill:#795548,color:#fff
    style Correction fill:#4CAF50,color:#fff
    style Prediction fill:#4CAF50,color:#fff
```

## 8. Przepływ Ładowania Konfiguracji

```mermaid
flowchart TD
    FILE["config/default.json<br/>Plik konfiguracyjny JSON"]
    
    CFG["Moduł Konfiguracji<br/>src/config/configuration.cpp"]
    
    subgraph ConfigSections["Sekcje Konfiguracji"]
        MOUNT["Konfiguracja Montażu<br/>• typ (equatorial/altaz)<br/>• szerokość, długość geogr.<br/>• mount_height<br/>• max_slew/tracking_rate<br/>• slew/tracking_acceleration<br/>• meridian_flip_enabled/delay<br/>• soft_limits (min/max/warning/decel)<br/>• park_position<br/>• axis_physical_parameters"]
        CAN["Konfiguracja CANopen<br/>• nazwa interfejsu<br/>• ID węzła<br/>• prędkość transmisji<br/>• sync interval"]
        TEL_CFG["Konfiguracja Teleskopu<br/>• focal_length, aperture<br/>• camera_model, pixel_size<br/>• wymiary sensora"]
        KF_CFG["Konfiguracja Filtra Kalmana<br/>• process_noise<br/>• measurement_noise<br/>• adaptive_q/r<br/>• innovation_threshold"]
        TP_CFG["Konfiguracja TPOINT<br/>• enabled_terms<br/>• min_measurements<br/>• max_residual"]
        DEROT_CFG["Konfiguracja Derotatora<br/>• type (none/canopen/stepper)<br/>• gear_ratio, max_speed<br/>• acceleration, backlash<br/>• calibration_table"]
        FR_CFG["Konfiguracja Pola Rotacyjnego<br/>• enabled<br/>• compensation_mode<br/>• max_rate<br/>• PID gains"]
        HAL_CFG["Konfiguracja HAL<br/>• type (canopen/simulated)<br/>• parametry CAN<br/>• watchdog timeout<br/>• PDO update rate"]
        NET["Konfiguracja Sieci<br/>• adres gRPC<br/>• port gRPC<br/>• SSL settings"]
        GUID_CFG["Konfiguracja Gajdera<br/>• enabled<br/>• connection_string<br/>• max_correction<br/>• aggression, exposure"]
        LOG_CFG["Konfiguracja Logowania<br/>• level, directory<br/>• rotation_days<br/>• max_file_size"]
    end
    
    subgraph MainInit["Inicjalizacja main()"]
        MAIN["main.cpp"]
        CONTROLLER["MountController<br/>utworzony"]
        GRPC["Serwer gRPC<br/>uruchomiony"]
    end
    
    FILE -->|"loadFromFile()"| CFG
    
    CFG --> MOUNT
    CFG --> CAN
    CFG --> TEL_CFG
    CFG --> KF_CFG
    CFG --> TP_CFG
    CFG --> DEROT_CFG
    CFG --> FR_CFG
    CFG --> HAL_CFG
    CFG --> NET
    CFG --> GUID_CFG
    CFG --> LOG_CFG
    
    MOUNT -->|"controller_config"| CONTROLLER
    CAN -->|"canopen_interface, node_id"| CONTROLLER
    TEL_CFG -->|"telescope focal_length"| CONTROLLER
    KF_CFG -->|"process_noise, measurement_noise"| CONTROLLER
    TP_CFG -->|"enabled_terms, min_measurements"| CONTROLLER
    DEROT_CFG -->|"parametry derotatora"| CONTROLLER
    FR_CFG -->|"parametry pola rotacyjnego"| CONTROLLER
    HAL_CFG -->|"hal_type, can config"| CONTROLLER
    GUID_CFG -->|"ustawienia gajdera"| CONTROLLER
    NET -->|"grpc_address, grpc_port"| GRPC
    LOG_CFG -->|"directory, level"| MAIN
    
    CONTROLLER -->|"initialize()"| MAIN
    GRPC -->|"start()"| MAIN
    MAIN -->|"pętla główna (100ms uśpienie)"| MAIN
    
    style FILE fill:#FF9800,color:#fff
    style CFG fill:#9E9E9E,color:#fff
    style MOUNT fill:#607D8B,color:#fff
    style CAN fill:#607D8B,color:#fff
    style TEL_CFG fill:#607D8B,color:#fff
    style KF_CFG fill:#607D8B,color:#fff
    style TP_CFG fill:#607D8B,color:#fff
    style DEROT_CFG fill:#607D8B,color:#fff
    style FR_CFG fill:#607D8B,color:#fff
    style HAL_CFG fill:#607D8B,color:#fff
    style NET fill:#607D8B,color:#fff
    style GUID_CFG fill:#607D8B,color:#fff
    style LOG_CFG fill:#607D8B,color:#fff
    style MAIN fill:#4CAF50,color:#fff
    style CONTROLLER fill:#4CAF50,color:#fff
    style GRPC fill:#4CAF50,color:#fff
```

## 9. Maszyna Stanów Parkowania/Odparkowania

```mermaid
stateDiagram-v2
    [*] --> UNINITIALIZED
    UNINITIALIZED --> IDLE: initialize()
    
    IDLE --> SLEWING: slewToEquatorial() / slewToHorizontal()
    SLEWING --> IDLE: cel osiągnięty / stop()
    SLEWING --> ERROR: awaria
    
    IDLE --> TRACKING: startTracking()
    TRACKING --> IDLE: stop()
    TRACKING --> ERROR: błąd śledzenia
    
    IDLE --> PARKING: park()
    PARKING --> PARKED: obie osie w (0,0)
    PARKING --> IDLE: timeout / stop()
    PARKED --> IDLE: unpark()
    
    ERROR --> IDLE: clearErrors()
    
    IDLE --> BOOTSTRAP: addBootstrapMeasurement()
    BOOTSTRAP --> IDLE: runBootstrapCalibration()
    
    note right of PARKING
        Wątek asynchroniczny przesuwa osie do (0,0)
        CANopen: ramki CAN → napędy
        Symulacja: krokowy ruch
        Timeout: PARK_TIMEOUT_MS
    end note
    
    note right of TRACKING
        Pętla śledzenia aktualizuje prędkości
        Korekcje gajdera stosowane
        Rotacja pola obliczana
        Filtr Kalmana aktywny
    end note
    
    note right of SLEWING
        Asynchroniczny wątek nawigacji
        Komenda pozycji → CANopen
        Odpytywanie do osiągnięcia celu
        Oderwany wątek; brak join
    end note
```

## 10. Przepływ Bazy Obiektów Astronomicznych

```mermaid
flowchart LR
    subgraph Services["Usługi Zewnętrzne"]
        GRPC_CLIENT["Klient gRPC<br/>(Skrypt)"]
        DB_GUI["GUI Administracyjne Bazy"]
    end
    
    subgraph DBService["Usługa Bazy Obiektów"]
        ODS["ObjectDatabaseService<br/>db/src/object_database_service.cpp"]
        PROTO["Definicja Protobuf<br/>db/proto/object_database.proto"]
    end
    
    subgraph Storage["Magazyn Danych"]
        SQLITE["Baza SQLite<br/>astronomy_objects.db"]
    end
    
    subgraph DataModels["Modele Danych"]
        EPHEMERIS["EphemerisData<br/>• object_id<br/>• object_name<br/>• punkty[]<br/>• epoch<br/>• reference_frame"]
        OBJECTS["AstronomyObject<br/>• catalog_id<br/>• ra, dec<br/>• proper_motion<br/>• magnituda<br/>• object_type"]
    end
    
    GRPC_CLIENT -->|"CreateObject()<br/>QueryObject()<br/>UpdateEphemeris()"| ODS
    DB_GUI -->|"operacje CRUD"| ODS
    
    ODS -->|"zapytania SQL"| SQLITE
    SQLITE -->|"zbiory wyników"| ODS
    
    ODS --> EPHEMERIS
    ODS --> OBJECTS
    
    EPHEMERIS -->|"używane przez"| KontrolerMontażu
    OBJECTS -->|"przeszukiwanie katalogu"| KontrolerMontażu
    
    style GRPC_CLIENT fill:#2196F3,color:#fff
    style DB_GUI fill:#2196F3,color:#fff
    style ODS fill:#9C27B0,color:#fff
    style PROTO fill:#9C27B0,color:#fff
    style SQLITE fill:#FF9800,color:#fff
    style EPHEMERIS fill:#4CAF50,color:#fff
    style OBJECTS fill:#4CAF50,color:#fff
```

## 11. Architektura Testów Mock

```mermaid
flowchart LR
    subgraph TestFramework["Framework Google Test"]
        T_MC["test_mount_controller.cpp<br/>919 linii, 25 grup testowych"]
        T_TP["test_tpoint_model.cpp<br/>184 linie, 17 przypadków"]
        T_AC["test_astronomical_calculations.cpp"]
    end
    
    subgraph TestSubjects["Testowane Obiekty"]
        MC_R["MountController (prawdziwy)"]
        TP_R["TPointModel (prawdziwy)"]
        AC_R["AstronomicalCalculations (prawdziwy)"]
    end
    
    subgraph MockLayer["Infrastruktura Mock"]
        MOCK_CAN["MockCanOpenInterface<br/>(brak prawdziwego sprzętu)"]
        MOCK_TIME["Symulowany czas<br/>(100ms tyki)"]
        MOCK_POS["Symulowane kroki pozycji<br/>(1.0 deg/tyk)"]
    end
    
    T_MC -->|"tworzy"| MC_R
    T_TP -->|"tworzy"| TP_R
    T_AC -->|"tworzy"| AC_R
    
    MC_R -->|"automatycznie wykrywa pusty interfejs →"| MOCK_CAN
    
    MOCK_CAN -->|"symulowane sprzężenie zwrotne"| MC_R
    MOCK_TIME -->|"symulacja asynchronicznej nawigacji"| MC_R
    MOCK_POS -->|"przesuwanie pozycji osi"| MC_R
    
    MC_R -->|"getStatus()"| T_MC
    TP_R -->|"getParameters()"| T_TP
    AC_R -->|"przekształcone współrzędne"| T_AC
    
    style TestFramework fill:#4CAF50,color:#fff
    style TestSubjects fill:#FF9800,color:#fff
    style MockLayer fill:#9E9E9E,color:#fff
```

## Legenda

```mermaid
flowchart LR
    EXT["System Zewnętrzny"]:::ext
    CORE["Komponent Rdzenia"]:::core
    HAL_IFACE["Interfejs HAL"]:::hal
    HARD["Sprzęt"]:::hard
    MODEL["Model"]:::model
    DATA["Dane/Konfiguracja"]:::data
    
    classDef ext fill:#2196F3,color:#fff
    classDef core fill:#4CAF50,color:#fff
    classDef hal fill:#FF5722,color:#fff
    classDef hard fill:#607D8B,color:#fff
    classDef model fill:#795548,color:#fff
    classDef data fill:#9E9E9E,color:#fff
```

## 12. Przepływ danych derotatora

```mermaid
sequenceDiagram
    participant Client as Klient gRPC
    participant MC as MountController
    participant DC as DerotatorController
    participant HAL as HAL (Silnik/Enkoder)

    Note over Client,DC: Konfiguracja
    Client->>MC: ConfigureDerotator(config)
    MC->>DC: configure(derotator_config)
    DC-->>MC: ok
    MC-->>Client: sukces

    Note over Client,DC: Włączenie rotacji pola
    Client->>MC: EnableFieldRotation(params)
    MC->>DC: enableFieldRotation(params)
    DC-->>MC: ok
    MC-->>Client: sukces

    Note over MC,DC: Pętla śledzenia (100 Hz)
    loop Co 100ms (wątek śledzenia MC)
        MC->>MC: computeFieldRotationRate()
        MC->>DC: setFieldRotationRate(rate_deg_per_sec)
        DC->>DC: updateCurrentAngle(rate, dt)
        DC->>HAL: motor.setVelocity(rate * gear_ratio)
        HAL-->>DC: pozycja enkodera
        DC->>DC: korekcja PID
    end

    Note over Client,DC: Sterowanie derotatorem
    Client->>MC: ControlFieldRotation(FIXED_ANGLE, 90)
    MC->>DC: controlFieldRotation(FIXED_ANGLE, 90)
    DC->>DC: ustaw kąt docelowy = 90°
    DC->>HAL: motor.setPosition(target)
    loop Dopóki kąt ≠ 90°
        HAL-->>DC: current_angle
        DC->>HAL: motor.setVelocity(korekcja)
    end
    DC-->>MC: osiągnięto kąt
    MC-->>Client: sukces

    Note over Client,DC: Home derotatora
    Client->>MC: HomeDerotator(AUTO)
    MC->>DC: home(AUTO)
    DC->>DC: rozpocznij sekwencję homingu
    DC->>HAL: szukaj krańcówki/znaku zerowego
    HAL-->>DC: znaleziono pozycję referencyjną
    DC->>DC: ustaw pozycję home
    DC-->>MC: homing zakończony
    MC-->>Client: sukces

    Note over Client,DC: Status derotatora
    Client->>MC: GetDerotatorStatus()
    MC->>DC: getStatus()
    DC-->>MC: DerotatorStatus(kąt, prędkość, tryb, błędy)
    MC-->>Client: DerotatorStatus
```

## 13. Przepływ danych sterownika ASCOM

```mermaid
sequenceDiagram
    participant APP as Aplikacja astronomiczna
    participant ALPACA as Alpaca REST API
    participant DRV as Sterownik ASCOM C#
    participant GC as GrpcClient.cs
    participant GRPC as Serwer gRPC
    participant MC as MountController

    Note over APP,MC: Połączenie
    APP->>ALPACA: PUT /api/v1/telescope/0/Connected
    ALPACA->>DRV: set_Connected(true)
    DRV->>GC: Connect()
    GC->>GRPC: GetState()
    GRPC-->>GC: ControllerState
    GC-->>DRV: połączono
    DRV-->>ALPACA: 200 OK
    ALPACA-->>APP: Connected = true

    Note over APP,MC: MoveAxis
    APP->>ALPACA: PUT /api/v1/telescope/0/MoveAxis
    ALPACA->>DRV: MoveAxis(axis, rate)
    DRV->>GC: ControlAxis(AXIS_1, VELOCITY_CONTROL, rate)
    GC->>GRPC: ControlAxis(request)
    GRPC->>MC: controlAxis(axis_id, VELOCITY_CONTROL, rate)
    MC-->>GRPC: ok
    GRPC-->>GC: ok
    GC-->>DRV: ok
    DRV-->>ALPACA: 200 OK

    Note over APP,MC: StateCache polling (co 2s)
    loop Co 2 sekundy
        DRV->>GC: GetState()
        GC->>GRPC: GetState()
        GRPC-->>GC: ControllerState
        GC-->>DRV: ControllerState
        DRV->>DRV: zaktualizuj cache (RA, DEC, ALT, AZ, itp.)
    end

    Note over APP,MC: SlewToCoordinatesAsync
    APP->>ALPACA: PUT /api/v1/telescope/0/SlewToCoordinatesAsync
    ALPACA->>DRV: SlewToCoordinatesAsync(ra, dec)
    DRV->>GC: SlewToCoordinates(coords)
    GC->>GRPC: SlewToCoordinates(request)
    GRPC->>MC: slewToEquatorial(ra, dec)
    MC-->>GRPC: ok
    GRPC-->>GC: ok
    DRV-->>ALPACA: 200 OK

    Note over APP,MC: PulseGuide
    APP->>ALPACA: PUT /api/v1/telescope/0/PulseGuide
    ALPACA->>DRV: PulseGuide(direction, duration)
    DRV->>DRV: oblicz rate z duration
    DRV->>GC: ControlAxis(axis, VELOCITY_CONTROL, rate)
    GC->>GRPC: ControlAxis(request)
    GRPC->>MC: controlAxis(axis, VELOCITY_CONTROL, rate)

    Note over APP,MC: Action (komendy niestandardowe)
    APP->>ALPACA: PUT /api/v1/telescope/0/Action
    ALPACA->>DRV: Action("ClearTPointMeasurements", "")
    DRV->>GC: ClearErrors()
    GC->>GRPC: ClearErrors()
    GRPC-->>GC: ok
    DRV-->>ALPACA: "OK"

    Note over APP,MC: ASCOM Rotator — MoveAbsolute
    APP->>ALPACA: PUT /api/v1/rotator/0/MoveAbsolute
    ALPACA->>DRV: MoveAbsolute(90)
    DRV->>GC: ControlFieldRotation(FIXED_ANGLE, 90)
    GC->>GRPC: ControlFieldRotation(request)
    GRPC->>MC: controlFieldRotation(FIXED_ANGLE, 90)
    MC-->>GRPC: ok
    GRPC-->>GC: ok
    DRV-->>ALPACA: 200 OK

    Note over APP,MC: Halt
    APP->>ALPACA: PUT /api/v1/rotator/0/Halt
    ALPACA->>DRV: Halt()
    DRV->>GC: ControlFieldRotation(DISABLED, 0)
    GC->>GRPC: ControlFieldRotation(DISABLED, 0)

    Note over APP,MC: Home
    APP->>ALPACA: PUT /api/v1/rotator/0/Home
    ALPACA->>DRV: Home()
    DRV->>GC: HomeDerotator(SEQUENTIAL)
    GC->>GRPC: HomeDerotator(SEQUENTIAL)
```

## 14. Przepływ danych sterownika INDI

```mermaid
sequenceDiagram
    participant EKOS as Ekos / KStars
    participant INDI as INDI Protocol
    participant DRV as Sterownik INDI C++
    participant GC as MountGrpcClient
    participant GRPC as Serwer gRPC
    participant MC as MountController

    Note over EKOS,MC: Połączenie
    EKOS->>INDI: setINDIProperty(DEVICE_CONNECT)
    INDI->>DRV: ISNewSwitch(CONNECTION, "CONNECT")
    DRV->>GC: connect(host, port)
    GC->>GRPC: GetState()
    GRPC-->>GC: ControllerState
    GC-->>DRV: połączono
    DRV-->>INDI: setProperty(CONNECTION, OK)
    INDI-->>EKOS: Connected

    Note over EKOS,MC: Slew
    EKOS->>INDI: setNumber(EQUATORIAL_EOD_COORD)
    INDI->>DRV: ISNewNumber(EQUATORIAL_EOD_COORD)
    DRV->>GC: SlewToCoordinates(ra, dec)
    GC->>GRPC: SlewToCoordinates(request)
    GRPC->>MC: slewToEquatorial(ra, dec)
    MC-->>GRPC: ok
    GRPC-->>GC: ok
    DRV-->>INDI: setProperty(SLEW, ACTIVE)
    INDI-->>EKOS: Slew in progress

    Note over EKOS,MC: ReadScopeStatus polling (co 1s)
    loop Co 1 sekundę
        DRV->>DRV: ReadScopeStatus()
        DRV->>GC: GetState()
        GC->>GRPC: GetState()
        GRPC-->>GC: ControllerState
        GC-->>DRV: ControllerState
        DRV->>DRV: updateINDIProperties()
        DRV-->>INDI: setNumber(RA, DEC)
        DRV-->>INDI: setNumber(SNR)
    end

    Note over EKOS,MC: MoveNS / MoveWE (prędkość)
    EKOS->>INDI: setNumber( TELESCOPE_NS_MOTION )
    INDI->>DRV: ISNewNumber(TELESCOPE_NS_MOTION)
    DRV->>DRV: axis_id = 0 (WE=RA, NS=Dec)
    DRV->>GC: ControlAxis(axis_id, VELOCITY_CONTROL, ±1.0)
    GC->>GRPC: ControlAxis(request)
    GRPC->>MC: controlAxis(axis, VELOCITY_CONTROL, rate)

    Note over EKOS,MC: Abort
    EKOS->>INDI: setSwitch(ABORT)
    INDI->>DRV: ISNewSwitch(ABORT, "ABORT")
    DRV->>GC: Stop()
    GC->>GRPC: Stop()

    Note over EKOS,MC: Park
    EKOS->>INDI: setSwitch(PARK)
    INDI->>DRV: ISNewSwitch(PARK, "PARK")
    DRV->>GC: Park()
    GC->>GRPC: Park()

    Note over EKOS,MC: INDI Rotator
    EKOS->>INDI: setNumber(ROTATOR_ANGLE, 180)
    INDI->>DRV: ISNewNumber(ROTATOR_ANGLE)
    DRV->>GC: ControlFieldRotation(FIXED_ANGLE, 180)
    GC->>GRPC: ControlFieldRotation(request)

    Note over EKOS,MC: HomeRotator
    EKOS->>INDI: setSwitch(ROTATOR_HOME)
    INDI->>DRV: ISNewSwitch(ROTATOR_HOME, "HOME")
    DRV->>GC: HomeDerotator(AUTO)
    GC->>GRPC: HomeDerotator(AUTO)
```
