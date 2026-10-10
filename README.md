# 🧭 Maze Runner

A playable maze game rendered entirely in the terminal using pure C.

Watch a random maze carve itself, run through it from corner to corner, then see BFS replay the shortest possible path so you can compare your run against the perfect one. No game engines, no external libraries, just C and ANSI escape codes.

---

## 🎥 Watch the Build

See the full ASMR coding session where I build this from scratch:

[![Watch on YouTube](https://img.shields.io/badge/Watch%20on-YouTube-red?style=for-the-badge&logo=youtube)](https://youtu.be/EQMEER8NFCg)

---

## ✨ Features

- 🌀 Animated maze generation with a glowing head carving the walls
- 🕹️ Play with WASD or arrow keys
- 👣 Trail that follows your path through the maze
- ⏱️ Live move counter and timer
- 🟢 Green start tile and 🔴 red exit tile
- 🔵 BFS replay that spreads across the maze in blue
- 🟡 Gold shortest-path trace after you win
- 🏆 Win screen comparing your moves to the optimal route
- 🔁 Restart the same maze or generate a brand new one
- ⏸️ Pause and speed controls during generation and replay
- 🖥️ Flicker-free rendering using buffered output
- 🛡️ Bounds-checked rendering to prevent buffer overflows
- 🧹 Clean exit with terminal restoration via `atexit`

---

## 📸 What It Does

The game generates a 35 x 10 cell maze, then hands control to you. Start at the green tile in the top-left and reach the red tile in the bottom-right.

- Move with `WASD` or arrow keys
- Your trail stays visible behind you
- Reach the exit to win
- BFS then replays the shortest path in blue and gold
- The win screen shows your moves, the optimal move count, and your time

---

## 🧱 Requirements

- Linux or macOS terminal
- GCC
- Truecolor-capable terminal
- A terminal window at least 146 columns wide and 23 rows tall

Most modern terminals support truecolor, including GNOME Terminal, Kitty, Alacritty, WezTerm, and tmux with truecolor support.

---

## 🚀 Build

Compile:

```bash
gcc -std=c11 -Wall -Wextra maze.c -o maze
```

---

## 🎮 Run

```bash
./maze
```

---

## 🕹️ Controls

| Input           | Action                                       |
| --------------- | -------------------------------------------- |
| `W` `A` `S` `D` | Move up / left / down / right                |
| Arrow keys      | Move (alternate)                             |
| `r`             | Restart the same maze                        |
| `n`             | Generate a new maze                          |
| `space`         | Pause (generation and replay only)           |
| `+` / `-`       | Speed up / slow down (generation and replay) |
| `q` / `Esc`     | Quit                                         |

---

## 🧠 How It Works

### Grid Layout

The maze has 35 x 10 logical cells, but the character grid is `2x + 1` in each direction (71 x 21). Even indices are walls and odd indices are cells, so a wall between two cells has its own spot in the array:

```text
col:  0 1 2 3 4
      # # # # #     <- row 0 (all wall)
      # . # . #     <- row 1 (cells at col 1 and 3, wall at col 2)
      # # # # #     <- row 2 (all wall)
```

Cell `(cx, cy)` lives at `(cx*2+1, cy*2+1)`. The wall between it and a neighbor in direction `(dx, dy)` lives at `(cx*2+1+dx, cy*2+1+dy)`.

### Maze Generation

The maze is built with a recursive backtracker (DFS), written iteratively with an explicit stack so it can run one step per call and be animated:

1. Look at the cell on top of the stack
2. Shuffle the four directions
3. If an unvisited neighbor exists, carve the wall between them and push the neighbor
4. Otherwise, pop the stack (backtrack)

This repeats until the stack is empty, which produces a perfect maze: exactly one path between any two cells and no loops.

### Breadth-First Search

After you reach the exit, BFS starts from the top-left cell and explores outward in rings. Each visited cell remembers which cell discovered it in a parent array. Because BFS expands in rings, the first time it reaches the exit it has found the shortest path.

### Path Tracing

Starting at the exit, the game follows the parent array back to the start, lighting each cell gold. The number of steps along the way is the optimal move count shown on the win screen.

### State Machine

Every phase advances one small step per call, so a single main loop drives the whole game:

```text
GEN -> PLAY -> REVEAL_SOLVE -> REVEAL_PATH -> WIN
```

Generation and the replay run automatically at a speed you can change. `PLAY` and `WIN` only react to your input.

### Safe Buffered Rendering

All output is written to a static buffer using a bounds-checked `buf_print` helper backed by `vsnprintf`, then sent to the terminal in one write. This keeps the screen from flickering and prevents buffer overflows.

### Raw Terminal Mode

The game switches the terminal to raw, non-blocking input with `termios` so single key presses are read instantly. Arrow keys arrive as three-byte escape sequences (`ESC [ A/B/C/D`) and are decoded in the input handler. The original terminal settings are restored on exit.

### Monotonic Timing

The move timer uses `clock_gettime(CLOCK_MONOTONIC)`, so it measures real elapsed time and is not affected by system clock changes.

---

## 🎨 Colors

| Element         | Color                      |
| --------------- | -------------------------- |
| Generation head | Pink `rgb(255, 60, 140)`   |
| Start tile      | Green `rgb(55, 150, 95)`   |
| Exit tile       | Red `rgb(190, 55, 95)`     |
| Player          | White `rgb(255, 255, 255)` |
| Player trail    | Teal `rgb(40, 100, 105)`   |
| BFS visited     | Blue `rgb(35, 80, 130)`    |
| Shortest path   | Gold `rgb(255, 200, 60)`   |
| Walls           | Slate `rgb(42, 42, 58)`    |
| Floor           | Dark `rgb(28, 28, 38)`     |

---

## 📁 Project Structure

```text
maze/
├── maze.c
└── README.md
```

---

## 🛠️ Possible Future Improvements

- Race mode: BFS runs live against you
- Hint key that reveals the next step on the shortest path
- Adjustable maze sizes and difficulty levels
- Other generation algorithms (Prim's, Kruskal's, Wilson's)
- Other solvers (DFS, A\*, wall follower) with side-by-side comparison
- Best time and fewest moves saved between runs
- Fog of war that only shows cells near you
- Multiple mazes in a row with a running score
