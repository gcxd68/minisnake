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
PENALTY_INTERVAL = 10
PENALTY_AMOUNT = 1
SPAWN_FRUIT_MAX_ATTEMPTS = 10000
MIN_PING_INTERVAL = 0.05
MAX_SCORE = GAME_WIDTH * GAME_HEIGHT * POINTS_PER_FRUIT

# Pseudo-random generator synchronized with the C client (LCG algorithm)
def lcg_rand(seed):
    return (seed * 1103515245 + 12345) & 0x7fffffff

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
# Format: { "token": {"score": int, "fruits_eaten": int, "last_ping": float, "cheated": bool, "seed": int, "last_x": int, "last_y": int, "last_steps": int} }
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
    Returns: format 'width|height|delay|speedup_factor|points|cheat_timeout|initial_size|penalty_interval|penalty_amount|spawn_fruit_max_attempts'
    """
    return f"{GAME_WIDTH}|{GAME_HEIGHT}|{INITIAL_DELAY}|{SPEEDUP_FACTOR}|{POINTS_PER_FRUIT}|{CHEAT_TIMEOUT}|{INITIAL_SIZE}|{PENALTY_INTERVAL}|{PENALTY_AMOUNT}|{SPAWN_FRUIT_MAX_ATTEMPTS}", 200

@app.route('/token', methods=['GET'])
def get_token():
    """ 
    Spawns a new session and returns the token AND the synchronized seed. 
    Rules are fetched separately via /rules.
    """
    cleanup_stale_sessions()
    token = secrets.token_hex(16)
    original_seed = secrets.randbits(31) # 31-bit seed for C compatibility
    
    # --- SEED BURN LOGIC ---
    # Simulate the C client's initial placement to keep the RNG sequence perfectly synced
    current_seed = original_seed
    
    offset_x = 0
    if GAME_WIDTH % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_x = current_seed % 2
        
    offset_y = 0
    if GAME_HEIGHT % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_y = current_seed % 2
    # -----------------------

    active_sessions[token] = {
        "score": 0,
        "fruits_eaten": 0,
        "last_ping": time.time(),
        "cheated": False,
        "seed": current_seed, # Important: Store the seed AFTER the initial burn
        "last_x": (GAME_WIDTH // 2) - offset_x,
        "last_y": (GAME_HEIGHT // 2) - offset_y,
        "last_steps": 0
    }
    
    # We return the original, unburned seed to the client so it can do the exact same math!
    return f"{token}|{original_seed}", 200

@app.route('/eat/<token>/<int:steps>/<int:fx>/<int:fy>', methods=['GET'])
def eat_fruit(token, steps, fx, fy):
    if token not in active_sessions or active_sessions[token]["cheated"]:
        return "Unauthorized", 403
    
    now = time.time()
    session = active_sessions[token]

    # Calculate steps taken specifically for THIS fruit
    delta_steps = steps - session["last_steps"]
    if delta_steps <= 0:
        session["cheated"] = True
        print(f"INVALID STEPS LOGIC: Total steps didn't increase for session {token}")
        return "Invalid steps logic", 400

    # --- 1. FRUIT VALIDATION (Window of candidates) ---
    # The server checks if (fx, fy) is one of the valid upcoming fruits
    valid_fruit = False
    temp_seed = session["seed"]
    
    # Allow up to 10000 re-rolls to account for snake body collisions
    for _ in range(10000):
        temp_seed = lcg_rand(temp_seed)
        cand_x = (temp_seed >> 16) % GAME_WIDTH
        temp_seed = lcg_rand(temp_seed)
        cand_y = (temp_seed >> 16) % GAME_HEIGHT
        
        if cand_x == fx and cand_y == fy:
            valid_fruit = True
            session["seed"] = temp_seed # Synchronize the seed at this exact point
            break

    if not valid_fruit:
        session["cheated"] = True
        print(f"INVALID FRUIT DETECTED: ({fx}, {fy}) for session {token}")
        return "Invalid fruit coordinates", 400

    # --- 2. MANHATTAN DISTANCE VALIDATION (Anti-Teleportation) ---
    # Calculates the absolute shortest path. You cannot take fewer steps than this distance.
    manhattan = abs(fx - session["last_x"]) + abs(fy - session["last_y"])
    if delta_steps < manhattan:
        session["cheated"] = True
        print(f"SPEEDHACK/TELEPORT: {delta_steps} steps for {manhattan} dist in session {token}")
        return "Impossible move", 400

    # --- 3. DYNAMIC ANTI-SPAM (Time-based speedhack check) ---
    # We must use fruits_eaten here because score decreases over time
    current_delay_sec = (INITIAL_DELAY / 1000000.0) * (SPEEDUP_FACTOR ** session["fruits_eaten"])
    
    # Theoretical minimum time for delta_steps, with 20% network margin
    min_ping_interval = current_delay_sec * delta_steps * 0.8
    
    if now - session["last_ping"] < min_ping_interval:
        session["cheated"] = True
        print(f"SPEEDHACK DETECTED: Ping interval too short for {delta_steps} steps.")
        return "Speedhack detected", 400

    # --- 4. EXACT SCORE DEGRADATION MATCHING THE C CLIENT ---
    # The server loops over the exact step interval to apply the dynamic penalty
    for step in range(session["last_steps"] + 1, steps + 1):
        if PENALTY_INTERVAL > 0 and step % PENALTY_INTERVAL == 0:
            if session["score"] >= PENALTY_AMOUNT:
                session["score"] -= PENALTY_AMOUNT
            else:
                session["score"] = 0 # Hard floor at 0

    # Add the fruit value AFTER applying the walking penalty
    session["score"] += POINTS_PER_FRUIT
    session["fruits_eaten"] += 1

    # Dynamic physical limit check
    if session["score"] >= MAX_SCORE:
        session["cheated"] = True
        print(f"MAX SCORE EXCEEDED: Session {token} flagged.")
        return "Score limit exceeded", 400

    # Update session state for the next fruit
    session["last_ping"] = now
    session["last_x"] = fx
    session["last_y"] = fy
    session["last_steps"] = steps 
    return "Yum", 200

@app.route('/cheat/<token>', methods=['GET'])
def flag_cheat(token):
    if token in active_sessions:
        active_sessions[token]["cheated"] = True
        print(f"CHEAT FLAG RECEIVED: Local manipulation detected for session {token}")
    return "Flagged", 200

@app.route('/submit/<token>/<name>/<int:steps>', methods=['GET'])
def submit_score(token, name, steps):
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
        # Apply degradation penalty for the final steps taken before dying
        for step in range(session["last_steps"] + 1, steps + 1):
            if PENALTY_INTERVAL > 0 and step % PENALTY_INTERVAL == 0:
                if session["score"] >= PENALTY_AMOUNT:
                    session["score"] -= PENALTY_AMOUNT
                else:
                    session["score"] = 0 # Hard floor at 0
                    
        final_score = session["score"]
        if final_score == 0:
            print(f"REJECTED: Score too low ({final_score}) for '{name}'")
            return "Score too low", 400

    if final_score > MAX_SCORE:
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
