# minisnake 🐍

A high-performance, terminal-based Snake game written in C, featuring a robust Go backend for authoritative online scoring and anti-cheat protection.

## 🚀 Quick Start

### 🎮 Play Now (Recommended)
If you want to compete on the global leaderboard immediately:
1. Go to the [Releases](https://github.com/gcxd68/minisnake/releases) page.
2. Download the pre-compiled binary for your system.
3. Authorize it: `chmod +x minisnake`.
4. Run it: `./minisnake` (it is pre-configured for Online Mode).

### 🛠️ Build from Source (Developers)
If you want to customize the game or run your own server:
1. Clone the repository.
2. Build the Client: `make`.
3. Build the Server (Optional): `make server`.
4. Launch: `./bin/minisnake` (Defaults to Offline Mode unless a `net` config is provided).

---

## Features

- 🕹️ **Classic Gameplay:** Smooth terminal experience with sub-millisecond input response.
- 🌐 **Authoritative Online Mode:** Global leaderboards powered by a Go backend that validates every move.
- 🛡️ **Advanced Anti-Cheat:** - **Server-Side Physics:** Every move is replayed on the server using O(1) Spatial Hashing.
    - **Latency-Aware Spawning:** A probability cloud algorithm ensures fruits never spawn where you are locally, compensating for network lag.
    - **Behavioral Telemetry:** Heuristics auto-calibrate dynamically from early player data to analyze movement variance and shadowban bots with high accuracy.
- 🎨 **Modern TUI:** Features VT100 scrolling regions, Unicode graphics, and thread-safe UI updates.
- ⚙️ **Dynamic Rules:** Game mechanics (speed, grid size) are synced from the server on startup.

---

## Usage Modes

### 1. Offline Mode
Play locally with custom dimensions. No network required.
```bash
# Default 25x20 board
./bin/minisnake
```

You can also specify custom dimensions (Constraints: Width 2-200, Height 2-50):
```bash
# Custom dimensions (Width: 2-200, Height: 2-50)
./minisnake 40 20
```

### 2. Online Mode (Manual Compilation)
To connect your manual build to a server:

1. Ensure the backend server is running (see [Server Setup](#server-setup)).
2. Create a `net` file in the root directory (use `net.example` as a template):
   ```text
   HOST=YOUR_SERVER_IP
   PORT=YOUR_SERVER_PORT
   ```
3. Recompile: `make re`.
4. The binary will now include the `ONLINE_BUILD` flag and attempt to sync with the backend.

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

The online leaderboard functions via a secure Server-Side Scoring Architecture proxy. The Go backend enforces critical validations before accepting scores:

- **Authoritative Physics & Anti-Cheat:** Evaluates game state strictly. Integrates O(1) Spatial Hashing to prevent wall-phasing and self-collision cheats. Behavioral telemetry algorithms flag impossible speedhacks and bots.
- **Latency & Time Algorithms:** Uses a dynamic sliding window to absorb network jitter while ensuring total game duration stays within theoretical minimum limits.
- **Fair Fruit Generation:** Calculates a probability cloud based on player latency to prevent fruits from spawning near the snake's unacknowledged path. The generation uses opaque server-side bounds, fundamentally blocking PRNG reversal.
- **Security & DoS Protection:** Implements strict limits on maximum concurrent sessions (5000), payload limits (`MaxBytesReader`) to mitigate memory exhaust attacks, and rate limits (20 req/s per IP). Actively sweeps ghost sessions and blocks outdated client versions.

### Running the Go Backend

The high-performance backend is compiled as a single static binary.

1. Ensure Go 1.22+ is installed.
2. Build the server: `make server`.
3. Start the server: `./bin/server`.

### Dynamic Server Configuration

You can dynamically adjust game constraints without rebuilding the backend.

1. Rename `server/rules.json.example` to `rules.json` and place it alongside the executable.
2. Override any desired rules. Omitted keys cleanly fall back to server defaults.

```json
{
  "GameWidth": 25,
  "GameHeight": 20,
  "InitialDelay": 250000.0,
  "SpeedupFactor": 0.985,
  "PointsPerFruit": 10
}
```

---

## Technical Configuration (Client)

You can customize graphical settings and input buffers purely for the client by adapting constants directly inside `include/minisnake.h` before compiling (`make re`).

- **Controls & Strings:** `MOVE_KEYS`, `EXIT_KEY`, `TERM_TITLE`, `MSG_LOSS`, `MSG_WIN`
- **Aesthetics Elements:** `WALL_CHAR`, `SNAKE_COLOR`, `SNAKE_HEADS`, `FRUIT_CHAR`, `FRUIT_PALETTE`
- **Memory & Grid Limits:** Modifying `MAX_WIDTH` and `MAX_HEIGHT` is supported up to a combined limit of `MAX_SIZE = 50,000` tiles to prevent C stack overflows.

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
