#ifndef BACKUP_H
#define BACKUP_H

#include <tamtypes.h>
#include <libcdvd.h>
#include <libcdvd-common.h>
#include "md5.h"

#define CHUNK_SECTORS   32
#define MB              (1024 * 1024)

/* Disc media types */
#define DISC_TYPE_PS1_CD      0
#define DISC_TYPE_PS2_CD      1
#define DISC_TYPE_PS2_DVD     2
#define DISC_TYPE_DVD_VIDEO   3
#define DISC_TYPE_UNKNOWN     4
#define DISC_TYPE_NO_DISC     5

/* Backup format options (bitmask) */
#define BACKUP_OPT_NONE          0x00
#define BACKUP_OPT_MD5           0x01   /* write .md5 checksum file */
#define BACKUP_OPT_CUE           0x02   /* write .cue file for PS1 CD */
#define BACKUP_OPT_VMC           0x04   /* wrap output in VMC container */
#define BACKUP_OPT_PATCH_CNF     0x08   /* patch SYSTEM.CNF BOOT2 for OPL */
#define BACKUP_OPT_ZSO           0x10   /* ZSO container (stored blocks) */

/* Disc information */
typedef struct {
    int    media_type;       /* one of DISC_TYPE_* */
    int    sector_size;      /* 2048 or 2352 */
    u32    estimated_sectors;/* estimated total sectors on disc */
    const char *type_name;   /* human readable name */
    const char *ext;         /* default extension (.iso / .bin) */
    const char *folder;      /* OPL folder (CD / DVD) */
} disc_info_t;

/* Backup result status */
#define BACKUP_STATUS_IDLE       0
#define BACKUP_STATUS_IN_PROGRESS 1
#define BACKUP_STATUS_DONE       2
#define BACKUP_STATUS_ERROR      3
#define BACKUP_STATUS_CANCELLED  4

/* Backup context */
typedef struct {
    char         out_path[256];
    int          options;
    int          sector_size;
    u32          total_written;
    u32          sector;
    int          status;
    int          fd;
    md5_ctx_t    md5_ctx;
    int          md5_initialized;
    char         game_id[32];
    disc_info_t  disc;
} backup_ctx_t;

extern backup_ctx_t g_backup;

int backup_detect_disc(disc_info_t *info);
int backup_get_disc_type_name(int type, char *buf, int buf_len);
int backup_init(const char *out_path, int options, const disc_info_t *disc,
                const char *game_id);
int backup_step(void);
void backup_cancel(void);
int backup_status(void);
u32 backup_get_written(void);
u32 backup_get_sector(void);
const char *backup_get_out_path(void);
int backup_write_cue(const char *bin_path, u32 total_sectors);
int backup_write_md5(const char *file_path);
int backup_patch_system_cnf(const char *out_dir, const char *game_id);
int backup_create_vmc_header(const char *out_path, u32 data_size);
int backup_create_zso_header(const char *out_path, u32 data_size, u32 block_size);

#endif
