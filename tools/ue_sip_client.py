#!/usr/bin/env python3
"""Minimal UE SIP client for SimIMS integration testing.

This script replays REGISTER and SUBSCRIBE requests in the same signaling
order as the provided packet-capture summaries:
  - register_request.data
  - subscribe_request.data

It intentionally focuses on raw SIP text format correctness rather than full
SIP stack behavior.
"""

from __future__ import annotations

import argparse
import re
import socket
import sys
import time
from dataclasses import dataclass
from typing import Dict, List, Tuple

REGISTER_CALL_ID = "849091855_1207149580@190:186:75:89c4:5c9b:30ff:fee4:e60f"
SUBSCRIBE_CALL_ID = "849091953_1207230380@190:186:75:89c4:5c9b:30ff:fee4:e60f"


def build_register_request() -> str:
    lines = [
        "REGISTER sip:ims.mnc011.mcc460.3gppnetwork.org SIP/2.0",
        "Via: SIP/2.0/TCP [190:186:75:89c4:5c9b:30ff:fee4:e60f]:5060;branch=z9hG4bK2193228166",
        "From: <sip:460112024122023@ims.mnc011.mcc460.3gppnetwork.org>;tag=849091858",
        "To: <sip:460112024122023@ims.mnc011.mcc460.3gppnetwork.org>",
        f"Call-ID: {REGISTER_CALL_ID}",
        "CSeq: 849091855 REGISTER",
        "Max-Forwards: 70",
        "Contact: <sip:[190:186:75:89c4:5c9b:30ff:fee4:e60f]:5060>;+g.3gpp.accesstype=\"cellular2\";audio;+g.3gpp.smsip;video;+g.3gpp.icsi-ref=\"urn%3Aurn-7%3A3gpp-service.ims.icsi.mmtel\";+sip.instance=\"<urn:gsma:imei:86700005-005046-0>\"",
        "Expires: 600000",
        "Require: sec-agree",
        "Proxy-Require: sec-agree",
        "Supported: path,sec-agree",
        "Allow: INVITE,BYE,CANCEL,ACK,NOTIFY,UPDATE,PRACK,INFO,MESSAGE,OPTIONS",
        "Authorization: Digest uri=\"sip:ims.mnc011.mcc460.3gppnetwork.org\",username=\"460112024122023@ims.mnc011.mcc460.3gppnetwork.org\",response=\"\",realm=\"ims.mnc011.mcc460.3gppnetwork.org\",nonce=\"\"",
        "User-Agent: nokia_nokia A2022P_RKQ1.210503.001",
        "Security-Client: ipsec-3gpp;alg=hmac-md5-96;ealg=aes-cbc;spi-c=73991669;spi-s=3963789885;port-c=42184;port-s=43002",
        "Content-Length: 0",
    ]
    return "\r\n".join(lines) + "\r\n\r\n"


def build_subscribe_request() -> str:
    lines = [
        "SUBSCRIBE sip:+8613824122023@ims.nokia.com.cn SIP/2.0",
        "Via: SIP/2.0/UDP [190:186:75:89c4:5c9b:30ff:fee4:e60f]:5060;branch=z9hG4bK279305682",
        "From: <sip:+8613824122023@ims.nokia.com.cn>;tag=849091955",
        "To: <sip:+8613824122023@ims.nokia.com.cn>",
        f"Call-ID: {SUBSCRIBE_CALL_ID}",
        "CSeq: 849091953 SUBSCRIBE",
        "Max-Forwards: 70",
        "Route: <sip:[2055:5::3e:77]:5060;lr>",
        "P-Access-Network-Info: 3GPP-NR-TDD;utran-cell-id-3gpp=4601107D000002A16001",
        "Event: reg",
        "Contact: <sip:[190:186:75:89c4:5c9b:30ff:fee4:e60f]:5060>",
        "P-Preferred-Identity: <sip:+8613824122023@ims.nokia.com.cn>",
        "Expires: 600000",
        "User-Agent: nokia_nokia A2022P_RKQ1.210503.001",
        "Content-Length: 0",
    ]
    return "\r\n".join(lines) + "\r\n\r\n"


@dataclass
class SipResponse:
    raw: str
    status_line: str
    headers: Dict[str, List[str]]


def parse_sip_response(raw: str) -> SipResponse:
    lines = raw.split("\r\n")
    status_line = lines[0] if lines else ""

    headers: Dict[str, List[str]] = {}
    for line in lines[1:]:
        if not line:
            break
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        key = name.strip().lower()
        headers.setdefault(key, []).append(value.strip())

    return SipResponse(raw=raw, status_line=status_line, headers=headers)


def _extract_content_length(header_block: str) -> int:
    match = re.search(r"(?im)^Content-Length\s*:\s*(\d+)\s*$", header_block)
    return int(match.group(1)) if match else 0


def _is_matching_200(response: SipResponse, expected_method: str, expected_call_id: str) -> bool:
    if not response.status_line.startswith("SIP/2.0 200"):
        return False

    cseq_values = response.headers.get("cseq", [])
    has_method = any(re.search(rf"\b{re.escape(expected_method)}\b", value) for value in cseq_values)
    if not has_method:
        return False

    call_id_values = response.headers.get("call-id", [])
    return expected_call_id in call_id_values


def _extract_one_sip_message(buffer: bytes) -> Tuple[bytes, bytes] | None:
    header_end = buffer.find(b"\r\n\r\n")
    if header_end == -1:
        return None

    header_block = buffer[: header_end + 4]
    content_len = _extract_content_length(header_block.decode("utf-8", errors="replace"))
    total_len = header_end + 4 + content_len
    if len(buffer) < total_len:
        return None

    return (buffer[:total_len], buffer[total_len:])


def recv_one_sip_message(sock: socket.socket) -> str:
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk

    header_end = data.find(b"\r\n\r\n")
    if header_end == -1:
        raise RuntimeError("incomplete SIP response headers")

    header_block = data[: header_end + 4]
    content_len = _extract_content_length(header_block.decode("utf-8", errors="replace"))
    expected_total = header_end + 4 + content_len

    while len(data) < expected_total:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk

    return data[:expected_total].decode("utf-8", errors="replace")


def wait_for_200_over_tcp(
    sock: socket.socket,
    expected_method: str,
    expected_call_id: str,
    timeout_sec: float,
) -> str:
    deadline = time.monotonic() + timeout_sec
    buffer = b""
    last_response: SipResponse | None = None

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        sock.settimeout(max(0.05, remaining))
        chunk = sock.recv(4096)
        if not chunk:
            break
        buffer += chunk

        while True:
            extracted = _extract_one_sip_message(buffer)
            if extracted is None:
                break
            raw_msg, buffer = extracted
            parsed = parse_sip_response(raw_msg.decode("utf-8", errors="replace"))
            last_response = parsed
            if _is_matching_200(parsed, expected_method, expected_call_id):
                return parsed.raw

    if last_response is not None:
        raise RuntimeError(f"did not receive matching 200 OK, last response: {last_response.status_line}")
    raise RuntimeError("did not receive SIP response before timeout")


def wait_for_200_over_udp(
    sock: socket.socket,
    expected_method: str,
    expected_call_id: str,
    timeout_sec: float,
) -> str:
    deadline = time.monotonic() + timeout_sec
    last_response: SipResponse | None = None

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        sock.settimeout(max(0.05, remaining))
        data, _ = sock.recvfrom(65535)
        parsed = parse_sip_response(data.decode("utf-8", errors="replace"))
        last_response = parsed
        if _is_matching_200(parsed, expected_method, expected_call_id):
            return parsed.raw

    if last_response is not None:
        raise RuntimeError(f"did not receive matching 200 OK, last response: {last_response.status_line}")
    raise RuntimeError("did not receive SIP response before timeout")


def send_over_tcp(
    payload: str,
    host: str,
    port: int,
    timeout_sec: float,
    expected_method: str,
    expected_call_id: str,
) -> str:
    infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    last_error: Exception | None = None

    for family, socktype, proto, _, sockaddr in infos:
        try:
            with socket.socket(family, socktype, proto) as sock:
                sock.settimeout(timeout_sec)
                sock.connect(sockaddr)
                sock.sendall(payload.encode("utf-8"))
                return wait_for_200_over_tcp(
                    sock=sock,
                    expected_method=expected_method,
                    expected_call_id=expected_call_id,
                    timeout_sec=timeout_sec,
                )
        except Exception as exc:  # pragma: no cover
            last_error = exc

    raise RuntimeError(f"TCP send failed: {last_error}")


def send_over_udp(
    payload: str,
    host: str,
    port: int,
    timeout_sec: float,
    expected_method: str,
    expected_call_id: str,
) -> str:
    infos = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
    last_error: Exception | None = None

    for family, socktype, proto, _, sockaddr in infos:
        try:
            with socket.socket(family, socktype, proto) as sock:
                sock.settimeout(timeout_sec)
                sock.sendto(payload.encode("utf-8"), sockaddr)
                return wait_for_200_over_udp(
                    sock=sock,
                    expected_method=expected_method,
                    expected_call_id=expected_call_id,
                    timeout_sec=timeout_sec,
                )
        except Exception as exc:  # pragma: no cover
            last_error = exc

    raise RuntimeError(f"UDP send failed: {last_error}")


def validate_response(
    response: SipResponse,
    expected_method: str,
    expected_call_id: str,
    expected_branch: str,
) -> Tuple[bool, List[str]]:
    errors: List[str] = []

    if not response.status_line.startswith("SIP/2.0 200"):
        errors.append(f"status is not 200 OK: {response.status_line}")

    cseq_values = response.headers.get("cseq", [])
    cseq_ok = any(re.search(rf"\b{re.escape(expected_method)}\b", value) for value in cseq_values)
    if not cseq_ok:
        errors.append(f"CSeq does not include method {expected_method}: {cseq_values}")

    call_id_values = response.headers.get("call-id", [])
    if expected_call_id not in call_id_values:
        errors.append(f"Call-ID mismatch, got {call_id_values}, want {expected_call_id}")

    via_values = response.headers.get("via", [])
    branch_ok = any(expected_branch in value for value in via_values)
    if not branch_ok:
        errors.append(f"Via branch mismatch, got {via_values}, want branch={expected_branch}")

    return (len(errors) == 0, errors)


def extract_branch(request_raw: str) -> str:
    match = re.search(r"(?im)^Via:\s*.*?;branch=([^;\r\n]+)", request_raw)
    if not match:
        raise RuntimeError("cannot find Via branch in request")
    return match.group(1).strip()


def run_step(
    step_name: str,
    request_raw: str,
    host: str,
    port: int,
    transports: List[str],
    timeout_sec: float,
    expected_method: str,
    expected_call_id: str,
    print_response: bool,
) -> None:
    branch = extract_branch(request_raw)

    last_error: Exception | None = None
    raw_response = ""
    used_transport = ""

    for transport in transports:
        try:
            if transport == "tcp":
                raw_response = send_over_tcp(
                    request_raw,
                    host,
                    port,
                    timeout_sec,
                    expected_method,
                    expected_call_id,
                )
            else:
                raw_response = send_over_udp(
                    request_raw,
                    host,
                    port,
                    timeout_sec,
                    expected_method,
                    expected_call_id,
                )
            used_transport = transport
            break
        except Exception as exc:
            last_error = exc

    if not raw_response:
        raise RuntimeError(
            f"{step_name}: send failed for transports {transports}, last error: {last_error}"
        )

    response = parse_sip_response(raw_response)
    ok, errors = validate_response(response, expected_method, expected_call_id, branch)

    print(f"[{step_name}] transport={used_transport.upper()} status={response.status_line}")
    if print_response:
        print(raw_response)

    if not ok:
        raise RuntimeError(f"{step_name}: response validation failed: {'; '.join(errors)}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Replay REGISTER + SUBSCRIBE SIP messages based on capture files"
    )
    parser.add_argument("--server-host", default="127.0.0.1", help="SIP server host")
    parser.add_argument("--register-port", type=int, default=5060, help="REGISTER destination port")
    parser.add_argument("--subscribe-port", type=int, default=5060, help="SUBSCRIBE destination port")
    parser.add_argument(
        "--register-transport",
        choices=["auto", "tcp", "udp"],
        default="auto",
        help="Transport for REGISTER (auto means try TCP then UDP)",
    )
    parser.add_argument(
        "--subscribe-transport",
        choices=["udp", "tcp"],
        default="udp",
        help="Transport for SUBSCRIBE",
    )
    parser.add_argument("--timeout", type=float, default=3.0, help="Socket timeout in seconds")
    parser.add_argument(
        "--register-retries",
        type=int,
        default=10,
        help="Maximum REGISTER attempts when 200 OK is not received",
    )
    parser.add_argument(
        "--register-retry-interval",
        type=float,
        default=2.0,
        help="Seconds to wait before re-sending REGISTER",
    )
    parser.add_argument(
        "--step-delay",
        type=float,
        default=0.2,
        help="Delay between REGISTER and SUBSCRIBE",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print request payloads only, do not send",
    )
    parser.add_argument(
        "--print-response",
        action="store_true",
        help="Print full SIP responses",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    register_request = build_register_request()
    subscribe_request = build_subscribe_request()

    if args.dry_run:
        print("===== REGISTER REQUEST =====")
        print(register_request)
        print("===== SUBSCRIBE REQUEST =====")
        print(subscribe_request)
        return 0

    register_transports = ["tcp", "udp"] if args.register_transport == "auto" else [args.register_transport]

    try:
        if args.register_retries < 1:
            raise RuntimeError("--register-retries must be >= 1")

        register_success = False
        register_error: Exception | None = None
        for attempt in range(1, args.register_retries + 1):
            try:
                print(f"[REGISTER] attempt {attempt}/{args.register_retries}")
                run_step(
                    step_name="REGISTER",
                    request_raw=register_request,
                    host=args.server_host,
                    port=args.register_port,
                    transports=register_transports,
                    timeout_sec=args.timeout,
                    expected_method="REGISTER",
                    expected_call_id=REGISTER_CALL_ID,
                    print_response=args.print_response,
                )
                register_success = True
                break
            except Exception as exc:
                register_error = exc
                if attempt < args.register_retries:
                    print(
                        f"[REGISTER] no 200 OK, retry in {args.register_retry_interval:.1f}s: {exc}"
                    )
                    time.sleep(args.register_retry_interval)

        if not register_success:
            raise RuntimeError(
                f"REGISTER failed after {args.register_retries} attempts: {register_error}"
            )

        if args.step_delay > 0:
            time.sleep(args.step_delay)

        run_step(
            step_name="SUBSCRIBE",
            request_raw=subscribe_request,
            host=args.server_host,
            port=args.subscribe_port,
            transports=[args.subscribe_transport],
            timeout_sec=args.timeout,
            expected_method="SUBSCRIBE",
            expected_call_id=SUBSCRIBE_CALL_ID,
            print_response=args.print_response,
        )
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print("Flow finished: REGISTER -> SUBSCRIBE both passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
