#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

/*      Configuration      */

/* Logical mazd dimensions (cells) */
#define LW 35
#define LH 10

/* Character grid dimensions (2x + 1 for walls between cells) */
#define GW (LW * 2 + 1)
#define GH (LH * 2 + 1)

#define MAX_CELLS (LW * LH)

typedef enum {
    STATE_GEN,              /* maze is being carved (auto) */
    STATE_PLAY,             /* you are navigating */
    STATE_REVEAL_SOLVE,     /* auto BFS replay (post-win) */
    STATE_REVEAL_PATH,      /* auto gold path trace */
    STATE_WIN               /* stats screen */
} AppState;

typedef struct {
    int x, y;
} Point;

/*      Global State      */

static struct termios g_orig_termios;
static volatile sig_atomic_t g_running = 1;

/* '#' = wall, ' ' = carved/open */
static char g_grid[GH][GW];

/* Generation state */
static bool  g_visited[LH][LW];
static Point g_stack[MAX_CELLS];
static int   g_sp = 0;

/* Solving state (used for the post-win reveal) */
static bool  g_solve_visited[LH][LW];
static Point g_parent[LH][LW];
static Point g_queue[MAX_CELLS];
static int   g_qh = 0, g_qt = 0;

/* Path tracing state */
static bool  g_is_path[LH][LW];
static Point g_path_cur;

/* Player state */
static Point  g_player;
static bool   g_player_visited[LH][LW];
static int    g_moves = 0;
static double g_start_time_val = 0.0;
static double g_final_time = 0.0;
static int    g_optimal_moves = 0;

static AppState g_state = STATE_GEN;
static bool     g_paused = false;
static int      g_speed = 2;      /* auto-steps per frame (gen/reveal only) */
static int      g_steps = 0;      /* total algorithm steps taken */

/*      Terminal Helpers      */

static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    printf("\033[?25h\033[0m\033[2J\033[H");
    fflush(stdout);
}

static void term_init(void) {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(term_restore);

    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?25l\033[2J");
    fflush(stdout);
}

static void on_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/*      Maze Logic      */

static void shuffle_dirs(int dirs[4]) {
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = dirs[i];
        dirs[i] = dirs[j];
        dirs[j] = tmp;
    }
}

static void maze_reset(void) {
    memset(g_grid, '#', sizeof(g_grid));
    memset(g_visited, 0, sizeof(g_visited));
    memset(g_solve_visited, 0, sizeof(g_solve_visited));
    memset(g_is_path, 0, sizeof(g_is_path));

    g_sp = 0;
    g_qh = g_qt = 0;
    g_steps = 0;
    g_state = STATE_GEN;

    /* Start generation at top-left */
    g_visited[0][0] = true;
    g_grid[1][1] = ' ';
    g_stack[g_sp++] = (Point){0, 0};
}

/* Returns true if generation is still running */
static bool gen_step(void) {
    if (g_sp == 0) return false;

    Point cur = g_stack[g_sp - 1];
    int dirs[4] = {0, 1, 2, 3}; /* N, S, E, W */
    shuffle_dirs(dirs);

    for (int i = 0; i < 4; i++) {
        int nx = cur.x, ny = cur.y;
        int dx = 0, dy = 0;

        if (dirs[i] == 0) dy = -1;       /* North */
        else if (dirs[i] == 1) dy = 1;   /* South */
        else if (dirs[i] == 2) dx = 1;   /* East */
        else if (dirs[i] == 3) dx = -1;  /* West */

        nx += dx;
        ny += dy;

        /* Bounds check */
        if (nx < 0 || nx >= LW || ny < 0 || ny >= LH) continue;
        if (g_visited[ny][nx]) continue;

        /* Carve wall between current and neighbor */
        int wx = cur.x * 2 + 1 + dx;
        int wy = cur.y * 2 + 1 + dy;
        g_grid[wy][wx] = ' ';

        /* Move to neighbor */
        g_visited[ny][nx] = true;
        g_grid[ny * 2 + 1][nx * 2 + 1] = ' ';
        g_stack[g_sp++] = (Point){nx, ny};

        g_steps++;
        return true;
    }

    /* Dead end: backtrack */
    g_sp--;
    g_steps++;
    return true;
}

static void solve_init(void) {
    memset(g_solve_visited, 0, sizeof(g_solve_visited));
    memset(g_parent, -1, sizeof(g_parent));
    memset(g_is_path, 0, sizeof(g_is_path));
    g_qh = g_qt = 0;

    g_solve_visited[0][0] = true;
    g_queue[g_qt++] = (Point){0, 0};
}

/* Returns true if BFS is still exploring */
static bool solve_step(void) {
    if (g_qh >= g_qt) return false;

    Point cur = g_queue[g_qh++];

    /* Reached the exit */
    if (cur.x == LW - 1 && cur.y == LH - 1) {
        g_path_cur = cur;
        return false;
    }

    int dx[4] = {0, 0, 1, -1};
    int dy[4] = {-1, 1, 0, 0};

    for (int i = 0; i < 4; i++) {
        int nx = cur.x + dx[i];
        int ny = cur.y + dy[i];

        if (nx < 0 || nx >= LW || ny < 0 || ny >= LH) continue;
        if (g_solve_visited[ny][nx]) continue;

        /* Check if wall between cur and neighbor is open */
        int wx = cur.x * 2 + 1 + dx[i];
        int wy = cur.y * 2 + 1 + dy[i];
        if (g_grid[wy][wx] == '#') continue;

        g_solve_visited[ny][nx] = true;
        g_parent[ny][nx] = cur;
        g_queue[g_qt++] = (Point){nx, ny};
    }

    g_steps++;
    return true;
}

/* Returns true if path tracing is still running */
static bool path_step(void) {
    g_is_path[g_path_cur.y][g_path_cur.x] = true;

    if (g_path_cur.x == 0 && g_path_cur.y == 0) return false;

    g_path_cur = g_parent[g_path_cur.y][g_path_cur.x];
    g_steps++;
    return true;
}

/*      Player Logic      */

/* Is the wall between the player's current cell and (cur + dx,dy) open? */
static bool wall_open(Point cur, int dx, int dy) {
    int nx = cur.x + dx, ny = cur.y + dy;
    if (nx < 0 || nx >= LW || ny < 0 || ny >= LH) return false;

    int wx = cur.x * 2 + 1 + dx;
    int wy = cur.y * 2 + 1 + dy;
    return g_grid[wy][wx] != '#';
}

/* Reset the player to the start of the (already generated) maze */
static void player_reset(void) {
    memset(g_player_visited, 0, sizeof(g_player_visited));
    g_player = (Point){0, 0};
    g_player_visited[0][0] = true;
    g_moves = 0;
    g_start_time_val = now_seconds();
    g_state = STATE_PLAY;
}

static void try_move(int dx, int dy) {
    if (g_state != STATE_PLAY) return;
    if (!wall_open(g_player, dx, dy)) return;

    g_player.x += dx;
    g_player.y += dy;
    g_moves++;
    g_player_visited[g_player.y][g_player.x] = true;

    if (g_player.x == LW - 1 && g_player.y == LH - 1) {
        g_final_time = now_seconds() - g_start_time_val;
        solve_init();
        g_state = STATE_REVEAL_SOLVE;
    }
}

/*      Rendering      */

#define BUF_SIZE (GW * GH * 24 + 4096)

static void buf_print(char *buf, size_t cap, int *p, const char *fmt, ...) {
    if (*p < 0 || (size_t)*p >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *p, cap - (size_t)*p, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *p += n;
        if ((size_t)*p > cap - 1) *p = (int)cap - 1;
    }
}

static void render(void) {
    static char buf[BUF_SIZE];
    int p = 0;

    buf_print(buf, sizeof buf, &p, "\033[H");

    /* Header: title | state | contextual detail */
    const char *state_str = "GENERATING";
    char detail[128];

    switch (g_state) {
        case STATE_GEN:
            state_str = "GENERATING";
            snprintf(detail, sizeof detail, "Steps: %d", g_steps);
            break;
        case STATE_PLAY: {
            state_str = "FIND THE EXIT";
            double elapsed = now_seconds() - g_start_time_val;
            snprintf(detail, sizeof detail, "Moves: %d   Time: %.1fs", g_moves, elapsed);
            break;
        }
        case STATE_REVEAL_SOLVE:
            state_str = "REPLAYING BFS SOLUTION";
            snprintf(detail, sizeof detail, " ");
            break;
        case STATE_REVEAL_PATH:
            state_str = "TRACING SHORTEST PATH";
            snprintf(detail, sizeof detail, " ");
            break;
        case STATE_WIN: {
            state_str = "SOLVED!";
            int extra = g_moves - g_optimal_moves;
            snprintf(detail, sizeof detail,
                     "Moves: %d (optimal %d, +%d)   Time: %.1fs",
                     g_moves, g_optimal_moves, extra, g_final_time);
            break;
        }
    }

    buf_print(buf, sizeof buf, &p,
              "\033[1;38;2;255;200;80m  M A Z E   R U N N E R\033[0m"
              "  \033[38;2;140;140;160m│\033[0m  "
              "\033[38;2;180;180;200m%s\033[0m"
              "  \033[38;2;140;140;160m│\033[0m  "
              "\033[38;2;180;180;200m%s\033[0m\n",
              state_str, detail);

    /* Maze Grid */
    for (int gy = 0; gy < GH; gy++) {
        buf_print(buf, sizeof buf, &p, "  ");
        for (int gx = 0; gx < GW; gx++) {

            /* Determine background color */
            int r = 18, g = 18, b = 24; /* Default uncarved dark */

            if (g_grid[gy][gx] == '#') {
                r = 42; g = 42; b = 58; /* Wall */
            } else {
                r = 28; g = 28; b = 38; /* Carved floor */

                /* If this is a logical cell, apply state colors */
                if (gx % 2 == 1 && gy % 2 == 1) {
                    int cx = gx / 2;
                    int cy = gy / 2;
                    bool is_start = (cx == 0 && cy == 0);
                    bool is_exit  = (cx == LW - 1 && cy == LH - 1);

                    /* Start/exit markers, always visible once carved */
                    if (is_start) { r = 55; g = 150; b = 95; }
                    if (is_exit)  { r = 190; g = 55; b = 95; }

                    if (g_state == STATE_GEN && g_sp > 0 &&
                        g_stack[g_sp - 1].x == cx && g_stack[g_sp - 1].y == cy) {
                        r = 255; g = 60; b = 140; /* Generation head pink */
                    }

                    if (g_state == STATE_PLAY) {
                        if (g_player_visited[cy][cx]) { r = 40; g = 100; b = 105; } /* trail */
                        if (g_player.x == cx && g_player.y == cy) {
                            r = 255; g = 255; b = 255; /* current position */
                        }
                    }

                    if (g_state == STATE_REVEAL_SOLVE || g_state == STATE_REVEAL_PATH ||
                        g_state == STATE_WIN) {
                        if (g_state == STATE_REVEAL_SOLVE && g_solve_visited[cy][cx]) {
                            r = 35; g = 80; b = 130; /* BFS visited blue */
                        }
                        if (g_is_path[cy][cx]) {
                            r = 255; g = 200; b = 60; /* Gold path */
                        }
                    }
                }
            }

            buf_print(buf, sizeof buf, &p, "\033[48;2;%d;%d;%dm  ", r, g, b);
        }
        buf_print(buf, sizeof buf, &p, "\033[0m\n");
    }

    /* Footer: controls depend on state */
    if (g_state == STATE_PLAY) {
        buf_print(buf, sizeof buf, &p,
            "  \033[38;2;110;112;125m[wasd/arrows]\033[38;2;80;82;95m move  "
            "\033[38;2;110;112;125m[r]\033[38;2;80;82;95m restart  "
            "\033[38;2;110;112;125m[n]\033[38;2;80;82;95m new maze  "
            "\033[38;2;110;112;125m[q]\033[38;2;80;82;95m quit\033[0m");
    } else if (g_state == STATE_WIN) {
        buf_print(buf, sizeof buf, &p,
            "  \033[38;2;110;112;125m[r]\033[38;2;80;82;95m play again  "
            "\033[38;2;110;112;125m[n]\033[38;2;80;82;95m new maze  "
            "\033[38;2;110;112;125m[q]\033[38;2;80;82;95m quit\033[0m");
    } else {
        buf_print(buf, sizeof buf, &p,
            "  \033[38;2;110;112;125m[space]\033[38;2;80;82;95m pause  "
            "\033[38;2;110;112;125m[n]\033[38;2;80;82;95m new  "
            "\033[38;2;110;112;125m[+/-]\033[38;2;80;82;95m speed  "
            "\033[38;2;110;112;125m[q]\033[38;2;80;82;95m quit\033[0m");
    }

    fputs(buf, stdout);
    fflush(stdout);
}

/*      Input & Main      */

static void handle_input(void) {
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {

        /* Arrow keys arrive as ESC [ A/B/C/D */
        if (c == 27) {
            char seq0, seq1;
            if (read(STDIN_FILENO, &seq0, 1) == 1 && read(STDIN_FILENO, &seq1, 1) == 1) {
                if (seq0 == '[') {
                    if (seq1 == 'A') try_move(0, -1);      /* up    */
                    else if (seq1 == 'B') try_move(0, 1);  /* down  */
                    else if (seq1 == 'C') try_move(1, 0);  /* right */
                    else if (seq1 == 'D') try_move(-1, 0); /* left  */
                }
            } else {
                g_running = 0; /* bare ESC quits */
            }
            continue;
        }

        switch (c) {
            case 'q': case 'Q':
                g_running = 0;
                break;
            case 'n': case 'N':
                maze_reset();
                break;
            case 'r': case 'R':
                if (g_state != STATE_GEN) player_reset();
                break;
            case ' ':
                if (g_state == STATE_GEN || g_state == STATE_REVEAL_SOLVE ||
                    g_state == STATE_REVEAL_PATH) {
                    g_paused = !g_paused;
                }
                break;
            case '+': case '=':
                if (g_speed < 20) g_speed++;
                break;
            case '-':
                if (g_speed > 1) g_speed--;
                break;
            case 'w': case 'W': try_move(0, -1); break;
            case 's': case 'S': try_move(0, 1); break;
            case 'a': case 'A': try_move(-1, 0); break;
            case 'd': case 'D': try_move(1, 0); break;
            default: break;
        }
    }
}

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    srand((unsigned int)time(NULL));
    term_init();
    maze_reset();

    while (g_running) {
        handle_input();

        if (!g_paused) {
            for (int i = 0; i < g_speed; i++) {
                if (g_state == STATE_GEN) {
                    if (!gen_step()) {
                        player_reset(); /* generation done -> you take over */
                    }
                } else if (g_state == STATE_REVEAL_SOLVE) {
                    if (!solve_step()) {
                        /* BFS reached the exit: measure the original length
                         * before the gold trace animation consumes g_path_cur */
                        Point tmp = g_path_cur;
                        int len = 0;
                        while (!(tmp.x == 0 && tmp.y == 0)) {
                            tmp = g_parent[tmp.y][tmp.x];
                            len++;
                        }
                        g_optimal_moves = len;
                        g_state = STATE_REVEAL_PATH;
                    }
                } else if (g_state == STATE_REVEAL_PATH) {
                    if (!path_step()) {
                        g_state = STATE_WIN;
                    }
                }
                /* STATE_PLAY and STATE_WIN advance only from input */
            }
        }

        render();
        usleep(30000); /* ~33 fps */
    }

    return 0;
}
