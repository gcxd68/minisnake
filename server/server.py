import os
import time
import secrets
import sqlite3
from flask import Flask
from dotenv import load_dotenv

# Load environment variables from the .env file
load_dotenv()

app = Flask(__name__)

# Look for PORT instead of VPS_PORT
PORT = int(os.environ.get("PORT", 8000))

# Server dictated game rules
GAME_WIDTH = 25
GAME_HEIGHT = 20
INITIAL_DELAY = 250000
SPEEDUP_FACTOR = 0.985
POINTS_PER_FRUIT = 10
CHEAT_TIMEOUT = 5000
INITIAL_SIZE = 1
MIN_PING_INTERVAL = 0.05
MAX_SCORE = GAME_WIDTH * GAME_HEIGHT * POINTS_PER_FRUIT

# --- SQLite Database Initialization ---
def init_db():
    """ Creates the local database file and table if they don't exist. """
    conn = sqlite3.connect('scores.db')
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS scores
                 (name TEXT, score INTEGER, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)''')
    conn.commit()
    conn.close()

init_db()
# --------------------------------------

# Dictionary to store active game sessions
# Format: { "token": {"score": int, "last_ping": float, "cheated": bool} }
active_sessions = {}

def cleanup_stale_sessions():
    """ Removes sessions inactive for > 15 minutes. """
    now = time.time()
    stale_tokens = [t for t, d in active_sessions.items() if now - d["last_ping"] > 900]
    for t in stale_tokens:
        del active_sessions[t]
    if stale_tokens:
        print(f"CLEANUP: Removed {len(stale_tokens)} abandoned sessions.")

@app.route('/rules', methods=['GET'])
def get_rules():
    """ 
    Public endpoint to fetch game dimensions and rules without spawning a session token.
    Returns: format 'width|height|delay|speedup_factor|points|cheat_timeout|initial_size'
    """
    return f"{GAME_WIDTH}|{GAME_HEIGHT}|{INITIAL_DELAY}|{SPEEDUP_FACTOR}|{POINTS_PER_FRUIT}|{CHEAT_TIMEOUT}|{INITIAL_SIZE}", 200

@app.route('/token', methods=['GET'])
def get_token():
    """ 
    Spawns a new session and returns only the token. 
    Rules are fetched separately via /rules.
    """
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
    if token not in active_sessions or active_sessions[token]["cheated"]:
        return "Unauthorized", 403
    
    now = time.time()
    session = active_sessions[token]

    # Dynamically calculate the theoretical minimum delay based on the current score
    fruits_eaten = session["score"] // POINTS_PER_FRUIT
    # Current delay in seconds (INITIAL_DELAY is in microseconds)
    current_delay_sec = (INITIAL_DELAY / 1000000.0) * (SPEEDUP_FACTOR ** fruits_eaten)
    # 20% margin to compensate for network latency and jitter
    min_ping_interval = current_delay_sec * 0.8

    # Dynamic anti-spam check
    if now - session["last_ping"] < min_ping_interval:
        session["cheated"] = True
        print(f"SPEEDHACK DETECTED: Invalid ping interval for session {token}")
        return "Speedhack detected", 400

    # Dynamic physical limit check
    if session["score"] >= MAX_SCORE:
        session["cheated"] = True
        print(f"MAX SCORE EXCEEDED: Session {token} flagged.")
        return "Score limit exceeded", 400

    session["score"] += POINTS_PER_FRUIT
    session["last_ping"] = now
    return "Yum", 200

@app.route('/cheat/<token>', methods=['GET'])
def flag_cheat(token):
    if token in active_sessions:
        active_sessions[token]["cheated"] = True
        print(f"CHEAT FLAG RECEIVED: Local manipulation detected for session {token}")
    return "Flagged", 200

@app.route('/submit/<token>/<name>', methods=['GET'])
def submit_score(token, name):
    if token not in active_sessions:
        return "Unauthorized", 403
    
    # SECURITY: Strict player name validation to prevent injection or DB saturation
    if not name.isalnum() or len(name) > 8:
        return "Invalid Name", 400

    session = active_sessions.pop(token)
    
    if session["cheated"]:
        print(f"BANNED: Player '{name}' flagged for cheating. Forcing score to 0.")
        final_score = 0
    else:
        final_score = session["score"]
        if final_score == 0:
            print(f"REJECTED: Score too low ({final_score}) for '{name}'")
            return "Score too low", 400

    if final_score < 0 or final_score > MAX_SCORE:
        print(f"REJECTED: Impossible calculated score {final_score} for '{name}'")
        return "Invalid Score", 400

    # Save to local SQLite Database
    try:
        conn = sqlite3.connect('scores.db')
        c = conn.cursor()
        c.execute("INSERT INTO scores (name, score) VALUES (?, ?)", (name, final_score))
        conn.commit()
        conn.close()
        print(f"VALID SESSION: '{name}' scored {final_score}. Saved to local DB.")
        return "OK", 200
    except Exception as e:
        print(f"Database error: {e}")
        return "Backend Error", 500

@app.route('/scores/<int:limit>', methods=['GET'])
def get_scores(limit):
    """ Public endpoint reading from the local SQLite DB, mocking Dreamlo's format. """
    try:
        conn = sqlite3.connect('scores.db')
        c = conn.cursor()
        # Fetch best scores, sort by score descending, then by oldest first if tie
        c.execute("SELECT name, score FROM scores ORDER BY score DESC, timestamp ASC LIMIT ?", (limit,))
        rows = c.fetchall()
        conn.close()

        # Format strictly like Dreamlo to ensure C client compatibility (Name|Score|Seconds|Extras)
        result = ""
        for row in rows:
            result += f"{row[0]}|{row[1]}|0|0\n"
            
        return result, 200
    except Exception as e:
        print(f"Database read error: {e}")
        return "Backend Error", 500

if __name__ == '__main__':
    # Using the new PORT variable
    app.run(host='0.0.0.0', port=PORT)
