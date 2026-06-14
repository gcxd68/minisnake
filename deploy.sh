#!/usr/bin/env bash
#
# deploy.sh — Deploy the minisnake Go backend as a hardened systemd service.
#
# Usage:   sudo ./deploy.sh
#
# What it does:
#   1. Auto-detects the project layout from the script's own location.
#   2. Builds the server binary if it is missing.
#   3. Installs a systemd unit that runs as your normal user (not root),
#      restarts on failure with an anti-crash-loop guard, and applies
#      filesystem sandboxing.
#   4. Fixes ownership so the non-root service can write scores.db.
#   5. Enables + starts the service and prints its status.
#
# Re-running is safe: it overwrites the unit and restarts cleanly.

set -euo pipefail

SERVICE_NAME="minisnake"
PORT="${PORT:-8000}"

# --- 0. Must run as root (systemd unit lives in /etc) -----------------------
if [[ "${EUID}" -ne 0 ]]; then
    echo "Error: this script must be run with sudo (it writes to /etc/systemd)." >&2
    echo "       Try: sudo ./deploy.sh" >&2
    exit 1
fi

# The real user who invoked sudo. The service will run as THIS user, not root.
RUN_USER="${SUDO_USER:-root}"
if [[ "${RUN_USER}" == "root" ]]; then
    echo "Warning: running the service as root is discouraged." >&2
    echo "         Run this script via 'sudo ./deploy.sh' from a normal user account." >&2
fi

# --- 1. Resolve paths from the script location ------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}"
SERVER_DIR="${PROJECT_ROOT}/server"
BIN_PATH="${PROJECT_ROOT}/bin/server"

if [[ ! -d "${SERVER_DIR}" ]]; then
    echo "Error: server directory not found at ${SERVER_DIR}" >&2
    echo "       Place deploy.sh at the project root (next to the Makefile)." >&2
    exit 1
fi

# --- 2. Build the binary if needed ------------------------------------------
if [[ ! -x "${BIN_PATH}" ]]; then
    echo ">> Server binary not found, building with 'make server'..."
    sudo -u "${RUN_USER}" make -C "${PROJECT_ROOT}" server
fi

if [[ ! -x "${BIN_PATH}" ]]; then
    echo "Error: build did not produce an executable at ${BIN_PATH}" >&2
    exit 1
fi

# --- 3. Fix ownership so the non-root service can write scores.db -----------
echo ">> Ensuring ${RUN_USER} owns ${SERVER_DIR} (for scores.db writes)..."
chown -R "${RUN_USER}:${RUN_USER}" "${SERVER_DIR}"

# --- 4. Write the systemd unit ----------------------------------------------
UNIT_PATH="/etc/systemd/system/${SERVICE_NAME}.service"
echo ">> Writing ${UNIT_PATH}..."

cat > "${UNIT_PATH}" <<EOF
[Unit]
Description=Minisnake Backend Server
After=network.target

[Service]
Type=simple
User=${RUN_USER}
WorkingDirectory=${SERVER_DIR}
ExecStart=${BIN_PATH}
Environment=PORT=${PORT}

# Inject hidden environment variables from .env if present
EnvironmentFile=-${SERVER_DIR}/.env

# Resilience: restart only on abnormal exit, with an anti-crash-loop guard.
Restart=on-failure
RestartSec=3
StartLimitIntervalSec=60
StartLimitBurst=5

# Security hardening: sandbox the service.
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=read-only
PrivateTmp=true
# scores.db lives here, so this path must stay writable.
ReadWritePaths=${SERVER_DIR}

[Install]
WantedBy=multi-user.target
EOF

# --- 5. Reload, enable, (re)start -------------------------------------------
echo ">> Reloading systemd and starting the service..."
systemctl daemon-reload
systemctl enable "${SERVICE_NAME}" >/dev/null
systemctl restart "${SERVICE_NAME}"

# --- 6. Report ---------------------------------------------------------------
sleep 1
echo
echo "==================================================================="
systemctl --no-pager --full status "${SERVICE_NAME}" || true
echo "==================================================================="
echo
echo "Done. The server runs as '${RUN_USER}' on port ${PORT}."
echo
echo "Survival commands:"
echo "  Status :  sudo systemctl status ${SERVICE_NAME}"
echo "  Logs   :  sudo journalctl -fu ${SERVICE_NAME}"
echo "  Stop   :  sudo systemctl stop ${SERVICE_NAME}"
echo "  Restart:  sudo systemctl restart ${SERVICE_NAME}"
echo
echo "If this is a VPS, open the port in the firewall, e.g.:"
echo "  sudo ufw allow ${PORT}"
