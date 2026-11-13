#ifndef GUI_H
#define GUI_H

#include <tamtypes.h>
#include <graph.h>
#include <draw.h>
#include <font.h>
#include "backup.h"

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 448

#define STATE_MENU         0
#define STATE_FILE_BROWSER 1
#define STATE_DISC_INFO    2
#define STATE_BACKUP       3
#define STATE_COMPLETE     4
#define STATE_ERROR        5

#define OPT_MD5      0x01
#define OPT_CUE      0x02
#define OPT_VMC      0x04
#define OPT_PATCH    0x08
#define OPT_ZSO      0x10

extern int gui_state;
extern int gui_selected;
extern int gui_backup_options;
extern char gui_save_path[256];
extern char gui_game_id[32];
extern disc_info_t gui_disc_info;

void gui_init(void);
void gui_render(qword_t *q);
void gui_shutdown(void);
void gui_file_browser_input(int pressed);
void gui_main_menu_input(int pressed);
void gui_disc_info_input(int pressed);

#endif
