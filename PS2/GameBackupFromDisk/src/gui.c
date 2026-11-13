#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "gui.h"
#include "input.h"

static framebuffer_t framebuffer;
static zbuffer_t zbuffer;
static fontx_t fontx;
static int fbp = -1;

int gui_state = STATE_MENU;
int gui_selected = 0;
int gui_backup_options = BACKUP_OPT_MD5 | BACKUP_OPT_CUE;
char gui_save_path[256] = "mass:/DVD";
disc_info_t gui_disc_info;

static char file_browser_path[256] = "mass:/";
static int file_browser_selected = 0;
static int file_browser_scroll = 0;
#define FILE_BROWSER_MAX_VISIBLE 12

static char dir_entries[64][256];
static int dir_entry_count = 0;
static int dir_entry_is_dir[64];

static void make_color(color_t *c, unsigned char r, unsigned char g, unsigned char b)
{
    c->r = r;
    c->g = g;
    c->b = b;
    c->a = 255;
    c->q = 0.0f;
}

static void draw_string(int x, int y, const char *text, unsigned char r, unsigned char g, unsigned char b, qword_t **q)
{
    vertex_t pos = { (float)x, (float)y, 0 };
    color_t color;
    make_color(&color, r, g, b);
    *q = fontx_print_ascii(*q, 0, (const unsigned char *)text, LEFT_ALIGN, &pos, &color, &fontx);
}

static void draw_fill_rect(float x, float y, float w, float h, unsigned char r, unsigned char g, unsigned char b, qword_t **q)
{
    rect_t rect;
    rect.v0.x = x;
    rect.v0.y = y;
    rect.v0.z = 0;
    rect.v1.x = x + w;
    rect.v1.y = y + h;
    rect.v1.z = 0;
    make_color(&rect.color, r, g, b);
    *q = draw_rect_filled(*q, 0, &rect);
}

static void draw_progress(int x, int y, int w, int h, int percent, qword_t **q)
{
    draw_fill_rect(x, y, w, h, 80, 80, 80, q);
    if (percent > 0) {
        int fill = (w * percent) / 100;
        if (fill > 2) {
            draw_fill_rect(x + 2, y + 2, fill - 4, h - 4, 0, 200, 0, q);
        }
    }
}

static void scan_directory(const char *path)
{
    dir_entry_count = 0;
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && dir_entry_count < 64) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        strncpy(dir_entries[dir_entry_count], entry->d_name, 255);
        dir_entries[dir_entry_count][255] = '\0';

        char full[512];
        snprintf(full, sizeof(full), "%s%s", path, entry->d_name);
        struct stat st;
        dir_entry_is_dir[dir_entry_count] = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        dir_entry_count++;
    }
    closedir(dir);
}

void gui_init(void)
{
    fbp = graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE);
    graph_initialize(fbp, SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32, 0, 0);
    graph_enable_output();
    graph_set_bgcolor(20, 30, 50);

    memset(&framebuffer, 0, sizeof(framebuffer));
    framebuffer.address = fbp;
    framebuffer.width = SCREEN_WIDTH;
    framebuffer.height = SCREEN_HEIGHT;
    framebuffer.psm = GS_PSM_32;
    framebuffer.mask = 0;

    graph_set_framebuffer(0, fbp, SCREEN_WIDTH, GS_PSM_32, 0, 0);

    memset(&zbuffer, 0, sizeof(zbuffer));
    zbuffer.enable = 0;

    fontx_load("rom0:KROM", &fontx, SINGLE_BYTE, 0, 0, 0);

    scan_directory(file_browser_path);
}

void gui_shutdown(void)
{
    if (fbp >= 0) graph_vram_free(fbp);
    fontx_unload(&fontx);
    graph_shutdown();
}

void gui_render(qword_t *q)
{
    q = draw_setup_environment(q, 0, &framebuffer, &zbuffer);
    q = draw_framebuffer(q, 0, &framebuffer);
    q = draw_clear(q, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 20, 30, 50);

    draw_fill_rect(0, 0, SCREEN_WIDTH, 64, 40, 60, 100, &q);
    draw_string(20, 18, "GameBackupFromDisk", 255, 255, 255, &q);
    draw_string(20, 42, "PS1 / PS2 Disc Backup", 180, 210, 255, &q);

    if (gui_state == STATE_MENU) {
        draw_string(20, 100, "Main Menu", 255, 255, 255, &q);
        const char *items[3] = { "Backup Disc", "Browse Save Location", "Exit" };
        for (int i = 0; i < 3; i++) {
            if (i == gui_selected) {
                draw_fill_rect(20, 130 + i * 44, 260, 36, 80, 120, 180, &q);
                draw_string(40, 140, items[i], 255, 255, 255, &q);
            } else {
                draw_string(40, 140, items[i], 200, 200, 200, &q);
            }
        }
        draw_string(20, 390, "UP/DOWN: Navigate  CROSS: Select  CIRCLE: Back", 150, 150, 150, &q);
    }
    else if (gui_state == STATE_FILE_BROWSER) {
        draw_string(20, 100, "Save Location:", 255, 255, 255, &q);
        draw_string(20, 120, file_browser_path, 200, 220, 255, &q);

        int start = file_browser_scroll;
        int end = start + FILE_BROWSER_MAX_VISIBLE;
        if (end > dir_entry_count) end = dir_entry_count;

        for (int i = start; i < end; i++) {
            int y = 150 + (i - start) * 28;
            if (i == file_browser_selected) {
                draw_fill_rect(20, y - 4, 600, 24, 80, 120, 180, &q);
                draw_string(40, y, dir_entry_is_dir[i] ? "[DIR] " : "     ", 255, 255, 0, &q);
                draw_string(90, y, dir_entries[i], 255, 255, 255, &q);
            } else {
                draw_string(40, y, dir_entry_is_dir[i] ? "[DIR] " : "     ", 100, 100, 100, &q);
                draw_string(90, y, dir_entries[i], 180, 180, 180, &q);
            }
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "Entries: %d  Selected: %d", dir_entry_count, file_browser_selected + 1);
        draw_string(20, 390, buf, 150, 150, 150, &q);
        draw_string(20, 410, "UP/DOWN: Navigate  CROSS: Enter/Select  CIRCLE: Back", 150, 150, 150, &q);
    }
    else if (gui_state == STATE_DISC_INFO) {
        draw_string(20, 100, "Disc Information", 255, 255, 255, &q);

        char type_buf[64];
        backup_get_disc_type_name(gui_disc_info.media_type, type_buf, sizeof(type_buf));
        draw_string(20, 130, "Media Type:", 200, 200, 200, &q);
        draw_string(130, 130, type_buf, 255, 255, 255, &q);

        char sector_buf[64];
        snprintf(sector_buf, sizeof(sector_buf), "%u bytes", gui_disc_info.sector_size);
        draw_string(20, 160, "Sector Size:", 200, 200, 200, &q);
        draw_string(130, 160, sector_buf, 255, 255, 255, &q);

        char est_buf[64];
        snprintf(est_buf, sizeof(est_buf), "~%u MB", gui_disc_info.estimated_sectors * gui_disc_info.sector_size / MB);
        draw_string(20, 190, "Estimated Size:", 200, 200, 200, &q);
        draw_string(130, 190, est_buf, 255, 255, 255, &q);

        char sect_buf[64];
        snprintf(sect_buf, sizeof(sect_buf), "%u sectors", gui_disc_info.estimated_sectors);
        draw_string(20, 220, "Estimated Sectors:", 200, 200, 200, &q);
        draw_string(130, 220, sect_buf, 255, 255, 255, &q);

        draw_string(20, 390, "CROSS: Continue Backup  CIRCLE: Back", 150, 150, 150, &q);
    }
    else if (gui_state == STATE_BACKUP) {
        draw_string(20, 100, "Backing up disc...", 255, 255, 255, &q);
        int percent = 0;
        if (g_backup.total_written > 0 && gui_disc_info.estimated_sectors > 0) {
            percent = (int)((g_backup.sector * 100) / gui_disc_info.estimated_sectors);
            if (percent > 100) percent = 100;
        }
        draw_progress(20, 140, 600, 36, percent, &q);

        char buf[256];
        snprintf(buf, sizeof(buf), "Written: %u MB   Sector: %u",
                 g_backup.total_written / MB, g_backup.sector);
        draw_string(20, 200, buf, 255, 255, 255, &q);
        draw_string(20, 240, "Path: ", 200, 200, 200, &q);
        draw_string(70, 240, g_backup.out_path, 255, 255, 0, &q);
        draw_string(20, 390, "CIRCLE: Cancel backup", 150, 150, 150, &q);
    }
    else if (gui_state == STATE_COMPLETE) {
        draw_string(20, 100, "Backup Complete!", 0, 255, 0, &q);
        char buf[256];
        snprintf(buf, sizeof(buf), "Saved: %s", g_backup.out_path);
        draw_string(20, 140, buf, 255, 255, 255, &q);
        snprintf(buf, sizeof(buf), "Size: %u MB   Sectors: %u",
                 g_backup.total_written / MB, g_backup.sector);
        draw_string(20, 170, buf, 255, 255, 255, &q);
        draw_string(20, 390, "CIRCLE: Return to menu", 150, 150, 150, &q);
    }
    else if (gui_state == STATE_ERROR) {
        draw_string(20, 100, "Backup Failed!", 255, 0, 0, &q);
        draw_string(20, 390, "CIRCLE: Return to menu", 150, 150, 150, &q);
    }

    q = draw_finish(q);
    draw_wait_finish();
}

void gui_file_browser_input(int pressed)
{
    if (pressed & PAD_UP) {
        if (file_browser_selected > 0) {
            file_browser_selected--;
            if (file_browser_selected < file_browser_scroll)
                file_browser_scroll = file_browser_selected;
        }
    }
    if (pressed & PAD_DOWN) {
        if (file_browser_selected < dir_entry_count - 1) {
            file_browser_selected++;
            if (file_browser_selected >= file_browser_scroll + FILE_BROWSER_MAX_VISIBLE)
                file_browser_scroll = file_browser_selected - FILE_BROWSER_MAX_VISIBLE + 1;
        }
    }
    if (pressed & PAD_CROSS) {
        if (file_browser_selected >= 0 && file_browser_selected < dir_entry_count) {
            if (dir_entry_is_dir[file_browser_selected]) {
                char new_path[512];
                snprintf(new_path, sizeof(new_path), "%s%s/", file_browser_path,
                         dir_entries[file_browser_selected]);
                strncpy(file_browser_path, new_path, sizeof(file_browser_path) - 1);
                file_browser_selected = 0;
                file_browser_scroll = 0;
                scan_directory(file_browser_path);
            } else {
                /* Select file as save path */
                snprintf(gui_save_path, sizeof(gui_save_path), "%s%s", file_browser_path,
                         dir_entries[file_browser_selected]);
                gui_state = STATE_MENU;
            }
        }
    }
    if (pressed & PAD_CIRCLE) {
        /* Go up one directory */
        int len = strlen(file_browser_path);
        while (len > 1 && file_browser_path[len - 1] == '/') len--;
        while (len > 0 && file_browser_path[len - 1] != '/') len--;
        if (len > 0) {
            file_browser_path[len] = '\0';
            file_browser_selected = 0;
            file_browser_scroll = 0;
            scan_directory(file_browser_path);
        }
    }
}

void gui_main_menu_input(int pressed)
{
    if (pressed & PAD_UP) {
        gui_selected = (gui_selected + 2) % 3;
    }
    if (pressed & PAD_DOWN) {
        gui_selected = (gui_selected + 1) % 3;
    }
    if (pressed & PAD_CROSS) {
        if (gui_selected == 0) {
            gui_state = STATE_DISC_INFO;
        } else if (gui_selected == 1) {
            gui_state = STATE_FILE_BROWSER;
        } else {
            gui_state = -1; /* exit */
        }
    }
}

void gui_disc_info_input(int pressed)
{
    if (pressed & PAD_CROSS) {
        gui_state = STATE_BACKUP;
    }
    if (pressed & PAD_CIRCLE) {
        gui_state = STATE_MENU;
    }
}
