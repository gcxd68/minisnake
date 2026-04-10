# minisnake 🐍

A lightweight, terminal-based Snake game written in C with a dedicated online scoring backend server.

## Features

- 🎮 **Classic snake gameplay** directly in your terminal
- 🌐 **Online leaderboard support** via a robust backend validation server
- 🎨 **Clean aesthetics** featuring Unicode character graphics and a smooth linear-interpolation splash screen
- ⚡ **Progressive difficulty** as the game speeds up dynamically
- ⚙️ **Dynamic server rules configuration** without recompiling the backend
- 🛡️ **Advanced anti-cheat protection** with strict pseudo-random (PRNG) synchronization between client and server

## Requirements

### Client
- `make` and `gcc` (or any compatible C compiler)
- POSIX-compliant system with `pthread` support
- Terminal with Unicode support (GNOME Terminal recommended)

### Server (Online Mode)
- **Go Backend (Recommended):** Go 1.20+
- **Python Backend (Alternative):** Python 3.8+

## Usage

### Offline Mode
Play locally without leaderboard features. Compiling and running without arguments will open a default 25x20 board.

```bash
make
./minisnake
```

You can also specify custom dimensions (Constraints: Width 2-200, Height 2-50):
```bash
./minisnake 40 20
```

### Online Mode
To compete on the leaderboard, a backend validation server must be accessible. Play sessions are locked to the server's actively configured board dimensions to ensure fairness.

1. Ensure the backend server is running (see [Server Setup](#server-setup)).
2. Create a `net` file in the repository root (use `net.example` as a template):
   ```text
   HOST=YOUR_SERVER_IP
   PORT=YOUR_SERVER_PORT
   ```
3. Compile and play:
   ```bash
   make
   ./minisnake online
   ```
After the game, if your score is greater than zero, you will be prompted to enter your name for the global leaderboard!

## Controls & Gameplay

| Key | Action |
|-----|--------|
| `W` / `↑` | Move Up |
| `A` / `←` | Move Left |
| `S` / `↓` | Move Down |
| `D` / `→` | Move Right |
| `X` | Quit |

**Rules:**
1. Guide the snake to eat the fruit (`@`) to earn points.
2. Each fruit extends your snake and speeds up the game!
3. Avoid hitting the boundary walls or yourself to survive.
4. Win by filling the entire game board! (You win!)

---

## Server Setup

The online leaderboard functions via a secure Server-Side Scoring Architecture proxy. The C client never submits its score directly; instead, it sends an asynchronous HTTP ping per fruit. Both backend options (Go and Python) securely enforce several critical validations:

- **Time Corridor (Anti-Spam & Anti-Pause):** A dynamic time window is calculated based on game speed and path length. Taking too little time flags a speedhack, while taking too much time flags pausing (e.g., using a debugger).
- **Maximum Score Authorization:** The server strictly validates that no submitted score exceeds the theoretical maximum capacity (`Width * Height * PointsPerFruit`). 
- **Detailed Audit Logging:** The server maintains precise, modified final logs for every single game session, capturing the player's IP, token, final score, and exact number of fruits eaten, or explicitly logging why a score was ignored/rejected.
- **Active Memory Management:** The server automatically tracks and sweeps stale IPs and abandoned "ghost" sessions, gracefully logging memory reclamation without spamming stdout.
- **Pathing and PRNG Constraints:** The server verifies Manhattan distances and synchronizes the fruit generation sequence to block teleports or RNG manipulation.

### 1. Backend Service Options
You have two drop-in options for the backend proxy:

**Option A: Go Backend (Recommended)**
High-performance and statically linked.
1. Make sure Go is installed on your machine.
2. Setup `.env` inside `server/go/` if you want a custom port.
3. Build the static binary by running `make server` from the project root.
4. Run the generated output executable: `server/go/bin/server`.

**Option B: Python Backend (Alternative)**
A Gunicorn fallback option.
1. Navigate to the `server/python/` directory.
2. Initialize `.env` from `.env.example` adding your `PORT`.
3. Run the deployment script (`./deploy.sh`) to start the worker.

### 2. Dynamic Server Configuration
You can now dynamically adjust the game constraints (points, speed, grid boundaries) directly from the server without having to rebuild the proxy.

1. Rename `server/rules.json.example` to `rules.json` and place it in the same directory where your active backend server is executed.
2. Override any desired rules:
```json
{
  "GameWidth": 25,
  "GameHeight": 20,
  "InitialDelay": 250000.0,
  "SpeedupFactor": 0.985,
  "PointsPerFruit": 10,
  "PenaltyInterval": 10,
  "PenaltyAmount": 1,
  "CheatTimeout": 5000,
  "SpawnFruitMaxAttempts": 10000
}
```
*Note: Any omitted keys will seamlessly fall back to the native server defaults. The server securely computes score caps based on these parsed values dynamically to prevent abuse.*

---

## Technical Configuration (Client)

You can customize graphical settings and input buffers purely for the client by adapting constants directly inside `include/minisnake.h` before compiling (`make re`).

- **Controls & Strings:** `MOVE_KEYS`, `EXIT_KEY`, `TERM_TITLE`, `MSG_LOSS`, `MSG_WIN`
- **Aesthetics Elements:** `WALL_CHAR`, `SNAKE_COLOR`, `SNAKE_HEADS`, `FRUIT_CHAR`, `FRUIT_PALETTE`
*(In online mode, mechanics and speed constants defined in the header will be gracefully ignored in favor of the active server's configurations).*

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` / `make all` | Compile the main C client |
| `make clean` | Remove client object files |
| `make fclean` | Remove client object files and binaries |
| `make re` | Rebuild client from scratch |
| `make server` | Build the standalone high-performance Go backend |
| `make clean-server` | Clean Go backend build cache |
| `make fclean-server`| Remove Go backend cache and binary |
| `make re-server` | Rebuild Go backend from scratch |
| `make re-all` | Full wipe and rebuild of both the client and Go server |

## License

Free to use and modify.

---

**Enjoy the game!** 🎮🐍
