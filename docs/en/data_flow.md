# System Data Flow Diagrams

This document describes the data flow between all major components of the Astronomical Mount Controller using Mermaid.js diagrams.

## 1. High-Level System Architecture

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

    subgraph External["🌐 External Systems"]
        GUI["💻 GUI Client<br/>(gRPC)"]:::ext
        SCRIPT["🤖 Script/Automation<br/>(gRPC)"]:::ext
        GUIDER["📡 Guider<br/>(TCP socket)"]:::ext
        OBS_DB["🗄️ Object Database<br/>(gRPC)"]:::ext
    end

    subgraph Core["⚙️ Mount Controller Core"]
        API["📨 gRPC API Server<br/>src/api/grpc_server.cpp"]:::core
        MC["🎯 Mount Controller<br/>src/controllers/mount_controller.cpp"]:::core
        
        subgraph Models["🧮 Computational Models"]
            TP["📐 TPOINT Model<br/>src/models/tpoint_model.cpp"]:::model
            KF["🔢 Kalman Filter<br/>src/models/kalman_filter.cpp"]:::model
            ET["🛰️ Ephemeris Tracker<br/>src/models/ephemeris_tracker.cpp"]:::model
            AC["🌌 Astronomical Calculations<br/>src/core/astronomical_calculations.cpp"]:::model
        end
        
        CFG["⚙️ Configuration<br/>src/config/configuration.cpp"]:::data
        LOG["📋 Logger<br/>src/logging/logger.cpp"]:::data
    end

    subgraph HAL["🔧 Hardware Abstraction Layer"]
        CAN["🔌 ICanOpenInterface<br/>include/controllers/icanopen_interface.h"]:::hal
        MOCK["🧪 Mock CANopen<br/>(simulation mode)"]:::mock
        REAL["🔴 Real CANopen<br/>(CANopenSocket/EDSS)"]:::real
    end

    subgraph Hardware["🖥️ Physical Hardware"]
        DRV1["⚡ Axis 1 Drive<br/>(RA/Azimuth)"]:::hw
        DRV2["⚡ Axis 2 Drive<br/>(Dec/Altitude)"]:::hw
        ENC1["📏 Axis 1 Encoder"]:::hw
        ENC2["📏 Axis 2 Encoder"]:::hw
        DEROT["🔄 Derotator"]:::hw
    end

    GUI -->|"slewToEquatorial()<br/>startTracking()<br/>park()"| API
    GUI -->|"getStatus()<br/>getTPointParameters()"| API
    SCRIPT -->|"automation commands"| API
    OBS_DB -->|"ephemeris data"| API
    
    API -->|"command dispatch"| MC
    MC -->|"status updates"| API
    
    MC -->|"coordinate transforms"| AC
    MC -->|"pointing errors"| TP
    MC -->|"state estimation"| KF
    MC -->|"moving object tracking"| ET
    
    MC -->|"enableDrive()<br/>setPositionTarget()<br/>setVelocityTarget()"| CAN
    CAN -->|"getDriveStatus()<br/>getPositionData()<br/>getEncoderData()"| MC
    
    CAN -->|"mock CAN bus"| MOCK
    CAN -->|"real CAN bus"| REAL
    
    MOCK -->|"simulated feedback"| CAN
    REAL -->|"CAN frames<br/>(PDO/SDO/NMT)"| DRV1
    REAL -->|"CAN frames"| DRV2
    DRV1 -->|"encoder feedback"| ENC1
    DRV2 -->|"encoder feedback"| ENC2
    MC -->|"field rotation"| DEROT
    
    GUIDER -->|"applyGuiderCorrection()<br/>(TCP socket)"| MC
    
    CFG -->|"config.json"| MC
    MC -->|"log events"| LOG
```

## 2. Control Loop Data Flow

```mermaid
sequenceDiagram
    participant Client as gRPC Client
    participant API as gRPC API Server
    participant MC as Mount Controller
    participant CAN as CANopen Interface
    participant HW as Hardware
    
    Client->>API: slewToEquatorial(ra, dec)
    API->>MC: command + coordinates
    MC->>MC: validateState()
    
    alt State Invalid
        MC-->>API: return false
        API-->>Client: error response
    else State Valid
        MC->>MC: convertToMountCoords(ra, dec)
        MC->>AC: equatorialToHorizontal() / haToEquatorial()
        AC-->>MC: (axis1_target, axis2_target)
        
        MC->>MC: applyTPointCorrections()
        MC->>TP: applyCorrections()
        TP-->>MC: (corrected_ha, corrected_dec)
        
        MC->>MC: launchAsyncSlewThread()
        
        par Slew Loop (async thread)
            loop every POLL_MS (100ms)
                CAN->>HW: setPositionTarget(axis, pos, vel)
                HW-->>CAN: drive acknowledgment
                CAN->>HW: getPositionData()
                HW-->>CAN: actual_position, actual_velocity
                CAN-->>MC: position feedback
                
                MC->>MC: computeFollowingError()
                alt Error > Tolerance
                    MC->>MC: adjustVelocityProfile()
                else Error < Tolerance
                    MC->>MC: markAxisReached()
                end
            end
        end
        
        MC-->>API: return true (command accepted)
        API-->>Client: acknowledge
        
        Note over MC: Slew continues in background thread
        MC->>MC: both axes reached target?
        MC->>MC: setState(IDLE)
        MC->>Client: status callback (optional)
    end
```

## 3. Tracking Control Loop

```mermaid
sequenceDiagram
    participant Client as gRPC Client
    participant MC as Mount Controller
    participant AC as Astronomical Calc
    participant CAN as CANopen
    participant GUID as Guider
    
    Client->>MC: startTracking(ra, dec, SIDEREAL)
    MC->>MC: setState(TRACKING)
    MC->>MC: launchAsyncTrackingThread()
    
    loop every POLL_MS
        MC->>AC: calculateApparentPlace()
        AC-->>MC: apparent (ra, dec)
        
        MC->>MC: computeVelocityCommands()
        MC->>CAN: setVelocityTarget(0, ra_rate)
        MC->>CAN: setVelocityTarget(1, dec_rate)
        CAN-->>MC: drive status
        
        MC->>MC: updateEncoders()
        MC->>KF: update(position, velocity)
        KF-->>MC: estimated state
        
        alt Guider Active
            GUID->>MC: applyGuiderCorrection(delta_ra, delta_dec)
            MC->>MC: clampCorrection()
            MC->>MC: adjustTrackingRates()
        end
        
        MC->>MC: checkTrackingError()
        alt Error > Threshold
            MC->>MC: performRecovery()
        end
        
        MC->>MC: updateFieldRotation()
        MC->>DEROT: setRotationRate()
    end
```

## 4. CANopen Communication Data Flow

```mermaid
sequenceDiagram
    participant MC as Mount Controller
    participant CAN as ICanOpenInterface
    participant MOCK as Mock CANopen
    participant REAL as Real CANopen
    participant BUS as CAN Bus
    participant DRIVE as Servo Drive
    
    MC->>CAN: initialize(config)
    
    alt config.interface == "" or "mock"
        CAN->>MOCK: useMockImplementation()
        MOCK-->>CAN: MockInterface created
    else config.interface == "can0" or "vcan0"
        CAN->>REAL: useRealImplementation()
        REAL->>BUS: connect(socketCAN)
        BUS-->>REAL: connected
    end
    
    CAN-->>MC: interface ready
    
    MC->>CAN: configureDrive(0, config_string)
    
    CAN->>DRIVE: SDO: 0x6060 (Mode of Operation)
    DRIVE-->>CAN: OK
    CAN->>DRIVE: SDO: 0x607A (Target Position)  
    DRIVE-->>CAN: OK
    CAN->>DRIVE: SDO: 0x6081 (Profile Velocity)
    DRIVE-->>CAN: OK
    
    MC->>CAN: enableDrive(0)
    CAN->>DRIVE: NMT: Start Remote Node
    CAN->>DRIVE: SDO: 0x6040 (Controlword) = 0x0006
    CAN->>DRIVE: SDO: 0x6040 (Controlword) = 0x0007
    CAN->>DRIVE: SDO: 0x6040 (Controlword) = 0x000F
    DRIVE-->>CAN: Statusword = 0x0037 (Operation Enabled)
    
    loop PDO Cycle (every SYNC period)
        CAN->>DRIVE: RPDO: Target Position / Velocity
        DRIVE-->>CAN: TPDO: Actual Position / Status
        CAN-->>MC: PositionData callback
        CAN-->>MC: DriveStatus callback
    end
    
    MC->>CAN: stopAxis(0)
    CAN->>DRIVE: SDO: 0x6040 (Controlword) = 0x0002
    DRIVE-->>CAN: Quick Stop executed
```

## 5. Ephemeris Track Data Flow

```mermaid
sequenceDiagram
    participant Client as gRPC Client
    participle ODB as Object Database
    participant API as gRPC API Server
    participant MC as Mount Controller  
    participle EPM as EphemerisModel
    participant EPT as EphemerisTracker
    participle EPI as EphemerisInterpolator
    
    Client->>ODB: queryEphemeris(object_id, time_range)
    ODB-->>Client: EphemerisData (points[])
    
    Client->>API: uploadEphemeris(data)
    API->>MC: uploadEphemeris(object_id, points)
    
    MC->>EPM: EphemerisModel(data, config)
    EPM->>EPI: EphemerisInterpolator(points, order)
    EPI-->>EPM: interpolator ready
    EPM-->>MC: model ready
    
    MC->>EPT: EphemerisTracker(model, lat, lon, alt)
    EPT-->>MC: tracker created
    
    Client->>API: startEphemerisTracking(object_id, time)
    API->>MC: startEphemerisTracking()
    
    MC->>EPT: startTracking(start_time, config)
    
    loop Tracking Loop (10 Hz)
        EPT->>EPM: getApparentPosition(time)
        EPM->>EPI: getPositionAtTime(time)
        
        alt time within ephemeris range
            EPI-->>EPM: (ra, dec, ra_rate, dec_rate)
            EPM->>EPM: applyEarthRotation()
            EPM->>EPM: applyAtmosphericRefraction()
            EPM->>EPM: applyTPointCorrections()
            EPM-->>EPT: apparent_position
        else time beyond range + extrapolation
            EPI->>EPI: predictPosition(time, max_extrap)
            EPI-->>EPM: predicted_position
            EPM-->>EPT: predicted + corrections
        end
        
        EPT->>MC: updateTargetPosition(ra, dec, rates)
        MC->>CAN: setVelocityTarget(axes, rates)
    end
    
    Client->>API: stopEphemerisTracking(tracker_id)
    API->>MC: stopEphemerisTracking()
    MC->>EPT: stopTracking()
    EPT-->>MC: tracking stopped, stats returned
    MC-->>API: success
    API-->>Client: confirmation
```

## 6. Guider Correction Data Flow

```mermaid
flowchart LR
    subgraph External
        G[Guider Software<br/>PHD2 / Ekos / ASCOM]
    end
    
    subgraph MountController
        direction TB
        GC[Guider Connection<br/>TCP socket]
        GR[Guider Rate Adjuster]
        TR[Tracking Loop<br/>RA/Dec Rate Calculator]
        CL[Clamp + Aggression]
        MC[Main Controller]
    end
    
    subgraph HAL
        CAN2[CANopen Interface]
    end
    
    subgraph Hardware
        D1[Axis 1 Drive]
        D2[Axis 2 Drive]
    end
    
    G -->|"TCP connection<br/>socket://ip:port"| GC
    GC -->|"correction<br/>(ra_arcsec, dec_arcsec)"| GR
    
    GR -->|"raw correction"| CL
    CL -->|"clamp to ±max_correction<br/>apply aggression factor"| TR
    
    MC -->|"sidereal/solar/lunar rate"| TR
    TR -->|"final rate = base + Δ"| MC
    
    MC -->|"setVelocityTarget(axis, rate)"| CAN2
    CAN2 -->|"velocity mode"| D1
    CAN2 -->|"velocity mode"| D2
    
    D1 -->|"actual position"| CAN2
    D2 -->|"actual position"| CAN2
    CAN2 -->|"getPositionData()"| MC
    MC -->|"status update<br/>(guider_active, tracking_error)"| GC
    
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

## 7. TPOINT Calibration Data Flow

```mermaid
flowchart TB
    subgraph UserInput["User/Client Commands"]
        direction TB
        A1["addTPointMeasurement()<br/>observed (RA, Dec)<br/>expected (RA, Dec)<br/>mount (HA, Dec)<br/>env (T, P, H)"]
        A2["runTPointCalibration()"]
        A3["getTPointParameters()"]
    end
    
    subgraph Storage["Measurement Storage"]
        B["tpoint_measurements[]<br/>std::vector<Measurement>"]
    end
    
    subgraph Solver["TPOINT Solver"]
        direction TB
        C1["Build residual vector<br/>b = observed - expected"]
        C2["Build design matrix<br/>A = partial derivatives<br/>for enabled terms"]
        C3["Least squares solution<br/>p = QR.solve(Aᵀ · b)"]
        C4["Quality metrics<br/>χ², RMS, DOF"]
    end
    
    subgraph Correction["Correction Application"]
        direction TB
        D1["applyCorrections()<br/>ΔRA = IA + CA·cos(HA) + ..."]
        D2["RA_corrected = RA - ΔRA<br/>Dec_corrected = Dec - ΔDec"]
    end
    
    subgraph Prediction["Mount Position Prediction"]
        direction TB
        E1["predictMountPosition()<br/>Newton-Raphson solver"]
        E2["Central difference Jacobian<br/>J = [∂f/∂HA, ∂f/∂Dec]"]
        E3["Iterate until convergence<br/>||Δx|| < ε"]
    end
    
    A1 --> B
    A2 --> B
    B --> C1
    C1 --> C2
    C2 --> C3
    C3 --> C4
    C4 -->|"p (TPOINT parameters)"| D1
    D1 --> D2
    D2 -->|"used in tracking"| Prediction
    Prediction -->|"required mount angles"| CANopen
    
    A3 --> C4
    C4 -->|"parameters JSON"| Client
    
    style UserInput fill:#2196F3,color:#fff
    style Storage fill:#9E9E9E,color:#fff
    style Solver fill:#795548,color:#fff
    style Correction fill:#4CAF50,color:#fff
    style Prediction fill:#4CAF50,color:#fff
```

## 8. Configuration Loading Flow

```mermaid
flowchart TD
    FILE["config/default.json<br/>JSON configuration file"]
    
    CFG["Configuration Loader<br/>src/config/configuration.cpp"]
    
    subgraph ConfigSections["Configuration Sections"]
        MOUNT["Mount Config<br/>• type (equatorial/altaz)<br/>• latitude, longitude, altitude<br/>• mount_height<br/>• max_slew/tracking_rate<br/>• slew/tracking_acceleration<br/>• meridian_flip_enabled/delay<br/>• soft_limits (min/max/warning/decel)<br/>• park_position<br/>• axis_physical_parameters"]
        CAN["CANopen Config<br/>• interface name<br/>• node ID<br/>• bitrate<br/>• sync interval"]
        TEL_CFG["Telescope Config<br/>• focal_length, aperture<br/>• camera_model, pixel_size<br/>• sensor dimensions"]
        KF_CFG["Kalman Config<br/>• process_noise<br/>• measurement_noise<br/>• adaptive_q/r<br/>• innovation_threshold"]
        TP_CFG["TPOINT Config<br/>• enabled_terms<br/>• min_measurements<br/>• max_residual"]
        DEROT_CFG["Derotator Config<br/>• type (none/canopen/stepper)<br/>• gear_ratio, max_speed<br/>• acceleration, backlash<br/>• calibration_table"]
        FR_CFG["Field Rotation Config<br/>• enabled<br/>• compensation_mode<br/>• max_rate<br/>• PID gains"]
        HAL_CFG["HAL Config<br/>• type (canopen/simulated)<br/>• CAN params<br/>• watchdog timeout<br/>• PDO update rate"]
        NET["Network Config<br/>• gRPC address<br/>• gRPC port<br/>• SSL settings"]
        GUID_CFG["Guider Config<br/>• enabled<br/>• connection_string<br/>• max_correction<br/>• aggression, exposure"]
        LOG_CFG["Logging Config<br/>• level, directory<br/>• rotation_days<br/>• max_file_size"]
    end
    
    subgraph MainInit["main() Initialization"]
        MAIN["main.cpp"]
        CONTROLLER["MountController<br/>instance created"]
        GRPC["gRPC Server<br/>started"]
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
    DEROT_CFG -->|"derotator params"| CONTROLLER
    FR_CFG -->|"field rotation params"| CONTROLLER
    HAL_CFG -->|"hal_type, can config"| CONTROLLER
    GUID_CFG -->|"guider settings"| CONTROLLER
    NET -->|"grpc_address, grpc_port"| GRPC
    LOG_CFG -->|"directory, level"| MAIN
    
    CONTROLLER -->|"initialize()"| MAIN
    GRPC -->|"start()"| MAIN
    MAIN -->|"main loop (100ms sleep)"| MAIN
    
    style FILE fill:#FF9800,color:#fff
    style CFG fill:#9E9E9E,color:#fff
    style MOUNT fill:#607D8B,color:#fff
    style CAN fill:#607D8B,color:#fff
    style KF_CFG fill:#607D8B,color:#fff
    style TP_CFG fill:#607D8B,color:#fff
    style NET fill:#607D8B,color:#fff
    style GUID_CFG fill:#607D8B,color:#fff
    style LOG_CFG fill:#607D8B,color:#fff
    style MAIN fill:#4CAF50,color:#fff
    style CONTROLLER fill:#4CAF50,color:#fff
    style GRPC fill:#4CAF50,color:#fff
```

## 9. Park/Unpark State Machine Flow

```mermaid
stateDiagram-v2
    [*] --> UNINITIALIZED
    UNINITIALIZED --> IDLE: initialize()
    
    IDLE --> SLEWING: slewToEquatorial() / slewToHorizontal()
    SLEWING --> IDLE: target reached / stop()
    SLEWING --> ERROR: failure
    
    IDLE --> TRACKING: startTracking()
    TRACKING --> IDLE: stop()
    TRACKING --> ERROR: tracking error
    
    IDLE --> PARKING: park()
    PARKING --> PARKED: both axes at (0,0)
    PARKING --> IDLE: timeout / stop()
    PARKED --> IDLE: unpark()
    
    ERROR --> IDLE: clearErrors()
    
    IDLE --> BOOTSTRAP: addBootstrapMeasurement()
    BOOTSTRAP --> IDLE: runBootstrapCalibration()
    
    note right of PARKING
        Async thread moves axes to (0,0)
        CANopen: CAN frames → drives
        Simulation: step-wise approach
        Timeout: PARK_TIMEOUT_MS
    end note
    
    note right of TRACKING
        Tracking loop updates rates
        Guider corrections applied
        Field rotation computed
        Kalman filter active
    end note
    
    note right of SLEWING
        Async slewing thread
        Position command → CANopen
        Polls until target reached
        Detected thread; no join
    end note
```

## 10. Object Database Data Flow

```mermaid
flowchart LR
    subgraph Services["External Services"]
        GRPC_CLIENT["gRPC Client<br/>(Script)"]
        DB_GUI["Database Admin GUI"]
    end
    
    subgraph DBService["Object Database Service"]
        ODS["ObjectDatabaseService<br/>db/src/object_database_service.cpp"]
        PROTO["Proto Definition<br/>db/proto/object_database.proto"]
    end
    
    subgraph Storage["Data Storage"]
        SQLITE["SQLite Database<br/>astronomy_objects.db"]
    end
    
    subgraph DataModels["Data Models"]
        EPHEMERIS["EphemerisData<br/>• object_id<br/>• object_name<br/>• points[]<br/>• epoch<br/>• reference_frame"]
        OBJECTS["AstronomyObject<br/>• catalog_id<br/>• ra, dec<br/>• proper_motion<br/>• magnitude<br/>• object_type"]
    end
    
    GRPC_CLIENT -->|"CreateObject()<br/>QueryObject()<br/>UpdateEphemeris()"| ODS
    DB_GUI -->|"CRUD operations"| ODS
    
    ODS -->|"SQL queries"| SQLITE
    SQLITE -->|"result sets"| ODS
    
    ODS --> EPHEMERIS
    ODS --> OBJECTS
    
    EPHEMERIS -->|"used by"| MountController
    OBJECTS -->|"catalog lookup"| MountController
    
    style GRPC_CLIENT fill:#2196F3,color:#fff
    style DB_GUI fill:#2196F3,color:#fff
    style ODS fill:#9C27B0,color:#fff
    style PROTO fill:#9C27B0,color:#fff
    style SQLITE fill:#FF9800,color:#fff
    style EPHEMERIS fill:#4CAF50,color:#fff
    style OBJECTS fill:#4CAF50,color:#fff
```

## 11. Test Mock Architecture

```mermaid
flowchart LR
    subgraph TestFramework["Google Test Framework"]
        T_MC["test_mount_controller.cpp<br/>919 lines, 25 test groups"]
        T_TP["test_tpoint_model.cpp<br/>184 lines, 17 test cases"]
        T_AC["test_astronomical_calculations.cpp"]
    end
    
    subgraph TestSubjects["Test Subjects"]
        MC_R["MountController (real)"]
        TP_R["TPointModel (real)"]
        AC_R["AstronomicalCalculations (real)"]
    end
    
    subgraph MockLayer["Mock Infrastructure"]
        MOCK_CAN["MockCanOpenInterface<br/>(no real hardware needed)"]
        MOCK_TIME["Simulated time<br/>(100ms ticks)"]
        MOCK_POS["Simulated position stepping<br/>(1.0 deg/tick)"]
    end
    
    T_MC -->|"creates"| MC_R
    T_TP -->|"creates"| TP_R
    T_AC -->|"creates"| AC_R
    
    MC_R -->|"auto-detects empty interface →"| MOCK_CAN
    
    MOCK_CAN -->|"simulated feedback"| MC_R
    MOCK_TIME -->|"async slewing simulation"| MC_R
    MOCK_POS -->|"axis position advancement"| MC_R
    
    MC_R -->|"getStatus()"| T_MC
    TP_R -->|"getParameters()"| T_TP
    AC_R -->|"transformed coordinates"| T_AC
    
    style TestFramework fill:#4CAF50,color:#fff
    style TestSubjects fill:#FF9800,color:#fff
    style MockLayer fill:#9E9E9E,color:#fff
```

## Legend

```mermaid
flowchart LR
    EXT["External System"]:::ext
    CORE["Core Component"]:::core
    HAL_IFACE["HAL Interface"]:::hal
    HARD["Hardware"]:::hard
    MODEL["Model"]:::model
    DATA["Data/Config"]:::data
    
    classDef ext fill:#2196F3,color:#fff
    classDef core fill:#4CAF50,color:#fff
    classDef hal fill:#FF5722,color:#fff
    classDef hard fill:#607D8B,color:#fff
    classDef model fill:#795548,color:#fff
    classDef data fill:#9E9E9E,color:#fff
```
