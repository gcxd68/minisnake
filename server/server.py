import os
import time
from flask import Flask
import requests
from dotenv import load_dotenv

# Load environment variables from the .env file
load_dotenv()

app = Flask(__name__)

# Configuration from environment variables
VPS_PORT = int(os.environ.get("VPS_PORT", 80))
VPS_PRIVATE_KEY = os.environ.get("VPS_PRIVATE_KEY")
VPS_PUBLIC_KEY  = os.environ.get("VPS_PUBLIC_KEY")
DREAMLO_PRIVATE_KEY = os.environ.get("DREAMLO_PRIVATE_KEY")
DREAMLO_PUBLIC_KEY  = os.environ.get("DREAMLO_PUBLIC_KEY")

# Dictionary to store seen signatures: signature -> timestamp
seen_requests = {}

def check_signature(sig, name, score, ts):
    now = int(time.time())
    
    # 1. Deny if the timestamp is older than 5 minutes (or too far in the future)
    if abs(now - ts) > 300:
        return False
        
    # 2. Deny if this exact signature has already been processed (Pure Replay attack)
    if sig in seen_requests:
        return False
        
    # 3. Clean up the server memory by removing expired signatures
    expired = [k for k, v in seen_requests.items() if (now - v) > 300]
    for k in expired:
        del seen_requests[k]
        
    # 4. Verify the djb2 fingerprint (replicates the logic from the C client)
    text = f"{VPS_PRIVATE_KEY}{name}{score}{ts}"
    h = 5381
    for c in text:
        h = (((h << 5) + h) + ord(c)) & 0xFFFFFFFF
        
    expected_sig = f"{h:08x}"
    return sig == expected_sig

@app.route('/lb/<sig>/add/<name>/<int:score>/<int:ts>', methods=['GET'])
def add_score(sig, name, score, ts):
    if not check_signature(sig, name, score, ts):
        print(f"SECURITY ALERT: Cheating/replay attempt blocked for '{name}'")
        return "Unauthorized", 403

    # Log the signature as "seen" to prevent Replay attacks
    seen_requests[sig] = ts
    print(f"Valid score (djb2 hash received). Forwarding to Dreamlo: {name} -> {score}")
    
    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PRIVATE_KEY}/add/{name}/{score}"
    try:
        response = requests.get(dreamlo_url, timeout=5)
        return response.text
    except Exception as e:
        print(f"Communication error with Dreamlo: {e}")
        return "Backend Error", 500

@app.route('/lb/<key>/pipe/<int:limit>', methods=['GET'])
def get_scores(key, limit):
    # Verify the public key for read-only access
    if key != VPS_PUBLIC_KEY:
        return "Unauthorized", 403

    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PUBLIC_KEY}/pipe/{limit}"
    try:
        response = requests.get(dreamlo_url, timeout=5)
        return response.text
    except Exception as e:
        return "Backend Error", 500

if __name__ == '__main__':
    # Start the server on all interfaces at the configured port
    app.run(host='0.0.0.0', port=VPS_PORT)
