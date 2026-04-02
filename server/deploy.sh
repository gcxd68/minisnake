#!/bin/bash

# Exit on error
set -e

echo "--- 🛠️ Starting Deployment ---"

# 1. Root/Sudo handling
if command -v sudo >/dev/null; then
    SUDO="sudo"
else
    SUDO=""
fi

# 2. Check for .env file
if [ ! -f ".env" ]; then
    echo "❌ Error: .env file not found!"
    exit 1
fi

# 3. Universal Package Manager & Cache Refresh
echo "Step 1: Detecting package manager..."
if command -v apt >/dev/null; then
    $SUDO apt update
    $SUDO apt install -y python3-venv 2>/dev/null || true
    PKG_INST="$SUDO apt install -y"
elif command -v dnf >/dev/null; then
    $SUDO dnf makecache || true
    PKG_INST="$SUDO dnf install -y"
elif command -v yum >/dev/null; then
    $SUDO yum makecache || true
    PKG_INST="$SUDO yum install -y"
elif command -v apk >/dev/null; then
    $SUDO apk update
    PKG_INST="$SUDO apk add"
else
    echo "❌ No supported package manager found."
    exit 1
fi

# 4. Essential Dependencies
echo "Step 2: Installing Python dependencies..."
$PKG_INST python3 python3-pip || { echo "❌ Python install failed"; exit 1; }

# 5. Time Sync
echo "Step 3: Syncing time..."
if command -v timedatectl >/dev/null; then
    # Enable network time synchronization
    $SUDO timedatectl set-ntp true || true
    # Force system to UTC to avoid timezone/daylight savings issues
    $SUDO timedatectl set-timezone UTC || true
else
    echo "⚠️ timedatectl not available, skipping NTP toggle"
fi

# 6. Robust Venv Creation
echo "Step 4: Setting up Venv..."
if [ ! -d "venv" ]; then
    python3 -m venv venv || {
        python3 -m ensurepip || exit 1
        python3 -m venv venv
    }
fi

# 7. Safe Python Installation
echo "Step 5: Installing Flask & Gunicorn..."
venv/bin/python -m pip install --upgrade pip || (sleep 2 && venv/bin/python -m pip install --upgrade pip)
venv/bin/python -m pip install flask requests python-dotenv gunicorn

# 8. Firewall (UFW Only)
echo "Step 6: Configuring Firewall..."
if command -v ufw >/dev/null; then
    $SUDO ufw allow 8000/tcp
    $SUDO ufw --force enable || true
fi

# 9. Process Management (Anti-Race Condition)
echo "Step 7: Cleaning up existing processes..."
if command -v pkill >/dev/null; then
    pkill -f "gunicorn.*server:app" || true
else
    ps aux | grep "gunicorn.*server:app" | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null || true
fi

# Wait for port to be released (up to 5 seconds)
MAX_RETRIES=5
RETRY_COUNT=0
while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    if ! (ss -tuln 2>/dev/null | grep -q ":8000 ") && ! (netstat -tuln 2>/dev/null | grep -q ":8000 "); then
        break
    fi
    echo "⏳ Waiting for port 8000 to be free... ($((RETRY_COUNT+1))/$MAX_RETRIES)"
    sleep 1
    RETRY_COUNT=$((RETRY_COUNT+1))
done

# 10. Launch with nohup
echo "Step 8: Launching Gunicorn..."
nohup venv/bin/gunicorn --workers 1 --threads 2 --bind 0.0.0.0:8000 server:app > gunicorn.log 2>&1 &

# 11. Deep Verification (Wait for Bind)
sleep 2
SUCCESS=0
if pgrep -f "gunicorn.*server:app" >/dev/null; then
    if (ss -tuln 2>/dev/null | grep -q ":8000 ") || (netstat -tuln 2>/dev/null | grep -q ":8000 "); then
        SUCCESS=1
    fi
fi

if [ $SUCCESS -eq 1 ]; then
    SERVER_IP=$(hostname -I | awk '{print $1}' || echo "localhost")
    echo "--- ✅ Deployment Complete! ---"
    echo "🌐 Server running on: http://$SERVER_IP:8000"
    echo "📄 Logs: tail -f gunicorn.log"
else
    echo "❌ Deployment Failed! Gunicorn process exists but port 8000 is not listening."
    echo "Check gunicorn.log for bind errors."
    exit 1
fi
