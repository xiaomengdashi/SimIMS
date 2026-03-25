#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
LOG_DIR="$REPO_ROOT/build/test-logs"
BARESIP_DIR="$REPO_ROOT/.baresip"

rm -f "$LOG_DIR/ims_baresip_register.log" "$LOG_DIR/baresip_register.log"

mkdir -p "$LOG_DIR"
mkdir -p "$BARESIP_DIR"

cat > "$BARESIP_DIR/accounts" <<'EOF'
<sip:testuser@ims.example.com;transport=udp>;auth_user=testuser;auth_pass=testpass;outbound="sip:127.0.0.1:5060;transport=udp";regint=3600
EOF

pkill -f ims_allinone || true

"$REPO_ROOT/bin/ims_allinone" "$REPO_ROOT/config/ims.yaml" > "$LOG_DIR/ims_baresip_register.log" 2>&1 &
IMS_PID=$!
trap 'kill "$IMS_PID" 2>/dev/null || true' EXIT

baresip -f "$BARESIP_DIR" 2>&1 | tee "$LOG_DIR/baresip_register.log"
