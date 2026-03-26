#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
LOG_DIR="$REPO_ROOT/build/test-logs"
RUNTIME_DIR="$REPO_ROOT/.baresip-invite"
CALLER_DIR="$RUNTIME_DIR/caller"
CALLEE_DIR="$RUNTIME_DIR/callee"
IMS_LOG="$LOG_DIR/ims_baresip_invite.log"
CALLER_LOG="$LOG_DIR/baresip_caller.log"
CALLEE_LOG="$LOG_DIR/baresip_callee.log"
CALLER_USER="testuser"
CALLEE_USER="callee"
DOMAIN="ims.example.com"
PASSWORD="testpass"
OUTBOUND='sip:127.0.0.1:5060;transport=udp'
RTPENGINE_STUB_LOG="$LOG_DIR/rtpengine_stub.log"

IMS_PID=""
CALLER_PID=""
CALLEE_PID=""
RTPENGINE_STUB_PID=""

cleanup() {
    for pid in "$CALLER_PID" "$CALLEE_PID" "$IMS_PID" "$RTPENGINE_STUB_PID"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
}
trap cleanup EXIT

mkdir -p "$LOG_DIR"
rm -rf "$RUNTIME_DIR"
mkdir -p "$CALLER_DIR" "$CALLEE_DIR"
rm -f "$IMS_LOG" "$CALLER_LOG" "$CALLEE_LOG" "$RTPENGINE_STUB_LOG"

write_config() {
    local target_dir="$1"
    local cons_port="$2"
    local http_port="$3"
    local ctrl_port="$4"

    cat > "$target_dir/config" <<EOF
# Auto-generated for dual baresip INVITE test
poll_method		epoll
call_local_timeout	120
call_max_calls		4
audio_player		alsa,default
audio_source		alsa,default
audio_alert		alsa,default
audio_level		no
ausrc_format		s16
auplay_format		s16
auenc_format		s16
audec_format		s16
audio_buffer		20-160
video_size		352x288
video_bitrate		500000
video_fps		25.00
video_fullscreen	no
videnc_format		yuv420p
rtp_tos			184
rtcp_mux		no
jitter_buffer_delay	5-10
rtp_stats		no
module_path		/usr/lib/baresip/modules
module			stdio.so
module			g711.so
module			alsa.so
module			stun.so
module			turn.so
module			ice.so
module_tmp		uuid.so
module_tmp		account.so
module_app		auloop.so
module_app		contact.so
module_app		debug_cmd.so
module_app		menu.so
module_app		vidloop.so
cons_listen		127.0.0.1:${cons_port}
http_listen		127.0.0.1:${http_port}
ctrl_tcp_listen		127.0.0.1:${ctrl_port}
opus_bitrate		28000
vumeter_stderr		yes
video_selfview		window
EOF
}

write_account() {
    local target_dir="$1"
    local user="$2"

    cat > "$target_dir/accounts" <<EOF
<sip:${user}@${DOMAIN};transport=udp>;auth_user=${user};auth_pass=${PASSWORD};outbound="${OUTBOUND}";regint=3600
EOF
}

write_config "$CALLER_DIR" 5555 8000 4444
write_config "$CALLEE_DIR" 5556 8001 4445
write_account "$CALLER_DIR" "$CALLER_USER"
write_account "$CALLEE_DIR" "$CALLEE_USER"

python3 - <<'PY' > "$RTPENGINE_STUB_LOG" 2>&1 &
import socket
import sys

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 22222))
print("rtpengine stub listening on 127.0.0.1:22222", flush=True)

while True:
    data, addr = sock.recvfrom(65535)
    try:
        text = data.decode("utf-8", errors="replace")
        cookie, _payload = text.split(" ", 1)
    except ValueError:
        continue

    response = f"{cookie} d6:result2:ok3:sdp0:e".encode()
    sock.sendto(response, addr)
PY
RTPENGINE_STUB_PID=$!

pkill -f ims_allinone || true

"$REPO_ROOT/bin/ims_allinone" "$REPO_ROOT/config/ims.yaml" > "$IMS_LOG" 2>&1 &
IMS_PID=$!

sleep 1

baresip -f "$CALLEE_DIR" -e "/answermode auto" -t 25 > "$CALLEE_LOG" 2>&1 &
CALLEE_PID=$!

sleep 3

baresip -f "$CALLER_DIR" -e "/dial sip:${CALLEE_USER}@${DOMAIN}" -t 25 > "$CALLER_LOG" 2>&1 &
CALLER_PID=$!

wait "$CALLER_PID" || true
wait "$CALLEE_PID" || true

printf '\n=== IMS log ===\n'
printf '%s\n' "$IMS_LOG"
printf '=== Caller log ===\n'
printf '%s\n' "$CALLER_LOG"
printf '=== Callee log ===\n'
printf '%s\n' "$CALLEE_LOG"
printf '=== RTPengine stub log ===\n'
printf '%s\n\n' "$RTPENGINE_STUB_LOG"

if ! grep -q "Registration complete for sip:${CALLER_USER}@${DOMAIN}" "$IMS_LOG"; then
    echo "ERROR: caller registration not observed in IMS log" >&2
    exit 1
fi

if ! grep -q "Registration complete for sip:${CALLEE_USER}@${DOMAIN}" "$IMS_LOG"; then
    echo "ERROR: callee registration not observed in IMS log" >&2
    exit 1
fi

if ! grep -q "INVITE received" "$IMS_LOG"; then
    echo "ERROR: no INVITE observed in IMS log" >&2
    exit 1
fi

if grep -q "Callee not found\|No active contacts" "$IMS_LOG"; then
    echo "ERROR: callee binding lookup failed" >&2
    exit 1
fi

if grep -q "Auth verified for sip:${CALLER_USER}@${DOMAIN}" "$IMS_LOG" && \
   grep -q "Auth verified for sip:${CALLEE_USER}@${DOMAIN}" "$IMS_LOG"; then
    :
else
    echo "ERROR: digest auth did not complete for both UEs" >&2
    exit 1
fi

if grep -Eq "Got response (180|183|200|486|603) for INVITE|SIP/2.0 (180|183|200|486|603)" "$IMS_LOG" "$CALLER_LOG" "$CALLEE_LOG"; then
    echo "PASS: dual baresip INVITE flow produced a verifiable SIP response"
    exit 0
fi

echo "ERROR: no verifiable INVITE response found; inspect logs above" >&2
exit 1
