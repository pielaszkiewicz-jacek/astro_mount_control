#!/usr/bin/env python3
"""
Astronomical Mount Controller - Fixed Python gRPC Client Example

This example demonstrates how to use the gRPC API to control
the astronomical mount controller from Python.
"""

import grpc
import time
from datetime import datetime
from google.protobuf.timestamp_pb2 import Timestamp
from google.protobuf.empty_pb2 import Empty

# Import generated gRPC code
import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../../build/proto'))

try:
    import mount_controller_pb2 as pb
    import mount_controller_pb2_grpc as pb_grpc
except ImportError:
    print("Error: Generated gRPC code not found.")
    print("Please build the project first to generate the Python gRPC code.")
    sys.exit(1)


class MountControllerClient:
    """Client for the Mount Controller gRPC service."""
    
    def __init__(self, address='localhost:50051'):
        """Initialize the client."""
        self.channel = grpc.insecure_channel(address)
        self.stub = pb_grpc.MountControllerServiceStub(self.channel)
        print(f"Connected to mount controller at {address}")
    
    def close(self):
        """Close the connection."""
        self.channel.close()
    
    def slew_to_coordinates(self, ra, dec):
        """Slew to equatorial coordinates."""
        coords = pb.Coordinates(ra=ra, dec=dec)
        self.stub.SlewToCoordinates(coords)
        print(f"Slewing to RA={ra}h, Dec={dec}°")
    
    def start_tracking(self, ra, dec):
        """Start tracking an object."""
        coords = pb.Coordinates(ra=ra, dec=dec)
        self.stub.TrackObject(coords)
        print(f"Started tracking RA={ra}h, Dec={dec}°")
    
    def stop(self):
        """Stop all motion."""
        self.stub.Stop(Empty())
        print("Stopped all motion")
    
    def park(self):
        """Park the mount."""
        self.stub.Park(Empty())
        print("Parking mount")
    
    def get_state(self):
        """Get the current controller state."""
        return self.stub.GetState(Empty())
    
    def add_measurement(self, observed_ra, observed_dec, expected_ra, expected_dec):
        """Add a calibration measurement."""
        timestamp = Timestamp()
        timestamp.FromDatetime(datetime.utcnow())
        
        measurement = pb.Measurement(
            observed=pb.Coordinates(ra=observed_ra, dec=observed_dec),
            expected=pb.Coordinates(ra=expected_ra, dec=expected_dec),
            timestamp=timestamp
        )
        
        self.stub.AddMeasurement(measurement)
        print(f"Added measurement: observed ({observed_ra}h, {observed_dec}°), "
              f"expected ({expected_ra}h, {expected_dec}°)")
    
    def get_tpoint_parameters(self):
        """Get TPOINT parameters."""
        return self.stub.GetTPointParameters(Empty())
    
    def get_rotation_matrix(self):
        """Get the rotation matrix."""
        return self.stub.GetRotationMatrix(Empty())
    
    def determine_pole_position(self, duration_hours=1.0):
        """Determine pole position using drift method."""
        request = pb.PoleDeterminationRequest(duration_hours=duration_hours)
        return self.stub.DeterminePolePosition(request)
    
    def save_state(self, file_path=""):
        """Save controller state."""
        request = pb.StateSaveRequest(file_path=file_path)
        return self.stub.SaveState(request)
    
    def load_state(self, file_path=""):
        """Load controller state."""
        request = pb.StateLoadRequest(file_path=file_path)
        self.stub.LoadState(request)
        print(f"Loaded state from {file_path if file_path else 'default location'}")
    
    def enable_encoders(self, encoder_type='ABSOLUTE', resolution=36000):
        """Enable encoders."""
        config = pb.EncoderConfig(
            type=pb.EncoderConfig.EncoderType.ABSOLUTE if encoder_type == 'ABSOLUTE' 
            else pb.EncoderConfig.EncoderType.INCREMENTAL,
            resolution=resolution,
            use_feedback=True
        )
        self.stub.EnableEncoders(config)
        print(f"Enabled {encoder_type.lower()} encoders with resolution {resolution}")
    
    def connect_guider(self, connection_string="tcp://localhost:7624"):
        """Connect to guider."""
        config = pb.GuiderConfig(
            connection_string=connection_string,
            max_correction=10.0,
            aggression=0.5
        )
        self.stub.ConnectGuider(config)
        print(f"Connected to guider at {connection_string}")
    
    def send_guider_correction(self, ra_correction, dec_correction):
        """Send guider correction."""
        correction = pb.GuiderCorrection(
            ra_correction=ra_correction,
            dec_correction=dec_correction
        )
        self.stub.SendGuiderCorrection(correction)
        print(f"Sent guider correction: RA={ra_correction}\", Dec={dec_correction}\"")
    
    def get_configuration(self):
        """Get current configuration."""
        return self.stub.GetConfiguration(Empty())
    
    def update_configuration(self, config):
        """Update configuration."""
        self.stub.UpdateConfiguration(config)
        print("Configuration updated")


def print_state(state):
    """Print controller state in a readable format."""
    status_names = {
        0: "UNKNOWN",
        1: "IDLE",
        2: "SLEWING",
        3: "TRACKING",
        4: "PARKED",
        5: "ERROR"
    }
    
    print(f"\nCurrent State:")
    print(f"  Status: {status_names.get(state.status, 'UNKNOWN')}")
    if state.tracked_object and state.tracked_object.coordinates:
        print(f"  Tracked object: RA={state.tracked_object.coordinates.ra:.3f}h, Dec={state.tracked_object.coordinates.dec:.3f}°")
    if state.current_position:
        print(f"  Position: Axis1={state.current_position.axis1:.3f}°, "
              f"Axis2={state.current_position.axis2:.3f}°")
    print(f"  Encoders: {'Enabled' if state.encoders_enabled else 'Disabled'}")
    print(f"  Guider: {'Active' if state.guider_active else 'Inactive'}")
    print(f"  TPOINT: {'Calibrated' if state.tpoint_params.coefficients else 'Not calibrated'}")


def main():
    """Main example function."""
    print("Astronomical Mount Controller - Python gRPC Client Example")
    print("==========================================================")
    
    # Create client
    client = MountControllerClient('localhost:50051')
    
    try:
        # Example 1: Get current state
        print("\n--- Example 1: Get Current State ---")
        state = client.get_state()
        print_state(state)
        
        # Example 2: Slew to coordinates
        print("\n--- Example 2: Slew to Coordinates ---")
        client.slew_to_coordinates(10.0, 45.0)
        
        # Wait a bit for slew to start
        time.sleep(2)
        
        # Check state during slew
        state = client.get_state()
        print_state(state)
        
        # Example 3: Start tracking
        print("\n--- Example 3: Start Tracking ---")
        client.start_tracking(12.0, 30.0)
        
        # Track for a few seconds
        for i in range(3):
            time.sleep(1)
            state = client.get_state()
            print(f"\nTracking update {i+1}:")
            print(f"  Position: {state.current_position.axis1:.3f}°, {state.current_position.axis2:.3f}°")
        
        # Example 4: Add calibration measurements
        print("\n--- Example 4: Add Calibration Measurements ---")
        import random
        
        for i in range(3):
            ra = 10.0 + i * 0.5
            dec = 40.0 + i * 1.0
            
            # Add small random errors
            observed_ra = ra + random.uniform(-0.005, 0.005)
            observed_dec = dec + random.uniform(-0.05, 0.05)
            
            client.add_measurement(observed_ra, observed_dec, ra, dec)
        
        # Example 5: Get TPOINT parameters
        print("\n--- Example 5: Get TPOINT Parameters ---")
        tpoint_params = client.get_tpoint_parameters()
        if tpoint_params.coefficients:
            print(f"TPOINT has {len(tpoint_params.coefficients)} coefficients")
            print(f"Chi-squared: {tpoint_params.chi_squared:.3f}")
        else:
            print("No TPOINT parameters available (need more measurements)")
        
        # Example 6: Get rotation matrix
        print("\n--- Example 6: Get Rotation Matrix ---")
        rotation = client.get_rotation_matrix()
        print(f"Rotation quaternion: q0={rotation.q0:.6f}, q1={rotation.q1:.6f}, "
              f"q2={rotation.q2:.6f}, q3={rotation.q3:.6f}")
        
        # Example 7: Determine pole position (simulated)
        print("\n--- Example 7: Determine Pole Position ---")
        try:
            pole = client.determine_pole_position(0.1)  # 0.1 hours for demo
            print(f"Pole position: Lat={pole.latitude:.6f}°, Lon={pole.longitude:.6f}°")
            print(f"Accuracy: {pole.accuracy:.2f} arcseconds")
        except Exception as e:
            print(f"Pole determination failed (may not be implemented yet): {e}")
        
        # Example 8: Enable encoders
        print("\n--- Example 8: Enable Encoders ---")
        client.enable_encoders('ABSOLUTE', 36000)
        
        # Example 9: Connect to guider
        print("\n--- Example 9: Connect to Guider ---")
        client.connect_guider()
        
        # Example 10: Send guider correction
        print("\n--- Example 10: Send Guider Correction ---")
        client.send_guider_correction(2.5, -1.5)
        
        # Example 11: Save state
        print("\n--- Example 11: Save Controller State ---")
        response = client.save_state("mount_state.json")
        print(f"State saved to: {response.file_path}")
        print(f"File size: {response.file_size} bytes")
        
        # Example 12: Stop tracking
        print("\n--- Example 12: Stop Tracking ---")
        client.stop()
        time.sleep(1)
        
        state = client.get_state()
        print_state(state)
        
        # Example 13: Park mount
        print("\n--- Example 13: Park Mount ---")
        client.park()
        
        # Wait for park to complete
        for i in range(5):
            time.sleep(1)
            state = client.get_state()
            if state.status == pb.ControllerState.PARKED:
                print("Mount parked successfully")
                break
        
        # Final state
        print("\n--- Final State ---")
        state = client.get_state()
        print_state(state)
        
        print("\nExample completed successfully!")
        
    except grpc.RpcError as e:
        print(f"gRPC error: {e.code()} - {e.details()}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.close()


if __name__ == '__main__':
    main()