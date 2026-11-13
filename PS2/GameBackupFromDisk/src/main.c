#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/stat.h>
#include <tamtypes.h>
#include <libcdvd.h>
#include <libcdvd-common.h>
#include <graph.h>
#include <draw.h>
#include <draw2d.h>
#include <font.h>
#include <libpad.h>

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 448
#define DMA_BUFFER_SIZE 4096
#define CHUNK_SECTORS   32
#define MB              (1024 * 1024)

#define MENU_BACKUP  0
#define MENU_EXIT    1
#define MENU_COUNT   2

#define STATE_MENU       0
#define STATE_BACKUP     1
#define STATE_COMPLETE   2
#define STATE_ERROR      3

static framebuffer_t framebuffer;
static zbuffer_t zbuffer;
static fontx_t fontx;
static qword_t dma_buffer[DMA_BUFFER_SIZE] __attribute__((aligned(64)));
static u8 read_buf[CHUNK_SECTORS * 2352] __attribute__((aligned(64)));
static struct padButtonStatus pad_data __attribute__((aligned(64)));

static int state = STATE_MENU;
static int selected_menu = 0;
static int disc_type = 0;
static int sector_size = 0;
static const char *ext = NULL;
static char game_id[32] = {0};
static char out_path[256] = {0};
static u32 total_written = 0;
static u32 sector = 0;
static int out_fd = -1;
static int backup_cancelled = 0;
static sceCdRMode read_mode;

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

static void render_frame(qword_t **q)
{
    *q = draw_setup_environment(*q, 0, &framebuffer, &zbuffer);
    *q = draw_clear(*q, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 20, 30, 50);

    draw_fill_rect(0, 0, SCREEN_WIDTH, 64, 40, 60, 100, q);
    draw_string(20, 18, "GameBackupFromDisk", 255, 255, 255, q);
    draw_string(20, 42, "PS1 / PS2 Disc Backup", 180, 210, 255, q);

    if (state == STATE_MENU) {
        draw_string(20, 120, "Select an option:", 255, 255, 255, q);
        const char *items[MENU_COUNT] = { "Backup Disc", "Exit" };
        for (int i = 0; i < MENU_COUNT; i++) {
            if (i == selected_menu) {
                draw_fill_rect(20, 150 + i * 48, 220, 36, 80, 120, 180, q);
                draw_string(40, 160, items[i], 255, 255, 255, q);
            } else {
                draw_string(40, 160, items[i], 200, 200, 200, q);
            }
        }
        draw_string(20, 400, "UP/DOWN: Navigate  CROSS: Select", 150, 150, 150, q);
    } else if (state == STATE_BACKUP) {
        draw_string(20, 120, "Backing up disc...", 255, 255, 255, q);
        int percent = 0;
        if (total_written > 0) {
            percent = (int)(((total_written / MB) * 100) / 700);
            if (percent > 100) percent = 100;
        }
        draw_progress(20, 160, 600, 36, percent, q);
        char buf[256];
        snprintf(buf, sizeof(buf), "Written: %u MB   Sector: %u", total_written / MB, sector);
        draw_string(20, 220, buf, 255, 255, 255, q);
        draw_string(20, 380, "Press CIRCLE to cancel", 150, 150, 150, q);
    } else if (state == STATE_COMPLETE) {
        draw_string(20, 120, "Backup complete!", 0, 255, 0, q);
        char buf[256];
        snprintf(buf, sizeof(buf), "Saved: %s", out_path);
        draw_string(20, 160, buf, 255, 255, 255, q);
        snprintf(buf, sizeof(buf), "Size: %u MB", total_written / MB);
        draw_string(20, 190, buf, 255, 255, 255, q);
        draw_string(20, 380, "Press CIRCLE to return", 150, 150, 150, q);
    } else if (state == STATE_ERROR) {
        draw_string(20, 120, "Backup failed!", 255, 0, 0, q);
        draw_string(20, 380, "Press CIRCLE to return", 150, 150, 150, q);
    }

    *q = draw_finish(*q);
    draw_wait_finish();
}

static int read_game_id(void)
{
    game_id[0] = '\0';
    sceCdlFILE sf;
    if (sceCdSearchFile(&sf, "\\SYSTEM.CNF;1") != 1) return 0;
    static u8 buf[2048] __attribute__((aligned(64)));
    if (sceCdRead(sf.lsn, (sf.size + 2047) / 2048, buf, &read_mode) != 1) return 0;
    if (sceCdSync(0) != 0) return 0;
    const char *ptr = (const char *)buf;
    const char *end = ptr + sf.size;
    while (ptr < end) {
        if (strncmp(ptr, "BOOT2", 5) == 0) {
            while (ptr < end && *ptr != '=') ptr++;
            if (ptr >= end) return 0;
            ptr++;
            while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ptr++;
            while (ptr < end && *ptr != '\\' && *ptr != ':') ptr++;
            if (ptr >= end) return 0;
            if (*ptr == '\\') ptr++;
            const char *start = ptr;
            while (ptr < end && *ptr != ';' && *ptr != '\r' && *ptr != '\n' && *ptr != ' ') ptr++;
            int len = (int)(ptr - start);
            if (len > 0 && len < (int)sizeof(game_id)) {
                memcpy(game_id, start, (size_t)len);
                game_id[len] = '\0';
            }
            return 1;
        }
        while (ptr < end && *ptr != '\n') ptr++;
        if (ptr < end) ptr++;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    qword_t *q;
    int running = 1;
    int last_buttons = 0;

    graph_initialize(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0);
    graph_enable_output();
    graph_set_bgcolor(20, 30, 50);

    memset(&framebuffer, 0, sizeof(framebuffer));
    framebuffer.address = 0;
    framebuffer.width = SCREEN_WIDTH;
    framebuffer.height = SCREEN_HEIGHT;
    framebuffer.psm = 0;

    memset(&zbuffer, 0, sizeof(zbuffer));
    zbuffer.enable = 0;

    fontx_load("rom0:KROM", &fontx, SINGLE_BYTE, 0, 0, 0);

    padInit(0);
    padPortOpen(0, 0, &pad_data);

    sceCdInit(SCECdINIT);

    read_mode.trycount = 20;
    read_mode.spindlctrl = SCECdSpinNom;
    read_mode.pad = 0;

    while (running) {
        graph_wait_vsync();
        q = dma_buffer;
        render_frame(&q);

        padRead(0, 0, &pad_data);
        unsigned short buttons = pad_data.btns;
        unsigned short pressed = buttons & ~last_buttons;
        last_buttons = buttons;

        if (state == STATE_MENU) {
            if (pressed & PAD_UP) {
                selected_menu = (selected_menu + MENU_COUNT - 1) % MENU_COUNT;
            }
            if (pressed & PAD_DOWN) {
                selected_menu = (selected_menu + 1) % MENU_COUNT;
            }
            if (pressed & PAD_CROSS) {
                if (selected_menu == MENU_EXIT) {
                    running = 0;
                } else {
                    disc_type = sceCdGetDiskType();
                    if (disc_type == SCECdNODISC || disc_type == SCECdGDTFUNCFAIL || disc_type == SCECdUNKNOWN) {
                        state = STATE_ERROR;
                    } else {
                        if (disc_type == SCECdPSCD || disc_type == SCECdPSCDDA || disc_type == SCECdPS2CD || disc_type == SCECdPS2CDDA || disc_type == SCECdCDDA) {
                            sector_size = 2352;
                            ext = ".bin";
                        } else {
                            sector_size = 2048;
                            ext = ".iso";
                        }
                        read_mode.datapattern = (sector_size == 2352) ? SCECdSecS2352 : SCECdSecS2048;

                        read_game_id();
                        if (disc_type == SCECdPSCD || disc_type == SCECdPSCDDA || disc_type == SCECdPS2CD || disc_type == SCECdPS2CDDA || disc_type == SCECdCDDA) {
                            snprintf(out_path, sizeof(out_path), "mass:/CD/%s%s", game_id[0] ? game_id : "GAMEBACKUP", ext);
                        } else {
                            snprintf(out_path, sizeof(out_path), "mass:/DVD/%s%s", game_id[0] ? game_id : "GAMEBACKUP", ext);
                        }
                        mkdir("mass:/CD", 0777);
                        mkdir("mass:/DVD", 0777);

                        state = STATE_BACKUP;
                        total_written = 0;
                        sector = 0;
                        backup_cancelled = 0;
                        out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (out_fd < 0) {
                            state = STATE_ERROR;
                        }
                    }
                }
            }
        } else if (state == STATE_BACKUP) {
            if (pressed & PAD_CIRCLE) {
                backup_cancelled = 1;
            }
            if (!backup_cancelled && out_fd >= 0) {
                if (sceCdRead(sector, CHUNK_SECTORS, read_buf, &read_mode) == 1) {
                    if (sceCdSync(0) == 0) {
                        int written = write(out_fd, read_buf, (size_t)CHUNK_SECTORS * sector_size);
                        if (written < 0) {
                            close(out_fd);
                            out_fd = -1;
                            state = STATE_ERROR;
                        } else {
                            total_written += (u32)written;
                            sector += CHUNK_SECTORS;
                        }
                    } else {
                        close(out_fd);
                        out_fd = -1;
                        state = STATE_ERROR;
                    }
                } else {
                    close(out_fd);
                    out_fd = -1;
                    if (backup_cancelled || total_written == 0) {
                        state = STATE_MENU;
                    } else {
                        state = STATE_COMPLETE;
                    }
                }
            } else if (backup_cancelled) {
                if (out_fd >= 0) close(out_fd);
                out_fd = -1;
                state = STATE_MENU;
            }
        } else if (state == STATE_COMPLETE || state == STATE_ERROR) {
            if (pressed & PAD_CIRCLE) {
                state = STATE_MENU;
            }
        }
    }

    if (out_fd >= 0) close(out_fd);
    padPortClose(0, 0);
    padEnd();
    fontx_unload(&fontx);
    graph_shutdown();

    return 0;
}
