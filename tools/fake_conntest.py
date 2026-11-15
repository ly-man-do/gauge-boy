#!/usr/bin/env python3
"""Answer the Nintendo 3DS connection test on a network with no internet.

The 3DS will not let apps use a Wi-Fi network that fails its connection
test (error 003-2002 on a Wi-Fi OBD-II adapter, e.g. the Vgate iCar Pro's network V-LINK). The test
looks up a few Nintendo host names and fetches a page over HTTP; it passes
on an HTTP 200 reply with the header "X-Organization: Nintendo".

This script is the proof for a helper on the adapter's network:

  - a DNS server (UDP 53) that answers the test host names with this
    computer's own address, and NXDOMAIN for everything else (there is no
    internet to forward to), and
  - an HTTP server (TCP 80) that serves the test reply for any path.

Set the 3DS's DNS to this computer's address, run the 3DS connection test,
and watch the log. Python 3 standard library only; dev-only, never shipped.

Run from anywhere:  python3 tools/fake_conntest.py
Stop with Ctrl-C. See tools/README.md for the test steps.
"""

import argparse
import datetime
import http.server
import socket
import socketserver
import struct
import sys
import threading

# Host names the 3DS connection test is known to look up.
TEST_HOSTS = {
    "conntest.nintendowifi.net",
    "ctest.cdn.nintendo.net",
    "nncs1.app.nintendowifi.net",
    "nncs2.app.nintendowifi.net",
}

TEST_BODY = (b"<!DOCTYPE html>\n<html><head><title>HTML Page</title></head>\n"
             b"<body>This is test.html page</body></html>\n")

TYPE_A, TYPE_AAAA, CLASS_IN = 1, 28, 1
RCODE_OK, RCODE_FORMERR, RCODE_NXDOMAIN = 0, 1, 3

log_lock = threading.Lock()


def log(msg):
    stamp = datetime.datetime.now().strftime("%H:%M:%S")
    with log_lock:
        print(f"[{stamp}] {msg}", flush=True)


def local_ip_for(peer_ip):
    """This computer's address on the interface that reaches peer_ip."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((peer_ip, 9))  # no packet is sent for a UDP connect
        return s.getsockname()[0]
    finally:
        s.close()


# ---------- DNS ----------

def parse_question(packet):
    """Return (qname, qtype, qclass, end offset) of the first question."""
    labels, i = [], 12
    while True:
        if i >= len(packet):
            raise ValueError("truncated name")
        n = packet[i]
        if n == 0:
            i += 1
            break
        if n & 0xC0:
            raise ValueError("compressed name in question")
        labels.append(packet[i + 1:i + 1 + n].decode("ascii", "replace"))
        i += 1 + n
    if i + 4 > len(packet):
        raise ValueError("truncated question")
    qtype, qclass = struct.unpack(">HH", packet[i:i + 4])
    return ".".join(labels).lower().rstrip("."), qtype, qclass, i + 4


def build_reply(query, rcode, answer_ip=None, question_end=None):
    """DNS reply to query: rcode, plus one A record if answer_ip is set."""
    qid, flags = struct.unpack(">HH", query[:4])
    opcode_rd = flags & 0x7900  # keep opcode and RD
    out_flags = 0x8000 | 0x0400 | opcode_rd | rcode  # QR, AA
    if question_end is None:
        return struct.pack(">HHHHHH", qid, out_flags, 0, 0, 0, 0)
    ancount = 1 if answer_ip else 0
    header = struct.pack(">HHHHHH", qid, out_flags, 1, ancount, 0, 0)
    body = query[12:question_end]
    if answer_ip:
        body += struct.pack(">HHHIH", 0xC00C, TYPE_A, CLASS_IN, 60, 4)
        body += socket.inet_aton(answer_ip)
    return header + body


def dns_answer(query, peer_ip, answer_all, fixed_ip):
    """Return (reply bytes, log text)."""
    if len(query) < 12:
        return None, "short packet ignored"
    try:
        name, qtype, qclass, end = parse_question(query)
    except ValueError as e:
        return build_reply(query, RCODE_FORMERR), f"bad query ({e})"

    ours = name in TEST_HOSTS or answer_all
    if not ours:
        return build_reply(query, RCODE_NXDOMAIN, None, end), f"{name} type {qtype}: NXDOMAIN"
    if qtype == TYPE_A and qclass == CLASS_IN:
        ip = fixed_ip or local_ip_for(peer_ip)
        return build_reply(query, RCODE_OK, ip, end), f"{name} A -> {ip}"
    # Our name, other record type (e.g. AAAA): exists, but no such record.
    return build_reply(query, RCODE_OK, None, end), f"{name} type {qtype}: no record"


def dns_loop(sock, answer_all, fixed_ip):
    while True:
        try:
            query, (peer_ip, peer_port) = sock.recvfrom(512)
        except OSError:
            return
        reply, text = dns_answer(query, peer_ip, answer_all, fixed_ip)
        log(f"DNS  {peer_ip}: {text}")
        if reply is not None:
            sock.sendto(reply, (peer_ip, peer_port))


# ---------- HTTP ----------

class TestPageHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def version_string(self):
        return "GaugeBoyConnTest/1"

    def _reply(self, with_body):
        host = self.headers.get("Host", "?")
        agent = self.headers.get("User-Agent", "?")
        log(f"HTTP {self.client_address[0]}: {self.command} http://{host}{self.path} "
            f"(User-Agent: {agent}) -> 200 test page")
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("X-Organization", "Nintendo")
        self.send_header("Content-Length", str(len(TEST_BODY)))
        self.send_header("Connection", "close")
        self.end_headers()
        if with_body:
            self.wfile.write(TEST_BODY)

    def do_GET(self):
        self._reply(True)

    def do_HEAD(self):
        self._reply(False)

    def log_message(self, fmt, *args):
        pass  # _reply logs instead


class ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ---------- main ----------

def lan_addresses():
    """This computer's IPv4 address towards the adapter (192.168.0.10) and
    towards the default route; on the adapter's network both are the same."""
    addrs = set()
    for probe in ("192.168.0.10", "1.1.1.1"):
        try:
            addrs.add(local_ip_for(probe))
        except OSError:
            pass
    return sorted(a for a in addrs if not a.startswith("127.") and a != "0.0.0.0")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dns-port", type=int, default=53)
    ap.add_argument("--http-port", type=int, default=80)
    ap.add_argument("--bind", default="0.0.0.0", help="address to listen on")
    ap.add_argument("--ip", help="address to hand out for the test names "
                    "(default: this computer's address on the 3DS's network)")
    ap.add_argument("--answer-all", action="store_true",
                    help="answer every name with this computer's address, not just the test names")
    args = ap.parse_args()

    hint = ("Another program is using the port: a VPN app (e.g. Mullvad, whose background service "
            "keeps running after you quit the app) often holds DNS port 53. Run with sudo: "
            "sudo python3 tools/fake_conntest.py (it then listens on this computer's own address).")
    # VPN apps (e.g. Mullvad's daemon, which keeps running after its app is
    # quit) can hold DNS port 53 on all addresses. With sudo, fall back to
    # this computer's own address, which is all the 3DS needs.
    dns, dns_bind, err = None, args.bind, None
    candidates = [args.bind] + (lan_addresses() if args.bind == "0.0.0.0" else [])
    for addr in candidates:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind((addr, args.dns_port))
            dns, dns_bind = sock, addr
            break
        except OSError as e:
            err = err or e
            sock.close()
    if dns is None:
        sys.exit(f"could not listen on UDP {args.bind}:{args.dns_port} (DNS): {err}\n{hint}")
    try:
        httpd = ThreadingHTTPServer((args.bind, args.http_port), TestPageHandler)
    except OSError as e:
        sys.exit(f"could not listen on TCP {args.bind}:{args.http_port} (HTTP): {e}\n{hint}")

    print("GaugeBoy fake connection test")
    print(f"  DNS  on UDP {dns_bind}:{args.dns_port}, test names -> "
          f"{args.ip or 'this computer'}, other names -> "
          f"{'this computer' if args.answer_all else 'NXDOMAIN'}")
    print(f"  HTTP on TCP {args.bind}:{args.http_port}, any path -> 200 + X-Organization: Nintendo")
    addrs = lan_addresses()
    if addrs:
        print("  Set the 3DS's Primary DNS to this computer: " + " or ".join(addrs))
    print("  Ctrl-C to stop.\n", flush=True)

    threading.Thread(target=dns_loop, args=(dns, args.answer_all, args.ip), daemon=True).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        httpd.server_close()
        dns.close()


if __name__ == "__main__":
    main()
