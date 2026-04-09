# minisnake 🐍

A lightweight, terminal-based Snake game written in C with minimal dependencies.

## Features

- 🎮 Classic Snake gameplay in your terminal
- 🌐 Online leaderboard (pre-built release only)
- 🎨 Unicode character graphics with a smooth linear-interpolation splash screen
- ⚡ Progressive difficulty (speeds up as you grow)
- 📏 Customizable game board dimensions
- 🛡️ Advanced server-side anti-cheat with strict PRNG synchronization
- 🎯 Score tracking
- 🛠️ Dedicated POSIX system interactions abstracting raw terminal limits (`minisnake_sys.c`)

## Requirements

- Make
- GCC or any C compiler
- POSIX-compliant system with POSIX threads support (`pthread`)
- Terminal with Unicode support (gnome-terminal recommended)

## Usage

### Online mode (pre-built release only)

Download the binary from the [latest release](../../releases/latest). Before running it, ensure it has execution permissions:

```bash
chmod +x minisnake
./minisnake online
```

Launches a dedicated gnome-terminal window with a fixed 25x20 board. After the game, if your score is greater than zero, enter your login to submit your score to the global leaderboard. Otherwise, you will simply be prompted to press Enter to continue.

> **Note:** Online mode is only available in pre-built releases. If you compile from source without `net.h`, online mode will not be available.

### Custom mode

```bash
./minisnake
./minisnake <width> <height>
```

**Example:**
```bash
./minisnake 40 20
```

Custom mode is offline only. Playing without arguments defaults to 25x20. The game window opens automatically in gnome-terminal.

### Constraints

- **Width:** Between 2 and 200
- **Height:** Between 2 and 50

## Controls

| Key | Action |
|-----|--------|
| `W` / `↑` | Move Up |
| `A` / `←` | Move Left |
| `S` / `↓` | Move Down |
| `D` / `→` | Move Right |
| `X` | Quit |

## Gameplay

1. Guide the snake to eat the fruit (`@`)
2. Each fruit increases your score by 10 points
3. The snake grows longer with each fruit eaten
4. The game speeds up progressively
5. Avoid hitting walls or yourself
6. Win by filling the entire board!

## Game Over Conditions

- 💥 Hitting the boundary walls
- 💥 Running into yourself
- 🏆 Filling the entire game board (You win!)

## Compilation (offline mode)

```bash
make
./minisnake
```

### Building with online mode

Direct connection to Dreamlo from the client is no longer possible for security reasons. Minisnake now features an advanced **Server-Side Scoring Architecture**. The C client never sends its score directly; instead, it communicates with a proxy server via asynchronous HTTP pings every time a fruit is eaten. The server calculates the score securely while enforcing physical limits and timing constraints to prevent hacking.

The anti-cheat backend incorporates several robust validation layers:
1. **Synchronized PRNG**: Server and client utilize an identical Linear Congruential Generator (LCG) dropping low-order bits (`>> 16`) to predict exactly where fruits must spawn safely.
2. **Speedhack & Teleport Filters**: Enforces minimum pathing (Manhattan distance) and minimum time delays factoring in the game's actual exponential speed-up.
3. **Dynamic Degradation**: Penalty calculations are securely mirrored to slowly decay the score if the snake deliberately avoids eating fruits over time.
4. **Anti-Spam**: Strict rate limiting (20 req/sec) to mitigate DDoS/Siege attacks.
5. **Anti-OOM Protection**: Global limit of concurrent active sessions (5000) with safe background memory cleanup to prevent resource exhaustion and IP spoofing leaks.
6. **Thread-Safe Architecture**: Full mutex/lock protection on state maps in both Go and Python proxy variants.
7. **Structured Logging**: Fully timestamped, level-based logs track all warnings natively.

#### 1. Server Setup (VPS)
We provide two drop-in options for the backend validation proxy: **Go** (High performance, statically linked) and **Python** (Gunicorn, legacy fallback). Both implement identical scoring constraints.

**Option A: Go Backend (Recommended)**
1. Ensure you have Go installed on your server (or simply build the binary locally matching your VPS architecture).
2. Configure `.env` in `server/go/`, matching the example file (if needed) for port configuration.
3. Build the Go server using `make server` from the root directory, which will output the static binary to `server/go/bin/server`.
4. Run the executable in a background service (e.g., using a systemd worker).

**Option B: Python Backend (Legacy/Alternative)**
1. Go to the `server/python/` directory and create an `.env` file based on `.env.example`. Provide the desired port:
   ```text
   PORT=YOUR_SERVER_PORT
   ```
2. Run the automated deployment script (`./deploy.sh`) to set up a virtual environment and start the Gunicorn worker.

#### 2. Client Setup (Game)
1. Create a `net` file in the root directory (you can use `net.example` as a template) and add the **VPS hostname/IP** and **port** you defined earlier:
   ```text
   HOST=YOUR_SERVER_IP
   PORT=YOUR_SERVER_PORT
   ```
2. Compile:
   ```bash
   make
   ./minisnake online
   ```

> **Architecture Note:** The client utilizes POSIX threads (`pthread`) to implement a "fire-and-forget" static resource pool for non-blocking asynchronous HTTP requests. It securely links against the server-side validator without stalling the game loop.

## Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Compile the main client project C |
| `make clean` | Remove client object files |
| `make fclean` | Remove client object files and binaries |
| `make re` | Rebuild client C from scratch |
| `make server` | Build the standalone high-performance Go backend |
| `make clean-server` | Clean Go backend cache |
| `make fclean-server`| Remove Go backend cache and binary |
| `make re-server` | Rebuild Go backend from scratch |
| `make re-all` | Full wipe and rebuild of both client and server |

## Configuration

You can modify these constants in `minisnake.h`:

**Gameplay & Board:**
- `DEF_INITIAL_DELAY` — Starting game speed in microseconds (default: 250000)
- `DEF_SPEEDUP_FACTOR` — Speed multiplier per fruit (default: 0.985f)
- `DEF_SPAWN_FRUIT_MAX_ATTEMPTS` — Loop safety limit preventing hangs on dense grids (default: 10000)
- `INPUT_Q_SIZE` — Number of inputs buffered (default: 2)
- `MIN_WIDTH` / `MAX_WIDTH` — Allowed board width range (default: 2 to 200)
- `MIN_HEIGHT` / `MAX_HEIGHT` — Allowed board height range (default: 2 to 50)
- `DEF_WIDTH` / `DEF_HEIGHT` — Default/Online mode board dimensions (default: 25x20)
- `SPLASH_FRAMES` / `SPLASH_USLEEP` — Startup animation interpolations.

**Controls & Text:**
- `MOVE_KEYS` — Movement controls (default: "ADWS")
- `EXIT_KEY` — Key to exit the game (default: "X")
- `TERM_TITLE` — Terminal window title (default: "minisnake")
- `MSG_LOSS` / `MSG_WIN` — End-game messages

**Visuals & Aesthetics:**
- `WALL_CHAR` / `WALL_COLOR` — Wall appearance
- `SNAKE_COLOR` / `SNAKE_BODY` / `SNAKE_HEADS` / `SNAKE_BENDS` — Snake appearance
- `FRUIT_CHAR` / `FRUIT_PALETTE` — Fruit character and color variations

## License

Free to use and modify.

---

**Enjoy the game!** 🎮🐍
