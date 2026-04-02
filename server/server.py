import os
import time
import secrets
from flask import Flask
import requests
from dotenv import load_dotenv

# Load environment variables from the .env file
load_dotenv()

app = Flask(__name__)

# Configuration (VPS_PRIVATE_KEY and VPS_PUBLIC_KEY are no longer needed!)
VPS_PORT = int(os.environ.get("VPS_PORT", 8000))
DREAMLO_PRIVATE_KEY = os.environ.get("DREAMLO_PRIVATE_KEY")
DREAMLO_PUBLIC_KEY  = os.environ.get("DREAMLO_PUBLIC_KEY")

# Dictionary to store active game sessions: { "token" : start_timestamp }
active_sessions = {}

@app.route('/start', methods=['GET'])
def start_session():
    """
    Called by the C client at the very beginning of the game.
    Generates a unique token and records the exact start time.
    """
    # Generate a unique 32-character hex token (16 bytes)
    token = secrets.token_hex(16)
    active_sessions[token] = time.time()
    return token, 200

@app.route('/submit/<token>/<name>/<int:score>', methods=['GET'])
def submit_score(token, name, score):
    """
    Called by the C client at the end of the game (Game Over).
    Validates the token, the elapsed time, and the physical game limits.
    """
    # 1. Does the token exist in the active sessions?
    if token not in active_sessions:
        print(f"REJECTED: Invalid or expired token for player '{name}'")
        return "Unauthorized", 403

    # 2. Consume the token (Anti-Replay)
    # .pop() retrieves the start time AND deletes the token instantly
    start_time = active_sessions.pop(token)
    duration = time.time() - start_time
    
    # 3. The TRUE Anti-Cheat: Physical consistency (Score vs Time)
    # In your game, a fruit gives 10 points. Even playing extremely fast,
    # it's impossible to score 100 points per second.
    # If the player scored faster than physically possible, it's a hack.
    if score > 0 and duration < (score / 100.0):
        print(f"CHEAT DETECTED: {score} points in {duration:.2f}s for '{name}' = Impossible")
        return "Speedhack Detected", 400

    # 4. Absolute grid limit
    # 25x20 grid = 500 cells. 500 cells * 10 points = 5000 theoretical max score.
    if score > 5000 or score < 0:
        print(f"REJECTED: Aberrant score of {score} for player '{name}'")
        return "Invalid Score", 400

    print(f"Valid Session: {name} scored {score} points (Duration: {duration:.0f}s). Forwarding to Dreamlo...")
    
    # Forward the validated score to Dreamlo's hidden API
    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PRIVATE_KEY}/add/{name}/{score}"
    try:
        requests.get(dreamlo_url, timeout=5)
        return "OK", 200
    except Exception as e:
        print(f"Communication error with Dreamlo: {e}")
        return "Backend Error", 500

@app.route('/scores/<int:limit>', methods=['GET'])
def get_scores(limit):
    """
    Public endpoint to read the leaderboard.
    VPS_PUBLIC_KEY is no longer needed, as this leaderboard is public anyway.
    """
    try:
        # We still use the DREAMLO public key on the server side to read
        response = requests.get(f"http://dreamlo.com/lb/{DREAMLO_PUBLIC_KEY}/pipe/{limit}", timeout=5)
        return response.text
    except Exception as e:
        return "Backend Error", 500

if __name__ == '__main__':
    # Start the server on all network interfaces at the configured port
    app.run(host='0.0.0.0', port=VPS_PORT)
