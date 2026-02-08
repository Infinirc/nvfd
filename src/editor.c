#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ncurses.h>

#include "editor.h"
#include "curve.h"
#include "nvfd.h"

/* Graph layout constants */
#define GRAPH_ROWS    15
#define GRAPH_COLS    50
#define GRAPH_TOP      3
#define GRAPH_LEFT     8
#define AXIS_LABEL_W   6

/* Temperature and speed bounds */
#define TEMP_MIN   0
#define TEMP_MAX 100
#define SPEED_MIN 30
#define SPEED_MAX 100

/* Color pairs */
#define CP_NORMAL    1
#define CP_SELECTED  2
#define CP_AXIS      3
#define CP_LINE      4
#define CP_TITLE     5
#define CP_STATUS    6
#define CP_PROMPT    7

typedef struct {
    FanCurve   curve;
    FanCurve   original;   /* for reset */
    int        selected;   /* index of selected point */
    int        dirty;      /* unsaved changes */
    int        running;
} EditorState;

/* Map temperature (0-100) to screen column */
static int temp_to_col(int temp) {
    return GRAPH_LEFT + (temp * GRAPH_COLS) / TEMP_MAX;
}

/* Map speed (SPEED_MIN-SPEED_MAX) to screen row */
static int speed_to_row(int speed) {
    int range = SPEED_MAX - SPEED_MIN;
    int clamped = speed < SPEED_MIN ? SPEED_MIN : (speed > SPEED_MAX ? SPEED_MAX : speed);
    return GRAPH_TOP + GRAPH_ROWS - 1 - ((clamped - SPEED_MIN) * (GRAPH_ROWS - 1)) / range;
}

/* Map screen column back to temperature */
static int col_to_temp(int col) {
    int t = ((col - GRAPH_LEFT) * TEMP_MAX) / GRAPH_COLS;
    return t < TEMP_MIN ? TEMP_MIN : (t > TEMP_MAX ? TEMP_MAX : t);
}

static void draw_title(void) {
    attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvprintw(0, 2, "NVFD Fan Curve Editor");
    attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);
    attron(COLOR_PAIR(CP_STATUS));
    mvprintw(0, GRAPH_LEFT + GRAPH_COLS - 22, "[s]Save [q]Quit [r]Reset");
    attroff(COLOR_PAIR(CP_STATUS));
}

static void draw_axes(void) {
    attron(COLOR_PAIR(CP_AXIS));

    /* Y-axis labels and ticks */
    for (int i = 0; i < GRAPH_ROWS; i++) {
        int speed = SPEED_MAX - (i * (SPEED_MAX - SPEED_MIN)) / (GRAPH_ROWS - 1);
        int row = GRAPH_TOP + i;
        if (i % 3 == 0 || i == GRAPH_ROWS - 1) {
            mvprintw(row, 1, "%3d%%", speed);
            mvaddch(row, GRAPH_LEFT - 2, '|');
        } else {
            mvprintw(row, 1, "    ");
            mvaddch(row, GRAPH_LEFT - 2, '|');
        }
    }

    /* X-axis line */
    int axis_row = GRAPH_TOP + GRAPH_ROWS;
    mvaddch(axis_row, GRAPH_LEFT - 2, '+');
    for (int c = GRAPH_LEFT - 1; c <= GRAPH_LEFT + GRAPH_COLS; c++)
        mvaddch(axis_row, c, '-');

    /* X-axis labels */
    for (int t = 0; t <= 100; t += 10) {
        int col = temp_to_col(t);
        mvaddch(axis_row, col, '+');
        mvprintw(axis_row + 1, col - 1, "%d", t);
    }
    mvprintw(axis_row + 1, GRAPH_LEFT + GRAPH_COLS + 2, "°C");

    attroff(COLOR_PAIR(CP_AXIS));
}

static void draw_interpolated_line(const FanCurve *curve) {
    if (curve->point_count < 2)
        return;

    attron(COLOR_PAIR(CP_LINE));

    for (int col = temp_to_col(curve->points[0].temperature);
         col <= temp_to_col(curve->points[curve->point_count - 1].temperature);
         col++) {
        int temp = col_to_temp(col);
        int speed = curve_interpolate(temp, curve);
        int row = speed_to_row(speed);

        /* Don't draw over point markers */
        int is_point = 0;
        for (int i = 0; i < curve->point_count; i++) {
            int pc = temp_to_col(curve->points[i].temperature);
            if (col >= pc - 1 && col <= pc + 1) {
                int pr = speed_to_row(curve->points[i].fan_speed);
                if (row == pr) {
                    is_point = 1;
                    break;
                }
            }
        }
        if (!is_point)
            mvaddch(row, col, '.');
    }

    attroff(COLOR_PAIR(CP_LINE));
}

static void draw_points(const EditorState *st) {
    for (int i = 0; i < st->curve.point_count; i++) {
        int col = temp_to_col(st->curve.points[i].temperature);
        int row = speed_to_row(st->curve.points[i].fan_speed);

        if (i == st->selected) {
            attron(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            mvprintw(row, col - 1, "[*]");
            attroff(COLOR_PAIR(CP_SELECTED) | A_BOLD);
        } else {
            attron(COLOR_PAIR(CP_NORMAL) | A_BOLD);
            mvaddch(row, col, '*');
            attroff(COLOR_PAIR(CP_NORMAL) | A_BOLD);
        }
    }
}

static void draw_status(const EditorState *st) {
    int row = GRAPH_TOP + GRAPH_ROWS + 3;

    /* Clear status lines */
    move(row, 0);
    clrtoeol();
    move(row + 1, 0);
    clrtoeol();

    attron(COLOR_PAIR(CP_STATUS));

    if (st->curve.point_count > 0) {
        int idx = st->selected;
        mvprintw(row, 2,
                 "Point %d/%d: %d°C -> %d%%",
                 idx + 1, st->curve.point_count,
                 st->curve.points[idx].temperature,
                 st->curve.points[idx].fan_speed);
    }

    mvprintw(row + 1, 2,
             "[%s%s] Temp  [%s%s] Speed  [t]Set Temp [f]Set Speed  [a]Add [d]Del [Tab]Next",
             "\xe2\x86\x90", "\xe2\x86\x92",  /* ← → */
             "\xe2\x86\x91", "\xe2\x86\x93");  /* ↑ ↓ */

    if (st->dirty) {
        attron(A_BOLD);
        mvprintw(row, GRAPH_LEFT + GRAPH_COLS - 8, "[modified]");
        attroff(A_BOLD);
    }

    attroff(COLOR_PAIR(CP_STATUS));
}

static void draw_screen(const EditorState *st) {
    erase();
    draw_title();
    draw_axes();
    draw_interpolated_line(&st->curve);
    draw_points(st);
    draw_status(st);
    refresh();
}

/* Sort points by temperature after modification */
static void sort_points(EditorState *st) {
    /* Simple insertion sort - curves are small */
    int sel_temp = st->curve.points[st->selected].temperature;
    int sel_speed = st->curve.points[st->selected].fan_speed;

    for (int i = 1; i < st->curve.point_count; i++) {
        FanCurvePoint tmp = st->curve.points[i];
        int j = i - 1;
        while (j >= 0 && st->curve.points[j].temperature > tmp.temperature) {
            st->curve.points[j + 1] = st->curve.points[j];
            j--;
        }
        st->curve.points[j + 1] = tmp;
    }

    /* Track selected point after sort */
    for (int i = 0; i < st->curve.point_count; i++) {
        if (st->curve.points[i].temperature == sel_temp &&
            st->curve.points[i].fan_speed == sel_speed) {
            st->selected = i;
            break;
        }
    }
}

/* Check if a temperature value conflicts with another point */
static int temp_conflicts(const EditorState *st, int temp, int exclude_idx) {
    for (int i = 0; i < st->curve.point_count; i++) {
        if (i != exclude_idx && st->curve.points[i].temperature == temp)
            return 1;
    }
    return 0;
}

/* Returns: 1=save&quit, 0=discard&quit, -1=cancel */
static int prompt_save(void) {
    int row = GRAPH_TOP + GRAPH_ROWS + 5;
    attron(COLOR_PAIR(CP_PROMPT) | A_BOLD);
    mvprintw(row, 2, "Save changes? [y]Save  [n]Discard  [c]Cancel ");
    attroff(COLOR_PAIR(CP_PROMPT) | A_BOLD);
    refresh();

    int ch;
    for (;;) {
        ch = getch();
        if (ch == 'y' || ch == 'Y') return 1;
        if (ch == 'n' || ch == 'N') return 0;
        if (ch == 'c' || ch == 'C' || ch == 27) return -1; /* Esc = cancel */
    }
}

/* Prompt user to type a number. Returns the value, or -1 on cancel. */
static int prompt_number(const char *label, int current, int min_val, int max_val) {
    int row = GRAPH_TOP + GRAPH_ROWS + 5;
    char buf[8];
    int pos = 0;

    memset(buf, 0, sizeof(buf));

    for (;;) {
        move(row, 0);
        clrtoeol();
        attron(COLOR_PAIR(CP_PROMPT) | A_BOLD);
        mvprintw(row, 2, "%s (now %d, range %d-%d): %s_",
                 label, current, min_val, max_val, buf);
        attroff(COLOR_PAIR(CP_PROMPT) | A_BOLD);
        refresh();

        int ch = getch();
        if (ch >= '0' && ch <= '9' && pos < 3) {
            buf[pos++] = (char)ch;
            buf[pos] = '\0';
        } else if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && pos > 0) {
            buf[--pos] = '\0';
        } else if (ch == '\n' || ch == '\r') {
            if (pos == 0) break; /* empty = cancel */
            int val = atoi(buf);
            if (val < min_val) val = min_val;
            if (val > max_val) val = max_val;
            /* Clear prompt line */
            move(row, 0);
            clrtoeol();
            return val;
        } else if (ch == 27) { /* Esc = cancel */
            break;
        }
    }
    move(row, 0);
    clrtoeol();
    return -1;
}

static void handle_input(EditorState *st, int ch) {
    switch (ch) {
    case 'q':
    case 'Q':
        if (st->dirty) {
            int choice = prompt_save();
            if (choice == 1) {
                /* Save and quit */
                curve_write(&st->curve);
                st->dirty = 0;
                st->running = 0;
            } else if (choice == 0) {
                /* Discard and quit */
                st->running = 0;
            }
            /* choice == -1: cancel, stay in editor */
        } else {
            st->running = 0;
        }
        break;

    case 's':
    case 'S':
        if (curve_write(&st->curve) == 0) {
            st->dirty = 0;
            st->running = 0;
        }
        break;

    case 'r':
    case 'R': {
        FanCurve def = {
            .points = {
                {30, 30}, {40, 40}, {50, 55},
                {60, 65}, {70, 85}, {80, 100}
            },
            .point_count = 6
        };
        st->curve = def;
        st->selected = 0;
        st->dirty = 1;
        break;
    }

    case '\t':
        if (st->curve.point_count > 0)
            st->selected = (st->selected + 1) % st->curve.point_count;
        break;

    case KEY_BTAB:
        if (st->curve.point_count > 0)
            st->selected = (st->selected - 1 + st->curve.point_count) % st->curve.point_count;
        break;

    case KEY_UP:
        if (st->curve.point_count > 0) {
            int *spd = &st->curve.points[st->selected].fan_speed;
            *spd += 5;
            if (*spd > SPEED_MAX) *spd = SPEED_MAX;
            st->dirty = 1;
        }
        break;

    case KEY_DOWN:
        if (st->curve.point_count > 0) {
            int *spd = &st->curve.points[st->selected].fan_speed;
            *spd -= 5;
            if (*spd < SPEED_MIN) *spd = SPEED_MIN;
            st->dirty = 1;
        }
        break;

    case KEY_RIGHT:
        if (st->curve.point_count > 0) {
            int *tmp = &st->curve.points[st->selected].temperature;
            int new_temp = *tmp + 5;
            if (new_temp > TEMP_MAX) new_temp = TEMP_MAX;
            while (new_temp > *tmp && temp_conflicts(st, new_temp, st->selected))
                new_temp--;
            if (new_temp != *tmp) {
                *tmp = new_temp;
                sort_points(st);
                st->dirty = 1;
            }
        }
        break;

    case KEY_LEFT:
        if (st->curve.point_count > 0) {
            int *tmp = &st->curve.points[st->selected].temperature;
            int new_temp = *tmp - 5;
            if (new_temp < TEMP_MIN) new_temp = TEMP_MIN;
            while (new_temp < *tmp && temp_conflicts(st, new_temp, st->selected))
                new_temp++;
            if (new_temp != *tmp) {
                *tmp = new_temp;
                sort_points(st);
                st->dirty = 1;
            }
        }
        break;

    case 'a':
    case 'A':
        if (st->curve.point_count >= MAX_CURVE_POINTS)
            break;
        if (st->curve.point_count == 0) {
            st->curve.points[0].temperature = 50;
            st->curve.points[0].fan_speed = 50;
            st->curve.point_count = 1;
            st->selected = 0;
            st->dirty = 1;
        } else {
            int new_temp, new_speed;
            if (st->selected < st->curve.point_count - 1) {
                /* Midpoint between selected and next */
                new_temp = (st->curve.points[st->selected].temperature +
                            st->curve.points[st->selected + 1].temperature) / 2;
                new_speed = (st->curve.points[st->selected].fan_speed +
                             st->curve.points[st->selected + 1].fan_speed) / 2;
            } else {
                /* After last point */
                int last_temp = st->curve.points[st->selected].temperature;
                new_temp = last_temp + 5;
                if (new_temp > TEMP_MAX)
                    new_temp = TEMP_MAX;
                new_speed = st->curve.points[st->selected].fan_speed;
            }
            /* Avoid duplicate temperatures */
            while (temp_conflicts(st, new_temp, -1) && new_temp < TEMP_MAX)
                new_temp++;
            if (temp_conflicts(st, new_temp, -1))
                break;  /* No room */
            if (new_speed < SPEED_MIN) new_speed = SPEED_MIN;
            if (new_speed > SPEED_MAX) new_speed = SPEED_MAX;

            int idx = st->curve.point_count;
            st->curve.points[idx].temperature = new_temp;
            st->curve.points[idx].fan_speed = new_speed;
            st->curve.point_count++;
            st->selected = idx;
            sort_points(st);
            st->dirty = 1;
        }
        break;

    case 'd':
    case 'D':
        if (st->curve.point_count <= 2)
            break;  /* Enforce minimum 2 points */
        for (int i = st->selected; i < st->curve.point_count - 1; i++)
            st->curve.points[i] = st->curve.points[i + 1];
        st->curve.point_count--;
        if (st->selected >= st->curve.point_count)
            st->selected = st->curve.point_count - 1;
        st->dirty = 1;
        break;

    case 't':
    case 'T':
        if (st->curve.point_count > 0) {
            int val = prompt_number("Temperature",
                                    st->curve.points[st->selected].temperature,
                                    TEMP_MIN, TEMP_MAX);
            if (val >= 0 && !temp_conflicts(st, val, st->selected)) {
                st->curve.points[st->selected].temperature = val;
                sort_points(st);
                st->dirty = 1;
            }
        }
        break;

    case 'f':
    case 'F':
        if (st->curve.point_count > 0) {
            int val = prompt_number("Fan speed",
                                    st->curve.points[st->selected].fan_speed,
                                    SPEED_MIN, SPEED_MAX);
            if (val >= 0) {
                st->curve.points[st->selected].fan_speed = val;
                st->dirty = 1;
            }
        }
        break;

    case KEY_MOUSE: {
        MEVENT event;
        if (getmouse(&event) == OK) {
            int mx = event.x;
            int my = event.y;
            /* Find nearest point to click */
            int best = -1;
            int best_dist = 999999;
            for (int i = 0; i < st->curve.point_count; i++) {
                int pc = temp_to_col(st->curve.points[i].temperature);
                int pr = speed_to_row(st->curve.points[i].fan_speed);
                int dx = mx - pc;
                int dy = my - pr;
                int dist = dx * dx + dy * dy;
                if (dist < best_dist) {
                    best_dist = dist;
                    best = i;
                }
            }
            if (best >= 0)
                st->selected = best;
        }
        break;
    }

    default:
        break;
    }
}

int editor_run(void) {
    /* Load current curve */
    FanCurve *loaded = curve_read();
    EditorState st;
    memset(&st, 0, sizeof(st));

    if (loaded) {
        st.curve = *loaded;
        free(loaded);
    } else {
        /* No curve file — use default */
        FanCurve def = {
            .points = {
                {30, 30}, {40, 40}, {50, 55},
                {60, 65}, {70, 85}, {80, 100}
            },
            .point_count = 6
        };
        st.curve = def;
    }

    st.original = st.curve;
    st.selected = 0;
    st.dirty = 0;
    st.running = 1;

    /* Detect if ncurses is already running (embedded in dashboard) */
    int standalone = (stdscr == NULL || isendwin());

    if (standalone) {
        setlocale(LC_ALL, "");
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
    }

    /* Always set blocking input and enable mouse */
    timeout(-1);
    mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED, NULL);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_NORMAL,   COLOR_WHITE,   -1);
        init_pair(CP_SELECTED, COLOR_GREEN,    -1);
        init_pair(CP_AXIS,     COLOR_CYAN,     -1);
        init_pair(CP_LINE,     COLOR_YELLOW,   -1);
        init_pair(CP_TITLE,    COLOR_WHITE,    -1);
        init_pair(CP_STATUS,   COLOR_WHITE,    -1);
        init_pair(CP_PROMPT,   COLOR_RED,      -1);
    }

    while (st.running) {
        draw_screen(&st);
        int ch = getch();
        handle_input(&st, ch);
    }

    if (standalone)
        endwin();

    return 0;
}
