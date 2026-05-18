# Przykłady użycia API gRPC

Ten dokument zawiera kompletną kolekcję przykładów użycia API gRPC Astronomical Mount Controller, pokrywając wszystkie dostępne scenariusze.

## Przygotowanie środowiska

### Python
```python
import grpc
import mount_controller_pb2 as proto
import mount_controller_pb2_grpc as rpc
from google.protobuf import timestamp_pb2
import time

# Utworzenie klienta gRPC
channel = grpc.insecure_channel('localhost:50051')
stub = rpc.MountControllerServiceStub(channel)
```

### C++
```cpp
#include <grpcpp/grpcpp.h>
#include "mount_controller.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// Utworzenie klienta
auto channel = grpc::CreateChannel("localhost:50051", 
    grpc::InsecureChannelCredentials());
auto stub = MountControllerService::NewStub(channel);
```

## Przykłady dla wszystkich scenariuszy

### 1. Podstawowe sterowanie montażem

#### SlewToCoordinates - Przesunięcie do współrzędnych
```python
def slew_to_target(ra_hours, dec_degrees):
    """Przesunięcie montażu do określonych współrzędnych."""
    coords = proto.Coordinates()
    coords.ra = ra_hours
    coords.dec = dec_degrees
    coords.epoch = 2000.0  # J2000
    
    try:
        stub.SlewToCoordinates(coords)
        print(f"Slew to RA={ra_hours}h, Dec={dec_degrees}° started")
    except grpc.RpcError as e:
        print(f"Error: {e.code()} - {e.details()}")
```

#### TrackObject - Śledzenie obiektu
```python
def track_object(ra_hours, dec_degrees, object_name="Target"):
    """Rozpoczęcie śledzenia obiektu."""
    coords = proto.Coordinates()
    coords.ra = ra_hours
    coords.dec = dec_degrees
    coords.object_id = object_name
    
    try:
        stub.TrackObject(coords)
        print(f"Tracking {object_name} at RA={ra_hours}h, Dec={dec_degrees}°")
    except grpc.RpcError as e:
        print(f"Error: {e.code()} - {e.details()}")
```

#### Stop - Zatrzymanie ruchów
```python
def stop_mount():
    """Zatrzymanie wszystkich ruchów montażu."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.Stop(empty)
    print("Mount stopped")
```

#### Park/Unpark - Parkowanie montażu
```python
def park_mount():
    """Parkowanie montażu w pozycji bezpiecznej."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.Park(empty)
    print("Mount parked")

def unpark_mount():
    """Odparkowanie montażu."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.Unpark(empty)
    print("Mount unparked")
```

### 2. Zarządzanie stanem

#### GetState - Pobranie aktualnego stanu
```python
def get_mount_state():
    """Pobranie kompleksowego stanu montażu."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    state = stub.GetState(empty)
    
    print(f"Status: {proto.ControllerState.MountStatus.Name(state.status)}")
    print(f"Position: RA={state.current_position.axis1}°, Dec={state.current_position.axis2}°")
    print(f"Tracking: RA rate={state.tracking_rate_ra} arcsec/s, "
          f"Dec rate={state.tracking_rate_dec} arcsec/s")
    print(f"Temperature: {state.temperature}°C, Pressure: {state.pressure} hPa")
    print(f"Pointing error: {state.pointing_error} arcsec")
    
    return state
```

#### SaveState/LoadState - Zapis/wczytanie stanu
```python
def save_system_state(filename="mount_state.bin"):
    """Zapisanie stanu systemu do pliku."""
    request = proto.StateSaveRequest()
    request.file_path = filename
    request.include_measurements = True
    
    response = stub.SaveState(request)
    print(f"State saved to {response.file_path} ({response.file_size} bytes)")

def load_system_state(filename="mount_state.bin"):
    """Wczytanie stanu systemu z pliku."""
    request = proto.StateLoadRequest()
    request.file_path = filename
    
    stub.LoadState(request)
    print(f"State loaded from {filename}")
```

### 3. Pomiar i kalibracja

#### AddMeasurement - Dodanie pomiaru kalibracyjnego
```python
def add_calibration_measurement(observed_ra, observed_dec, 
                                expected_ra, expected_dec):
    """Dodanie pomiaru do kalibracji TPOINT."""
    measurement = proto.Measurement()
    
    # Obserwowane współrzędne
    measurement.observed.ra = observed_ra
    measurement.observed.dec = observed_dec
    
    # Oczekiwane współrzędne
    measurement.expected.ra = expected_ra
    measurement.expected.dec = expected_dec
    
    # Warunki środowiskowe
    measurement.temperature = 15.0
    measurement.pressure = 1013.25
    measurement.humidity = 0.5
    
    # Znacznik czasu
    ts = timestamp_pb2.Timestamp()
    ts.GetCurrentTime()
    measurement.timestamp.CopyFrom(ts)
    
    stub.AddMeasurement(measurement)
    print(f"Added measurement: observed({observed_ra}h,{observed_dec}°) "
          f"vs expected({expected_ra}h,{expected_dec}°)")
```

#### GetTPointParameters - Pobranie parametrów TPOINT
```python
def get_tpoint_parameters():
    """Pobranie aktualnych parametrów modelu TPOINT."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    params = stub.GetTPointParameters(empty)
    
    print(f"TPOINT parameters:")
    print(f"  Chi-squared: {params.chi_squared}")
    print(f"  Last update: {params.last_update}")
    print(f"  Coefficients: {len(params.coefficients)} terms")
    
    for i, coeff in enumerate(params.coefficients[:10]):  # Pierwsze 10
        print(f"    Term {i}: {coeff}")
    
    return params
```

#### RunTPointCalibration - Uruchomienie kalibracji
```python
def run_tpoint_calibration():
    """Uruchomienie pełnej kalibracji TPOINT."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.RunTPointCalibration(empty)
    print("TPOINT calibration started")
```

#### GetRotationMatrix - Pobranie macierzy rotacji
```python
def get_rotation_matrix():
    """Pobranie macierzy rotacji (kwaterniony)."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    matrix = stub.GetRotationMatrix(empty)
    
    print(f"Rotation matrix (quaternion):")
    print(f"  q0={matrix.q0}, q1={matrix.q1}, q2={matrix.q2}, q3={matrix.q3}")
    print(f"  Valid from: {matrix.valid_from}")
    
    return matrix
```

### 4. Określanie pozycji bieguna

#### DeterminePolePosition - Określenie pozycji bieguna metodą dryfu
```python
def determine_pole_position(duration_hours=2.0):
    """Określenie pozycji bieguna niebieskiego."""
    request = proto.PoleDeterminationRequest()
    request.duration_hours = duration_hours
    request.measurement_count = 50
    
    pole = stub.DeterminePolePosition(request)
    
    print(f"Pole position determined:")
    print(f"  Latitude: {pole.latitude}°")
    print(f"  Longitude: {pole.longitude}°")
    print(f"  Altitude: {pole.altitude} m")
    print(f"  Accuracy: {pole.accuracy} arcsec")
    print(f"  Determined at: {pole.determined_at}")
    
    return pole
```

### 5. Sterowanie enkoderami

#### EnableEncoders - Włączenie i konfiguracja enkoderów
```python
def enable_encoders(encoder_type='ABSOLUTE', resolution=36000):
    """Włączenie i konfiguracja systemu enkoderów."""
    config = proto.EncoderConfig()
    
    if encoder_type.upper() == 'ABSOLUTE':
        config.type = proto.EncoderConfig.ABSOLUTE
    else:
        config.type = proto.EncoderConfig.INCREMENTAL
    
    config.resolution = resolution
    config.use_feedback = True
    
    stub.EnableEncoders(config)
    print(f"Encoders enabled: type={encoder_type}, resolution={resolution}")
```

#### DisableEncoders - Wyłączenie enkoderów
```python
def disable_encoders():
    """Wyłączenie systemu enkoderów."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.DisableEncoders(empty)
    print("Encoders disabled")
```

### 6. Sterowanie guiderem

#### ConnectGuider - Połączenie z systemem autoguiding
```python
def connect_guider(host='localhost', port=7624, max_correction=10.0):
    """Połączenie z systemem autoguiding."""
    config = proto.GuiderConfig()
    config.connection_string = f"tcp://{host}:{port}"
    config.max_correction = max_correction
    config.aggression = 0.5
    
    stub.ConnectGuider(config)
    print(f"Guider connected to {config.connection_string}")
```

#### SendGuiderCorrection - Wysłanie korekcji
```python
def send_guider_correction(ra_correction, dec_correction):
    """Wysłanie korekcji od guidera."""
    correction = proto.GuiderCorrection()
    correction.ra_correction = ra_correction
    correction.dec_correction = dec_correction
    
    ts = timestamp_pb2.Timestamp()
    ts.GetCurrentTime()
    correction.timestamp.CopyFrom(ts)
    
    stub.SendGuiderCorrection(correction)
    print(f"Guider correction sent: RA={ra_correction}\", Dec={dec_correction}\"")
```

#### DisconnectGuider - Rozłączenie guidera
```python
def disconnect_guider():
    """Rozłączenie z systemem autoguiding."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.DisconnectGuider(empty)
    print("Guider disconnected")
```

### 7. Konfiguracja systemu

#### GetConfiguration - Pobranie pełnej konfiguracji
```python
def get_full_configuration():
    """Pobranie kompletnej konfiguracji systemu."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    config = stub.GetConfiguration(empty)
    
    print(f"System configuration:")
    print(f"  Location: Lat={config.latitude}°, Lon={config.longitude}°, Alt={config.altitude}m")
    print(f"  Mount: Type={config.mount_type}, Height={config.mount_height}m")
    print(f"  Gears: Axis1={config.axis1_gear_ratio}:1, Axis2={config.axis2_gear_ratio}:1")
    print(f"  Encoders: {'Enabled' if config.use_encoders else 'Disabled'}")
    
    # Parametry fizyczne osi HA
    if config.HasField('ha_axis_params'):
        ha = config.ha_axis_params
        print(f"  HA Axis Physical Parameters:")
        print(f"    Motor: {ha.motor_steps_per_rev} steps/rev, "
              f"{ha.motor_microstepping}x microstepping")
        print(f"    Gear: ratio={ha.gear_ratio}, worm={ha.worm_ratio}")
        print(f"    Encoder: {ha.encoder_resolution} counts/rev")
    
    return config
```

#### UpdateConfiguration - Aktualizacja konfiguracji
```python
def update_mount_configuration(new_latitude, new_longitude):
    """Aktualizacja konfiguracji systemu."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    current_config = stub.GetConfiguration(empty)
    
    # Modyfikacja wybranych parametrów
    current_config.latitude = new_latitude
    current_config.longitude = new_longitude
    current_config.default_temperature = 20.0
    
    # Aktualizacja parametrów fizycznych osi
    if current_config.HasField('ha_axis_params'):
        current_config.ha_axis_params.backlash = 5.0  # Zmniejszenie luzu
        current_config.ha_axis_params.cyclic_error_amplitude = 10.0
    
    if current_config.HasField('dec_axis_params'):
        current_config.dec_axis_params.backlash = 4.5
        current_config.dec_axis_params.cyclic_error_amplitude = 8.0
    
    stub.UpdateConfiguration(current_config)
    print(f"Configuration updated: new location ({new_latitude}°, {new_longitude}°)")
```

### 8. Generacja i wykonanie trajektorii

#### GenerateTrajectory - Generacja trajektorii ruchu
```python
def generate_trajectory(start_pos, target_pos, max_velocity=2.0):
    """Generacja płynnej trajektorii ruchu."""
    params = proto.TrajectoryParams()
    params.type = proto.TrajectoryParams.S_CURVE
    params.max_velocity = max_velocity  # deg/s
    params.max_acceleration = 1.0       # deg/s²
    params.max_jerk = 0.5               # deg/s³
    params.start_position = start_pos
    params.target_position = target_pos
    params.update_rate = 100.0          # Hz
    
    trajectory = stub.GenerateTrajectory(params)
    
    print(f"Trajectory generated:")
    print(f"  Points: {len(trajectory.points)}")
    print(f"  Duration: {trajectory.points[-1].time - trajectory.points[0].time:.2f}s")
    print(f"  Generated at: {trajectory.generated_at}")
    
    return trajectory
```

#### ExecuteTrajectory - Wykonanie trajektorii
```python
def execute_trajectory(trajectory):
    """Wykonanie wygenerowanej trajektorii."""
    stub.ExecuteTrajectory(trajectory)
    print("Trajectory execution started")
```

#### StopTrajectory - Zatrzymanie trajektorii
```python
def stop_trajectory():
    """Zatrzymanie wykonywanej trajektorii."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.StopTrajectory(empty)
    print("Trajectory stopped")
```

### 9. Śledzenie obiektów ruchomych (Ephemeris-based tracking)

#### UploadEphemeris - Przesłanie danych efemeryd
```python
def upload_comet_ephemeris():
    """Przesłanie danych efemeryd komety."""
    ephemeris = proto.EphemerisData()
    ephemeris.object_id = "C/2023 A3"
    ephemeris.object_name = "Comet Tsuchinshan-ATLAS"
    ephemeris.object_type = "comet"
    ephemeris.interpolation_order = 3  # Cubic interpolation
    ephemeris.reference_frame = "J2000"
    ephemeris.source = "JPL Horizons"
    
    # Dodanie punktów efemerydy (przykładowe dane)
    for i in range(10):
        point = ephemeris.points.add()
        point.time.seconds = int(time.time()) + i * 3600
        point.ra = 10.0 + i * 0.1
        point.dec = 20.0 + i * 0.05
        point.ra_rate = 0.01  # godziny/godzinę
        point.dec_rate = 0.005  # stopnie/godzinę
    
    ephemeris.valid_from.seconds = int(time.time())
    ephemeris.valid_to.seconds = int(time.time()) + 24 * 3600
    
    stub.UploadEphemeris(ephemeris)
    print(f"Ephemeris uploaded for {ephemeris.object_name}")
```

#### StartEphemerisTracking - Rozpoczęcie śledzenia efemeryd
```python
def start_ephemeris_tracking(object_id):
    """Rozpoczęcie śledzenia obiektu na podstawie efemeryd."""
    request = proto.StartEphemerisTrackingRequest()
    request.object_id = object_id
    request.wait_at_start = True
    request.slew_margin_seconds = 300.0  # 5 minut marginesu
    
    status = stub.StartEphemerisTracking(request)
    
    print(f"Ephemeris tracking started:")
    print(f"  Object: {status.object_name} ({status.object_id})")
    print(f"  State: {proto.EphemerisTrackStatus.TrackingState.Name(status.state)}")
    print(f"  Start time: {status.track_start_time}")
    print(f"  End time: {status.track_end_time}")
    
    return status
```

#### GetEphemerisTrackStatus - Pobranie statusu śledzenia
```python
def get_ephemeris_status():
    """Pobranie aktualnego statusu śledzenia efemeryd."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    status = stub.GetEphemerisTrackStatus(empty)
    
    print(f"Ephemeris tracking status:")
    print(f"  State: {proto.EphemerisTrackStatus.TrackingState.Name(status.state)}")
    print(f"  Object: {status.object_name}")
    print(f"  Position error: {status.position_error_arcsec:.2f} arcsec")
    print(f"  Time remaining: {status.time_remaining_seconds:.0f} seconds")
    
    return status
```

#### StopEphemerisTracking - Zatrzymanie śledzenia
```python
def stop_ephemeris_tracking():
    """Zatrzymanie śledzenia obiektu ruchomego."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    stub.StopEphemerisTracking(empty)
    print("Ephemeris tracking stopped")
```

### 10. Kontrola zdrowia systemu

#### CheckHealth - Sprawdzenie zdrowia systemu
```python
def check_system_health():
    """Sprawdzenie zdrowia wszystkich komponentów systemu."""
    request = proto.HealthCheckRequest()
    request.service = "all"  # Sprawdź wszystkie usługi
    
    response = stub.CheckHealth(request)
    
    print(f"System health check:")
    print(f"  Status: {proto.HealthCheckResponse.ServingStatus.Name(response.status)}")
    print(f"  Service: {response.service}")
    
    if response.HasField('metrics'):
        metrics = response.metrics
        print(f"  CPU usage: {metrics.cpu_usage:.1f}%")
        print(f"  Memory usage: {metrics.memory_usage_mb:.0f} MB")
        print(f"  Uptime: {metrics.uptime_seconds:.0f} seconds")
    
    return response
```

### 11. Sterowanie derotatorem / polem widzenia (Field Rotation)

#### ConfigureDerotator - Konfiguracja derotatora

```python
def configure_derotator():
    """Konfiguracja parametrów sprzętowych derotatora."""
    config = proto.DerotatorConfig()
    config.type = proto.DerotatorConfig.CANOPEN
    config.connection_string = "can0:3"
    config.gear_ratio = 5.0
    config.max_speed = 5.0
    config.max_acceleration = 1.0
    config.backlash = 2.5
    config.absolute_encoder = True
    config.encoder_resolution = 131072
    config.homing_offset = 0.0

    stub.ConfigureDerotator(config)
    print("Derotator skonfigurowany")
```

```cpp
// Przykład C++
astro_mount::DerotatorConfig config;
config.set_type(astro_mount::DerotatorConfig::CANOPEN);
config.set_connection_string("can0:3");
config.set_gear_ratio(5.0);
config.set_max_speed(5.0);
config.set_max_acceleration(1.0);
config.set_backlash(2.5);
config.set_absolute_encoder(true);
config.set_encoder_resolution(131072);

grpc::ClientContext context;
google::protobuf::Empty response;
stub->ConfigureDerotator(&context, config, &response);
```

#### EnableFieldRotation - Włączenie kompensacji pola widzenia

```python
def enable_field_rotation(szerokosc, wysokosc, azymut):
    """Włączenie kompensacji pola widzenia dla montażu azymutalnego."""
    params = proto.FieldRotationParams()
    params.enabled = True
    params.latitude = szerokosc
    params.altitude = wysokosc
    params.azimuth = azymut

    stub.EnableFieldRotation(params)
    print("Kompensacja pola widzenia włączona")
```

#### ControlFieldRotation - Bezpośrednie sterowanie polem widzenia

```python
def control_field_rotation(tryb, kat_docelowy=None, predkosc=None):
    """Sterowanie kątem lub prędkością pola widzenia."""
    request = proto.FieldRotationControlRequest()

    if tryb == "alt_az":
        request.mode = proto.FieldRotationControlRequest.ALT_AZ
    elif tryb == "fixed_angle":
        request.mode = proto.FieldRotationControlRequest.FIXED_ANGLE
        request.target_angle = kat_docelowy
    elif tryb == "custom":
        request.mode = proto.FieldRotationControlRequest.CUSTOM
        request.rotation_rate = predkosc

    stub.ControlFieldRotation(request)
    print(f"Sterowanie polem widzenia ustawione na tryb: {tryb}")
```

#### HomeDerotator - Homeowanie derotatora

```python
def home_derotator():
    """Homeowanie derotatora - znalezienie pozycji zerowej."""
    request = proto.DerotatorHomingRequest()
    request.method = proto.DerotatorHomingRequest.AUTO
    request.search_speed = 2.0
    request.calibrate_after = True

    stub.HomeDerotator(request)
    print("Homeowanie derotatora rozpoczęte")

    # Odpytywanie aż do zakończenia
    while True:
        status = stub.GetDerotatorStatus(google_dot_protobuf_dot_empty__pb2.Empty())
        if status.homed:
            print(f"Derotator zahomeowany pod kątem: {status.current_angle:.2f} deg")
            break
        time.sleep(0.5)
```

### 12. Konfiguracja HAL

#### GetHALConfig - Pobranie konfiguracji HAL

```python
def get_hal_config():
    """Pobranie aktualnej konfiguracji HAL."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    config = stub.GetHALConfig(empty)

    print(f"Konfiguracja HAL:")
    print(f"  Typ: {proto.HALType.Name(config.type)}")
    print(f"  Nazwa: {config.name}")
    print(f"  Osie: {len(config.axes)}")
    for axis in config.axes:
        print(f"    Oś {axis.id}: {axis.name}")

    return config
```

#### SetHALConfig - Aktualizacja konfiguracji HAL

```python
def set_hal_config():
    """Aktualizacja konfiguracji HAL."""
    request = proto.HALConfigRequest()
    request.config.type = proto.HAL_CANOPEN
    request.config.name = "Główna warstwa HAL montażu"

    # Konfiguracja PID
    request.config.pid_params.kp = 0.5
    request.config.pid_params.ki = 0.01
    request.config.pid_params.kd = 0.001
    request.config.pid_params.integral_limit = 100.0
    request.config.pid_params.output_limit = 200.0

    # Konfiguracja bezpieczeństwa
    request.config.safety.enable_limits = True
    request.config.safety.enable_emergency_stop = True
    request.config.safety.enable_temperature_monitoring = True

    stub.SetHALConfig(request)
    print("Konfiguracja HAL zaktualizowana")
```

#### GetHALStatus - Pobranie statusu HAL

```python
def get_hal_status():
    """Pobranie statusu i możliwości HAL."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    status = stub.GetHALStatus(empty)

    print(f"Status HAL:")
    print(f"  Zainicjalizowany: {status.initialized}")
    print(f"  Uruchomiony: {status.running}")
    print(f"  Typ: {proto.HALType.Name(status.type)}")
    print(f"  Platforma: {status.platform_name}")
    print(f"  Obsługiwane funkcje: {', '.join(status.supported_features)}")

    return status
```

### 13. Kalibracja bootstrapowa (Initial Alignment)

#### AddBootstrapMeasurement - Dodanie pomiaru bootstrapowego

```python
def add_bootstrap_measurement(observed_ra, observed_dec, expected_ra, expected_dec):
    """Dodanie pomiaru bootstrapowego do wstępnego ustawienia."""
    measurement = proto.BootstrapMeasurement()
    measurement.observed.ra = observed_ra
    measurement.observed.dec = observed_dec
    measurement.expected.ra = expected_ra
    measurement.expected.dec = expected_dec
    measurement.estimated_error_arcsec = 30.0
    measurement.use_for_initial_alignment = True

    now = time.time()
    measurement.timestamp.seconds = int(now)
    measurement.timestamp.nanos = int((now - int(now)) * 1e9)

    stub.AddBootstrapMeasurement(measurement)
    print(f"Pomiar bootstrapowy dodany: ({observed_ra:.4f}, {observed_dec:.4f})")
```

#### RunBootstrapCalibration - Uruchomienie kalibracji bootstrapowej

```python
def run_bootstrap_calibration():
    """Uruchomienie kalibracji bootstrapowej w celu obliczenia wstępnego ustawienia."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    result = stub.RunBootstrapCalibration(empty)

    if result.success:
        print(f"Kalibracja bootstrapowa zakończona sukcesem!")
        print(f"  Błąd ustawienia: {result.alignment_error_arcsec:.2f} arcsec")
        print(f"  Wykorzystane pomiary: {result.measurement_count}")
        print(f"  Gotowy do TPOINT: {result.ready_for_tpoint}")
    else:
        print(f"Kalibracja nie powiodła się: {result.error_message}")

    return result
```

#### GetBootstrapStatus - Pobranie statusu kalibracji

```python
def get_bootstrap_status():
    """Pobranie statusu kalibracji bootstrapowej."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    status = stub.GetBootstrapStatus(empty)

    print(f"Status bootstrapa:")
    print(f"  Skalibrowany: {status.calibrated}")
    print(f"  Pomiary: {status.measurement_count}")
    print(f"  Błąd ustawienia: {status.current_alignment_error_arcsec:.2f} arcsec")
    print(f"  Gotowy do TPOINT: {status.ready_for_tpoint}")

    return status
```

### 14. Niskopoziomowe sterowanie osiami

#### ControlAxis - Bezpośrednie sterowanie osią

```python
def control_axis(axis_id, target_position, velocity):
    """Bezpośrednie sterowanie pozycją osi."""
    request = proto.AxisControlRequest()
    request.axis_id = axis_id
    request.mode = proto.POSITION_CONTROL
    request.target_position = target_position
    request.max_velocity = velocity
    request.acceleration = 1.0

    stub.ControlAxis(request)
    print(f"Oś {axis_id} przesuwa się do {target_position:.2f} deg")
```

#### StopAxis - Zatrzymanie osi

```python
def stop_axis(axis_id, decelerate=True):
    """Zatrzymanie określonej osi."""
    request = proto.AxisStopRequest()
    request.axis_id = axis_id
    request.decelerate = decelerate
    request.deceleration = 2.0

    stub.StopAxis(request)
    print(f"Oś {axis_id} zatrzymana")
```

#### GetAxisStatus - Pobranie statusu osi

```python
def get_axis_status():
    """Pobranie szczegółowego statusu osi."""
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    status = stub.GetAxisStatus(empty)

    print(f"Status osi:")
    print(f"  Aktualna pozycja: {status.current_position:.4f} deg")
    print(f"  Aktualna prędkość: {status.current_velocity:.6f} deg/s")
    print(f"  W ruchu: {status.moving}")
    print(f"  Cel osiągnięty: {status.target_reached}")
    print(f"  Błąd: {status.error}")

    return status
```

## Kompleksowe scenariusze użycia

### Scenariusz 1: Pełna sesja obserwacyjna
```python
def full_observing_session(target_ra, target_dec, object_name):
    """Kompletna sesja obserwacyjna od inicjalizacji do parkowania."""
    
    # 1. Inicjalizacja
    print("=== Initializing observing session ===")
    config = get_full_configuration()
    
    # 2. Włączenie enkoderów
    enable_encoders('ABSOLUTE', 36000)
    
    # 3. Przesunięcie do celu
    print(f"\n=== Slew to target: {object_name} ===")
    slew_to_target(target_ra, target_dec)
    time.sleep(5)  # Czekaj na ukończenie przesunięcia
    
    # 4. Rozpoczęcie śledzenia
    print(f"\n=== Start tracking ===")
    track_object(target_ra, target_dec, object_name)
    
    # 5. Włączenie guidera
    print(f"\n=== Enable guider ===")
    connect_guider('localhost', 7624)
    
    # 6. Monitorowanie przez 10 minut
    print(f"\n=== Monitoring for 10 minutes ===")
    for i in range(10):
        state = get_mount_state()
        print(f"Minute {i+1}: Pointing error = {state.pointing_error:.2f}\"")
        time.sleep(60)
    
    # 7. Parkowanie
    print(f"\n=== Parking mount ===")
    disconnect_guider()
    stop_mount()
    park_mount()
    
    print("\n=== Observing session completed ===")
```

### Scenariusz 2: Kalibracja TPOINT
```python
def tpoint_calibration_session():
    """Sesja kalibracji modelu TPOINT."""
    
    print("=== Starting TPOINT calibration session ===")
    
    # 1. Zbieranie pomiarów
    print("\n=== Collecting measurements ===")
    stars = [
        (0.0, 90.0),   # Polaris
        (6.0, 45.0),   # Capella
        (12.0, 0.0),   # Spica
        (18.0, -30.0)  # Fomalhaut
    ]
    
    for ra, dec in stars:
        # Symulacja pomiaru z błędem
        observed_ra = ra + (random.random() - 0.5) * 0.01
        observed_dec = dec + (random.random() - 0.5) * 0.01
        
        add_calibration_measurement(observed_ra, observed_dec, ra, dec)
        print(f"Added measurement for RA={ra}h, Dec={dec}°")
        time.sleep(1)
    
    # 2. Uruchomienie kalibracji
    print("\n=== Running TPOINT calibration ===")
    run_tpoint_calibration()
    time.sleep(2)
    
    # 3. Pobranie wyników
    print("\n=== Getting calibration results ===")
    params = get_tpoint_parameters()
    
    # 4. Weryfikacja poprawy
    print("\n=== Verification ===")
    initial_state = get_mount_state()
    print(f"Initial pointing error: {initial_state.pointing_error:.2f}\"")
    
    # Po kalibracji błąd powinien się zmniejszyć
    print("TPOINT calibration session completed")
```

### Scenariusz 3: Śledzenie satelity
```python
def satellite_tracking_session(satellite_id):
    """Śledzenie satelity na podstawie efemeryd."""
    
    print(f"=== Satellite tracking session: {satellite_id} ===")
    
    # 1. Przesłanie efemeryd
    print("\n=== Uploading satellite ephemeris ===")
    upload_satellite_ephemeris(satellite_id)
    
    # 2. Rozpoczęcie śledzenia
    print("\n=== Starting ephemeris tracking ===")
    status = start_ephemeris_tracking(satellite_id)
    
    # 3. Monitorowanie śledzenia
    print("\n=== Monitoring tracking ===")
    for i in range(5):
        status = get_ephemeris_status()
        print(f"Update {i+1}: Error={status.position_error_arcsec:.2f}\", "
              f"Remaining={status.time_remaining_seconds:.0f}s")
        time.sleep(30)
    
    # 4. Zatrzymanie śledzenia
    print("\n=== Stopping tracking ===")
    stop_ephemeris_tracking()
    
    # 5. Pobranie metryk
    print("\n=== Getting tracking metrics ===")
    empty = proto.google_dot_protobuf_dot_empty__pb2.Empty()
    metrics = stub.GetEphemerisMetrics(empty)
    
    print(f"Tracking metrics:")
    print(f"  Total track time: {metrics.total_track_time_seconds}s")
    print(f"  Avg position error: {metrics.avg_position_error_arcsec:.2f}\"")
    print(f"  Max position error: {metrics.max_position_error_arcsec:.2f}\"")
    
    print("\n=== Satellite tracking session completed ===")
```

### Scenariusz 4: Wstępna kalibracja montażu (Bootstrap)

```python
def initial_mount_calibration():
    """Wstępna kalibracja montażu z użyciem bootstrap i wyznaczania bieguna.
    
    Ten scenariusz przedstawia kompletny przepływ pracy podczas
    początkowej kalibracji:
    1. Dodanie pomiarów bootstrap z jasnych gwiazd
    2. Uruchomienie solvera kalibracji bootstrap
    3. Wyznaczenie precyzyjnej pozycji bieguna
    4. Weryfikacja jakości kalibracji
    """
    
    print("=== Wstępna kalibracja montażu ===")
    
    # 1. Dodanie pomiarów bootstrap
    #    Należy wycelować w 2-3 znane jasne gwiazdy w celu ustalenia
    #    początkowej orientacji montażu
    print("\n=== Krok 1: Dodawanie pomiarów bootstrap ===")
    
    bootstrap_stars = [
        {"ra": 2.32, "dec": 89.26, "name": "Polaris"},
        {"ra": 6.75, "dec": 45.0,  "name": "Capella"},
        {"ra": 10.68, "dec": 12.5, "name": "Regulus"},
    ]
    
    for star in bootstrap_stars:
        # Symulacja małych błędów wskazywania dla realistycznych pomiarów
        observed_ra = star["ra"] + (random.random() - 0.5) * 0.5
        observed_dec = star["dec"] + (random.random() - 0.5) * 0.5
        
        success = add_bootstrap_measurement(
            observed_ra, observed_dec,
            star["ra"], star["dec"],
            temperature=15.0, pressure=1013.0, humidity=0.5
        )
        print(f"  {'✓' if success else '✗'} {star['name']}: "
              f"observed=({observed_ra:.4f}h, {observed_dec:.2f}°)")
        time.sleep(1)
    
    # 2. Uruchomienie kalibracji bootstrap
    print("\n=== Krok 2: Uruchamianie kalibracji bootstrap ===")
    success = run_bootstrap_calibration()
    if not success:
        print("✗ Kalibracja bootstrap nie powiodła się — sprawdź pomiary")
        return False
    
    # 3. Pobranie statusu i metryk jakości kalibracji
    print("\n=== Krok 3: Metryki jakości kalibracji ===")
    status = get_bootstrap_status()
    print(f"  Jakość wyrównania: {status.alignment_quality:.2f}%")
    print(f"  Błąd RMS: {status.rms_error_arcsec:.2f}\"")
    print(f"  Liczba pomiarów: {status.measurement_count}")
    print(f"  Skalibrowany: {status.calibrated}")
    
    if status.rms_error_arcsec > 30.0:
        print("⚠️  Błąd RMS > 30\" — rozważ dodanie większej liczby pomiarów")
    
    # 4. Wyznaczenie pozycji bieguna (metoda dryfu)
    print("\n=== Krok 4: Wyznaczanie pozycji bieguna ===")
    print("Rozpoczynanie wyrównania przez dryf (5 minut)...")
    ra_correction, dec_correction, accuracy = determine_pole_position(5.0 / 60.0)
    
    print(f"  Korekta RA: {ra_correction:.2f}\"")
    print(f"  Korekta Dec: {dec_correction:.2f}\"")
    print(f"  Dokładność: {accuracy:.2f}\"")
    
    if accuracy > 60.0:
        print("⚠️  Dokładność wyrównania bieguna > 60\" — powtórz dla lepszej precyzji")
    
    # 5. Weryfikacja końcowej dokładności wskazywania
    print("\n=== Krok 5: Weryfikacja wskazywania ===")
    state = get_mount_state()
    print(f"  Stan montażu: {state.state}")
    print(f"  Błąd wskazywania: {state.pointing_error:.2f}\"")
    
    if state.pointing_error < 10.0:
        print("\n✅ Kalibracja montażu zakończona — gotowy do obserwacji")
    else:
        print("\n⚠️  Błąd wskazywania > 10\" — rozważ kalibrację TPOINT")
    
    return True
```

## Rozwiązywanie problemów

### Obsługa błędów
```python
def safe_api_call(api_function, *args, **kwargs):
    """Bezpieczne wywołanie API z obsługą błędów."""
    try:
        return api_function(*args, **kwargs)
    except grpc.RpcError as e:
        if e.code() == grpc.StatusCode.UNAVAILABLE:
            print("Error: Service unavailable. Check if server is running.")
        elif e.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
            print("Error: Request timeout. Operation took too long.")
        elif e.code() == grpc.StatusCode.INVALID_ARGUMENT:
            print(f"Error: Invalid arguments: {e.details()}")
        elif e.code() == grpc.StatusCode.FAILED_PRECONDITION:
            print(f"Error: Precondition failed: {e.details()}")
        else:
            print(f"Error {e.code()}: {e.details()}")
        return None
```

### Diagnostyka połączenia
```python
def diagnose_connection():
    """Diagnostyka połączenia z serwerem."""
    print("=== Connection diagnostics ===")
    
    try:
        # Test podstawowego połączenia
        health = check_system_health()
        print(f"✓ Health check passed: {health.status}")
        
        # Test pobrania stanu
        state = get_mount_state()
        print(f"✓ State retrieval passed: {state.status}")
        
        # Test konfiguracji
        config = get_full_configuration()
        print(f"✓ Configuration retrieval passed")
        
        print("\n=== All connection tests passed ===")
        return True
        
    except grpc.RpcError as e:
        print(f"✗ Connection test failed: {e.code()} - {e.details()}")
        return False
```

## Najlepsze praktyki

1. **Zawsze sprawdzaj status przed operacjami** - Użyj `GetState()` aby upewnić się, że montaż jest w odpowiednim stanie.
2. **Obsługuj błędy gRPC** - Zawsze używaj bloków try-except dla wywołań API.
3. **Używaj znaczników czasu** - Dla operacji wymagających synchronizacji czasowej zawsze używaj znaczników czasu.
4. **Monitoruj zużycie zasobów** - Regularnie sprawdzaj zdrowie systemu za pomocą `CheckHealth()`.
5. **Zapisuj stany krytyczne** - Używaj `SaveState()` przed ważnymi operacjami.
6. **Kalibruj regularnie** - Wykonuj kalibrację TPOINT po zmianie lokalizacji lub parametrów montażu.
7. **Weryfikuj konfigurację** - Po zmianie konfiguracji pobierz ją z powrotem i zweryfikuj.
8. **Używaj trajektorii dla płynnych ruchów** - Dla precyzyjnych ruchów używaj `GenerateTrajectory()` i `ExecuteTrajectory()`.

---

*Ten dokument zawiera przykłady dla wszystkich 32 metod API zdefiniowanych w `proto/mount_controller.proto`. Przykłady pokrywają wszystkie scenariusze użycia systemu Astronomical Mount Controller.*