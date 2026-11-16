"""GaugeBoy's extra ELM327-emulator scenarios (dev only, never shipped).

The emulator package only has CAN cars ('car', 'default') and a K-Line
motorbike. GaugeBoy's test cars are not CAN, so this adds:

  ridgeline  ISO 9141-2 (protocol 3, ATDPN A3), like the owner's 2008 Honda
             Ridgeline: a 4.5 s protocol search, PIDs 01-5F, CEL off, no
             codes, a multi-line VIN (made up, not the owner's).
  tracker    ISO 9141-2, like the owner's 1996 Geo Tracker: PIDs 01-1F only,
             CEL off, no codes and no VIN over OBD (choose the make in SETUP).
  j1850      SAE J1850 VPW (protocol 2, ATDPN A2), a made-up 2002 Chevrolet
             truck with the CEL on, two stored codes and one pending code.

Replies are what an ELM327 prints with headers off (ATH0), which is how
GaugeBoy runs the adapter: data bytes only, one line per frame, no CAN
header or ISO-TP frame numbers. Values come from the owner's logs
(gauge-boy-bxf.14, bxf.16).

Run through tools/run-emulator.sh (ELM_SCENARIO=ridgeline ...), or directly:
    tools/.venv/bin/python -I tools/elm_scenarios.py -n 35000 -s ridgeline
"""

import sys

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


def scenario(protocol_number, protocol_name, pids, status, stored, pending, vin, volts):
    """A non-CAN car. pids maps a Mode 01 request ("0100") to its reply."""
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
    names = {"0100": "ELM_PIDS_A", "0120": "ELM_PIDS_B", "0140": "ELM_PIDS_C",
             "0105": "COOLANT_TEMP", "010C": "RPM"}
    for req, reply in pids.items():
        key = names.get(req, "PID_" + req)
        s[key] = {"Request": "^" + req + "[0-9]?$", "Descr": "Mode 01 " + req,
                  "Response": ST(reply)}
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
         "0105": "41 05 5A", "010C": "41 0C 0B B8"},
        status="41 01 00 07 E5 00", stored=None, pending=None,
        vin="2HJYK16618H000123", volts="13.2V"),
    "tracker": scenario(
        3, "ISO 9141-2",
        {"0100": "41 00 BE 3F B8 10", "0105": "41 05 55", "010C": "41 0C 0A F0"},
        status="41 01 00 07 E1 00", stored=None, pending=None,
        vin=None, volts="11.1V"),
    "j1850": scenario(
        2, "SAE J1850 VPW",
        {"0100": "41 00 BE 3E B8 11", "0120": "41 20 80 00 00 00",
         "0105": "41 05 5F", "010C": "41 0C 0C 80"},
        status="41 01 82 07 65 04",
        stored="43 01 71 03 00 00 00",      # P0171, P0300
        pending="47 04 20 00 00 00 00",     # P0420
        vin="1GCEK14V62Z000417", volts="12.9V"),
}

ObdMessage.update(SCENARIOS)

if __name__ == "__main__":
    sys.argv[0] = "elm"
    sys.exit(elm_main.main())
