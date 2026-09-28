#!/usr/bin/env bash
#
# canopen_params_2000h.sh — odczyt i zapis parametrów grupy 10.2
# "制造商定义参数 2000h 组说明" (Manufacturer Defined Parameter Group 2000h)
# napędów NiMotion STMP42SXI (BLDC/PMSM, CANopen/CiA402) przez SDO.
#
# Źródło: docs/1787619745915-ymnq3q.pdf, rozdział 10.2 (tabela 2000h–2031h).
#
# Grupa obejmuje parametry producenta:
#   2000h   parametry silnika PMSM (napięcie, moc, prądy, momenty, prędkości,
#           rezystancja/indukcyjności, enkoder)
#   2001h   parametry sterownika (prądy wyjściowe, progi napięcia szyny, filtry)
#   2002h   sterowanie podstawowe (CtrlModeSelec 0–5, wersje FW/HW)
#   2003h/2004h   wejścia/wyjścia fizyczne (DI1–DI9, DO1–DO5, AI1/AI2)
#   2005h/2006h/2007h   parametry pozycji/prędkości/momentu (tryby NiMotion)
#   2008h/2009h/200Ah   regulatory, filtry, notch, zabezpieczenia
#   200Bh   parametry monitorowane (RO)
#   200Ch   konfiguracja komunikacji (adres osi, baud-rate)
#   200Eh   mapowanie kodów błędów (int32)
#   2011h/2012h   wielosegmentowa pozycja / prędkość (16 segmentów)
#   2017h   wirtualne wejścia VDI1–VDI16
#   2031h   zmienne komunikacyjne (poziom VDI, stan DO)
#
# Użycie:
#   ./scripts/canopen_params_2000h.sh <node_id ...> [interfejs_can]           # odczyt wszystkich
#   ./scripts/canopen_params_2000h.sh <node_id> --read <param,...> [iface]    # odczyt wybranych
#   ./scripts/canopen_params_2000h.sh <node_id> --write <param>=<w> [iface]   # zapis
#   ./scripts/canopen_params_2000h.sh --list                                 # lista parametrów
#
# Format <param>: "2002:01", "0x2002:0x01", "2002h:01h", "2002" (cały indeks)
#                 lub nazwa (np. "CtrlModeSelec", "MaxSpeed").
#
# Przykłady:
#   ./scripts/canopen_params_2000h.sh 1 can0                    # pełny zrzut grupy 2000h
#   ./scripts/canopen_params_2000h.sh 1 --read 2002:01 can0     # tryb sterowania
#   ./scripts/canopen_params_2000h.sh 1 --write 2002:01=0 can0  # CiA402 mode

set -euo pipefail

CAN_IF="${CAN_IF:-can0}"

usage() {
    cat <<'EOF'
Użycie:
  canopen_params_2000h.sh <node_id ...> [interfejs_can]
  canopen_params_2000h.sh <node_id ...> --read <param,...> [interfejs_can]
  canopen_params_2000h.sh <node_id> --write <param>=<wartość> [interfejs_can] [--save]
  canopen_params_2000h.sh --list

Argumenty:
  node_id            adresy CANopen (1..127). W trybie zapisu dokładnie jeden.
  interfejs_can      interfejs CAN (domyślnie: can0 lub zmienna CAN_IF)

Opcje:
  --list             wypisz wszystkie parametry grupy 2000h (bez dostępu do CAN)
  --read <param,...> odczyt wybranych parametrów (domyślnie: wszystkie)
  --write <p>=<w>    zapis parametru; <p> = "2002:01", "MaxSpeed" itp.,
                     <w> = wartość dziesiętna lub 0x... (hex)
  --save             po zapisie zapisz parametry do EEPROM (1010h:01h = 0x65766173)

Format <param>:
  2002:01            indeks i subindeks (hex)
  0x2002:0x01        jawnie hex
  2002h:01h          notacja dokumentacji
  2002               cały indeks (wszystkie subindeksy)
  MaxSpeed           nazwa (lub fragment nazwy)

Przykłady:
  ./scripts/canopen_params_2000h.sh 1 can0
  ./scripts/canopen_params_2000h.sh 1 --read 2002:01 can0
  ./scripts/canopen_params_2000h.sh 1 --write CtrlModeSelec=0 can0
EOF
}

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die()  { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

is_valid_node_id() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 127 ))
}

hex32le() {
    local v="$1" h
    h=$(printf '%016X' "$v")
    h=${h:8:8}
    printf '%s%s%s%s' "${h:6:2}" "${h:4:2}" "${h:2:2}" "${h:0:2}"
}

# ─────────────────────────────────────────────────────────────────────────────
# Rejestr parametrów grupy 2000h (10.2) — STMP42SXI.
# Format wiersza: INDEX:SUB|NAME|TYPE|SIZE|ACCESS|UNIT|FACTORY|OPTS
#   TYPE  : uint8|int8|uint16|int16|uint32|int32
#   SIZE  : liczba bajtów danych SDO (1, 2 lub 4)
#   ACCESS: RO|RW
# ─────────────────────────────────────────────────────────────────────────────
declare -a PARAMS

load_registry() {
    PARAMS=()
    local line
    while IFS= read -r line; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        PARAMS+=("$line")
    done <<'REGISTRY_EOF'
# 2000h — parametry silnika
2000:06|BusEncoderType|uint16|2|RW|-|1|0-1
2000:07|RatedVoltage|uint16|2|RW|V|48|24-48
2000:08|RatedPower|uint16|2|RW|0.01KW|40|
2000:09|RatedCurrent|uint16|2|RW|0.01A|1250|
2000:0A|MaxCurrent|uint16|2|RW|0.01A|2250|
2000:0B|MaxCurrentDuration|uint16|2|RW|0.1s|3000|
2000:0C|RatedTorque|uint16|2|RW|0.01Nm|127|
2000:0D|MaxTorque|uint16|2|RW|0.01Nm|2000|
2000:0E|RatedSpeed|uint16|2|RW|rpm|3000|
2000:0F|MaxSpeed|uint16|2|RW|rpm|6000|
2000:10|InertiaJm|uint16|2|RW|0.01kgcm2|30|
2000:11|MotorPolePairs|uint16|2|RW|1|4|
2000:12|StatorResistance|uint16|2|RW|0.001Ohm|115|
2000:13|StatorInductanceLq|uint16|2|RW|0.01mH|25|
2000:14|StatorInductanceLd|uint16|2|RW|0.01mH|25|
2000:16|TorqueCoeffKt|uint16|2|RW|0.01Nm/Arms|11|
2000:19|EncoderAbsBits|uint32|4|RW|bit|17|17-bit
2000:1A|EncoderPoles|uint16|2|RW|1|1|
2000:1C|PositionSensorSelect|uint16|2|RW|-|2|
2000:1D|ZSignalElecAngle|uint16|2|RW|1/4096|0|
2000:1E|UPhaseRiseElecAngle|uint16|2|RW|1/4096|0|
# 2001h — parametry sterownika
2001:07|RatedOutputCurrent|uint16|2|RO|0.01A|1200|
2001:08|MaxOutputCurrent|uint16|2|RO|0.01A|1500|
2001:0E|BusOverVoltageProtect|uint16|2|RW|V|100|
2001:0F|BusVoltageReleasePoint|uint16|2|RW|V|55|
2001:10|BusUnderVoltagePoint|uint16|2|RW|V|24|
2001:11|DriverOverCurrentProtect|uint16|2|RW|A|10|
2001:18|CurrentSampleFilter|uint16|2|RW|0.01|100|
2001:1C|CurrentLoopCutoffFreq|uint16|2|RW|Hz|800|
2001:1D|OpenLoopRunCurrent|uint16|2|RW|0.01A|625|
# 2002h — sterowanie podstawowe
2002:01|CtrlModeSelec|uint16|2|RW|-|4|0=CiA402,1=NiMotion pos,2=NiMotion vel,3=NiMotion torque,4=NiMotion open-loop,5=motor identification
2002:04|OutputPulseDuty|uint16|2|RW|1%|50|0-100
2002:13|BrakeResistorSelect|uint16|2|RW|1|0|0-2
2002:1C|FirmwareCode|uint32|4|RO|-|0|
2002:1E|IAPSoftwareVersion|uint32|4|RO|-|0|
2002:1F|HardwareVersion|uint32|4|RO|-|0|
# 2003h — wejścia fizyczne
2003:03|DI1FunSelec|uint16|2|RW|1|1|0-48
2003:04|DI1LogicSelec|uint16|2|RW|1|0|0-4
2003:05|DI2FunSelec|uint16|2|RW|1|2|0-48
2003:06|DI2LogicSelec|uint16|2|RW|1|2|0-4
2003:07|DI3FunSelec|uint16|2|RW|1|12|0-48
2003:08|DI3LogicSelec|uint16|2|RW|1|0|0-4
2003:09|DI4FunSelec|uint16|2|RW|1|0|0-48
2003:0A|DI4LogicSelec|uint16|2|RW|1|0|0-4
2003:0B|DI5FunSelec|uint16|2|RW|1|0|0-48
2003:0C|DI5LogicSelec|uint16|2|RW|1|0|0-4
2003:0D|DI6FunSelec|uint16|2|RW|1|0|0-48
2003:0E|DI6LogicSelec|uint16|2|RW|1|0|0-4
2003:0F|DI7FunSelec|uint16|2|RW|1|0|0-48
2003:10|DI7LogicSelec|uint16|2|RW|1|0|0-4
2003:11|DI8FunSelec|uint16|2|RW|1|0|0-48
2003:12|DI8LogicSelec|uint16|2|RW|1|0|0-4
2003:13|DI9FunSelec|uint16|2|RW|1|0|0-48
2003:14|DI9LogicSelec|uint16|2|RW|1|0|0-4
2003:15|PowerOnValidFunc|uint16|2|RW|1|0|
2003:16|PowerOnValidFunc2|uint16|2|RW|1|0|
2003:17|AI1Offset|int16|2|RW|mV|0|
2003:18|AI1FilterTime|int16|2|RW|-|0|
2003:19|AI1DeadZone|uint16|2|RW|mV|100|
2003:1A|AI1Gain|int16|2|RW|0.001|0|
2003:1B|AI2Offset|int16|2|RW|mV|0|
2003:1C|AI2FilterTime|int16|2|RW|-|0|
2003:1D|AI2DeadZone|uint16|2|RW|mV|0|
2003:1E|AI2Gain|int16|2|RW|0.001|0|
2003:1F|Analog10VSpeed|uint16|2|RW|rpm|0|0-4000
2003:20|Analog10VTorque|uint16|2|RW|0.1%|0|0-1000
# 2004h — wyjścia fizyczne
2004:01|DO1FunSelec|uint16|2|RW|1|0|0-30
2004:02|DO1LogicSelec|uint16|2|RW|1|0|0-1
2004:03|DO2FunSelec|uint16|2|RW|1|0|0-30
2004:04|DO2LogicSelec|uint16|2|RW|1|0|0-1
2004:05|DO3FunSelec|uint16|2|RW|1|0|0-30
2004:06|DO3LogicSelec|uint16|2|RW|1|0|0-1
2004:07|DO4FunSelec|uint16|2|RW|1|0|0-30
2004:08|DO4LogicSelec|uint16|2|RW|1|0|0-1
2004:09|DO5FunSelec|uint16|2|RW|1|0|0-30
2004:0A|DO5LogicSelec|uint16|2|RW|1|0|0-1
# 2005h — sterowanie pozycją
2005:01|PosCmdSource|uint16|2|RW|1|1|0=pulse,1=step amount
2005:05|StepAmount|int16|2|RW|inc|100|-32768..32767
2005:18|PositionRecoverMode|int16|2|RW|1|0|0-1
2005:1C|HomingTimeLimit|uint16|2|RW|ms|10000|
2005:1D|BlockPosition|int32|4|RW|1|1000|
# 2006h — sterowanie prędkością
2006:01|MainSpeedSourceA|uint16|2|RW|1|0|0=digital(2006:04),3=duty
2006:02|AuxSpeedSourceB|uint16|2|RW|1|0|0=digital(2006:04)
2006:03|SpeedCmdSelect|uint16|2|RW|1|0|0=A,1=B,2=A+B
2006:04|SpeedCmdKeypadValue|int16|2|RW|rpm|10|
2006:06|ActiveSpeedValue|int16|2|RW|rpm|0|
2006:07|SpeedAccelRampTime|uint16|2|RW|ms|10|
2006:08|SpeedDecelRampTime|uint16|2|RW|ms|10|
2006:09|MaxSpeedThreshold|uint16|2|RW|rpm|6000|
2006:0A|FwdSpeedThreshold|uint16|2|RW|rpm|4000|
2006:0B|RevSpeedThreshold|uint16|2|RW|rpm|4000|
2006:12|SpeedFeedbackUnitSelect|uint16|2|RW|1|0|0=rpm,1=user unit
2006:21|TempAlarmUpperThreshold|uint16|2|RW|1C|0|60-120
# 2007h — sterowanie momentem
2007:01|MainTorqueSourceA|uint16|2|RW|1|0|0=digital(2007:04)
2007:02|AuxTorqueSourceB|uint16|2|RW|1|0|0=digital(2007:04)
2007:03|TorqueCmdSelect|uint16|2|RW|1|0|0=A,1=B,2=A+B
2007:04|TorqueCmdKeypadValue|int16|2|RW|0.1%|10|
2007:05|ActiveTorqueValue|int16|2|RW|0.1%|10|
2007:06|TorqueFilterTime|uint16|2|RW|0.01ms|0|
2007:0A|PosInternalTorqueLimit|uint16|2|RW|0.1%|1000|
2007:0B|NegInternalTorqueLimit|uint16|2|RW|0.1%|1000|
2007:10|TorqueCtrlFwdSpeedLimit|uint16|2|RW|rpm|3000|
2007:11|TorqueCtrlRevSpeedLimit|uint16|2|RW|rpm|3000|
2007:12|PTModeTorqueReachThreshold|uint16|2|RW|0.001Nm|0|
2007:13|StallHomeTorque|uint16|2|RW|0.1%|500|
2007:14|PTModeTorqueWindowTime|uint16|2|RW|ms|0|
2007:15|StallHomeTime|uint16|2|RW|ms|500|
# 2008h — regulatory
2008:01|SpeedLoopGain|uint16|2|RW|0.1Hz|500|1-20000
2008:02|SpeedLoopIntegralTime|uint16|2|RW|0.01ms|800|0-51200
2008:03|PositionLoopGain|uint16|2|RW|1|1500|0-20000
2008:04|SecondSpeedLoopGain|uint16|2|RW|0.1Hz|1|1-20000
2008:05|SecondSpeedLoopIntegralTime|uint16|2|RW|0.01ms|15|0-51200
2008:06|SecondPositionLoopGain|uint16|2|RW|1|0|0-20000
2008:08|GainSwitchMode|uint16|2|RW|1|0|0-9
2008:09|GainSwitchCondition|uint16|2|RW|1|0|0-10
2008:0A|GainSwitchDelayTime|uint16|2|RW|0.1ms|0|0-10000
2008:0B|GainSwitchLevel|uint16|2|RW|1|0|0-20000
2008:0C|GainSwitchHysteresis|uint16|2|RW|1|0|0-20000
2008:0F|SpeedFdFwdFilterTime|uint16|2|RW|0.01ms|0|
2008:10|SpeedFdFwdGain|uint16|2|RW|0.1%|0|
2008:11|TorqueFdFwdFilterTime|uint16|2|RW|0.01ms|0|
2008:12|TorqueFdFwdGain|uint16|2|RW|0.001|0|
2008:14|SpeedFbLowPassCutoff|uint16|2|RW|Hz|900|0-4000
# 2009h — filtry notch
2009:06|OfflInertiaAutoTunMode|uint16|2|RO|1|0|
2009:0D|Notch1Freq|uint16|2|RW|Hz|0|0-2000
2009:0E|Notch1Width|uint16|2|RW|Hz|0|0-2000
2009:0F|Notch1Depth|uint16|2|RW|%|0|0-100
2009:10|Notch2Freq|uint16|2|RW|Hz|0|0-2000
2009:11|Notch2Width|uint16|2|RW|Hz|0|0-2000
2009:12|Notch2Depth|uint16|2|RW|1|0|0-100
2009:13|Notch3Freq|uint16|2|RW|Hz|0|0-2000
2009:14|Notch3Width|uint16|2|RW|Hz|0|0-2000
2009:15|Notch3Depth|uint16|2|RW|1|0|0-100
2009:16|Notch4Freq|uint16|2|RW|Hz|0|0-2000
2009:17|Notch4Width|uint16|2|RW|Hz|0|0-2000
2009:18|Notch4Depth|uint16|2|RW|1|0|0-100
# 200Ah — zabezpieczenia
200A:06|OverspeedFaultThreshold|uint16|2|RW|1|0|
# 200Bh — parametry monitorowane
200B:01|DriverState|uint16|2|RO|1|0|0=not ready,1=ready,6=pos closed loop,8=vel closed loop,9=torque,10=open loop,12=error
200B:02|ActualMotorSpeed|int16|2|RO|rpm|0|
200B:04|InternalTorqueCmd|int16|2|RO|-|0|
200B:05|InputSignalMonitor|uint16|2|RO|1|0|
200B:06|OutputSignalMonitor|uint16|2|RO|1|0|
200B:0A|InputPWMFreq|uint16|2|RO|Hz|0|
200B:0F|TotalPowerOnTime|uint32|4|RO|s|0|
200B:10|AI1SampledVoltage|uint16|2|RO|mV|-1200|
200B:11|AI2SampledVoltage|uint16|2|RO|mV|-1200|
200B:12|PhaseACurrent|uint16|2|RO|0.01A|0|
200B:13|PhaseBCurrent|uint16|2|RO|0.01A|0|
200B:14|PhaseCCurrent|uint16|2|RO|0.01A|0|
200B:15|BusVoltage|uint16|2|RO|0.1V|0|
200B:16|ModuleTemperature|int16|2|RO|C|0|
200B:2D|ActualMotorSpeedFine|uint16|2|RO|0.1rpm|0|
# 200Ch — komunikacja
200C:01|CommunicationSelect|uint16|2|RW|1|1|0-2
200C:02|AxisAddress|uint16|2|RW|1|1|1-247 (Node-ID CANopen: 1-127)
200C:03|SerialBaudRate|uint16|2|RW|1|8|0=10k,1=20k,2=50k,3=100k,4=125k,5=250k,6=500k,7=800k,8=1M
# 200Eh — kody błędów
200E:01|MotorOverload|int32|4|RW|1|67183377|
200E:02|MotorStall|int32|4|RW|1|8978|
200E:03|PowerOvervoltage|int32|4|RW|1|78352|
200E:04|PowerUndervoltage|int32|4|RW|1|16855584|
200E:05|OverTemperature|int32|4|RW|1|82448|
200E:06|UnderTemperature|int32|4|RW|1|82464|
200E:07|ParamSetError|int32|4|RW|1|90912|
200E:08|MotorOverspeed|int32|4|RW|1|94992|
200E:09|CommFault|int32|4|RW|1|95489|
200E:0A|HomingTimeoutError|int32|4|RW|1|34320|
200E:0B|PositionError|int32|4|RW|1|34321|
200E:0C|SoftwareLimitFault|int32|4|RW|1|50431507|
200E:0D|LimitSwitchFault|int32|4|RW|1|50431507|
200E:0E|OutputPhaseFault|int32|4|RW|1|12933|
200E:0F|CurvePlanningError|int32|4|RW|1|67208725|
200E:10|TargetPositionOverflow|int32|4|RW|1|99862|
200E:11|CurveParamTooSmall|int32|4|RW|1|83985943|
# 2011h — wielosegmentowa pozycja
2011:01|MultiPosRunMode|uint16|2|RW|1|0|0-2
2011:02|MultiPosEndSegment|uint16|2|RW|1|1|1-16
2011:04|MultiPosLoopCount|uint16|2|RW|1|0|
2011:05|MultiPosCmdType|uint16|2|RW|1|0|0=relative,1=absolute
2011:06|MultiPosLoopStartSegment|uint16|2|RW|1|0|0-16
2011:07|Seg1MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:08|Seg1MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:09|Seg1AccelTime|uint16|2|RW|ms|0|
2011:0A|Seg1WaitTime|uint16|2|RW|ms|0|
2011:0B|Seg2MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:0C|Seg2MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:0D|Seg2AccelTime|uint16|2|RW|ms|0|
2011:0E|Seg2WaitTime|uint16|2|RW|ms|0|
2011:0F|Seg3MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:10|Seg3MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:11|Seg3AccelTime|uint16|2|RW|ms|0|
2011:12|Seg3WaitTime|uint16|2|RW|ms|0|
2011:13|Seg4MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:14|Seg4MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:15|Seg4AccelTime|uint16|2|RW|ms|0|
2011:16|Seg4WaitTime|uint16|2|RW|ms|0|
2011:17|Seg5MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:18|Seg5MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:19|Seg5AccelTime|uint16|2|RW|ms|0|
2011:1A|Seg5WaitTime|uint16|2|RW|ms|0|
2011:1B|Seg6MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:1C|Seg6MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:1D|Seg6AccelTime|uint16|2|RW|ms|0|
2011:1E|Seg6WaitTime|uint16|2|RW|ms|0|
2011:1F|Seg7MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:20|Seg7MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:21|Seg7AccelTime|uint16|2|RW|ms|0|
2011:22|Seg7WaitTime|uint16|2|RW|ms|0|
2011:23|Seg8MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:24|Seg8MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:25|Seg8AccelTime|uint16|2|RW|ms|0|
2011:26|Seg8WaitTime|uint16|2|RW|ms|0|
2011:27|Seg9MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:28|Seg9MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:29|Seg9AccelTime|uint16|2|RW|ms|0|
2011:2A|Seg9WaitTime|uint16|2|RW|ms|0|
2011:2B|Seg10MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:2C|Seg10MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:2D|Seg10AccelTime|uint16|2|RW|ms|0|
2011:2E|Seg10WaitTime|uint16|2|RW|ms|0|
2011:2F|Seg11MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:30|Seg11MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:31|Seg11AccelTime|uint16|2|RW|ms|0|
2011:32|Seg11WaitTime|uint16|2|RW|ms|0|
2011:33|Seg12MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:34|Seg12MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:35|Seg12AccelTime|uint16|2|RW|ms|0|
2011:36|Seg12WaitTime|uint16|2|RW|ms|0|
2011:37|Seg13MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:38|Seg13MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:39|Seg13AccelTime|uint16|2|RW|ms|0|
2011:3A|Seg13WaitTime|uint16|2|RW|ms|0|
2011:3B|Seg14MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:3C|Seg14MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:3D|Seg14AccelTime|uint16|2|RW|ms|0|
2011:3E|Seg14WaitTime|uint16|2|RW|ms|0|
2011:3F|Seg15MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:40|Seg15MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:41|Seg15AccelTime|uint16|2|RW|ms|0|
2011:42|Seg15WaitTime|uint16|2|RW|ms|0|
2011:43|Seg16MoveDisplacement|int32|4|RW|UserUnit|-1073741824|
2011:44|Seg16MaxSpeed|uint16|2|RW|rpm|1|0-3000
2011:45|Seg16AccelTime|uint16|2|RW|ms|0|
2011:46|Seg16WaitTime|uint16|2|RW|ms|0|
# 2012h — wielosegmentowa prędkość
2012:01|MultiSpdRunMode|uint16|2|RW|1|0|0-2
2012:02|MultiSpdEndSegment|uint16|2|RW|1|1|0-16
2012:03|MultiSpdLoopCount|uint16|2|RW|1|0|
2012:0C|Seg1SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:0D|Seg1RunTime|uint16|2|RW|ms|0|
2012:0E|Seg1AccelTime|uint16|2|RW|ms|0|
2012:0F|Seg2SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:10|Seg2RunTime|uint16|2|RW|ms|0|
2012:11|Seg2AccelTime|uint16|2|RW|ms|0|
2012:12|Seg3SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:13|Seg3RunTime|uint16|2|RW|ms|0|
2012:14|Seg3AccelTime|uint16|2|RW|ms|0|
2012:15|Seg4SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:16|Seg4RunTime|uint16|2|RW|ms|0|
2012:17|Seg4AccelTime|uint16|2|RW|ms|0|
2012:18|Seg5SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:19|Seg5RunTime|uint16|2|RW|ms|0|
2012:1A|Seg5AccelTime|uint16|2|RW|ms|0|
2012:1B|Seg6SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:1C|Seg6RunTime|uint16|2|RW|ms|0|
2012:1D|Seg6AccelTime|uint16|2|RW|ms|0|
2012:1E|Seg7SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:1F|Seg7RunTime|uint16|2|RW|ms|0|
2012:20|Seg7AccelTime|uint16|2|RW|ms|0|
2012:21|Seg8SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:22|Seg8RunTime|uint16|2|RW|ms|0|
2012:23|Seg8AccelTime|uint16|2|RW|ms|0|
2012:24|Seg9SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:25|Seg9RunTime|uint16|2|RW|ms|0|
2012:26|Seg9AccelTime|uint16|2|RW|ms|0|
2012:27|Seg10SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:28|Seg10RunTime|uint16|2|RW|ms|0|
2012:29|Seg10AccelTime|uint16|2|RW|ms|0|
2012:2A|Seg11SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:2B|Seg11RunTime|uint16|2|RW|ms|0|
2012:2C|Seg11AccelTime|uint16|2|RW|ms|0|
2012:2D|Seg12SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:2E|Seg12RunTime|uint16|2|RW|ms|0|
2012:2F|Seg12AccelTime|uint16|2|RW|ms|0|
2012:30|Seg13SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:31|Seg13RunTime|uint16|2|RW|ms|0|
2012:32|Seg13AccelTime|uint16|2|RW|ms|0|
2012:33|Seg14SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:34|Seg14RunTime|uint16|2|RW|ms|0|
2012:35|Seg14AccelTime|uint16|2|RW|ms|0|
2012:36|Seg15SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:37|Seg15RunTime|uint16|2|RW|ms|0|
2012:38|Seg15AccelTime|uint16|2|RW|ms|0|
2012:39|Seg16SpeedCmd|int16|2|RW|rpm|-6000|0-3000
2012:3A|Seg16RunTime|uint16|2|RW|ms|0|
2012:3B|Seg16AccelTime|uint16|2|RW|ms|0|
# 2017h — wirtualne wejścia VDI
2017:01|VDI1FunSelec|uint16|2|RW|1|0|0-39
2017:02|VDI1LogicSelec|uint16|2|RW|1|0|0-1
2017:03|VDI2FunSelec|uint16|2|RW|1|0|0-39
2017:04|VDI2LogicSelec|uint16|2|RW|1|0|0-1
2017:05|VDI3FunSelec|uint16|2|RW|1|0|0-39
2017:06|VDI3LogicSelec|uint16|2|RW|1|0|0-1
2017:07|VDI4FunSelec|uint16|2|RW|1|0|0-39
2017:08|VDI4LogicSelec|uint16|2|RW|1|0|0-1
2017:09|VDI5FunSelec|uint16|2|RW|1|0|0-39
2017:0A|VDI5LogicSelec|uint16|2|RW|1|0|0-1
2017:0B|VDI6FunSelec|uint16|2|RW|1|0|0-39
2017:0C|VDI6LogicSelec|uint16|2|RW|1|0|0-1
2017:0D|VDI7FunSelec|uint16|2|RW|1|0|0-39
2017:0E|VDI7LogicSelec|uint16|2|RW|1|0|0-1
2017:0F|VDI8FunSelec|uint16|2|RW|1|0|0-39
2017:10|VDI8LogicSelec|uint16|2|RW|1|0|0-1
2017:11|VDI9FunSelec|uint16|2|RW|1|0|0-39
2017:12|VDI9LogicSelec|uint16|2|RW|1|0|0-1
2017:13|VDI10FunSelec|uint16|2|RW|1|0|0-39
2017:14|VDI10LogicSelec|uint16|2|RW|1|0|0-1
2017:15|VDI11FunSelec|uint16|2|RW|1|0|0-39
2017:16|VDI11LogicSelec|uint16|2|RW|1|0|0-1
2017:17|VDI12FunSelec|uint16|2|RW|1|0|0-39
2017:18|VDI12LogicSelec|uint16|2|RW|1|0|0-1
2017:19|VDI13FunSelec|uint16|2|RW|1|0|0-39
2017:1A|VDI13LogicSelec|uint16|2|RW|1|0|0-1
2017:1B|VDI14FunSelec|uint16|2|RW|1|0|0-39
2017:1C|VDI14LogicSelec|uint16|2|RW|1|0|0-1
2017:1D|VDI15FunSelec|uint16|2|RW|1|0|0-39
2017:1E|VDI15LogicSelec|uint16|2|RW|1|0|0-1
2017:1F|VDI16FunSelec|uint16|2|RW|1|0|0-39
2017:20|VDI16LogicSelec|uint16|2|RW|1|0|0-1
# 2031h — zmienne komunikacyjne
2031:01|VDIVirtualLevel|uint16|2|RW|1|0|
2031:02|DOOutputState|uint16|2|RW|1|0|
REGISTRY_EOF
}

# Parsowanie wiersza rejestru do zmiennych globalnych P_*.
read_param_line() {
    IFS='|' read -r P_KEY P_NAME P_TYPE P_SIZE P_ACCESS P_UNIT P_FACTORY P_OPTS <<<"$1"
}

# Czy zapytanie wygląda na "indeks[:subindeks]" (hex, opcjonalnie 0x i h)?
looks_numeric() {
    local q="$1"
    q="${q//0[xX]/}"
    q="${q//[hH]/}"
    q="${q//:/}"
    [[ "$q" =~ ^[0-9A-Fa-f]{4,6}$ ]]
}

# Wyszukuje parametr(y) w rejestrze. Wypisuje pasujące wiersze.
find_param() {
    local q="$1" nq idx sub line li ls
    if looks_numeric "$q"; then
        nq="${q^^}"
        nq="${nq//0X/}"
        nq="${nq//[H]/}"
        idx="${nq%%:*}"
        sub="${nq##*:}"
        [[ "$sub" == "$nq" ]] && sub=""
        for line in "${PARAMS[@]}"; do
            read_param_line "$line"
            li="${P_KEY%%:*}"
            ls="${P_KEY##*:}"
            if (( 16#$li == 16#$idx )); then
                if [[ -z "$sub" ]] || (( 16#$ls == 16#$sub )); then
                    printf '%s\n' "$line"
                fi
            fi
        done
    else
        local pat="${q,,}" exact="" line
        for line in "${PARAMS[@]}"; do
            read_param_line "$line"
            if [[ "${P_NAME,,}" == "$pat" ]]; then
                exact+="$line"$'\n'
            fi
        done
        if [[ -n "$exact" ]]; then
            printf '%s' "$exact"
        else
            for line in "${PARAMS[@]}"; do
                read_param_line "$line"
                if [[ "${P_NAME,,}" == *"$pat"* ]]; then
                    printf '%s\n' "$line"
                fi
            done
        fi
    fi
}

# Wypisuje dokładnie jeden wiersz. Zwraca 1 gdy brak, 2 gdy niejednoznaczne.
resolve_one() {
    local query="$1" matches n
    matches=$(find_param "$query")
    n=$(grep -c . <<<"$matches" || true)
    if (( n == 0 )); then
        return 1
    elif (( n > 1 )); then
        return 2
    fi
    printf '%s\n' "$matches"
    return 0
}

# Wysyła ramkę CAN i czeka na odpowiedź SDO. Nasłuch uruchamiany jest PRZED
# wysłaniem ramki. Wypisuje na stdout dane odpowiedzi (hex) i zwraca 0, gdy
# jej pierwszy bajt zgadza się z oczekiwanym.
sdo_exchange() {
    local node="$1" frame="$2" expected="$3" timeout_ms="${4:-1000}"
    local resp_id tmp dump_pid line data
    resp_id=$(printf '%X' $((0x580 + node)))
    tmp=$(mktemp)
    timeout "$(( (timeout_ms + 999) / 1000 ))s" candump -n 1 "${CAN_IF},${resp_id}:7FF" >"$tmp" 2>/dev/null &
    dump_pid=$!
    sleep 0.1
    if ! cansend "${CAN_IF}" "${frame}" >/dev/null 2>&1; then
        kill "$dump_pid" 2>/dev/null
        wait "$dump_pid" 2>/dev/null
        rm -f "$tmp"
        return 1
    fi
    wait "$dump_pid" 2>/dev/null || true
    if [[ -s "$tmp" ]]; then
        line=$(head -n1 "$tmp")
        rm -f "$tmp"
        data=$(awk '{for(i=4;i<=NF;i++) printf "%s",$i}' <<<"$line" | tr -d '\r ')
        data=$(tr 'a-f' 'A-F' <<<"$data")
        printf '%s' "$data"
        [[ "${data:0:2}" == "$expected" ]]
    else
        rm -f "$tmp"
        return 1
    fi
}

# Odczyt wartości z obiektu <index>:<sub> (size = 1, 2 lub 4 bajty).
read_sdo_uint() {
    local node="$1" idxh="$2" subh="$3" size="$4" signed="$5"
    local idx idxlo idxhi sub frame resp expected value
    idx=$((16#$idxh))
    idxlo=$(printf '%02X' $(( idx & 0xFF )))
    idxhi=$(printf '%02X' $(( (idx >> 8) & 0xFF )))
    sub=$(printf '%02X' $((16#$subh)))
    frame="$(printf '%X' $((0x600 + node)))#40${idxlo}${idxhi}${sub}00000000"
    case "$size" in
        1) expected="4F" ;;
        2) expected="4B" ;;
        4) expected="43" ;;
        *) return 1 ;;
    esac
    if ! resp=$(sdo_exchange "$node" "$frame" "$expected" 500); then
        return 1
    fi
    case "$size" in
        1) value=$((16#${resp:8:2})) ;;
        2) value=$((16#${resp:10:2}${resp:8:2})) ;;
        4) value=$((16#${resp:14:2}${resp:12:2}${resp:10:2}${resp:8:2})) ;;
    esac
    if [[ "$signed" == "signed" ]]; then
        case "$size" in
            1) (( value & 0x80 )) && value=$(( value - 0x100 )) ;;
            2) (( value & 0x8000 )) && value=$(( value - 0x10000 )) ;;
            4) (( value & 0x80000000 )) && value=$(( value - 0x100000000 )) ;;
        esac
    fi
    printf '%s' "$value"
    return 0
}

# Zapis wartości do obiektu <index>:<sub> (size = 1, 2 lub 4 bajty). Zwraca 0/1.
write_sdo_uint() {
    local node="$1" idxh="$2" subh="$3" size="$4" value="$5"
    local idx idxlo idxhi sub b0 b1 b2 b3 cmd frame resp
    idx=$((16#$idxh))
    idxlo=$(printf '%02X' $(( idx & 0xFF )))
    idxhi=$(printf '%02X' $(( (idx >> 8) & 0xFF )))
    sub=$(printf '%02X' $((16#$subh)))
    case "$size" in
        1)
            cmd="2F"
            b0=$(printf '%02X' $(( value & 0xFF )))
            frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}000000"
            ;;
        2)
            cmd="2B"
            b0=$(printf '%02X' $(( value & 0xFF )))
            b1=$(printf '%02X' $(( (value >> 8) & 0xFF )))
            frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}${b1}0000"
            ;;
        4)
            cmd="23"
            b0=$(printf '%02X' $(( value & 0xFF )))
            b1=$(printf '%02X' $(( (value >> 8) & 0xFF )))
            b2=$(printf '%02X' $(( (value >> 16) & 0xFF )))
            b3=$(printf '%02X' $(( (value >> 24) & 0xFF )))
            frame="$(printf '%X' $((0x600 + node)))#${cmd}${idxlo}${idxhi}${sub}${b0}${b1}${b2}${b3}"
            ;;
        *) return 1 ;;
    esac
    log "TX ${frame}"
    if resp=$(sdo_exchange "$node" "$frame" "60" 1000); then
        return 0
    fi
    if [[ "${resp:0:2}" == "80" ]]; then
        log "Abort SDO: ${resp}"
    else
        log "Brak potwierdzenia zapisu"
    fi
    return 1
}

parse_value() {
    local v="$1"
    if [[ "$v" =~ ^0[xX][0-9A-Fa-f]+$ ]]; then
        printf '%s' $((16#${v:2}))
        return 0
    elif [[ "$v" =~ ^-?[0-9]+$ ]]; then
        printf '%s' "$v"
        return 0
    fi
    return 1
}

value_in_range() {
    local type="$1" v="$2"
    case "$type" in
        uint8)  (( v >= 0 && v <= 255 )) ;;
        int8)   (( v >= -128 && v <= 127 )) ;;
        uint16) (( v >= 0 && v <= 65535 )) ;;
        int16)  (( v >= -32768 && v <= 32767 )) ;;
        uint32) (( v >= 0 && v <= 4294967295 )) ;;
        int32)  (( v >= -2147483648 && v <= 2147483647 )) ;;
        *) return 1 ;;
    esac
}

read_param() {
    local node="$1" line="$2"
    read_param_line "$line"
    local signed="unsigned" value
    [[ "$P_TYPE" == int* ]] && signed="signed"
    if value=$(read_sdo_uint "$node" "${P_KEY%%:*}" "${P_KEY##*:}" "$P_SIZE" "$signed"); then
        printf '  %4sh:%02sh %-26s = %-12s [%s %s' \
            "${P_KEY%%:*}" "$((16#${P_KEY##*:}))" "$P_NAME" "$value" "$P_TYPE" "$P_SIZE"
        [[ -n "$P_ACCESS" ]] && printf ' %s' "$P_ACCESS"
        [[ -n "$P_UNIT" && "$P_UNIT" != "-" ]] && printf ', jedn. %s' "$P_UNIT"
        [[ -n "$P_FACTORY" && "$P_FACTORY" != "-" ]] && printf ', fabr. %s' "$P_FACTORY"
        printf ']'
        [[ -n "$P_OPTS" ]] && printf '\n    (%s)' "$P_OPTS"
        printf '\n'
        return 0
    fi
    printf '  %4sh:%02sh %-26s : brak odpowiedzi\n' \
        "${P_KEY%%:*}" "$((16#${P_KEY##*:}))" "$P_NAME"
    return 1
}

write_param() {
    local node="$1" line="$2" value="$3"
    read_param_line "$line"
    if [[ "$P_ACCESS" == "RO" ]]; then
        log "${P_KEY} (${P_NAME}): parametr tylko do odczytu — pomijam zapis"
        return 1
    fi
    if ! value_in_range "$P_TYPE" "$value"; then
        log "${P_KEY} (${P_NAME}): wartość ${value} poza zakresem typu ${P_TYPE}"
        return 1
    fi
    log "${P_KEY} ${P_NAME} <- ${value} (${P_TYPE})"
    if write_sdo_uint "$node" "${P_KEY%%:*}" "${P_KEY##*:}" "$P_SIZE" "$value"; then
        log "Zapis ${P_KEY} ${P_NAME} = ${value}: OK"
        return 0
    fi
    return 1
}

verify_param() {
    local node="$1" line="$2" expected="$3"
    read_param_line "$line"
    local signed="unsigned" value
    [[ "$P_TYPE" == int* ]] && signed="signed"
    if value=$(read_sdo_uint "$node" "${P_KEY%%:*}" "${P_KEY##*:}" "$P_SIZE" "$signed"); then
        if (( value == expected )); then
            log "Weryfikacja ${P_KEY} ${P_NAME} = ${value}: OK"
            return 0
        fi
        log "Weryfikacja ${P_KEY} NIEZGODNA: zapisano ${expected}, odczytano ${value}"
        return 1
    fi
    log "Brak odpowiedzi przy weryfikacji ${P_KEY}"
    return 1
}

save_eeprom() {
    local node="$1" frame
    frame="$(printf '%X' $((0x600 + node)))#23101001$(hex32le 0x65766173)"
    log "TX ${frame} (save EEPROM)"
    if sdo_exchange "$node" "$frame" "60" 1000 >/dev/null; then
        log "Zapis do EEPROM: OK"
    else
        log "UWAGA: brak potwierdzenia zapisu do EEPROM"
    fi
}

list_registry() {
    local line
    printf '%-9s %-26s %-8s %-4s %-6s %-10s %-12s %s\n' \
        "Index" "Nazwa" "Typ" "Bajt" "Dostęp" "Jedn." "Fabrycznie" "Opcje/opis"
    printf '%s\n' "--------------------------------------------------------------------------------------------------------------"
    for line in "${PARAMS[@]}"; do
        read_param_line "$line"
        printf '%-9s %-26s %-8s %-4s %-6s %-10s %-12s %s\n' \
            "${P_KEY%%:*}h:${P_KEY##*:}h" "$P_NAME" "$P_TYPE" "$P_SIZE" "$P_ACCESS" "$P_UNIT" "$P_FACTORY" "$P_OPTS"
    done
    printf '\nŁącznie: %d parametrów.\n' "${#PARAMS[@]}"
}

main() {
    load_registry

    local ids=()
    local iface=""
    local read_sel=""
    local writes=()
    local do_save=0
    local do_list=0
    local node

    while (( $# )); do
        case "$1" in
            --help|-h) usage; exit 0 ;;
            --list)     do_list=1; shift ;;
            --read)     read_sel+=" $2"; shift 2 ;;
            --write)    writes+=("$2"); shift 2 ;;
            --save)     do_save=1; shift ;;
            *)
                if [[ "$1" =~ ^(can[0-9]+|vcan[0-9]+)$ ]]; then
                    iface="$1"
                else
                    ids+=("$1")
                fi
                shift
                ;;
        esac
    done

    if (( do_list )); then
        list_registry
        exit 0
    fi

    [[ -n "$iface" ]] && CAN_IF="$iface"
    [[ ${#ids[@]} -eq 0 ]] && ids=(1 2)

    command -v cansend >/dev/null 2>&1 || die "Nie znaleziono 'cansend' (pakiet can-utils)."
    command -v candump >/dev/null 2>&1 || die "Nie znaleziono 'candump' (pakiet can-utils)."

    if (( ${#writes[@]} > 0 )); then
        [[ ${#ids[@]} -eq 1 ]] || die "Tryb zapisu wymaga dokładnie jednego node_id."
        node="${ids[0]}"
        is_valid_node_id "$node" || die "Nieprawidlowy node_id: $node (dopuszczalne 1..127)"

        local wr_lines=()
        local wr_values=()
        local spec param value value_dec line
        for spec in "${writes[@]}"; do
            [[ "$spec" == *"="* ]] || die "Format zapisu: --write <parametr>=<wartość> (brak '=' w: ${spec})"
            param="${spec%%=*}"
            value="${spec#*=}"
            if ! line=$(resolve_one "$param"); then
                die "Nie znaleziono jednoznacznego parametru: $param"
            fi
            if ! value_dec=$(parse_value "$value"); then
                die "Nieprawidłowa wartość: $value (dla parametru $param)"
            fi
            wr_lines+=("$line")
            wr_values+=("$value_dec")
            write_param "$node" "$line" "$value_dec"
        done

        (( do_save )) && save_eeprom "$node"

        log "Stan po zapisie (node ${node}):"
        local i
        for i in "${!wr_lines[@]}"; do
            verify_param "$node" "${wr_lines[$i]}" "${wr_values[$i]}" || true
        done
    else
        read_sel="${read_sel//,/ }"
        local query matches line
        for node in "${ids[@]}"; do
            is_valid_node_id "$node" || { log "Pominięto nieprawidłowy node_id: $node"; continue; }
            if [[ -z "$read_sel" ]]; then
                log "node ${node}: odczyt grupy 2000h (${#PARAMS[@]} parametrów)"
                for line in "${PARAMS[@]}"; do
                    read_param "$node" "$line" || true
                done
            else
                log "node ${node}: odczyt wybranych parametrów"
                for query in $read_sel; do
                    matches=$(find_param "$query")
                    if [[ -z "$matches" ]]; then
                        log "Nie znaleziono parametru: $query"
                        continue
                    fi
                    while IFS= read -r line; do
                        read_param "$node" "$line" || true
                    done <<<"$matches"
                done
            fi
        done
    fi
}

main "$@"
