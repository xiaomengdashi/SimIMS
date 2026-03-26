#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
LOG_DIR="$REPO_ROOT/build/test-logs"
BARESIP_DIR="$REPO_ROOT/.baresip"

UE_IMPU=${UE_IMPU:-sip:460112024122023@ims.mnc011.mcc460.3gppnetwork.org}
UE_IMPI=${UE_IMPI:-460112024122023@ims.mnc011.mcc460.3gppnetwork.org}
UE_PASSWORD=${UE_PASSWORD:-testpass}
UE_PCSCF_IP=${UE_PCSCF_IP:-127.0.0.1}
UE_PCSCF_PORT=${UE_PCSCF_PORT:-5060}
UE_TRANSPORT=${UE_TRANSPORT:-tcp}
UE_REGINT=${UE_REGINT:-600000}

rm -f "$LOG_DIR/ims_baresip_register.log" "$LOG_DIR/baresip_register.log"

mkdir -p "$LOG_DIR"
mkdir -p "$BARESIP_DIR"

cat > "$BARESIP_DIR/accounts" <<EOF
<${UE_IMPU};transport=${UE_TRANSPORT}>;auth_user=${UE_IMPI};auth_pass=${UE_PASSWORD};outbound="sip:${UE_PCSCF_IP}:${UE_PCSCF_PORT};transport=${UE_TRANSPORT}";regint=${UE_REGINT}
EOF

pkill -f ims_allinone || true

"$REPO_ROOT/bin/ims_allinone" "$REPO_ROOT/config/ims.yaml" > "$LOG_DIR/ims_baresip_register.log" 2>&1 &
IMS_PID=$!
trap 'kill "$IMS_PID" 2>/dev/null || true' EXIT

baresip -f "$BARESIP_DIR" 2>&1 | tee "$LOG_DIR/baresip_register.log"
