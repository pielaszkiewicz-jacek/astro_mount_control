# Astronomical Mount Controller - Examples

This directory contains example code for using the Astronomical Mount Controller.

## C++ Examples

### `example_usage.cpp`
A comprehensive example demonstrating:
- Controller initialization and configuration
- Slew operations to equatorial coordinates
- Object tracking with different modes
- Calibration measurement collection
- TPOINT model calibration
- Rotation matrix retrieval
- Pole position determination
- State save/load operations
- Guider integration
- Mount parking and shutdown

**Building and running:**
```bash
cd examples/cpp
g++ -std=c++17 -I../../include -L../../build/lib -lastro_mount_core example_usage.cpp -o example_usage
./example_usage
```

## Python Examples

### `example_usage.py`
A Python gRPC client example demonstrating:
- Connection to the gRPC server
- Remote mount control operations
- State monitoring and status display
- Calibration data submission
- TPOINT parameter retrieval
- Pole position determination via drift method
- Encoder configuration
- Guider integration
- State management operations

**Requirements:**
```bash
pip install grpcio grpcio-tools google-protobuf
```

**Running:**
```bash
cd examples/python
python example_usage.py
```

**Note:** The gRPC Python code needs to be generated first. Use the build script:
```bash
cd ../..
./scripts/build.sh
```

## API Usage Patterns

### 1. Basic Mount Control
```python
# Connect to controller
client = MountControllerClient('localhost:50051')

# Slew to target
client.slew_to_coordinates(10.0, 45.0)

# Start tracking
client.start_tracking(12.0, 30.0)

# Stop motion
client.stop()

# Park mount
client.park()
```

### 2. Calibration and TPOINT
```python
# Add calibration measurements
for i in range(10):
    observed_ra = expected_ra + random_error_ra
    observed_dec = expected_dec + random_error_dec
    client.add_measurement(observed_ra, observed_dec, expected_ra, expected_dec)

# Get TPOINT parameters
tpoint_params = client.get_tpoint_parameters()
print(f"TPOINT chi-squared: {tpoint_params.chi_squared}")
```

### 3. Advanced Features
```python
# Determine pole position
pole = client.determine_pole_position(duration_hours=2.0)
print(f"Pole: Lat={pole.latitude}°, Lon={pole.longitude}°")

# Enable encoders
client.enable_encoders('ABSOLUTE', resolution=36000)

# Connect guider
client.connect_guider('tcp://localhost:7624')

# Send guider corrections
client.send_guider_correction(2.5, -1.5)
```

### 4. State Management
```python
# Save controller state
response = client.save_state("backup_state.json")
print(f"State saved to {response.file_path}")

# Load controller state
client.load_state("backup_state.json")
```

## Testing the System

### Unit Tests
```bash
cd build
./test_astronomical_calculations
./test_tpoint_model
```

### Integration Test
A simple integration test script is provided to verify all components work together.

## Troubleshooting

### Common Issues

1. **gRPC connection refused**: Ensure the mount controller service is running
   ```bash
   sudo systemctl status astro-mount-controller
   ```

2. **Missing dependencies**: Install required libraries
   ```bash
   sudo apt-get install libsofa-dev libeigen3-dev libgrpc++-dev
   ```

3. **Permission errors**: Check user permissions and service configuration
   ```bash
   sudo chown -R astro:astro /var/log/astro-mount
   ```

### Debugging Tips

- Enable debug logging in configuration:
  ```json
  {
    "logging": {
      "level": "DEBUG",
      "console_output": true
    }
  }
  ```

- Check system logs:
  ```bash
  sudo journalctl -u astro-mount-controller -f
  ```

- Test gRPC connectivity:
  ```bash
  grpc_cli call localhost:50051 GetState ""
  ```

## Next Steps

1. **Custom Integration**: Modify examples to integrate with your specific hardware
2. **GUI Development**: Build a graphical interface using the gRPC API
3. **Automation Scripts**: Create scripts for automated observing sessions
4. **Data Analysis**: Extend with data analysis tools for measurement validation

For more information, see the main project documentation.