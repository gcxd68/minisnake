# minisnake 🐍

A lightweight, terminal-based Snake game written in C with minimal dependencies.

## Features

- 🎮 Classic Snake gameplay in your terminal
- 🌐 Online leaderboard (pre-built release only)
- 🎨 Unicode character graphics
- ⚡ Progressive difficulty (speeds up as you grow)
- 📏 Customizable game board dimensions
- 🎯 Score tracking

## Requirements

- Make
- GCC or any C compiler
- POSIX-compliant system (Linux)
- Terminal with Unicode support (gnome-terminal recommended)

## Usage

### Online mode (pre-built release only)

Download the binary from the [latest release](../../releases/latest). Before running it, ensure it has execution permissions:

```bash
chmod +x minisnake
./minisnake online
```

Launches a dedicated gnome-terminal window with a fixed 25x20 board. After the game, enter your login to submit your score to the global leaderboard.

> **Note:** Online mode is only available in pre-built releases. If you compile from source without `keys.h`, online mode will not be available.

### Custom mode

```bash
./minisnake <width> <height>
```

**Example:**
```bash
./minisnake 40 20
```

Custom mode is offline only. The game window opens automatically in gnome-terminal.

### Constraints

- **Width:** Between 2 and 200
- **Height:** Between 2 and 50

## Controls

| Key | Action |
|-----|--------|
| `W` | Move Up |
| `A` | Move Left |
| `S` | Move Down |
| `D` | Move Right |
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
./minisnake <width> <height>
```

### Building with online mode

1. Get your Dreamlo public and private keys from [dreamlo.com](http://dreamlo.com)
2. Create a `keys` file in the root directory (you can use `keys.example` as a template) and add your credentials:
   ```text
   PUBLIC_KEY=YOUR_PUBLIC_KEY_HERE
   PRIVATE_KEY=YOUR_PRIVATE_KEY_HERE
3. (Optional) Choose an XOR key for obfuscation and set it in obfuscator.py (default is 0x42).
4. Generate `keys.h`:
   ```bash
   python3 obfuscator.py > keys.h
   ```
5. Compile:
   ```bash
   make
   ./minisnake online
   ```

> Keys are XOR-obfuscated at compile time so they don't appear in plain text in the binary.

## Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Compile the project |
| `make clean` | Remove object files |
| `make fclean` | Remove object files and executable |
| `make re` | Rebuild everything from scratch |

## Configuration

You can modify these constants in `minisnake.h`:

**Gameplay & Board:**
- `INITIAL_DELAY` — Starting game speed in microseconds (default: 250000)
- `SPEEDUP_FACTOR` — Speed multiplier per fruit (default: 0.985f)
- `INPUT_Q_SIZE` — Number of inputs buffered (default: 2)
- `MIN_WIDTH` / `MAX_WIDTH` — Allowed board width range (default: 2 to 200)
- `MIN_HEIGHT` / `MAX_HEIGHT` — Allowed board height range (default: 2 to 50)
- `ONLINE_WIDTH` / `ONLINE_HEIGHT` — Online mode board dimensions (default: 25x20)

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
