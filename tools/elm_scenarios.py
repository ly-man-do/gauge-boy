"""Gauge Boy's extra ELM327-emulator scenarios (dev only, never shipped).

The emulator package only has CAN cars ('car', 'default') and a K-Line
motorbike. Gauge Boy's test cars are not CAN, so this adds:

  ridgeline  ISO 9141-2 (protocol 3, ATDPN A3), like the owner's 2008 Honda
             Ridgeline: a 4.5 s protocol search, PIDs 01-5F, CEL off, no
             codes, a multi-line VIN (made up, not the owner's).
  tracker    ISO 9141-2, like the owner's 1996 Geo Tracker: PIDs 01-1F only,
             CEL off, no codes and no VIN over OBD (choose the make in SETUP).
  j1850      SAE J1850 VPW (protocol 2, ATDPN A2), a made-up 2002 Chevrolet
             truck with the CEL on, two stored codes and one pending code,
             and a freeze frame (Mode 02) for P0171.
  ascent     CAN 11-bit 500k (protocol 6), like the owner's 2023 Subaru
             Ascent (turbo): its supported-PID lists, no PID 0B, manifold
             pressure in PID 87 (idle vacuum and boost), baro in PID 33.
             Built on the emulator's own CAN 'car' scenario, so combined
             requests ("010C0D05...") are answered too.
  track      the ridgeline with its speed and RPM going round a 50 s loop
             (stop, 0-130 km/h through the gears, cruise, hard stop) for
             testing Time Attack and the shift light.
  blips      the ridgeline parked, blipping the throttle every 3 s (Rev Sync).

Replies are what an ELM327 prints with headers off (ATH0), which is how
Gauge Boy runs the adapter: data bytes only, one line per frame, no CAN
header or ISO-TP frame numbers. Values come from the owner's logs
(gauge-boy-bxf.14, bxf.16).

Run through tools/run-emulator.sh (ELM_SCENARIO=ridgeline ...), or directly:
    tools/.venv/bin/python -I tools/elm_scenarios.py -n 35000 -s ridgeline
"""

import copy
import math
import re
import sys
import time

from elm import __main__ as elm_main
from elm.obd_message import ObdMessage


def ST(line):
    """One reply line."""
    return "<writeln>" + line + "</writeln>"


NO_DATA = ST("NO DATA")


def searching(seconds):
    """'SEARCHING...' and the search delay before the first 0100 answer."""
    return (lambda self, cmd, pid, uc_val:
            "<string>SEARCHING...</string><exec>time.sleep(" + str(seconds) + ")</exec>" + ST("")
            if self.counters[pid] == 1 else "")


def vin_lines(vin):
    """Mode 09 PID 02 on a non-CAN bus: five frames, 49 02 nn + 4 bytes."""
    data = [0, 0, 0] + [ord(c) for c in vin]
    lines = []
    for n in range(5):
        chunk = data[n * 4:n * 4 + 4]
        lines.append(ST("49 02 %02X %s" % (n + 1, " ".join("%02X" % b for b in chunk))))
    return lines


def default_key(request):
    """The key of the emulator's own entry for this request (so ours replaces
    it instead of both answering), or a new one."""
    for key, entry in ObdMessage["default"].items():
        if "Request" in entry and re.match(entry["Request"], request):
            return key
    return "PID_" + request


def scenario(protocol_number, protocol_name, pids, status, stored, pending, vin, volts):
    """A non-CAN car. pids maps a Mode 01 request ("0100") to its reply, or
    to a list of replies the emulator picks from (so live values move)."""
    s = {
        # Same keys as the emulator's 'AT' and 'default' entries, so these win.
        "AT_DESCRIBE_PROTO_N": {"Request": "^ATDPN$", "Descr": "Protocol number",
                                "Response": ST("A%X" % protocol_number)},
        "AT_DESCRIBE_PROTO": {"Request": "^ATDP$", "Descr": "Protocol",
                              "Response": ST("AUTO, " + protocol_name)},
        "AT_R_VOLT": {"Request": "^ATRV$", "Descr": "Battery voltage",
                      "Response": ST(volts)},
        "ELM_VOLTAGE": {"Request": "^ATRV$", "Descr": "Battery voltage",
                        "Response": ST(volts)},
        "STATUS": {"Request": "^0101[0-9]?$", "Descr": "CEL and readiness",
                   "Response": ST(status)},
        "SPEED": {"Request": "^010D[0-9]?$", "Descr": "Vehicle speed (parked)",
                  "Response": ST("41 0D 00")},
        "GET_DTC": {"Request": "^03[0-9]?$", "Descr": "Stored codes",
                    "Response": ST(stored) if stored else NO_DATA},
        "SHOW_DIAG_TC": {"Request": "^03[0-9]?$", "Descr": "Stored codes",
                         "Response": ST(stored) if stored else NO_DATA},
        "GET_CURRENT_DTC": {"Request": "^07[0-9]?$", "Descr": "Pending codes",
                            "Response": ST(pending) if pending else NO_DATA},
        "SHOW_PENDING_TC": {"Request": "^07[0-9]?$", "Descr": "Pending codes",
                            "Response": ST(pending) if pending else NO_DATA},
        "UNKNOWN_0A": {"Request": "^0A[0-9]?$", "Descr": "Permanent codes (not on this bus)",
                       "Response": NO_DATA},
        "CLEAR_DIAG_TC": {"Request": "^04[0-9]?$", "Descr": "Clear codes",
                          "Response": ST("44")},
        "VIN": {"Request": "^0902[0-9]?$", "Descr": "VIN",
                "Response": "".join(vin_lines(vin)) if vin else NO_DATA},
    }
    for req, reply in pids.items():
        replies = [ST(r) for r in reply] if isinstance(reply, list) else ST(reply)
        s[default_key(req)] = {"Request": "^" + req + "[0-9]?$", "Descr": "Mode 01 " + req,
                               "Response": replies}
    s["ELM_PIDS_A"]["ResponseHeader"] = searching(4.5)
    # PIDs the car doesn't list: NO DATA, as a real car.
    for key in ("ELM_PIDS_B", "ELM_PIDS_C"):
        if key not in s:
            s[key] = {"Request": "^01%s[0-9]?$" % ("20" if key.endswith("B") else "40"),
                      "Descr": "Not supported", "Response": NO_DATA}
    return s


SCENARIOS = {
    "ridgeline": scenario(
        3, "ISO 9141-2",
        {"0100": "41 00 BF BE A8 91", "0120": "41 20 80 00 11 01", "0140": "41 40 08 00 00 00",
         # Warm idle, parked. Bank 2 trims too: the V6 has two banks.
         "0104": ["41 04 33", "41 04 35"],                             # load 20-21 %
         "0105": "41 05 7B",                                           # coolant 83 C
         "0106": ["41 06 80", "41 06 82", "41 06 7E", "41 06 84"],     # STFT B1 -1.6..+3.1 %
         "0107": "41 07 83",                                           # LTFT B1 +2.3 %
         "0108": ["41 08 81", "41 08 7D", "41 08 83"],                 # STFT B2
         "0109": "41 09 85",                                           # LTFT B2 +3.9 %
         "010B": ["41 0B 1E", "41 0B 1F"],                             # MAP 30-31 kPa
         "010C": ["41 0C 0A F0", "41 0C 0B 18", "41 0C 0A C8"],        # 700 rpm +- 10
         "010D": "41 0D 00",
         "010E": ["41 0E 94", "41 0E 96"],                             # timing 10-11 deg
         "010F": "41 0F 46",                                           # intake air 30 C
         "0111": "41 11 26"},                                          # throttle 15 %
        status="41 01 00 07 E5 00", stored=None, pending=None,
        vin="2HJYK16528H000123", volts="13.2V"),
    "tracker": scenario(
        3, "ISO 9141-2",
        {"0100": "41 00 BE 3F B8 10",
         # One bank (inline 4), MAF, no PID 33.
         "0104": "41 04 40",
         "0105": "41 05 78",
         "0106": ["41 06 80", "41 06 7C", "41 06 85"],
         "0107": "41 07 7C",                                           # LTFT -3.1 %
         "010B": "41 0B 23",
         "010C": ["41 0C 0C 80", "41 0C 0C A8"],                       # 800-810 rpm
         "010D": "41 0D 00",
         "010E": "41 0E 90",
         "010F": "41 0F 40",
         "0110": ["41 10 01 90", "41 10 01 9A"],                       # MAF 4.0 g/s
         "0111": "41 11 1F"},
        status="41 01 00 07 E1 00", stored=None, pending=None,
        vin=None, volts="11.1V"),
    "j1850": scenario(
        2, "SAE J1850 VPW",
        {"0100": "41 00 BE 3E B8 11", "0120": "41 20 80 00 00 00",
         # Running lean on bank 1 (P0171 stored): high positive trims.
         "0104": "41 04 38",
         "0105": "41 05 7D",
         "0106": ["41 06 98", "41 06 9C"],                             # STFT B1 +19..22 %
         "0107": "41 07 9A",                                           # LTFT B1 +20.3 %
         "010B": "41 0B 22",
         "010C": ["41 0C 0C 80", "41 0C 0C 30"],
         "010D": "41 0D 00",
         "010E": "41 0E 8C",
         "010F": "41 0F 3C",
         "0111": "41 11 22"},
        status="41 01 82 07 65 04",
        stored="43 01 71 03 00 00 00",      # P0171, P0300
        pending="47 04 20 00 00 00 00",     # P0420
        vin="1GCEK14V62Z000417", volts="12.9V"),
}



def can(data):
    """One CAN reply frame from ECU 7E8, as the emulator's 'car' scenario
    writes them (headers are off in Gauge Boy, so only the data shows)."""
    return "<header>7E8</header><size>%02X</size><data>%s</data>" % (len(data.split()), data)


def car_key(scen, request):
    """The key of the 'car' entry answering this request, or a new one."""
    for key, entry in scen.items():
        if "Request" in entry and re.match(entry["Request"], request):
            return key
    return "PID_" + request


def ascent():
    """The 2023 Subaru Ascent (owner log 2026-11-16, gauge-boy-3n6.13)."""
    s = copy.deepcopy(ObdMessage["car"])
    pids = {
        # Supported-PID lists from the engine ECU (the log's first lines).
        "0100": "41 00 BE 1D A8 13", "0120": "41 20 90 1F B0 11",
        "0140": "41 40 FE DC A0 51", "0160": "41 60 69 08 40 01",
        "0180": "41 80 02 04 00 0D", "01A0": "41 A0 04 00 00 00",
        "0133": "41 33 64",                                           # baro 100 kPa
        # PID 87, sensor A only: 35 kPa at idle, 140 kPa (5.8 psi boost).
        "0187": ["41 87 01 04 60 00 00", "41 87 01 04 60 00 00", "41 87 01 11 80 00 00"],
    }
    for req, reply in pids.items():
        replies = [can(r) for r in reply] if isinstance(reply, list) else can(reply)
        s[car_key(s, req)] = {"Request": "^" + req + "[0-9]?$", "Descr": "Mode 01 " + req,
                              "Header": "7E0", "Response": replies}
    # A made-up Ascent VIN (not the owner's), so the vPIC model decodes.
    vin = "4S4WMAHD0P3000001"
    s["VIN"] = {"Request": "^0902[0-9]?$", "Descr": "VIN", "Header": "7E0",
                "Response": "<pos_answer>01 %s</pos_answer>" % " ".join("%02X" % ord(c) for c in vin)}
    # No MAP in PID 0B on this car.
    s[car_key(s, "010B")] = {"Request": "^010B[0-9]?$", "Descr": "Not supported",
                             "Header": "7E0", "Response": NO_DATA}
    return s


SCENARIOS["ascent"] = ascent()


# Time Attack test loop (50 s, repeating): stopped 12 s, 0-130 km/h at
# 3 m/s^2 (10.8 km/h a second), 6 s at 130, then a 30 km/h-a-second stop.
# Expected: 0-60 mph 8.94 s, 0-100 km/h 9.26 s, 60-0 mph 3.22 s / 43 m,
# 100-0 km/h 3.33 s / 46 m, 1/4 mile 17.2 s at 130 km/h.
TRACK_LOOP_S = 50.0
_track_t0 = time.monotonic()


def track_kmh(t):
    t %= TRACK_LOOP_S
    top = 130.0
    accel_end = 12.0 + top / 10.8
    brake_at = accel_end + 6.0
    if t < 12.0:
        return 0.0
    if t < accel_end:
        return (t - 12.0) * 10.8
    if t < brake_at:
        return top
    return max(0.0, top - (t - brake_at) * 30.0)


def track_speed(self, cmd, pid, uc_val):
    kmh = int(track_kmh(time.monotonic() - _track_t0))
    return ST("41 0D %02X" % min(kmh, 255))


# RPM on the loop: 750 stopped (with a blip every 3 s), through five gears (rpm per km/h), shifting
# up at 6300 rpm, so the shift light (default 6000) flashes before each shift.
TRACK_GEARS = (110.0, 64.0, 44.0, 33.0, 27.0)


def track_rpm_at(t):
    kmh = track_kmh(t)
    if kmh < 1.0:
        # Parked: a throttle blip every 3 s (for Rev Sync), peaking at
        # 2600, 3300 or 4000 rpm in turn.
        phase = t % 3.0
        if phase < 0.8:
            peak = (2600.0, 3300.0, 4000.0)[int(t // 3.0) % 3]
            return 750.0 + (peak - 750.0) * math.sin(math.pi * phase / 0.8)
        return 750.0
    for ratio in TRACK_GEARS:
        if kmh * ratio <= 6300.0:
            return max(1200.0, kmh * ratio)
    return kmh * TRACK_GEARS[-1]


def track_rpm(self, cmd, pid, uc_val):
    rpm = int(track_rpm_at(time.monotonic() - _track_t0) * 4)
    return ST("41 0C %02X %02X" % (rpm >> 8, rpm & 0xFF))


def track():
    """The ridgeline with its speed and RPM going round the test loop above."""
    s = copy.deepcopy(SCENARIOS["ridgeline"])
    s[default_key("010D")] = {"Request": "^010D[0-9]?$", "Descr": "Vehicle speed (test loop)",
                              "ResponseHeader": track_speed, "Response": ""}
    s[default_key("010C")] = {"Request": "^010C[0-9]?$", "Descr": "RPM (test loop)",
                              "ResponseHeader": track_rpm, "Response": ""}
    return s


SCENARIOS["track"] = track()


def blips():
    """The ridgeline parked, the throttle blipped every 3 s (for Rev Sync)."""
    s = copy.deepcopy(SCENARIOS["ridgeline"])
    s[default_key("010C")] = {
        "Request": "^010C[0-9]?$", "Descr": "RPM (parked, blips)", "Response": "",
        "ResponseHeader": lambda self, cmd, pid, uc_val: ST(
            "41 0C %02X %02X" % divmod(int(track_rpm_at((time.monotonic() - _track_t0) % 12.0)
                                           * 4), 256))}
    return s


SCENARIOS["blips"] = blips()

# A freeze frame for the j1850 truck's stored P0171 (Mode 02, frame 0):
# running lean, warm, at idle.
for req, reply in {
        "020200": "42 02 00 01 71",               # P0171 set it
        "020000": "42 00 00 5E 3E 80 00",         # 02, 04-07, 0B-0F, 11
        "020400": "42 04 00 40", "020500": "42 05 00 7B", "020600": "42 06 00 98",
        "020700": "42 07 00 9A", "020B00": "42 0B 00 22", "020C00": "42 0C 00 0C 80",
        "020D00": "42 0D 00 00", "020E00": "42 0E 00 8C", "020F00": "42 0F 00 3C",
        "021100": "42 11 00 22"}.items():
    SCENARIOS["j1850"]["FREEZE_" + req] = {"Request": "^" + req + "$", "Descr": "Freeze frame",
                                           "Response": ST(reply)}

ObdMessage.update(SCENARIOS)

if __name__ == "__main__":
    sys.argv[0] = "elm"
    sys.exit(elm_main.main())
