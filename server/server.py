import os
import time
import secrets
from flask import Flask
import requests
from dotenv import load_dotenv

# Load environment variables from the .env file
load_dotenv()

app = Flask(__name__)

VPS_PORT = int(os.environ.get("VPS_PORT", 8000))
DREAMLO_PRIVATE_KEY = os.environ.get("DREAMLO_PRIVATE_KEY")
DREAMLO_PUBLIC_KEY  = os.environ.get("DREAMLO_PUBLIC_KEY")

# Dictionary to store active game sessions
# Format: { "token": {"score": int, "last_ping": float, "cheated": bool} }
active_sessions = {}

# --- Garbage Collector (Memory Leak Protection) ---
def cleanup_stale_sessions():
    """ 
    Removes sessions that have been inactive for more than 15 minutes.
    Prevents memory leaks if a player closes the terminal without finishing the game.
    """
    now = time.time()
    # 900 seconds = 15 minutes without any /eat ping
    stale_tokens = [token for token, data in active_sessions.items() 
                    if now - data["last_ping"] > 900]
    
    for token in stale_tokens:
        del active_sessions[token]
        
    if stale_tokens:
        print(f"CLEANUP: Removed {len(stale_tokens)} abandoned sessions.")
# --------------------------------------------------

@app.route('/start', methods=['GET'])
def start_session():
    """
    Called by the C client when the game starts.
    Generates a token and initializes the server-side score.
    """
    # Clean up old sessions when a new player connects
    cleanup_stale_sessions()
    
    token = secrets.token_hex(16)
    active_sessions[token] = {
        "score": 0,
        "last_ping": time.time(),
        "cheated": False
    }
    return token, 200

@app.route('/eat/<token>', methods=['GET'])
def eat_fruit(token):
    """
    Called asynchronously by the C client each time the snake eats a fruit.
    The server calculates the score and verifies physical time constraints.
    """
    if token not in active_sessions or active_sessions[token]["cheated"]:
        return "Unauthorized", 403
    
    now = time.time()
    session = active_sessions[token]

    # Anti-Spam: It's physically impossible to eat 2 fruits in under 0.05s
    if now - session["last_ping"] < 0.05:
        session["cheated"] = True
        print(f"SPEEDHACK DETECTED: Invalid ping interval for session {token}")
        return "Speedhack detected", 400

    # Theoretical physical limit (25x20 = 500 cells = 5000 points)
    # Prevents attackers from replaying the /eat endpoint via packet sniffing
    if session["score"] >= 5000:
        session["cheated"] = True
        print(f"MAX SCORE EXCEEDED: Session {token} flagged.")
        return "Score limit exceeded", 400

    # The server increments the score securely
    session["score"] += 10
    session["last_ping"] = now
    return "Yum", 200

@app.route('/cheat/<token>', methods=['GET'])
def flag_cheat(token):
    """
    Called asynchronously if the C client's local anti-cheat triggers.
    """
    if token in active_sessions:
        active_sessions[token]["cheated"] = True
        print(f"CHEAT FLAG RECEIVED: Local manipulation detected for session {token}")
    return "Flagged", 200

@app.route('/submit/<token>/<name>', methods=['GET'])
def submit_score(token, name):
    """
    Called at Game Over. The client NO LONGER sends the score.
    The server uses its own securely calculated score.
    """
    if token not in active_sessions:
        return "Unauthorized", 403
    
    # Retrieve and destroy the session (prevents replay attacks)
    session = active_sessions.pop(token)
    
    if session["cheated"]:
        print(f"REJECTED: Player '{name}' was flagged for cheating.")
        return "Cheater", 403

    # Absolute grid limit check (500 cells * 10 points = 5000)
    final_score = session["score"]
    if final_score > 5000:
        print(f"REJECTED: Impossible calculated score {final_score} for '{name}'")
        return "Invalid Score", 400

    print(f"VALID SESSION: '{name}' scored {final_score}. Forwarding to Dreamlo...")
    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PRIVATE_KEY}/add/{name}/{final_score}"
    
    try:
        requests.get(dreamlo_url, timeout=5)
        return "OK", 200
    except Exception as e:
        print(f"Communication error with Dreamlo: {e}")
        return "Backend Error", 500

@app.route('/scores/<int:limit>', methods=['GET'])
def get_scores(limit):
    """ Public endpoint to read the leaderboard. """
    try:
        response = requests.get(f"http://dreamlo.com/lb/{DREAMLO_PUBLIC_KEY}/pipe/{limit}", timeout=5)
        return response.text
    except Exception as e:
        return "Backend Error", 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=VPS_PORT)
