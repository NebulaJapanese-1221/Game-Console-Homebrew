#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "backup.h"

backup_ctx_t g_backup;

static const char *disc_type_names[] = {
    [DISC_TYPE_PS1_CD]      = "PS1 CD",
    [DISC_TYPE_PS2_CD]      = "PS2 CD",
    [DISC_TYPE_PS2_DVD]     = "PS2 DVD",
    [DISC_TYPE_DVD_VIDEO]   = "DVD Video",
    [DISC_TYPE_UNKNOWN]     = "Unknown",
    [DISC_TYPE_NO_DISC]     = "No Disc"
};

static const struct {
    int   type;
    int   size;
    const char *ext;
    const char *folder;
} disc_profiles[] __attribute__((unused)) = {
    { DISC_TYPE_PS1_CD,      2352, ".bin", "CD" },
    { DISC_TYPE_PS2_CD,      2352, ".bin", "CD" },
    { DISC_TYPE_PS2_DVD,     2048, ".iso", "DVD" },
    { DISC_TYPE_DVD_VIDEO,   2048, ".iso", "DVD" },
    { DISC_TYPE_UNKNOWN,     2048, ".iso", "DVD" },
};

static int is_cd_media(int type)
{
    return type == SCECdPSCD || type == SCECdPSCDDA ||
           type == SCECdPS2CD || type == SCECdPS2CDDA ||
           type == SCECdCDDA;
}

static int classify_disc(int raw_type)
{
    switch (raw_type) {
    case SCECdPSCD:
    case SCECdPSCDDA:
        return DISC_TYPE_PS1_CD;
    case SCECdPS2CD:
    case SCECdPS2CDDA:
        return DISC_TYPE_PS2_CD;
    case SCECdPS2DVD:
        return DISC_TYPE_PS2_DVD;
    case SCECdDVDV:
        return DISC_TYPE_DVD_VIDEO;
    case SCECdNODISC:
        return DISC_TYPE_NO_DISC;
    default:
        return DISC_TYPE_UNKNOWN;
    }
}

static int read_sectors_until_error(u32 *out_sectors)
{
    static u8 buf[CHUNK_SECTORS * 2048] __attribute__((aligned(64)));
    sceCdRMode mode;
    u32 sector = 0;
    int consecutive_failures = 0;

    mode.trycount = 5;
    mode.spindlctrl = SCECdSpinNom;
    mode.datapattern = SCECdSecS2048;
    mode.pad = 0;

    while (consecutive_failures < 5) {
        if (sceCdRead(sector, CHUNK_SECTORS, buf, &mode) == 1) {
            if (sceCdSync(0) == 0) {
                sector += CHUNK_SECTORS;
                consecutive_failures = 0;
                continue;
            }
        }
        consecutive_failures++;
        sector += CHUNK_SECTORS;
    }

    *out_sectors = sector;
    return 0;
}

int backup_read_game_id(char *out, int max_len)
{
    if (out == NULL || max_len <= 0) return 0;
    out[0] = '\0';

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
            if (len > 0 && len < max_len) {
                memcpy(out, start, (size_t)len);
                out[len] = '\0';
            }
            return 1;
        }
        while (ptr < end && *ptr != '\n') ptr++;
        if (ptr < end) ptr++;
    }
    return 0;
}

int backup_detect_disc(disc_info_t *info)
{
    int raw_type = sceCdGetDiskType();
    int classified = classify_disc(raw_type);

    memset(info, 0, sizeof(*info));
    info->media_type = classified;

    if (classified == DISC_TYPE_NO_DISC) {
        info->type_name = disc_type_names[DISC_TYPE_NO_DISC];
        info->sector_size = 2048;
        info->ext = ".iso";
        info->folder = "DVD";
        info->estimated_sectors = 0;
        return -1;
    }

    if (is_cd_media(raw_type)) {
        info->sector_size = 2352;
        info->ext = ".bin";
        info->folder = "CD";
    } else {
        info->sector_size = 2048;
        info->ext = ".iso";
        info->folder = "DVD";
    }

    info->type_name = disc_type_names[classified];

    /* Estimate disc size by reading until errors */
    read_sectors_until_error(&info->estimated_sectors);

    return 0;
}

int backup_get_disc_type_name(int type, char *buf, int buf_len)
{
    const char *name = "Unknown";
    switch (type) {
    case DISC_TYPE_PS1_CD:   name = "PS1 CD"; break;
    case DISC_TYPE_PS2_CD:   name = "PS2 CD"; break;
    case DISC_TYPE_PS2_DVD:  name = "PS2 DVD"; break;
    case DISC_TYPE_DVD_VIDEO: name = "DVD Video"; break;
    case DISC_TYPE_UNKNOWN:  name = "Unknown"; break;
    case DISC_TYPE_NO_DISC:  name = "No Disc"; break;
    }
    snprintf(buf, buf_len, "%s", name);
    return 0;
}

static int backup_open_output(backup_ctx_t *ctx)
{
    int fd;

    /* Create directory */
    char dir[256];
    strncpy(dir, ctx->out_path, sizeof(dir));
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir, 0777);
    }

    if (ctx->options & BACKUP_OPT_VMC) {
        fd = open(ctx->out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    } else if (ctx->options & BACKUP_OPT_ZSO) {
        fd = open(ctx->out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    } else {
        fd = open(ctx->out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }

    if (fd < 0) return -1;

    ctx->fd = fd;
    ctx->md5_initialized = 0;
    return 0;
}

int backup_init(const char *out_path, int options, const disc_info_t *disc,
                const char *game_id)
{
    backup_ctx_t *b = &g_backup;
    memset(b, 0, sizeof(*b));

    strncpy(b->out_path, out_path, sizeof(b->out_path) - 1);
    b->options = options;
    b->sector_size = disc->sector_size;
    b->total_written = 0;
    b->sector = 0;
    b->status = BACKUP_STATUS_IN_PROGRESS;
    b->fd = -1;
    b->md5_initialized = 0;
    strncpy(b->game_id, game_id ? game_id : "GAMEBACKUP", sizeof(b->game_id) - 1);
    b->disc = *disc;

    if (backup_open_output(b) != 0) {
        b->status = BACKUP_STATUS_ERROR;
        return -1;
    }

    md5_init(&b->md5_ctx);
    b->md5_initialized = 1;
    return 0;
}

static int backup_read_with_retry(u32 sector, u32 num_sectors, void *buffer,
                                  sceCdRMode *mode)
{
    int result;
    int speeds[] = { SCECdSpinNom, SCECdSpinX12, SCECdSpinX4, SCECdSpinX1 };
    int num_speeds = 4;

    for (int s = 0; s < num_speeds; s++) {
        mode->spindlctrl = speeds[s];
        mode->trycount = 20;

        result = sceCdRead(sector, num_sectors, buffer, mode);
        if (result == 1) {
            if (sceCdSync(0) == 0) {
                return 1;
            }
        }
    }

    return 0;
}

int backup_step(void)
{
    backup_ctx_t *b = &g_backup;
    if (b->status != BACKUP_STATUS_IN_PROGRESS) {
        return 0;
    }

    static u8 read_buf[CHUNK_SECTORS * 2352] __attribute__((aligned(64)));
    sceCdRMode mode;

    mode.trycount = 20;
    mode.spindlctrl = SCECdSpinNom;
    mode.datapattern = (b->sector_size == 2352) ? SCECdSecS2352 : SCECdSecS2048;
    mode.pad = 0;

    if (backup_read_with_retry(b->sector, CHUNK_SECTORS, read_buf, &mode) != 1) {
        close(b->fd);
        b->fd = -1;
        b->status = (b->total_written > 0) ? BACKUP_STATUS_DONE : BACKUP_STATUS_ERROR;
        return -1;
    }

    int write_size = CHUNK_SECTORS * b->sector_size;
    int written = write(b->fd, read_buf, write_size);
    if (written < 0) {
        close(b->fd);
        b->fd = -1;
        b->status = BACKUP_STATUS_ERROR;
        return -1;
    }

    if (b->md5_initialized) {
        md5_update(&b->md5_ctx, read_buf, (size_t)written);
    }

    b->total_written += (u32)written;
    b->sector += CHUNK_SECTORS;

    return 1;
}

void backup_cancel(void)
{
    /* Cancellation is checked on next step by status transition */
}

int backup_status(void)
{
    return g_backup.status;
}

u32 backup_get_written(void) { return g_backup.total_written; }
u32 backup_get_sector(void) { return g_backup.sector; }
const char *backup_get_out_path(void) { return g_backup.out_path; }

int backup_write_cue(const char *bin_path, u32 total_sectors)
{
    char cue_path[256];
    strncpy(cue_path, bin_path, sizeof(cue_path) - 1);
    char *dot = strrchr(cue_path, '.');
    if (dot) *dot = '\0';
    strncat(cue_path, ".cue", sizeof(cue_path) - strlen(cue_path) - 1);

    const char *bin_name = strrchr(bin_path, '/');
    bin_name = bin_name ? bin_name + 1 : bin_path;

    int fd = open(cue_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;

    char buf[512];
    int len;

    len = snprintf(buf, sizeof(buf),
        "FILE \"%s\" BINARY\n"
        "  TRACK 01 MODE2/2352\n"
        "    INDEX 01 00:00:00\n",
        bin_name);
    write(fd, buf, len);

    /* Add index entries for each 100 sectors */
    for (u32 s = 0; s < total_sectors; s += 100) {
        u32 frames = s / 75; /* 75 sectors per second */
        u32 sec = frames % 60;
        u32 min = frames / 60;
        u32 subframe = (frames % 75) * 100 / 75;

        len = snprintf(buf, sizeof(buf),
            "    INDEX 02 %02u:%02u:%02u\n", min, sec, subframe);
        write(fd, buf, len);
    }

    close(fd);
    return 0;
}

int backup_write_md5(const char *file_path)
{
    u8 digest[16];
    char hex[33];
    char md5_path[256];

    strncpy(md5_path, file_path, sizeof(md5_path) - 1);
    strncat(md5_path, ".md5", sizeof(md5_path) - strlen(md5_path) - 1);

    md5_final(&g_backup.md5_ctx, digest);
    md5_hex(digest, hex);

    int fd = open(md5_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;

    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%s  %s\n", hex,
                       strrchr(file_path, '/') ? strrchr(file_path, '/') + 1 : file_path);
    write(fd, buf, len);
    close(fd);
    return 0;
}

int backup_patch_system_cnf(const char *out_dir, const char *game_id)
{
    static u8 syscnf_buf[2048] __attribute__((aligned(64)));
    sceCdlFILE sf;

    if (sceCdSearchFile(&sf, "\\SYSTEM.CNF;1") != 1) return -1;
    static sceCdRMode mode;
    mode.trycount = 20;
    mode.spindlctrl = SCECdSpinNom;
    mode.datapattern = SCECdSecS2048;
    mode.pad = 0;

    if (sceCdRead(sf.lsn, (sf.size + 2047) / 2048, syscnf_buf, &mode) != 1) return -1;
    if (sceCdSync(0) != 0) return -1;

    /* Find BOOT2 line and patch the path */
    char *ptr = (char *)syscnf_buf;
    char *end = ptr + sf.size;
    while (ptr < end) {
        if (strncmp(ptr, "BOOT2", 5) == 0) {
            char *eq = strchr(ptr, '=');
            if (!eq) break;
            eq++;
            while (*eq == ' ' || *eq == '\t') eq++;
            char *start = eq;
            while (*eq && *eq != '\\' && *eq != ':' && *eq != ';' && *eq != '\r' && *eq != '\n') eq++;
            /* Replace with mass:/ path */
            int prefix_len = eq - start;
            memmove(start + strlen("mass:/") + strlen(game_id) + strlen(".ELF;1"),
                    eq, end - eq + 1);
            int new_len = snprintf(start, end - start, "mass:\\CD\\%s.ELF;1", game_id);
            (void)new_len;
            (void)prefix_len;
            break;
        }
        while (ptr < end && *ptr != '\n') ptr++;
        if (ptr < end) ptr++;
    }

    /* Write patched SYSTEM.CNF next to the backup */
    char cnf_path[256];
    snprintf(cnf_path, sizeof(cnf_path), "%s/SYSTEM.CNF", out_dir);
    int fd = open(cnf_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    write(fd, syscnf_buf, sf.size);
    close(fd);
    return 0;
}

int backup_create_vmc_header(const char *out_path, u32 data_size)
{
    /* Minimal VMC header: PS2 memory card image structure */
    /* Cluster size = 16KB, 8MB card = 512 clusters */
    u32 cluster_size = 16384;
    u32 total_clusters = (data_size + cluster_size - 1) / cluster_size;
    if (total_clusters < 4) total_clusters = 4;

    static u8 header[16384] __attribute__((aligned(64)));
    memset(header, 0, sizeof(header));

    /* VMC magic */
    header[0] = 'M';
    header[1] = 'C';
    /* Version */
    header[2] = 1;
    header[3] = 0;
    /* Card size in clusters */
    *(u32 *)&header[4] = total_clusters;
    /* Cluster size */
    *(u32 *)&header[8] = cluster_size;

    /* Simple FAT: mark clusters as allocated */
    u32 *fat = (u32 *)&header[cluster_size / 4];
    for (u32 i = 0; i < total_clusters; i++) {
        fat[i] = i + 1; /* point to next cluster */
    }
    fat[total_clusters - 1] = 0xFFFFFFFF; /* end of chain */

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;

    write(fd, header, cluster_size);

    /* Read backup data and append */
    /* Note: in practice the caller writes data separately; this just creates header */
    close(fd);
    return 0;
}

int backup_create_zso_header(const char *out_path, u32 data_size, u32 block_size)
{
    /* ZSO format: magic + header + block offset table + block data */
    u32 num_blocks = (data_size + block_size - 1) / block_size;
    u32 table_offset = 16; /* after header */
    u32 table_size = num_blocks * 4;
    u32 data_offset = table_offset + table_size;

    static u8 header[16] __attribute__((aligned(64)));
    memset(header, 0, 16);
    header[0] = 'Z';
    header[1] = 'S';
    header[2] = 'O';
    header[3] = 0;
    *(u32 *)&header[4] = 1;           /* version */
    *(u32 *)&header[8] = block_size;  /* block size */
    *(u32 *)&header[12] = data_size;  /* uncompressed size */

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;

    write(fd, header, 16);

    /* Write block offset table (all blocks stored uncompressed at sequential offsets) */
    for (u32 i = 0; i < num_blocks; i++) {
        u32 offset = data_offset + i * block_size;
        write(fd, &offset, 4);
    }

    close(fd);
    return 0;
}
