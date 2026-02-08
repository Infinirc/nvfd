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
    int      dirty;
    char     init_mode[MAX_GPU_COUNT][16];
    int      init_speed[MAX_GPU_COUNT];
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
            g->temp = -1;
            g->utilization = -1;
            g->mem_used = 0;
            g->mem_total = 0;
            g->power = -1;
            g->power_limit = 0;
            g->fan_count = 0;
            continue;
        }

        gpu_get_name(device, g->name, sizeof(g->name));
        g->temp = gpu_get_temperature(device);
        g->utilization = gpu_get_utilization(device);
        if (gpu_get_memory(device, &g->mem_used, &g->mem_total) != 0) {
            g->mem_used = 0;
            g->mem_total = 0;
        }
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

    if (st->dirty) {
        attron(COLOR_PAIR(DC_CURVE) | A_BOLD);
        printw("  [modified]");
        attroff(COLOR_PAIR(DC_CURVE) | A_BOLD);
    }

    attron(COLOR_PAIR(DC_STATUS));
    mvprintw(0, st->term_cols - 10, "[q] Quit");
    attroff(COLOR_PAIR(DC_STATUS));

    draw_separator(1, st->term_cols);
}

static int gpu_section_height(const GpuData *g) {
    int h = 8 + g->fan_count;
    if (strcmp(g->mode, "curve") == 0)
        h += 6; /* separator + curve info */
    return h;
}

static int total_full_height(const DashboardState *st) {
    int h = 3; /* title(2) + status_bar(1) */
    for (unsigned int i = 0; i < st->gpu_count; i++) {
        if (i > 0) h += 1; /* separator between GPUs */
        h += gpu_section_height(&st->gpus[i]);
    }
    return h;
}

static int draw_gpu_section(const DashboardState *st, int row, unsigned int gpu_index) {
    const GpuData *g = &st->gpus[gpu_index];
    int col_label = 3;
    int col_val = 13;
    int col_bar = 22;

    /* GPU name with selection indicator */
    if (gpu_index == st->selected_gpu && st->gpu_count > 1) {
        attron(COLOR_PAIR(DC_CURSOR) | A_BOLD);
        mvprintw(row, 1, "\xe2\x96\xb8"); /* ▸ */
        attroff(COLOR_PAIR(DC_CURSOR) | A_BOLD);
    }
    attron(COLOR_PAIR(DC_TITLE) | A_BOLD);
    mvprintw(row, col_label - 1, "GPU %u: %s", gpu_index, g->name);
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

    if (st->gpu_count > 1) {
        mvprintw(row, offset, "  [M] All");
        offset += 9;
    }

    if (strcmp(g->mode, "manual") == 0) {
        mvprintw(row, offset, "  [\xe2\x86\x91\xe2\x86\x93] Speed \xc2\xb1""5");
        offset += 17;
    }

    if (strcmp(g->mode, "curve") == 0) {
        mvprintw(row, offset, "  [e] Edit Curve");
        offset += 16;
    }

    mvprintw(row, offset, "  [q] Quit");

    mvprintw(row, st->term_cols - 17, "Auto-refresh 1s");
    attroff(COLOR_PAIR(DC_STATUS) | A_REVERSE);
}

static void draw_tab_bar(const DashboardState *st, int row) {
    int col = 1;
    for (unsigned int i = 0; i < st->gpu_count; i++) {
        char label[16];
        int len = snprintf(label, sizeof(label), " GPU %u ", i);
        if (i == st->selected_gpu) {
            attron(COLOR_PAIR(DC_MODE_SEL) | A_BOLD | A_REVERSE);
            mvprintw(row, col, "%s", label);
            attroff(COLOR_PAIR(DC_MODE_SEL) | A_BOLD | A_REVERSE);
        } else {
            attron(COLOR_PAIR(DC_MODE_DIM));
            mvprintw(row, col, "%s", label);
            attroff(COLOR_PAIR(DC_MODE_DIM));
        }
        col += len;
    }
}

static void draw_gpu_with_curve(const DashboardState *st, int *row, unsigned int gpu_index) {
    *row = draw_gpu_section(st, *row, gpu_index);
    if (strcmp(st->gpus[gpu_index].mode, "curve") == 0) {
        (*row)++;
        draw_separator(*row, st->term_cols);
        (*row)++;
        draw_curve_info(*row, st->gpus[gpu_index].temp);
        *row += 4;
    }
}

static void draw_screen(const DashboardState *st) {
    erase();

    if (st->term_rows < 12 || st->term_cols < 55) {
        mvprintw(0, 0, "Terminal too small (%dx%d)",
                 st->term_cols, st->term_rows);
        mvprintw(1, 0, "Need at least 55x12");
        refresh();
        return;
    }

    draw_title(st);

    int row = 2;

    if (st->gpu_count > 1 && total_full_height(st) <= st->term_rows) {
        /* Full mode: show all GPUs stacked */
        for (unsigned int i = 0; i < st->gpu_count; i++) {
            if (i > 0) {
                draw_separator(row, st->term_cols);
                row++;
            }
            draw_gpu_with_curve(st, &row, i);
        }
    } else {
        /* Single GPU or tabbed mode */
        if (st->gpu_count > 1) {
            draw_tab_bar(st, row);
            row++;
        }
        draw_gpu_with_curve(st, &row, st->selected_gpu);
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

static void apply_curve_fans(const DashboardState *st) {
    /* Check if any GPU is in curve mode first */
    int any_curve = 0;
    for (unsigned int i = 0; i < st->gpu_count; i++) {
        if (strcmp(st->gpus[i].mode, "curve") == 0) {
            any_curve = 1;
            break;
        }
    }
    if (!any_curve)
        return;

    /* Read curve once, apply to all curve-mode GPUs */
    FanCurve *curve = curve_read();

    for (unsigned int i = 0; i < st->gpu_count; i++) {
        if (strcmp(st->gpus[i].mode, "curve") != 0)
            continue;

        nvmlDevice_t device;
        if (gpu_get_handle(i, &device) != 0)
            continue;

        int temp = gpu_get_temperature(device);
        if (temp < 0)
            continue;

        int fan_speed;
        if (curve)
            fan_speed = curve_interpolate(temp, curve);
        else
            fan_speed = curve_default_interpolate(temp);
        fan_set_gpu_speed(i, (unsigned int)fan_speed);
    }

    free(curve);
}

/* Returns: 1=save, 0=discard, -1=cancel */
static int prompt_save_dashboard(int rows) {
    int row = rows - 3;

    move(row, 0);
    clrtoeol();

    attron(COLOR_PAIR(DC_TITLE) | A_BOLD | A_REVERSE);
    mvprintw(row, 2, " Save settings? [y]Save  [n]Discard  [c]Cancel ");
    attroff(COLOR_PAIR(DC_TITLE) | A_BOLD | A_REVERSE);
    attron(COLOR_PAIR(DC_MODE_DIM));
    mvprintw(row + 1, 3, "Save keeps fan settings; use systemd daemon for persistent control");
    attroff(COLOR_PAIR(DC_MODE_DIM));
    refresh();

    timeout(-1);
    int ch;
    for (;;) {
        ch = getch();
        if (ch == 'y' || ch == 'Y') { timeout(1000); return 1; }
        if (ch == 'n' || ch == 'N') { timeout(1000); return 0; }
        if (ch == 'c' || ch == 'C' || ch == 27) { timeout(1000); return -1; }
    }
}

static void handle_input(DashboardState *st, int ch) {
    GpuData *g = &st->gpus[st->selected_gpu];

    switch (ch) {
    case 'q':
    case 'Q':
        if (st->dirty) {
            int choice = prompt_save_dashboard(st->term_rows);
            if (choice == 1) {
                /* Save: config already written via apply_mode, just exit */
                st->running = 0;
            } else if (choice == 0) {
                /* Discard: restore initial config and fan state */
                for (unsigned int i = 0; i < st->gpu_count; i++)
                    apply_mode(i, st->init_mode[i], st->init_speed[i]);
                st->running = 0;
            }
            /* choice == -1: cancel, stay in dashboard */
        } else {
            st->running = 0;
        }
        break;

    case '\t':
        if (st->gpu_count > 1)
            st->selected_gpu = (st->selected_gpu + 1) % st->gpu_count;
        break;

    case KEY_BTAB:
        if (st->gpu_count > 1)
            st->selected_gpu = (st->selected_gpu + st->gpu_count - 1) % st->gpu_count;
        break;

    case 'm': {
        /* Cycle selected GPU: auto -> manual -> curve -> auto */
        int speed = g->manual_speed;
        if (strcmp(g->mode, "auto") == 0) {
            if (speed < 30) speed = 50;
            apply_mode(st->selected_gpu, "manual", speed);
        } else if (strcmp(g->mode, "manual") == 0) {
            apply_mode(st->selected_gpu, "curve", 0);
        } else {
            apply_mode(st->selected_gpu, "auto", 0);
        }
        st->dirty = 1;
        break;
    }

    case 'M': {
        /* Cycle ALL GPUs: use selected GPU's mode as reference */
        const char *new_mode;
        if (strcmp(g->mode, "auto") == 0)
            new_mode = "manual";
        else if (strcmp(g->mode, "manual") == 0)
            new_mode = "curve";
        else
            new_mode = "auto";
        for (unsigned int i = 0; i < st->gpu_count; i++) {
            int spd = 0;
            if (strcmp(new_mode, "manual") == 0) {
                spd = st->gpus[i].manual_speed;
                if (spd < 30) spd = 50;
            }
            apply_mode(i, new_mode, spd);
        }
        st->dirty = 1;
        break;
    }

    case KEY_UP:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed + 5;
            if (spd > 100) spd = 100;
            apply_mode(st->selected_gpu, "manual", spd);
            st->dirty = 1;
        }
        break;

    case KEY_DOWN:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed - 5;
            if (spd < 30) spd = 30;
            apply_mode(st->selected_gpu, "manual", spd);
            st->dirty = 1;
        }
        break;

    case KEY_PPAGE:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed + 10;
            if (spd > 100) spd = 100;
            apply_mode(st->selected_gpu, "manual", spd);
            st->dirty = 1;
        }
        break;

    case KEY_NPAGE:
        if (strcmp(g->mode, "manual") == 0) {
            int spd = g->manual_speed - 10;
            if (spd < 30) spd = 30;
            apply_mode(st->selected_gpu, "manual", spd);
            st->dirty = 1;
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
    st.dirty = 0;

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

    /* Capture initial state for save/discard on quit */
    dashboard_refresh_data(&st);
    for (unsigned int i = 0; i < st.gpu_count; i++) {
        strncpy(st.init_mode[i], st.gpus[i].mode, sizeof(st.init_mode[i]) - 1);
        st.init_mode[i][sizeof(st.init_mode[i]) - 1] = '\0';
        st.init_speed[i] = st.gpus[i].manual_speed;
    }

    while (st.running && keep_running) {
        dashboard_refresh_data(&st);
        apply_curve_fans(&st);
        draw_screen(&st);
        int ch = getch();
        if (ch != ERR)
            handle_input(&st, ch);
    }

    endwin();
    return 0;
}
