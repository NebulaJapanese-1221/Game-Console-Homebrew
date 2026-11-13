#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <tamtypes.h>
#include <libcdvd.h>
#include <libcdvd-common.h>
#include "gui.h"
#include "backup.h"
#include "input.h"

static char game_id[32] = {0};
static char out_path[256] = {0};

static int read_game_id(void)
{
    game_id[0] = '\0';
    sceCdlFILE sf;
    if (sceCdSearchFile(&sf, "\\SYSTEM.CNF;1") != 1) return 0;
    static u8 buf[2048] __attribute__((aligned(64)));
    static sceCdRMode mode;
    mode.trycount = 20;
    mode.spindlctrl = SCECdSpinNom;
    mode.datapattern = SCECdSecS2048;
    mode.pad = 0;
    if (sceCdRead(sf.lsn, (sf.size + 2047) / 2048, buf, &mode) != 1) return 0;
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

static void build_out_path(void)
{
    const char *ext = gui_disc_info.ext;
    const char *folder = gui_disc_info.folder;

    char base[256];
    snprintf(base, sizeof(base), "mass:/%s", folder);
    mkdir(base, 0777);

    const char *name = (game_id[0] != '\0') ? game_id : "GAMEBACKUP";
    snprintf(out_path, sizeof(out_path), "mass:/%s/%s%s",
             folder, name, ext);
}

int main(int argc, char *argv[])
{
    qword_t dma_buffer[4096] __attribute__((aligned(64)));
    qword_t *q;
    int running = 1;

    gui_init();
    input_init();
    sceCdInit(SCECdINIT);

    while (running) {
        graph_wait_vsync();
        q = dma_buffer;
        gui_render(q);

        int pressed = input_update();

        if (gui_state == STATE_MENU) {
            gui_main_menu_input(pressed);
            if (gui_state < 0) running = 0;
        }
        else if (gui_state == STATE_FILE_BROWSER) {
            gui_file_browser_input(pressed);
        }
        else if (gui_state == STATE_DISC_INFO) {
            gui_disc_info_input(pressed);
        }
        else if (gui_state == STATE_BACKUP) {
            static int backup_started = 0;
            if (!backup_started) {
                read_game_id();
                build_out_path();
                int options = gui_backup_options;
                if (gui_disc_info.media_type == DISC_TYPE_PS1_CD) {
                    options |= BACKUP_OPT_CUE;
                }
                if (backup_init(out_path, options, &gui_disc_info, game_id) != 0) {
                    gui_state = STATE_ERROR;
                    backup_started = 0;
                } else {
                    backup_started = 1;
                }
            } else {
                int step = backup_step();
                if (step <= 0) {
                    int st = backup_status();
                    backup_started = 0;
                    if (st == BACKUP_STATUS_DONE) {
                        if (g_backup.options & BACKUP_OPT_MD5) {
                            backup_write_md5(out_path);
                        }
                        if (g_backup.options & BACKUP_OPT_CUE) {
                            backup_write_cue(out_path, g_backup.sector);
                        }
                        if (g_backup.options & BACKUP_OPT_PATCH_CNF) {
                            backup_patch_system_cnf("mass:/CD", game_id);
                        }
                        gui_state = STATE_COMPLETE;
                    } else if (st == BACKUP_STATUS_ERROR) {
                        gui_state = STATE_ERROR;
                    } else {
                        gui_state = STATE_MENU;
                    }
                }
            }
        }
        else if (gui_state == STATE_COMPLETE || gui_state == STATE_ERROR) {
            if (pressed & PAD_CIRCLE) {
                gui_state = STATE_MENU;
            }
        }
    }

    input_shutdown();
    gui_shutdown();
    return 0;
}
