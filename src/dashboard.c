#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ncurses.h>
#include <jansson.h>

#include "dashboard.h"
#include "nvfd.h"
#include "gpu.h"
#include "fan.h"
#include "curve.h"
#include "config.h"
#include "editor.h"

/* Color pairs */
#define DC_TITLE     1
#define DC_SEPARATOR 2
#define DC_LABEL     3
#define DC_VALUE     4
#define DC_BAR_FILL  5
#define DC_BAR_EMPTY 6
#define DC_MODE_SEL  7
#define DC_MODE_DIM  8
#define DC_STATUS    9
#define DC_CURVE    10
#define DC_CURSOR   11

#define BAR_WIDTH    30

typedef struct {
    /* Per-GPU cached data */
    char     name[NVML_DEVICE_NAME_BUFFER_SIZE];
    int      temp;
    int      utilization;
    unsigned long long mem_used;
    unsigned long long mem_total;
    int      power;        /* milliwatts */
    int      power_limit;  /* milliwatts */
    int      fan_count;
    int      fan_speed[MAX_FAN_COUNT];
    char     mode[16];     /* "auto", "manual", "curve" */
    int      manual_speed; /* config speed for manual mode */
} GpuData;

typedef struct {
    GpuData  gpus[MAX_GPU_COUNT];
    unsigned int gpu_count;
    unsigned int selected_gpu;
    int      running;
    int      term_rows;
    int      term_cols;
} DashboardState;

static void init_colors(void) {
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(DC_TITLE,     COLOR_WHITE,   -1);
        init_pair(DC_SEPARATOR, COLOR_CYAN,    -1);
        init_pair(DC_LABEL,     COLOR_WHITE,   -1);
        init_pair(DC_VALUE,     COLOR_GREEN,   -1);
        init_pair(DC_BAR_FILL,  COLOR_GREEN,   -1);
        init_pair(DC_BAR_EMPTY, COLOR_WHITE,   -1);
        init_pair(DC_MODE_SEL,  COLOR_GREEN,   -1);
        init_pair(DC_MODE_DIM,  COLOR_WHITE,   -1);
        init_pair(DC_STATUS,    COLOR_WHITE,   -1);
        init_pair(DC_CURVE,     COLOR_YELLOW,  -1);
        init_pair(DC_CURSOR,    COLOR_CYAN,    -1);
    }
}

static void dashboard_refresh_data(DashboardState *st) {
    st->gpu_count = device_count;
    getmaxyx(stdscr, st->term_rows, st->term_cols);

    json_t *root = config_read();

    for (unsigned int i = 0; i < st->gpu_count; i++) {
        GpuData *g = &st->gpus[i];
        nvmlDevice_t device;

        if (gpu_get_handle(i, &device) != 0) {
            snprintf(g->name, sizeof(g->name), "GPU %u (error)", i);
            continue;
        }

        gpu_get_name(device, g->name, sizeof(g->name));
        g->temp = gpu_get_temperature(device);
        g->utilization = gpu_get_utilization(device);
        gpu_get_memory(device, &g->mem_used, &g->mem_total);
        g->power = gpu_get_power(device);
        g->power_limit = gpu_get_power_limit(device);
        g->fan_count = fan_get_count(device);
        if (g->fan_count > MAX_FAN_COUNT)
            g->fan_count = MAX_FAN_COUNT;

        for (int f = 0; f < g->fan_count; f++)
            g->fan_speed[f] = fan_get_speed(device, (unsigned int)f);

        /* Read mode from config */
        char gpu_key[20];
        snprintf(gpu_key, sizeof(gpu_key), "gpu%d", i);
        json_t *cfg = json_object_get(root, gpu_key);

        if (json_is_object(cfg)) {
            const char *mode = json_string_value(json_object_get(cfg, "mode"));
            if (mode)
                strncpy(g->mode, mode, sizeof(g->mode) - 1);
            else
                strncpy(g->mode, "auto", sizeof(g->mode) - 1);
            g->mode[sizeof(g->mode) - 1] = '\0';

            if (strcmp(g->mode, "manual") == 0)
                g->manual_speed = (int)json_integer_value(json_object_get(cfg, "speed"));
            else
                g->manual_speed = 0;
        } else {
            strncpy(g->mode, "auto", sizeof(g->mode) - 1);
            g->mode[sizeof(g->mode) - 1] = '\0';
            g->manual_speed = 0;
        }
    }

    json_decref(root);
}

static void draw_bar(int row, int col, int width, int percent, int color_pair) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int filled = (percent * width) / 100;

    mvaddch(row, col, '[');

    attron(COLOR_PAIR(color_pair) | A_BOLD);
    for (int i = 0; i < filled; i++)
        mvaddstr(row, col + 1 + i, "#");
    attroff(COLOR_PAIR(color_pair) | A_BOLD);

    attron(COLOR_PAIR(DC_BAR_EMPTY));
    for (int i = filled; i < width; i++)
        mvaddstr(row, col + 1 + i, "\xc2\xb7"); /* middle dot · */
    attroff(COLOR_PAIR(DC_BAR_EMPTY));

    mvaddch(row, col + 1 + width, ']');
}

static void draw_separator(int row, int cols) {
    attron(COLOR_PAIR(DC_SEPARATOR));
    for (int c = 0; c < cols; c++)
        mvaddstr(row, c, "\xe2\x94\x80"); /* ─ */
    attroff(COLOR_PAIR(DC_SEPARATOR));
}

static void draw_title(const DashboardState *st) {
    attron(COLOR_PAIR(DC_TITLE) | A_BOLD);
    mvprintw(0, 1, " NVFD v%s", NVFD_VERSION);
    attroff(COLOR_PAIR(DC_TITLE) | A_BOLD);
    attron(COLOR_PAIR(DC_SEPARATOR));
    printw(" \xe2\x94\x80 "); /* ─ */
    attroff(COLOR_PAIR(DC_SEPARATOR));
    attron(COLOR_PAIR(DC_TITLE) | A_BOLD);
    printw("GPU Fan Control");
    attroff(COLOR_PAIR(DC_TITLE) | A_BOLD);

    attron(COLOR_PAIR(DC_STATUS));
    mvprintw(0, st->term_cols - 10, "[q] Quit");
    attroff(COLOR_PAIR(DC_STATUS));

    draw_separator(1, st->term_cols);
}

static int draw_gpu_section(const DashboardState *st, int row) {
    const GpuData *g = &st->gpus[st->selected_gpu];
    int col_label = 3;
    int col_val = 13;
    int col_bar = 22;

    /* GPU name */
    attron(COLOR_PAIR(DC_TITLE) | A_BOLD);
    mvprintw(row, col_label - 1, "GPU %u: %s", st->selected_gpu, g->name);
    attroff(COLOR_PAIR(DC_TITLE) | A_BOLD);
    row++;

    /* Temperature */
    attron(COLOR_PAIR(DC_LABEL));
    mvprintw(row, col_label, "Temp");
    attroff(COLOR_PAIR(DC_LABEL));
    attron(COLOR_PAIR(DC_VALUE) | A_BOLD);
    mvprintw(row, col_val, "%3d\xc2\xb0""C", g->temp >= 0 ? g->temp : 0);
    attroff(COLOR_PAIR(DC_VALUE) | A_BOLD);
    if (g->temp >= 0)
        draw_bar(row, col_bar, BAR_WIDTH, g->temp, DC_BAR_FILL);
    row++;

    /* GPU utilization */
    attron(COLOR_PAIR(DC_LABEL));
    mvprintw(row, col_label, "GPU Use");
    attroff(COLOR_PAIR(DC_LABEL));
    attron(COLOR_PAIR(DC_VALUE) | A_BOLD);
    mvprintw(row, col_val, "%3d%%", g->utilization >= 0 ? g->utilization : 0);
    attroff(COLOR_PAIR(DC_VALUE) | A_BOLD);
    if (g->utilization >= 0)
        draw_bar(row, col_bar, BAR_WIDTH, g->utilization, DC_BAR_FILL);
    row++;

    /* Memory */
    attron(COLOR_PAIR(DC_LABEL));
    mvprintw(row, col_label, "Memory");
    attroff(COLOR_PAIR(DC_LABEL));
    if (g->mem_total > 0) {
        double used_gb = (double)g->mem_used / (1024.0 * 1024.0 * 1024.0);
        double total_gb = (double)g->mem_total / (1024.0 * 1024.0 * 1024.0);
        attron(COLOR_PAIR(DC_VALUE) | A_BOLD);
        mvprintw(row, col_val, "%.1f / %.1f GB", used_gb, total_gb);
        attroff(COLOR_PAIR(DC_VALUE) | A_BOLD);
    }
    row++;

    /* Power */
    attron(COLOR_PAIR(DC_LABEL));
    mvprintw(row, col_label, "Power");
    attroff(COLOR_PAIR(DC_LABEL));
    if (g->power >= 0 && g->power_limit > 0) {
        int watts = g->power / 1000;
        int limit_watts = g->power_limit / 1000;
        attron(COLOR_PAIR(DC_VALUE) | A_BOLD);
        mvprintw(row, col_val, "%d / %d W", watts, limit_watts);
        attroff(COLOR_PAIR(DC_VALUE) | A_BOLD);
    }
    row++;

    /* Blank line before fans */
    row++;

    /* Fans */
    for (int f = 0; f < g->fan_count; f++) {
        attron(COLOR_PAIR(DC_LABEL));
        mvprintw(row, col_label, "Fan %d", f);
        attroff(COLOR_PAIR(DC_LABEL));
        if (g->fan_speed[f] >= 0) {
            attron(COLOR_PAIR(DC_VALUE) | A_BOLD);
            mvprintw(row, col_val, "%3d%%", g->fan_speed[f]);
            attroff(COLOR_PAIR(DC_VALUE) | A_BOLD);
            draw_bar(row, col_bar, BAR_WIDTH, g->fan_speed[f], DC_BAR_FILL);
        }
        row++;
    }

    /* Blank line before mode */
    row++;

    /* Mode display */
    attron(COLOR_PAIR(DC_LABEL));
    mvprintw(row, col_label, "Mode:");
    attroff(COLOR_PAIR(DC_LABEL));

    const char *modes[] = {"auto", "manual", "curve"};
    const char *labels[] = {"Auto", "Manual", "Curve"};
    int mcol = col_label + 7;

    for (int m = 0; m < 3; m++) {
        if (strcmp(g->mode, modes[m]) == 0) {
            attron(COLOR_PAIR(DC_MODE_SEL) | A_BOLD | A_REVERSE);
            mvprintw(row, mcol, " %s ", labels[m]);
            attroff(COLOR_PAIR(DC_MODE_SEL) | A_BOLD | A_REVERSE);
        } else {
            attron(COLOR_PAIR(DC_MODE_DIM));
            mvprintw(row, mcol, " %s ", labels[m]);
            attroff(COLOR_PAIR(DC_MODE_DIM));
        }
        mcol += (int)strlen(labels[m]) + 3;
    }

    /* Speed indicator for manual mode */
    if (strcmp(g->mode, "manual") == 0) {
        mcol += 4;
        attron(COLOR_PAIR(DC_VALUE) | A_BOLD);
        mvprintw(row, mcol, "Speed: %d%%", g->manual_speed);
        attroff(COLOR_PAIR(DC_VALUE) | A_BOLD);
    }

    row++;
    return row;
}

static void draw_curve_info(int start_row, int current_temp) {
    FanCurve *curve = curve_read();

    attron(COLOR_PAIR(DC_LABEL) | A_BOLD);
    mvprintw(start_row, 3, "Fan Curve:");
    attroff(COLOR_PAIR(DC_LABEL) | A_BOLD);

    if (!curve) {
        attron(COLOR_PAIR(DC_MODE_DIM));
        printw("  (no curve file - using default)");
        attroff(COLOR_PAIR(DC_MODE_DIM));
        return;
    }

    /* Show current temp → interpolated speed */
    if (current_temp >= 0) {
        int cur_speed = curve_interpolate(current_temp, curve);
        attron(COLOR_PAIR(DC_CURSOR) | A_BOLD);
        mvprintw(start_row, 40, "Now: %d\xc2\xb0""C \xe2\x86\x92 %d%%",
                 current_temp, cur_speed);
        attroff(COLOR_PAIR(DC_CURSOR) | A_BOLD);
    }

    start_row++;

    /* Curve points table */
    attron(COLOR_PAIR(DC_SEPARATOR));
    mvprintw(start_row, 5, "Temp  ");
    attroff(COLOR_PAIR(DC_SEPARATOR));

    for (int i = 0; i < curve->point_count; i++) {
        int is_active = (current_temp >= 0 &&
                         i < curve->point_count - 1 &&
                         current_temp >= curve->points[i].temperature &&
                         current_temp < curve->points[i + 1].temperature);
        /* Also highlight last point if temp >= last */
        if (i == curve->point_count - 1 && current_temp >= 0 &&
            current_temp >= curve->points[i].temperature)
            is_active = 1;

        if (is_active)
            attron(COLOR_PAIR(DC_CURSOR) | A_BOLD);
        else
            attron(COLOR_PAIR(DC_VALUE));
        printw(" %3d\xc2\xb0""C", curve->points[i].temperature);
        if (is_active)
            attroff(COLOR_PAIR(DC_CURSOR) | A_BOLD);
        else
            attroff(COLOR_PAIR(DC_VALUE));
    }

    start_row++;

    attron(COLOR_PAIR(DC_SEPARATOR));
    mvprintw(start_row, 5, "Speed ");
    attroff(COLOR_PAIR(DC_SEPARATOR));

    for (int i = 0; i < curve->point_count; i++) {
        int is_active = (current_temp >= 0 &&
                         i < curve->point_count - 1 &&
                         current_temp >= curve->points[i].temperature &&
                         current_temp < curve->points[i + 1].temperature);
        if (i == curve->point_count - 1 && current_temp >= 0 &&
            current_temp >= curve->points[i].temperature)
            is_active = 1;

        if (is_active)
            attron(COLOR_PAIR(DC_CURSOR) | A_BOLD);
        else
            attron(COLOR_PAIR(DC_CURVE));
        printw(" %3d%% ", curve->points[i].fan_speed);
        if (is_active)
            attroff(COLOR_PAIR(DC_CURSOR) | A_BOLD);
        else
            attroff(COLOR_PAIR(DC_CURVE));
    }

    start_row++;
    attron(COLOR_PAIR(DC_MODE_DIM));
    mvprintw(start_row, 5, "Press [e] to open curve editor");
    attroff(COLOR_PAIR(DC_MODE_DIM));

    free(curve);
}

static void draw_status_bar(const DashboardState *st) {
    const GpuData *g = &st->gpus[st->selected_gpu];
    int row = st->term_rows - 1;

    attron(COLOR_PAIR(DC_STATUS) | A_REVERSE);
    for (int c = 0; c < st->term_cols; c++)
        mvaddch(row, c, ' ');

    int offset = 1;
    if (st->gpu_count > 1) {
        mvprintw(row, offset, "[Tab] GPU  ");
        offset += 11;
    }

    mvprintw(row, offset, "[m] Mode");
    offset += 8;

    if (strcmp(g->mode, "manual") == 0) {
        mvprintw(row, offset, "  [\xe2\x86\x91\xe2\x86\x93] Speed");
        offset += 14;
    }

    if (strcmp(g->mode, "curve") == 0) {
        mvprintw(row, offset, "  [e] Edit Curve");
        offset += 16;
    }

    mvprintw(row, offset, "  [q] Quit");

    mvprintw(row, st->term_cols - 17, "Auto-refresh 1s");
    attroff(COLOR_PAIR(DC_STATUS) | A_REVERSE);
}

static void draw_screen(const DashboardState *st) {
    erase();
    draw_title(st);

    int row = 2;
    row = draw_gpu_section(st, row);

    const GpuData *g = &st->gpus[st->selected_gpu];

    /* Only show curve info in curve mode */
    if (strcmp(g->mode, "curve") == 0) {
        row++;
        draw_separator(row, st->term_cols);
        row++;
        draw_curve_info(row, g->temp);
    }

    draw_status_bar(st);
    refresh();
}

static void apply_mode(unsigned int gpu_index, const char *mode, int speed) {
    char gpu_key[20];
    snprintf(gpu_key, sizeof(gpu_key), "gpu%d", gpu_index);
    config_write_gpu(gpu_key, mode, speed);

    if (strcmp(mode, "auto") == 0) {
        fan_reset_to_auto(gpu_index);
    } else if (strcmp(mode, "manual") == 0) {
        fan_set_gpu_speed(gpu_index, (unsigned int)speed);
    }
    /* curve mode: apply immediately in TUI */
    if (strcmp(mode, "curve") == 0) {
        nvmlDevice_t device;
        if (gpu_get_handle(gpu_index, &device) == 0) {
            int temp = gpu_get_temperature(device);
            if (temp >= 0) {
                FanCurve *curve = curve_read();
                int fan_speed;
                if (curve) {
                    fan_speed = curve_interpolate(temp, curve);
                    free(curve);
                } else {
                    fan_speed = curve_default_interpolate(temp);
                }
                fan_set_gpu_speed(gpu_index, (unsigned int)fan_speed);
            }
        }
    }
}

static void handle_input(DashboardState *st, int ch) {
    GpuData *g = &st->gpus[st->selected_gpu];

    switch (ch) {
    case 'q':
    case 'Q':
        st->running = 0;
        break;

    case '\t':
        if (st->gpu_count > 1)
            st->selected_gpu = (st->selected_gpu + 1) % st->gpu_count;
        break;

    case KEY_BTAB:
        if (st->gpu_count > 1)
            st->selected_gpu = (st->selected_gpu + st->gpu_count - 1) % st->gpu_count;
        break;

    case 'm':
    case 'M': {
        /* Cycle: auto -> manual -> curve -> auto */
        int speed = g->manual_speed;
        if (strcmp(g->mode, "auto") == 0) {
            if (speed < 30) speed = 50;
            apply_mode(st->selected_gpu, "manual", speed);
        } else if (strcmp(g->mode, "manual") == 0) {
            apply_mode(st->selected_gpu, "curve", 0);
        } else {
            apply_mode(st->selected_gpu, "auto", 0);
        }
        break;
    }

    case KEY_UP:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed + 1;
            if (spd > 100) spd = 100;
            apply_mode(st->selected_gpu, "manual", spd);
        }
        break;

    case KEY_DOWN:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed - 1;
            if (spd < 30) spd = 30;
            apply_mode(st->selected_gpu, "manual", spd);
        }
        break;

    case KEY_PPAGE:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed + 5;
            if (spd > 100) spd = 100;
            apply_mode(st->selected_gpu, "manual", spd);
        }
        break;

    case KEY_NPAGE:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed - 5;
            if (spd < 30) spd = 30;
            apply_mode(st->selected_gpu, "manual", spd);
        }
        break;

    case 'e':
    case 'E':
        if (strcmp(g->mode, "curve") != 0)
            break;
        config_ensure_dir();
        editor_run();
        /* Restore dashboard ncurses settings */
        init_colors();
        timeout(1000);
        curs_set(0);
        break;

    default:
        break;
    }
}

int dashboard_run(void) {
    DashboardState st;
    memset(&st, 0, sizeof(st));
    st.running = 1;
    st.selected_gpu = 0;

    /* Must set locale before initscr() for UTF-8 support */
    setlocale(LC_ALL, "");

    /* Init ncurses */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(1000);

    init_colors();

    while (st.running && keep_running) {
        dashboard_refresh_data(&st);
        draw_screen(&st);
        int ch = getch();
        if (ch != ERR)
            handle_input(&st, ch);
    }

    endwin();

    /* Reset fans to auto on clean exit */
    fan_reset_all_to_auto();
    return 0;
}
