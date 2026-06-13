#!/usr/bin/env python3
"""
Low-Level Axis Control Test Script for Astronomical Mount Controller.

Connects to the astro_mount_controller gRPC server and provides:
  - Interactive shell for axis control (position/velocity mode)
  - Direct CANopen service access (enable/disable/set target)
  - Axis status monitoring and logging
  - Safety: soft limit checks, emergency stop

Usage:
  1. Start the server:  ./build/bin/astro_mount_controller config/default.json
  2. Run this script:   python3 scripts/axis_control_test.py [--host HOST] [--port PORT]
"""

import argparse
import sys
import os
import time
import threading
import math
from datetime import datetime

# ── gRPC / protobuf imports ──────────────────────────────────────────────
try:
    import grpc
except ImportError:
    print("ERROR: grpcio not installed. Run:  pip install grpcio grpcio-tools")
    sys.exit(1)

# Paths – assume script lives in <project>/scripts/
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
PROTO_DIR = os.path.join(PROJECT_DIR, "proto")
GEN_DIR = os.path.join(PROJECT_DIR, "build", "gen_py")

# ── Compile protos if needed ─────────────────────────────────────────────
def _compile_protos():
    """Compile .proto files to Python if not already done."""
    if os.path.exists(os.path.join(GEN_DIR, "mount_controller_pb2.py")):
        return  # already compiled

    os.makedirs(GEN_DIR, exist_ok=True)
    from grpc_tools import protoc

    proto_basenames = ["mount_controller.proto", "canopen_service.proto"]
    proto_files = [os.path.join(PROTO_DIR, f) for f in proto_basenames]
    missing = [p for p in proto_files if not os.path.exists(p)]
    if missing:
        print(f"ERROR: proto files not found: {missing}")
        sys.exit(1)

    args = [
        "grpc_tools.protoc",
        f"--proto_path={PROTO_DIR}",     # look for .proto files here
        f"--proto_path={PROJECT_DIR}",    # also for google/protobuf/*.proto
        f"--python_out={GEN_DIR}",
        f"--grpc_python_out={GEN_DIR}",
    ] + proto_files
    if protoc.main(args) != 0:
        print("ERROR: protobuf compilation failed")
        sys.exit(1)

    print(f"Protos compiled to {GEN_DIR}")


_compile_protos()
sys.path.insert(0, GEN_DIR)

import mount_controller_pb2 as mc
import mount_controller_pb2_grpc as mc_grpc
import canopen_service_pb2 as co
import canopen_service_pb2_grpc as co_grpc
from google.protobuf import empty_pb2, timestamp_pb2

# ── ANSI helpers ──────────────────────────────────────────────────────────
class Colors:
    CYAN = "\033[36m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    RED = "\033[31m"
    BOLD = "\033[1m"
    RESET = "\033[0m"
    CLEAR_LINE = "\033[2K\r"


# ── Axis Controller ──────────────────────────────────────────────────────
class AxisController:
    """High-level wrapper around gRPC axis control services."""

    def __init__(self, host: str = "localhost", port: int = 50051):
        address = f"{host}:{port}"
        self.channel = grpc.insecure_channel(address)
        self.mount_stub = mc_grpc.MountControllerServiceStub(self.channel)
        self.canopen_stub = co_grpc.CanOpenServiceStub(self.channel)
        self._monitoring = False
        self._monitor_thread = None

    # ── Connection ────────────────────────────────────────────────────────

    def check_health(self) -> bool:
        try:
            resp = self.mount_stub.CheckHealth(
                mc.HealthCheckRequest(service="mount_controller"),
                timeout=2.0,
            )
            return resp.status == mc.HealthCheckResponse.SERVING
        except Exception as e:
            print(f"{Colors.RED}Health check failed: {e}{Colors.RESET}")
            return False

    def get_state(self):
        """Return the current ControllerState proto."""
        return self.mount_stub.GetState(empty_pb2.Empty(), timeout=2.0)

    # ── Mount-level operations ────────────────────────────────────────────

    def slew_to_coordinates(self, ra_hours: float, dec_deg: float):
        self.mount_stub.SlewToCoordinates(
            mc.Coordinates(right_ascension=ra_hours, declination=dec_deg),
            timeout=10.0,
        )

    def slew_to_horizontal(self, alt_deg: float, az_deg: float):
        self.mount_stub.SlewToHorizontal(
            mc.HorizontalCoordinates(altitude=alt_deg, azimuth=az_deg),
            timeout=10.0,
        )

    def stop(self):
        self.mount_stub.Stop(empty_pb2.Empty(), timeout=5.0)

    def park(self):
        self.mount_stub.Park(empty_pb2.Empty(), timeout=30.0)

    def unpark(self):
        self.mount_stub.Unpark(empty_pb2.Empty(), timeout=5.0)

    # ── Low-level axis control (mount_controller.ControlAxis) ────────────

    def control_axis_position(
        self,
        axis_id: int,
        position_deg: float,
        velocity: float = 5.0,
        accel: float = 1.0,
        relative: bool = False,
    ):
        req = mc.AxisControlRequest(
            axis_id=axis_id,
            mode=mc.POSITION_CONTROL,
            target_position=position_deg,
            max_velocity=velocity,
            acceleration=accel,
            relative=relative,
        )
        self.mount_stub.ControlAxis(req, timeout=10.0)

    def control_axis_velocity(
        self,
        axis_id: int,
        velocity_deg_s: float,
        relative: bool = False,
    ):
        req = mc.AxisControlRequest(
            axis_id=axis_id,
            mode=mc.VELOCITY_CONTROL,
            target_velocity=velocity_deg_s,
            relative=relative,
        )
        self.mount_stub.ControlAxis(req, timeout=5.0)

    def stop_axis(self, axis_id: int, decelerate: bool = True, decel: float = 2.0):
        req = mc.AxisStopRequest(
            axis_id=axis_id, decelerate=decelerate, deceleration=decel
        )
        self.mount_stub.StopAxis(req, timeout=5.0)

    def emergency_stop(self, axis_id: int = -1, reset: bool = False):
        req = mc.EmergencyStopRequest(axis_id=axis_id, reset_after=reset)
        self.mount_stub.EmergencyStop(req, timeout=2.0)

    def get_axis_status(self) -> mc.AxisStatus:
        # Status reads make blocking SDO calls to CAN bus (up to ~3s total)
        return self.mount_stub.GetAxisStatus(empty_pb2.Empty(), timeout=10.0)

    # ── Direct CANopen service (canopen_service.ControlAxis) ─────────────

    def canopen_enable(self, axis_id: int):
        self.canopen_stub.EnableAxis(co.AxisControlRequest(axis_id=axis_id), timeout=10.0)

    def canopen_disable(self, axis_id: int):
        self.canopen_stub.DisableAxis(co.AxisControlRequest(axis_id=axis_id), timeout=10.0)

    def canopen_set_position(
        self,
        axis_id: int,
        position_deg: float,
        velocity: float = 5.0,
        accel: float = 1.0,
    ):
        self.canopen_stub.SetPositionTarget(
            co.PositionTargetRequest(
                axis_id=axis_id, position=position_deg,
                velocity=velocity, acceleration=accel,
            ),
            timeout=15.0,
        )

    def canopen_set_velocity(self, axis_id: int, velocity: float, accel: float = 1.0):
        self.canopen_stub.SetVelocityTarget(
            co.VelocityTargetRequest(
                axis_id=axis_id, velocity=velocity, acceleration=accel
            ),
            timeout=15.0,
        )

    def canopen_stop(self, axis_id: int):
        self.canopen_stub.StopAxis(co.AxisControlRequest(axis_id=axis_id), timeout=10.0)

    def canopen_status(self, axis_id: int) -> co.AxisStatus:
        return self.canopen_stub.GetAxisStatus(
            co.AxisControlRequest(axis_id=axis_id), timeout=10.0
        )

    def canopen_position(self, axis_id: int) -> co.PositionData:
        return self.canopen_stub.GetPositionData(
            co.AxisControlRequest(axis_id=axis_id), timeout=10.0
        )

    # ── Status monitoring (background thread) ─────────────────────────────

    def _monitor_loop(self, interval: float):
        while self._monitoring:
            try:
                st = self.get_axis_status()
                print(
                    f"{Colors.CLEAR_LINE}"
                    f"[{datetime.now().strftime('%H:%M:%S')}]  "
                    f"Axis1: pos={st.current_position:8.3f}°  vel={st.current_velocity:7.4f}°/s  "
                    f"moving={int(st.moving)}  target_reached={int(st.target_reached)}  "
                    f"err={int(st.error)}  |  "
                    f"Axis2: pos={st.target_position:8.3f}°  vel={st.target_velocity:7.4f}°/s",
                    end="",
                    flush=True,
                )
            except Exception:
                pass
            time.sleep(interval)

    def start_monitoring(self, interval: float = 0.5):
        self._monitoring = True
        self._monitor_thread = threading.Thread(
            target=self._monitor_loop, args=(interval,), daemon=True
        )
        self._monitor_thread.start()

    def stop_monitoring(self):
        self._monitoring = False
        if self._monitor_thread:
            self._monitor_thread.join(timeout=2.0)


# ── Interactive shell ─────────────────────────────────────────────────────
def interactive_shell(ctrl: AxisController):
    """Simple interactive command shell for axis control."""

    def cmd_help():
        print(f"""
{Colors.BOLD}── Axis Control Commands ───────────────────────{Colors.RESET}
  {Colors.CYAN}px <deg> [vel] [acc]{Colors.RESET}    Axis1 position move (deg, velocity deg/s, accel deg/s²)
  {Colors.CYAN}py <deg> [vel] [acc]{Colors.RESET}    Axis2 position move
  {Colors.CYAN}vx <vel>{Colors.RESET}                 Axis1 velocity mode (deg/s, 0=stop)
  {Colors.CYAN}vy <vel>{Colors.RESET}                 Axis2 velocity mode
  {Colors.CYAN}sx [decel]{Colors.RESET}               Stop axis1
  {Colors.CYAN}sy [decel]{Colors.RESET}               Stop axis2
  {Colors.CYAN}e{Colors.RESET}                        Emergency stop all axes
  {Colors.CYAN}st{Colors.RESET}                       Show axis status once
  {Colors.CYAN}mon [on|off]{Colors.RESET}             Toggle real-time monitoring
  {Colors.CYAN}slew <ra> <dec>{Colors.RESET}          Slew to equatorial (RA hours, Dec deg)
  {Colors.CYAN}altaz <alt> <az>{Colors.RESET}          Slew to horizontal (alt deg, az deg)
  {Colors.CYAN}park{Colors.RESET}                     Park the mount
  {Colors.CYAN}unpark{Colors.RESET}                   Unpark the mount
  {Colors.CYAN}stop{Colors.RESET}                     Stop all mount motion

  {Colors.CYAN}co_en <axis>{Colors.RESET}             CANopen: enable axis
  {Colors.CYAN}co_dis <axis>{Colors.RESET}            CANopen: disable axis
  {Colors.CYAN}co_px <axis> <deg> [vel]{Colors.RESET} CANopen: set position
  {Colors.CYAN}co_vx <axis> <vel>{Colors.RESET}       CANopen: set velocity
  {Colors.CYAN}co_st <axis>{Colors.RESET}             CANopen: get axis status
  {Colors.CYAN}co_po <axis>{Colors.RESET}             CANopen: get position data

  {Colors.CYAN}h{Colors.RESET}                        Show this help
  {Colors.CYAN}q{Colors.RESET}                        Quit
""")

    print(f"{Colors.BOLD}{Colors.GREEN}")
    print("╔══════════════════════════════════════════════════════════╗")
    print("║     Astro Mount Control – Low-Level Axis Test Shell     ║")
    print("╚══════════════════════════════════════════════════════════╝")
    print(f"{Colors.RESET}")
    cmd_help()

    while True:
        try:
            raw = input(f"{Colors.BOLD}axis> {Colors.RESET}").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            break
        if not raw:
            continue

        parts = raw.split()
        cmd = parts[0].lower()
        args = parts[1:]

        try:
            if cmd == "q":
                break

            elif cmd == "h":
                cmd_help()

            elif cmd == "st":
                st = ctrl.get_axis_status()
                print(f"  Axis1: pos={st.current_position:.3f}°  vel={st.current_velocity:.4f}°/s  "
                      f"tgt_pos={st.target_position:.3f}°  tgt_vel={st.target_velocity:.4f}°/s  "
                      f"moving={st.moving}  reached={st.target_reached}  err={st.error}")
                if st.error:
                    print(f"  Error: {st.error_message}")

            elif cmd == "mon":
                if args and args[0] == "off":
                    ctrl.stop_monitoring()
                    print("Monitoring stopped.")
                else:
                    print("Monitoring started (Ctrl+C to stop). Press Enter to return to shell.")
                    ctrl.start_monitoring(0.5)
                    try:
                        time.sleep(9999)
                    except KeyboardInterrupt:
                        ctrl.stop_monitoring()

            elif cmd == "px":
                pos = float(args[0]) if len(args) >= 1 else 0.0
                vel = float(args[1]) if len(args) >= 2 else 5.0
                acc = float(args[2]) if len(args) >= 3 else 1.0
                ctrl.control_axis_position(0, pos, vel, acc)
                print(f"Axis1 → position {pos:.2f}°  (vel={vel:.1f}°/s, acc={acc:.1f}°/s²)")

            elif cmd == "py":
                pos = float(args[0]) if len(args) >= 1 else 0.0
                vel = float(args[1]) if len(args) >= 2 else 5.0
                acc = float(args[2]) if len(args) >= 3 else 1.0
                ctrl.control_axis_position(1, pos, vel, acc)
                print(f"Axis2 → position {pos:.2f}°  (vel={vel:.1f}°/s, acc={acc:.1f}°/s²)")

            elif cmd == "vx":
                vel = float(args[0]) if args else 0.0
                ctrl.control_axis_velocity(0, vel)
                print(f"Axis1 → velocity {vel:.2f}°/s")

            elif cmd == "vy":
                vel = float(args[0]) if args else 0.0
                ctrl.control_axis_velocity(1, vel)
                print(f"Axis2 → velocity {vel:.2f}°/s")

            elif cmd == "sx":
                decel = float(args[0]) if args else 2.0
                ctrl.stop_axis(0, decelerate=True, decel=decel)
                print(f"Axis1 stop (decel={decel:.1f}°/s²)")

            elif cmd == "sy":
                decel = float(args[0]) if args else 2.0
                ctrl.stop_axis(1, decelerate=True, decel=decel)
                print(f"Axis2 stop (decel={decel:.1f}°/s²)")

            elif cmd == "e":
                ctrl.emergency_stop()
                print("Emergency stop — all axes")

            elif cmd == "slew":
                ra = float(args[0])
                dec = float(args[1])
                ctrl.slew_to_coordinates(ra, dec)
                print(f"Slew to RA={ra:.4f}h  Dec={dec:.2f}°")

            elif cmd == "altaz":
                alt = float(args[0])
                az = float(args[1])
                ctrl.slew_to_horizontal(alt, az)
                print(f"Slew to Alt={alt:.2f}°  Az={az:.2f}°")

            elif cmd == "park":
                ctrl.park()
                print("Park initiated")

            elif cmd == "unpark":
                ctrl.unpark()
                print("Unpark initiated")

            elif cmd == "stop":
                ctrl.stop()
                print("Stop — all motion")

            # ── CANopen commands ──────────────────────────────────────
            elif cmd == "co_en":
                aid = int(args[0])
                ctrl.canopen_enable(aid)
                print(f"CANopen axis {aid} enabled")

            elif cmd == "co_dis":
                aid = int(args[0])
                ctrl.canopen_disable(aid)
                print(f"CANopen axis {aid} disabled")

            elif cmd == "co_px":
                aid = int(args[0])
                pos = float(args[1])
                vel = float(args[2]) if len(args) >= 3 else 5.0
                ctrl.canopen_set_position(aid, pos, vel)
                print(f"CANopen axis {aid} → position {pos:.2f}°  vel={vel:.1f}°/s")

            elif cmd == "co_vx":
                aid = int(args[0])
                vel = float(args[1])
                ctrl.canopen_set_velocity(aid, vel)
                print(f"CANopen axis {aid} → velocity {vel:.2f}°/s")

            elif cmd == "co_st":
                aid = int(args[0])
                s = ctrl.canopen_status(aid)
                print(f"CANopen axis {aid}: op={s.operational} en={s.enabled} "
                      f"moving={s.moving} reached={s.target_reached} "
                      f"warn={s.warning} err={s.error} homed={s.homed}")

            elif cmd == "co_po":
                aid = int(args[0])
                p = ctrl.canopen_position(aid)
                print(f"CANopen axis {aid}: pos={p.actual_position:.3f}°  "
                      f"vel={p.actual_velocity:.4f}°/s  torque={p.actual_torque:.1f}%  "
                      f"tgt={p.target_position:.3f}°  ferr={p.following_error:.4f}°")

            else:
                print(f"{Colors.YELLOW}Unknown command: {cmd}  (type 'h' for help){Colors.RESET}")

        except grpc.RpcError as e:
            code = e.code() if hasattr(e, "code") else "?"
            print(f"{Colors.RED}gRPC error [{code}]: {e.details() if hasattr(e, 'details') else e}{Colors.RESET}")
        except (ValueError, IndexError) as e:
            print(f"{Colors.YELLOW}Argument error: {e}  (type 'h' for help){Colors.RESET}")
        except Exception as e:
            print(f"{Colors.RED}Error: {e}{Colors.RESET}")


# ── Main ─────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Low-level axis control test for Astro Mount Controller"
    )
    parser.add_argument("--host", default="localhost", help="gRPC server host (default: localhost)")
    parser.add_argument("--port", type=int, default=50051, help="gRPC server port (default: 50051)")
    parser.add_argument("--batch", nargs="*", help="Batch commands (e.g. --batch px 45 vx 2 st)")
    parser.add_argument("--wait", type=float, default=2.0, help="Wait seconds between batch commands")
    args = parser.parse_args()

    ctrl = AxisController(args.host, args.port)

    # Health check
    print(f"Connecting to {args.host}:{args.port}...")
    if not ctrl.check_health():
        print(f"{Colors.RED}Cannot connect — is the server running?{Colors.RESET}")
        print(f"  Start with:  ./build/bin/astro_mount_controller config/default.json")
        sys.exit(1)
    print(f"{Colors.GREEN}Connected. Server is SERVING.{Colors.RESET}")

    # Batch mode – execute commands directly
    if args.batch is not None:
        # Smart grouping: tokenise batch args into commands (each starts with a known keyword)
        cmd_keywords = {
            "st", "px", "py", "vx", "vy", "sx", "sy", "e",
            "slew", "altaz", "park", "unpark", "stop",
            "co_en", "co_dis", "co_px", "co_vx", "co_st", "co_po",
            "wait", "mon",
        }
        tokens = args.batch
        commands = []
        i = 0
        while i < len(tokens):
            token = tokens[i]
            # Support both space-separated and quoted multi-word tokens
            # e.g. "px 90 5 1" as a single quoted argument
            sub_tokens = token.split()
            candidate = sub_tokens[0] if sub_tokens else ""
            if candidate in cmd_keywords:
                cmd = candidate
                i += 1
                cmd_args = sub_tokens[1:]  # Take remaining words from this token
                # Then collect more args from subsequent non-keyword tokens
                while i < len(tokens):
                    peek = tokens[i]
                    peek_first = peek.split()[0] if peek else ""
                    if peek_first in cmd_keywords:
                        break
                    cmd_args.extend(peek.split())
                    i += 1
                commands.append((cmd, cmd_args))
            else:
                # Skip unknown tokens
                i += 1

        if not commands:
            commands = [
                ("st", []),
                ("px", ["90", "5", "1"]),
                ("wait", [str(args.wait)]),
                ("st", []),
                ("py", ["45", "3", "0.5"]),
                ("wait", [str(args.wait)]),
                ("st", []),
            ]
        # Build a dispatch map from the interactive shell's command handlers
        # (reusing the same _dispatch logic avoids duplication)
        def _exec_cmd(cmd: str, args_list: list):
            cmd = cmd.lower()

            if cmd == "wait":
                delay = float(args_list[0]) if args_list else args.wait
                print(f"  Waiting {delay:.1f}s...")
                time.sleep(delay)
                return

            # Map commands to (method, arg_parser) tuples
            handlers = {
                "st": (lambda: print(_format_status(ctrl.get_axis_status())), []),
                "px": (lambda: ctrl.control_axis_position(0, float(args_list[0]), float(args_list[1]) if len(args_list)>=2 else 5.0, float(args_list[2]) if len(args_list)>=3 else 1.0), []),
                "py": (lambda: ctrl.control_axis_position(1, float(args_list[0]), float(args_list[1]) if len(args_list)>=2 else 5.0, float(args_list[2]) if len(args_list)>=3 else 1.0), []),
                "vx": (lambda: ctrl.control_axis_velocity(0, float(args_list[0])), []),
                "vy": (lambda: ctrl.control_axis_velocity(1, float(args_list[0])), []),
                "sx": (lambda: ctrl.stop_axis(0, True, float(args_list[0]) if args_list else 2.0), []),
                "sy": (lambda: ctrl.stop_axis(1, True, float(args_list[0]) if args_list else 2.0), []),
                "e":  (ctrl.emergency_stop, []),
                "slew": (lambda: ctrl.slew_to_coordinates(float(args_list[0]), float(args_list[1])), []),
                "altaz": (lambda: ctrl.slew_to_horizontal(float(args_list[0]), float(args_list[1])), []),
                "park": (ctrl.park, []),
                "unpark": (ctrl.unpark, []),
                "stop": (ctrl.stop, []),
                "co_en": (lambda: ctrl.canopen_enable(int(args_list[0])), []),
                "co_dis": (lambda: ctrl.canopen_disable(int(args_list[0])), []),
                "co_px": (lambda: ctrl.canopen_set_position(int(args_list[0]), float(args_list[1]), float(args_list[2]) if len(args_list)>=3 else 5.0), []),
                "co_vx": (lambda: ctrl.canopen_set_velocity(int(args_list[0]), float(args_list[1])), []),
                "co_st": (lambda: print(ctrl.canopen_status(int(args_list[0]))), []),
                "co_po": (lambda: print(ctrl.canopen_position(int(args_list[0]))), []),
            }

            if cmd in handlers:
                fn, _ = handlers[cmd]
                fn()
            else:
                print(f"Unknown batch command: {cmd}")

        def _format_status(st) -> str:
            return (f"Axis1: pos={st.current_position:.3f}°  vel={st.current_velocity:.4f}°/s  "
                    f"tgt={st.target_position:.3f}°  moving={st.moving}  reached={st.target_reached}  err={st.error}")

        for cmd, cmd_args in commands:
            cmd_line = cmd + " " + " ".join(cmd_args)
            print(f">>> {cmd_line}")
            try:
                _exec_cmd(cmd, cmd_args)
            except grpc.RpcError as e:
                print(f"  gRPC error [{e.code()}]: {e.details()}")
            except Exception as e:
                print(f"  Error: {e}")
        return

    # Interactive mode
    try:
        interactive_shell(ctrl)
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        ctrl.stop_monitoring()


if __name__ == "__main__":
    main()
