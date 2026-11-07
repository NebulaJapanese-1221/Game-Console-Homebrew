#include "miner.h"
#include "net.h"
#include "config.h"
#include "pool.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/ime_dialog.h>
#include <vita2d.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define SCREEN_W 960
#define SCREEN_H 544

typedef enum {
    SCREEN_DASHBOARD,
    SCREEN_SETTINGS,
    SCREEN_SETTINGS_EDIT,
    SCREEN_ABOUT,
    SCREEN_DONATION,
    SCREEN_LOG
} UiScreen;

static UiScreen g_screen = SCREEN_DASHBOARD;
static int g_sel = 0;
static int g_edit_field = 0;
static char g_text_in[256] = {0};
static size_t g_text_cursor = 0;
static bool g_quit_requested = false;
static bool g_open_keyboard = false;
static bool g_ime_available = false;
static char g_status_message[96] = {0};

static bool g_prev_cross = false;
static bool g_prev_circle = false;
static bool g_prev_square = false;
static bool g_prev_up = false;
static bool g_prev_down = false;
static bool g_prev_left = false;
static bool g_prev_right = false;

static uint64_t g_last_stats = 0;
static double g_cached_hashrate = 0.0;
static char g_ip[16] = {0};

static Config g_cfg;

static vita2d_pgf *g_font = NULL;

static uint64_t g_button_cooldown = 0;

static int btn_pressed(unsigned int cur, unsigned int prev, unsigned int mask) {
    return (cur & mask) && !(prev & mask);
}

/* Touch state */
static int g_touch_x = 0;
static int g_touch_y = 0;
static bool g_prev_touch_pressed = false;
static bool g_touch_just_pressed = false;

static void update_touch(void) {
    SceTouchData touch;
    memset(&touch, 0, sizeof(touch));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    bool pressed = false;
    if (touch.reportNum > 0) {
        g_touch_x = touch.report[0].x * SCREEN_W / 1920;
        g_touch_y = touch.report[0].y * SCREEN_H / 1088;
        pressed = true;
    }
    g_touch_just_pressed = pressed && !g_prev_touch_pressed;
    g_prev_touch_pressed = pressed;
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

typedef struct {
    int x, y, w, h;
    const char *label;
    bool hovered;
} Button;

static bool button_update(Button *b, bool just_pressed) {
    bool inside = point_in_rect(g_touch_x, g_touch_y, b->x, b->y, b->w, b->h);
    b->hovered = inside;
    if (just_pressed && inside) {
        return true;
    }
    return false;
}

static void draw_button(Button *b) {
    unsigned int col = b->hovered ? 0xFF007ACC : 0xFF2A3B4C;
    unsigned int outline = 0xFF556B7F;
    vita2d_draw_rectangle(b->x, b->y, b->w, b->h, col);
    vita2d_draw_rectangle(b->x, b->y, b->w, 2, outline);
    vita2d_draw_rectangle(b->x, b->y + b->h - 2, b->w, 2, outline);
    vita2d_draw_rectangle(b->x, b->y, 2, b->h, outline);
    vita2d_draw_rectangle(b->x + b->w - 2, b->y, 2, b->h, outline);

    int tw = (int)strlen(b->label) * 12;
    int tx = b->x + (b->w - tw) / 2;
    int ty = b->y + (b->h - 18) / 2;
    vita2d_pgf_draw_text(g_font, tx, ty, 0xFFFFFFFF, 1.0f, b->label);
}

static void draw_panel(int x, int y, int w, int h, const char *title) {
    vita2d_draw_rectangle(x, y, w, h, 0xFF1E2A36);
    vita2d_draw_rectangle(x, y, w, 2, 0xFF3A4B5C);
    vita2d_draw_rectangle(x, y + h - 2, w, 2, 0xFF3A4B5C);
    vita2d_draw_rectangle(x, y, 2, h, 0xFF3A4B5C);
    vita2d_draw_rectangle(x + w - 2, y, 2, h, 0xFF3A4B5C);
    vita2d_draw_rectangle(x, y, w, 30, 0xFF0E1420);
    vita2d_draw_rectangle(x, y + 28, w, 2, 0xFF3A4B5C);
    vita2d_pgf_draw_text(g_font, x + 12, y + 6, 0xFFFFFFFF, 1.0f, title);
}

static void draw_status_dot(int x, int y, bool active) {
    vita2d_draw_rectangle(x, y, 16, 16, active ? 0xFF00FF00 : 0xFFFF0000);
    vita2d_draw_rectangle(x, y, 16, 2, 0xFF556B7F);
    vita2d_draw_rectangle(x, y + 14, 16, 2, 0xFF556B7F);
    vita2d_draw_rectangle(x, y, 2, 16, 0xFF556B7F);
    vita2d_draw_rectangle(x + 14, y, 2, 16, 0xFF556B7F);
}

static void draw_progress_bar(int x, int y, int w, int h, float pct, unsigned int color) {
    vita2d_draw_rectangle(x, y, w, h, 0xFF0A1218);
    vita2d_draw_rectangle(x, y, w, 2, 0xFF3A4B5C);
    vita2d_draw_rectangle(x, y + h - 2, w, 2, 0xFF3A4B5C);
    vita2d_draw_rectangle(x, y, 2, h, 0xFF3A4B5C);
    vita2d_draw_rectangle(x + w - 2, y, 2, h, 0xFF3A4B5C);
    int fill = (int)((float)(w - 4) * pct);
    if (fill > 0) {
        vita2d_draw_rectangle(x + 2, y + 2, fill, h - 4, color);
    }
}

static char g_log_lines[8][96];
static int g_log_count = 0;

static void ui_log(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (int i = 7; i > 0; i--) {
        snprintf(g_log_lines[i], 96, "%s", g_log_lines[i - 1]);
    }
    snprintf(g_log_lines[0], 96, "%s", buf);
    if (g_log_count < 8) g_log_count++;
}

static void begin_edit(int field) {
    g_edit_field = field;
    snprintf(g_text_in, sizeof(g_text_in), "%s", config_field_buf(field));
    g_text_cursor = strlen(g_text_in);
    g_screen = SCREEN_SETTINGS_EDIT;
}

static void move_edit_cursor(int direction) {
    size_t length = strlen(g_text_in);
    if (direction < 0 && g_text_cursor > 0) g_text_cursor--;
    if (direction > 0 && g_text_cursor < length) g_text_cursor++;
}

static void cycle_edit_character(int direction) {
    static const char characters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@:/+=";
    static const char digits[] = "0123456789";
    const char *set = g_edit_field == 1 ? digits : characters;
    size_t set_len = strlen(set);
    size_t length = strlen(g_text_in);
    size_t max_len = (size_t)config_field_max_len(g_edit_field) - 1;
    if (g_text_cursor > length || (g_text_cursor == length && length >= max_len)) return;

    char current = g_text_cursor < length ? g_text_in[g_text_cursor] :
                   (g_edit_field == 1 ? '0' : 'a');
    const char *found = strchr(set, current);
    size_t index = found ? (size_t)(found - set) : 0;
    index = direction < 0 ? (index + set_len - 1) % set_len : (index + 1) % set_len;
    if (g_text_cursor == length) {
        g_text_in[length + 1] = '\0';
        g_text_in[length] = set[index];
        g_text_cursor++;
    } else {
        g_text_in[g_text_cursor] = set[index];
    }
}

static void delete_edit_character(void) {
    size_t length = strlen(g_text_in);
    if (g_text_cursor >= length) return;
    memmove(g_text_in + g_text_cursor, g_text_in + g_text_cursor + 1,
            length - g_text_cursor);
}

static size_t text_to_ime(const char *src, SceWChar16 *dst, size_t capacity) {
    size_t i = 0;
    while (src[i] && i + 1 < capacity) {
        dst[i] = (unsigned char)src[i];
        i++;
    }
    dst[i] = 0;
    return i;
}

static void ime_to_text(const SceWChar16 *src, char *dst, size_t capacity) {
    size_t i = 0;
    while (src[i] && i + 1 < capacity) {
        SceWChar16 ch = src[i++];
        dst[i - 1] = ch >= 0x20 && ch <= 0x7e ? (char)ch : '?';
    }
    dst[i] = '\0';
}

static int open_system_keyboard(void) {
    static const SceWChar16 title[] = {
        'E','n','t','e','r',' ','v','a','l','u','e',0
    };
    SceWChar16 initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH] = {0};
    SceWChar16 output[SCE_IME_DIALOG_MAX_TEXT_LENGTH] = {0};
    SceImeDialogParam param;
    SceImeDialogResult result;

    text_to_ime(g_text_in, initial, sizeof(initial) / sizeof(initial[0]));
    sceImeDialogParamInit(&param);
    param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    param.languagesForced = 1;
    param.type = g_edit_field == 1 ? SCE_IME_TYPE_NUMBER :
                 g_edit_field == 0 ? SCE_IME_TYPE_URL : SCE_IME_TYPE_BASIC_LATIN;
    param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = title;
    param.maxTextLength = (SceUInt32)config_field_max_len(g_edit_field) - 1;
    param.initialText = initial;
    param.inputTextBuffer = output;
    param.enterLabel = SCE_IME_ENTER_LABEL_DEFAULT;

    int ret = sceImeDialogInit(&param);
    if (ret < 0) return ret;
    while (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        sceKernelDelayThread(16000);
    }

    memset(&result, 0, sizeof(result));
    ret = sceImeDialogGetResult(&result);
    if (ret >= 0 && result.result == SCE_COMMON_DIALOG_RESULT_OK &&
        result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        char candidate[sizeof(g_text_in)];
        ime_to_text(output, candidate, sizeof(candidate));
        if (g_edit_field == 1) {
            bool valid = candidate[0] != '\0';
            for (size_t i = 0; candidate[i]; i++) {
                if (candidate[i] < '0' || candidate[i] > '9') {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                snprintf(g_status_message, sizeof(g_status_message), "%s",
                         "Port must contain digits only");
            } else {
                snprintf(g_text_in, sizeof(g_text_in), "%s", candidate);
            }
        } else {
            snprintf(g_text_in, sizeof(g_text_in), "%s", candidate);
        }
        g_text_cursor = strlen(g_text_in);
    }
    int term_ret = sceImeDialogTerm();
    return ret < 0 ? ret : term_ret;
}

static void render_edit(void) {
    static const char *field_names[] = {
        "SERVER", "PORT", "USERNAME", "WORKER", "MINING KEY", "DONATION ADDRESS"
    };
    char display[288];
    size_t length = strlen(g_text_in);
    size_t prefix = g_text_cursor > length ? length : g_text_cursor;
    int n = snprintf(display, sizeof(display), "%.*s[%c]%s",
                     (int)prefix, g_text_in,
                     g_text_cursor < length ? g_text_in[g_text_cursor] : '_',
                     g_text_cursor < length ? g_text_in + g_text_cursor + 1 : "");
    if (n < 0) display[0] = '\0';

    vita2d_start_drawing();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, 0xFF0E1420);
    draw_panel(40, 72, 880, 300, "EDIT SETTING");
    vita2d_pgf_draw_text(g_font, 80, 130, 0xFF80B3D6, 0.9f, field_names[g_edit_field]);
    vita2d_draw_rectangle(80, 155, 800, 42, 0xFF0A1218);
    vita2d_pgf_draw_text(g_font, 92, 164, 0xFFFFFFFF, 0.9f, display);
    vita2d_pgf_draw_text(g_font, 80, 230, 0xFFC0C0C0, 0.7f,
                         "D-pad Up/Down: change character  Left/Right: move cursor");
    vita2d_pgf_draw_text(g_font, 80, 260, 0xFFC0C0C0, 0.7f,
                         "Triangle: delete  Square: Vita keyboard  X: apply  Circle: cancel");

    Button btn_char_down = { 60, 310, 150, 48, "CHAR -" };
    Button btn_left = { 225, 310, 120, 48, "CURSOR <" };
    Button btn_right = { 360, 310, 120, 48, "CURSOR >" };
    Button btn_char_up = { 495, 310, 150, 48, "CHAR +" };
    Button btn_delete = { 660, 310, 120, 48, "DELETE" };
    Button btn_keyboard = { 795, 310, 120, 48, "KEYBOARD" };
    Button btn_cancel = { 240, 430, 200, 64, "CANCEL" };
    Button btn_done = { 520, 430, 200, 64, "DONE" };

    if (button_update(&btn_char_down, g_touch_just_pressed)) cycle_edit_character(-1);
    if (button_update(&btn_left, g_touch_just_pressed)) move_edit_cursor(-1);
    if (button_update(&btn_right, g_touch_just_pressed)) move_edit_cursor(1);
    if (button_update(&btn_char_up, g_touch_just_pressed)) cycle_edit_character(1);
    if (button_update(&btn_delete, g_touch_just_pressed)) delete_edit_character();
    if (button_update(&btn_keyboard, g_touch_just_pressed)) {
        g_open_keyboard = true;
    }
    if (button_update(&btn_cancel, g_touch_just_pressed)) g_screen = SCREEN_SETTINGS;
    if (button_update(&btn_done, g_touch_just_pressed)) {
        config_commit_field(g_edit_field, g_text_in);
        g_screen = SCREEN_SETTINGS;
    }

    draw_button(&btn_char_down);
    draw_button(&btn_left);
    draw_button(&btn_right);
    draw_button(&btn_char_up);
    draw_button(&btn_delete);
    draw_button(&btn_keyboard);
    draw_button(&btn_cancel);
    draw_button(&btn_done);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void render_dashboard(void) {
    Miner stats;
    miner_get_stats(&stats);
    Miner *m = &stats;

    vita2d_start_drawing();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, 0xFF0E1420);

    /* Header */
    vita2d_draw_rectangle(0, 0, SCREEN_W, 56, 0xFF16242E);
    vita2d_draw_rectangle(0, 56, SCREEN_W, 2, 0xFF3A4B5C);
    vita2d_pgf_draw_text(g_font, 20, 16, 0xFFFFFFFF, 1.2f, "VITA DUCO MINER");
    vita2d_pgf_draw_text(g_font, 20, 36, 0xFF808080, 0.7f, "DuinoCoin GUI Miner  PSVita");

    draw_status_dot(800, 20, m->connected);
    vita2d_pgf_draw_text(g_font, 824, 16, m->connected ? 0xFF00FF00 : 0xFFFF0000, 1.0f,
                         m->connected ? "ONLINE" : "OFFLINE");

    /* Left panel: Mining Status */
    draw_panel(20, 72, 548, 388, "MINING STATUS");

    int cx = 50, cy = 112;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "HASHRATE");
    char hbuf[32];
    snprintf(hbuf, sizeof(hbuf), "%.1f H/s", g_cached_hashrate);
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFF00FF80, 1.2f, hbuf);

    draw_progress_bar(cx, cy + 66, 200, 14, g_cached_hashrate > 0 ? 0.5f : 0.0f, 0xFF00FF80);

    cy = 220;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "SHARES");
    snprintf(hbuf, sizeof(hbuf), "ACCEPTED  %llu", (unsigned long long)m->shares_accepted);
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFF00FF80, 0.8f, hbuf);
    snprintf(hbuf, sizeof(hbuf), "REJECTED  %llu", (unsigned long long)m->shares_rejected);
    vita2d_pgf_draw_text(g_font, cx, cy + 56, 0xFFFF5555, 0.8f, hbuf);

    cy = 320;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "DIFFICULTY");
    snprintf(hbuf, sizeof(hbuf), "%u", m->difficulty);
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFC0C0C0, 0.9f, hbuf);

    cy = 360;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "LAST NONCE");
    snprintf(hbuf, sizeof(hbuf), "%u", m->nonce);
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFC0C0C0, 0.9f, hbuf);

    /* Right panel: Connection */
    draw_panel(588, 72, 352, 388, "CONNECTION");
    cx = 618; cy = 112;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "SERVER");
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFC0C0C0, 0.8f, m->server);
    snprintf(hbuf, sizeof(hbuf), ":%u", m->port);
    vita2d_pgf_draw_text(g_font, cx + (int)strlen(m->server) * 12, cy + 28, 0xFFC0C0C0, 0.8f, hbuf);

    cy += 70;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "USERNAME");
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFC0C0C0, 0.8f, m->username);
    if (m->worker[0]) {
        snprintf(hbuf, sizeof(hbuf), ".%s", m->worker);
        vita2d_pgf_draw_text(g_font, cx + (int)strlen(m->username) * 12, cy + 28, 0xFF80B3D6, 0.8f, hbuf);
    }

    cy += 70;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "LOCAL IP");
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFC0C0C0, 0.8f, g_ip[0] ? g_ip : "0.0.0.0");

    cy += 70;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "HASHRATE");
    snprintf(hbuf, sizeof(hbuf), "%.1f H/s", g_cached_hashrate);
    vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFC0C0C0, 0.8f, hbuf);

    cy += 70;
    vita2d_pgf_draw_text(g_font, cx, cy, 0xFF80B3D6, 0.8f, "POOL");
    snprintf(hbuf, sizeof(hbuf), "%s:%u", miner_pool_host() ? miner_pool_host() : "server.duinocoin.com",
             miner_pool_port());
    vita2d_pgf_draw_text(g_font, cx, cy + 28, miner_pool_valid() ? 0xFF00FF80 : 0xFFC0C0C0, 0.8f, hbuf);

    if (m->reject_reason[0]) {
        cy += 70;
        vita2d_pgf_draw_text(g_font, cx, cy, 0xFFFF5555, 0.7f, "REJECT:");
        vita2d_pgf_draw_text(g_font, cx, cy + 28, 0xFFFF8080, 0.7f, m->reject_reason);
    }

    /* Bottom buttons */
    Button btn_mine = { 20, 472, 210, 64, m->mining ? "STOP MINING" : "START MINING" };
    Button btn_set = { 240, 472, 140, 64, "SETTINGS" };
    Button btn_abt = { 390, 472, 120, 64, "ABOUT" };
    Button btn_donate = { 520, 472, 130, 64, "DONATE" };
    Button btn_log = { 660, 472, 110, 64, "LOG" };
    Button btn_quit = { 780, 472, 160, 64, "QUIT" };

    if (button_update(&btn_mine, g_touch_just_pressed)) {
        miner_toggle_mining();
        Miner updated;
        miner_get_stats(&updated);
        ui_log("%s", updated.mining ? "Mining started" : "Mining stopped");
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_set, g_touch_just_pressed)) {
        g_screen = SCREEN_SETTINGS;
        g_sel = 0;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_abt, g_touch_just_pressed)) {
        g_screen = SCREEN_ABOUT;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_donate, g_touch_just_pressed)) {
        g_screen = SCREEN_DONATION;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_log, g_touch_just_pressed)) {
        g_screen = SCREEN_LOG;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_quit, g_touch_just_pressed)) {
        g_quit_requested = true;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }

    draw_button(&btn_mine);
    draw_button(&btn_set);
    draw_button(&btn_abt);
    draw_button(&btn_donate);
    draw_button(&btn_log);
    draw_button(&btn_quit);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void draw_setting_field(int field, const char *label, int x, int y, int width) {
    unsigned int border = g_sel == field ? 0xFF00AAFF : 0xFF3A4B5C;
    vita2d_pgf_draw_text(g_font, x, y, 0xFF80B3D6, 0.8f, label);
    vita2d_draw_rectangle(x, y + 24, width, 34, 0xFF0A1218);
    vita2d_draw_rectangle(x, y + 24, width, 2, border);
    vita2d_draw_rectangle(x, y + 56, width, 2, border);
    vita2d_draw_rectangle(x, y + 24, 2, 34, border);
    vita2d_draw_rectangle(x + width - 2, y + 24, 2, 34, border);
    vita2d_pgf_draw_text(g_font, x + 8, y + 31, 0xFFFFFFFF, 0.75f,
                         config_field_buf(field));
}

static void render_settings(void) {
    vita2d_start_drawing();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, 0xFF0E1420);
    vita2d_draw_rectangle(0, 0, SCREEN_W, 56, 0xFF16242E);
    vita2d_draw_rectangle(0, 56, SCREEN_W, 2, 0xFF3A4B5C);
    vita2d_pgf_draw_text(g_font, 20, 16, 0xFFFFFFFF, 1.2f, "SETTINGS");

    draw_setting_field(0, "SERVER", 60, 82, 560);
    draw_setting_field(1, "PORT", 650, 82, 250);
    draw_setting_field(2, "USERNAME", 60, 172, 410);
    draw_setting_field(3, "WORKER", 500, 172, 400);
    draw_setting_field(4, "MINING KEY", 60, 262, 410);
    draw_setting_field(5, "DONATION ADDRESS", 500, 262, 400);

    Button btn_auto = { 60, 360, 220, 48, config_get_auto_start() ? "AUTO-START: ON" : "AUTO-START: OFF" };
    Button btn_fetch = { 60, 455, 250, 64, "FETCH POOL" };
    Button btn_save = { 355, 455, 240, 64, "SAVE" };
    Button btn_back = { 645, 455, 240, 64, "BACK" };

    if (button_update(&btn_auto, g_touch_just_pressed)) {
        config_set_auto_start(!config_get_auto_start());
        btn_auto.label = config_get_auto_start() ? "AUTO-START: ON" : "AUTO-START: OFF";
    }

    static const int field_x[] = {60, 650, 60, 500, 60, 500};
    static const int field_y[] = {106, 106, 196, 196, 286, 286};
    static const int field_w[] = {560, 250, 410, 400, 410, 400};
    if (g_touch_just_pressed) {
        for (int field = 0; field < 6; field++) {
            if (point_in_rect(g_touch_x, g_touch_y, field_x[field], field_y[field],
                              field_w[field], 34)) {
                begin_edit(field);
                break;
            }
        }
    }

    if (button_update(&btn_fetch, g_touch_just_pressed)) {
        snprintf(g_status_message, sizeof(g_status_message), "%s", "Fetching pool...");
        if (miner_fetch_pool() == 0) {
            snprintf(g_set_server, sizeof(g_set_server), "%s", miner_pool_host());
            snprintf(g_set_port, sizeof(g_set_port), "%u", miner_pool_port());
            snprintf(g_cfg.pool_host, sizeof(g_cfg.pool_host), "%s", miner_pool_host());
            g_cfg.pool_port = miner_pool_port();
            g_cfg.pool_fetched = true;
            snprintf(g_status_message, sizeof(g_status_message), "Pool: %s:%u",
                     miner_pool_host(), miner_pool_port());
        } else {
            snprintf(g_status_message, sizeof(g_status_message), "%s", "Pool fetch failed");
        }
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_save, g_touch_just_pressed)) {
        char *end;
        unsigned long parsed_port = strtoul(g_set_port, &end, 10);
        if (!g_set_server[0] || !g_set_user[0] || end == g_set_port ||
            *end != '\0' || parsed_port == 0 || parsed_port > 65535) {
            snprintf(g_status_message, sizeof(g_status_message), "%s",
                     "Server, username, or port is invalid");
        } else {
            g_cfg.auto_start = config_get_auto_start();
            snprintf(g_cfg.server, sizeof(g_cfg.server), "%s", g_set_server);
            g_cfg.port = (uint16_t)parsed_port;
            snprintf(g_cfg.username, sizeof(g_cfg.username), "%s", g_set_user);
            snprintf(g_cfg.worker, sizeof(g_cfg.worker), "%s", g_set_worker);
            snprintf(g_cfg.mining_key, sizeof(g_cfg.mining_key), "%s", g_set_key);
            snprintf(g_cfg.donation_addr, sizeof(g_cfg.donation_addr), "%s", g_set_donation);

            if (config_save(&g_cfg) < 0) {
                snprintf(g_status_message, sizeof(g_status_message), "%s",
                         "Could not save settings");
            } else {
                miner_set_server(g_cfg.server, g_cfg.port);
                miner_set_username(g_cfg.username);
                miner_set_worker(g_cfg.worker);
                miner_set_mining_key(g_cfg.mining_key);
                miner_set_auto_start(g_cfg.auto_start);
                ui_log("Settings saved");
                g_status_message[0] = '\0';
                g_screen = SCREEN_DASHBOARD;
            }
        }
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    if (button_update(&btn_back, g_touch_just_pressed)) {
        g_screen = SCREEN_DASHBOARD;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }

    if (g_status_message[0]) {
        vita2d_pgf_draw_text(g_font, 60, 420, 0xFFFF8080, 0.75f, g_status_message);
    }
    draw_button(&btn_auto);
    draw_button(&btn_fetch);
    draw_button(&btn_save);
    draw_button(&btn_back);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void render_about(void) {
    vita2d_start_drawing();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, 0xFF0E1420);

    draw_panel(20, 72, 920, 388, "ABOUT");
    vita2d_pgf_draw_text(g_font, 80, 130, 0xFFFFFFFF, 1.2f, "VitaDucoMiner");
    vita2d_pgf_draw_text(g_font, 80, 170, 0xFFC0C0C0, 0.9f, "DuinoCoin Miner for PSVita");
    vita2d_pgf_draw_text(g_font, 80, 200, 0xFF80B3D6, 0.7f, "GUI: vita2d  SDL-free  VitaSDK");
    vita2d_pgf_draw_text(g_font, 80, 220, 0xFF80B3D6, 0.7f, "Algorithm: DUCO-S1 (SHA-1)");
    vita2d_pgf_draw_text(g_font, 80, 240, 0xFF808080, 0.7f, "Unofficial DuinoCoin miner");

    Button btn_back = { 360, 380, 240, 64, "BACK" };
    if (button_update(&btn_back, g_touch_just_pressed)) {
        g_screen = SCREEN_DASHBOARD;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    draw_button(&btn_back);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void render_donation(void) {
    vita2d_start_drawing();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, 0xFF0E1420);

    draw_panel(20, 72, 920, 388, "DONATE");
    vita2d_pgf_draw_text(g_font, 80, 130, 0xFFFFFFFF, 1.1f, "Support VitaDucoMiner");
    vita2d_pgf_draw_text(g_font, 80, 170, 0xFFC0C0C0, 0.8f, "DuinoCoin Address:");
    vita2d_pgf_draw_text(g_font, 80, 200, 0xFF00FF80, 0.9f, g_set_donation[0] ? g_set_donation : "(none set)");
    vita2d_pgf_draw_text(g_font, 80, 250, 0xFF808080, 0.7f, "Set donation address in Settings");

    Button btn_back = { 360, 380, 240, 64, "BACK" };
    if (button_update(&btn_back, g_touch_just_pressed)) {
        g_screen = SCREEN_DASHBOARD;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    draw_button(&btn_back);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void render_log(void) {
    vita2d_start_drawing();
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, 0xFF0E1420);

    draw_panel(20, 72, 920, 388, "ACTIVITY LOG");
    int cnt = g_log_count;
    if (cnt == 0) {
        vita2d_pgf_draw_text(g_font, 80, 140, 0xFF808080, 0.8f, "(no log entries)");
    }
    for (int i = 0; i < cnt && i < 10; i++) {
        vita2d_pgf_draw_text(g_font, 80, 140 + i * 28, 0xFFC0C0C0, 0.7f, g_log_lines[i]);
    }

    Button btn_back = { 360, 380, 240, 64, "BACK" };
    if (button_update(&btn_back, g_touch_just_pressed)) {
        g_screen = SCREEN_DASHBOARD;
        g_button_cooldown = sceKernelGetSystemTimeWide();
    }
    draw_button(&btn_back);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    int ime_module_ret = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    if (ime_module_ret < 0) {
        ui_log("Vita keyboard unavailable (%08X)", ime_module_ret);
    } else {
        g_ime_available = true;
    }

    Config cfg;
    if (config_load(&cfg) < 0) {
        config_set_defaults(&cfg);
    }
    memcpy(&g_cfg, &cfg, sizeof(Config));

    if (net_init() < 0) {
        ui_log("net_init failed");
    }

    if (vita2d_init() < 0) {
        net_close();
        sceKernelExitProcess(1);
    }

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    g_font = vita2d_load_default_pgf();
    if (!g_font) {
        vita2d_fini();
        net_close();
        sceKernelExitProcess(1);
        return 1;
    }

    if (miner_start() != 0) {
        ui_log("Could not start miner thread");
    }

    miner_set_server(g_cfg.server, g_cfg.port);
    miner_set_username(g_cfg.username);
    miner_set_worker(g_cfg.worker);
    miner_set_mining_key(g_cfg.mining_key);
    miner_set_auto_start(g_cfg.auto_start);

    bool running = true;
    const char *ip = net_local_ip();
    snprintf(g_ip, sizeof(g_ip), "%s", ip ? ip : "0.0.0.0");

    g_last_stats = sceKernelGetSystemTimeWide();

    if (g_cfg.auto_start) {
        miner_toggle_mining();
        ui_log("Auto-start mining");
    }

    while (running) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);

        bool cross = btn_pressed(pad.buttons, g_prev_cross, SCE_CTRL_CROSS);
        bool circle = btn_pressed(pad.buttons, g_prev_circle, SCE_CTRL_CIRCLE);
        bool square = btn_pressed(pad.buttons, g_prev_square, SCE_CTRL_SQUARE);
        bool up = btn_pressed(pad.buttons, g_prev_up, SCE_CTRL_UP);
        bool down = btn_pressed(pad.buttons, g_prev_down, SCE_CTRL_DOWN);
        bool left = btn_pressed(pad.buttons, g_prev_left, SCE_CTRL_LEFT);
        bool right = btn_pressed(pad.buttons, g_prev_right, SCE_CTRL_RIGHT);

        g_prev_cross = pad.buttons & SCE_CTRL_CROSS;
        g_prev_circle = pad.buttons & SCE_CTRL_CIRCLE;
        g_prev_square = pad.buttons & SCE_CTRL_SQUARE;
        g_prev_up = pad.buttons & SCE_CTRL_UP;
        g_prev_down = pad.buttons & SCE_CTRL_DOWN;
        g_prev_left = pad.buttons & SCE_CTRL_LEFT;
        g_prev_right = pad.buttons & SCE_CTRL_RIGHT;

        update_touch();

        uint64_t now = sceKernelGetSystemTimeWide();
        bool cooldown_active = (now - g_button_cooldown) < 150000ull;

        Miner stats;
        miner_get_stats(&stats);
        Miner *m = &stats;
        if (!cooldown_active && now - g_last_stats >= 500000ull) {
            g_last_stats = now;
            g_cached_hashrate = m->hashrate;
            const char *ip2 = net_local_ip();
            snprintf(g_ip, sizeof(g_ip), "%s", ip2 ? ip2 : "0.0.0.0");
        }

        switch (g_screen) {
            case SCREEN_DASHBOARD:
                render_dashboard();
                if (!cooldown_active && square) {
                    g_screen = SCREEN_SETTINGS;
                    g_sel = 0;
                    g_button_cooldown = now;
                } else if (!cooldown_active && cross) {
                    miner_toggle_mining();
                    Miner updated;
                    miner_get_stats(&updated);
                    ui_log("%s", updated.mining ? "Mining started" : "Mining stopped");
                    g_button_cooldown = now;
                } else if (!cooldown_active && circle) {
                    running = false;
                }
                break;

            case SCREEN_SETTINGS:
                render_settings();
                if (!cooldown_active && circle) {
                    g_screen = SCREEN_DASHBOARD;
                    g_button_cooldown = now;
                } else if (!cooldown_active && up) {
                    g_sel = (g_sel > 0) ? g_sel - 1 : 6;
                    g_button_cooldown = now;
                } else if (!cooldown_active && down) {
                    g_sel = (g_sel < 6) ? g_sel + 1 : 0;
                    g_button_cooldown = now;
                } else if (!cooldown_active && cross) {
                    if (g_sel < 6) {
                        begin_edit(g_sel);
                    } else {
                        config_set_auto_start(!config_get_auto_start());
                    }
                    g_button_cooldown = now;
                }
                break;

            case SCREEN_SETTINGS_EDIT:
                render_edit();
                if (!cooldown_active && left) {
                    move_edit_cursor(-1);
                    g_button_cooldown = now;
                } else if (!cooldown_active && right) {
                    move_edit_cursor(1);
                    g_button_cooldown = now;
                } else if (!cooldown_active && up) {
                    cycle_edit_character(1);
                    g_button_cooldown = now;
                } else if (!cooldown_active && down) {
                    cycle_edit_character(-1);
                    g_button_cooldown = now;
                } else if (!cooldown_active && pad.buttons & SCE_CTRL_TRIANGLE) {
                    delete_edit_character();
                    g_button_cooldown = now;
                } else if (!cooldown_active && square) {
                    g_open_keyboard = true;
                    g_button_cooldown = now;
                } else if (!cooldown_active && cross) {
                    config_commit_field(g_edit_field, g_text_in);
                    g_screen = SCREEN_SETTINGS;
                    g_button_cooldown = now;
                } else if (!cooldown_active && circle) {
                    g_screen = SCREEN_SETTINGS;
                    g_button_cooldown = now;
                }
                break;

            case SCREEN_ABOUT:
                render_about();
                if (!cooldown_active && (circle || cross)) {
                    g_screen = SCREEN_DASHBOARD;
                    g_button_cooldown = now;
                }
                break;

            case SCREEN_DONATION:
                render_donation();
                if (!cooldown_active && (circle || cross)) {
                    g_screen = SCREEN_DASHBOARD;
                    g_button_cooldown = now;
                }
                break;

            case SCREEN_LOG:
                render_log();
                if (!cooldown_active && (circle || cross)) {
                    g_screen = SCREEN_DASHBOARD;
                    g_button_cooldown = now;
                }
                break;
        }

        if (g_open_keyboard) {
            g_open_keyboard = false;
            if (!g_ime_available) {
                snprintf(g_status_message, sizeof(g_status_message), "%s",
                         "Vita keyboard is unavailable");
            } else {
                int keyboard_ret = open_system_keyboard();
                if (keyboard_ret < 0) {
                snprintf(g_status_message, sizeof(g_status_message),
                         "Vita keyboard failed (%08X)", keyboard_ret);
                ui_log("%s", g_status_message);
                }
            }
        }
        if (g_quit_requested) running = false;
        sceKernelDelayThread(16000);
    }

    miner_stop();
    miner_join();
    if (g_font) vita2d_free_pgf(g_font);
    vita2d_fini();
    net_close();
    if (g_ime_available) sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
    sceKernelExitProcess(0);
    return 0;
}
