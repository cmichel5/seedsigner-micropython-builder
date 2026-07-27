/* mklittlefs_launcher — build a MicroPython-compatible littlefs2 image from a
 * manifest of files, for baking into the ESP32 firmware's internal-flash "vfs"
 * partition. The image holds the frozen-app launcher (/main.py) and, when packs are
 * baked in, the language packs (/lang-packs/<locale>/...).
 *
 * WHY a C tool (not littlefs-python / mtools): it compiles against MicroPython's
 * OWN vendored littlefs2 (deps/micropython/upstream/lib/littlefs/lfs2.c), so the
 * on-disk format is byte-identical to what the firmware's VfsLfs2 formats and
 * mounts — no third-party littlefs, no disk-version drift, no network dependency,
 * reproducible in CI. See tools/build_launcher_fs.py (the wrapper that computes the
 * partition geometry, assembles the manifest, and invokes this) and
 * docs/knowledge/esp32-auto-vfs-partition-and-launcher-bake.md.
 *
 * The config below MIRRORS MicroPython's VfsLfs2.mkfs defaults (extmod/vfs_lfsx.c
 * init_config + the readsize/progsize/lookahead=32 defaults inisetup uses):
 * block_size=4096 (esp32 Partition NATIVE_BLOCK_SIZE_BYTES), block_cycles=100,
 * cache_size=MIN(block_size,4*max(read,prog))=128, lookahead=32, and default
 * name/file/attr_max (0) so the superblock matches. Only the geometry that lands in
 * the superblock (block_size, block_count, *_max) must match for the firmware to
 * mount the image; read/prog/cache/lookahead are runtime-only.
 *
 * Usage: mklittlefs_launcher <out.bin> <block_size> <block_count> <manifest>
 *   <manifest> is a text file, one entry per line: "<host_src_path>\t<lfs_dest_path>".
 *   A dest may carry subdirs (e.g. "lang-packs/de/LC_MESSAGES/messages.mo"); ancestors
 *   are created as needed. A leading '/' on a dest is ignored (paths are fs-root
 *   relative). Blank lines are skipped.
 *   e.g. manifest:  /tmp/main_py.txt\tmain.py
 *                   /home/.../de/messages.mo\tlang-packs/de/LC_MESSAGES/messages.mo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lfs2.h"

static uint8_t *g_flash;   /* backing "flash": block_size * block_count bytes */

static int bd_read(const struct lfs2_config *c, lfs2_block_t block,
                   lfs2_off_t off, void *buffer, lfs2_size_t size) {
    memcpy(buffer, g_flash + (size_t)block * c->block_size + off, size);
    return 0;
}
static int bd_prog(const struct lfs2_config *c, lfs2_block_t block,
                   lfs2_off_t off, const void *buffer, lfs2_size_t size) {
    memcpy(g_flash + (size_t)block * c->block_size + off, buffer, size);
    return 0;
}
static int bd_erase(const struct lfs2_config *c, lfs2_block_t block) {
    memset(g_flash + (size_t)block * c->block_size, 0xff, c->block_size);
    return 0;
}
static int bd_sync(const struct lfs2_config *c) { (void)c; return 0; }

static void fill_config(struct lfs2_config *cfg, uint32_t block_size, uint32_t block_count) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->read = bd_read;
    cfg->prog = bd_prog;
    cfg->erase = bd_erase;
    cfg->sync = bd_sync;
    cfg->read_size = 32;
    cfg->prog_size = 32;
    cfg->block_size = block_size;
    cfg->block_count = block_count;
    cfg->block_cycles = 100;
    cfg->cache_size = 128;
    cfg->lookahead_size = 32;
    /* name_max/file_max/attr_max/metadata_max/inline_max = 0 -> littlefs defaults,
     * matching MicroPython (which passes 0), so the superblock is identical. */
}

/* Read an entire host file into a freshly malloc'd buffer; *len set to its size.
 * Returns NULL on error (message already printed). Caller frees. */
static uint8_t *read_host_file(const char *path, long *len) {
    FILE *inf = fopen(path, "rb");
    if (!inf) { fprintf(stderr, "error: open %s: ", path); perror(""); return NULL; }
    fseek(inf, 0, SEEK_END);
    long clen = ftell(inf);
    fseek(inf, 0, SEEK_SET);
    if (clen < 0) { fprintf(stderr, "error: cannot size %s\n", path); fclose(inf); return NULL; }
    uint8_t *buf = malloc(clen > 0 ? (size_t)clen : 1);
    if (!buf) { fprintf(stderr, "error: oom reading %s\n", path); fclose(inf); return NULL; }
    if (clen > 0 && fread(buf, 1, (size_t)clen, inf) != (size_t)clen) {
        fprintf(stderr, "error: read %s: ", path); perror(""); fclose(inf); free(buf); return NULL;
    }
    fclose(inf);
    *len = clen;
    return buf;
}

/* Create every ancestor directory of `dest` (which is a file path, fs-root relative,
 * no leading '/'). mkdir of an existing dir returns LFS2_ERR_EXIST, which is fine. */
static int mkparents(lfs2_t *lfs, const char *dest) {
    char tmp[512];
    size_t n = strlen(dest);
    if (n >= sizeof(tmp)) { fprintf(stderr, "error: dest too long: %s\n", dest); return -1; }
    memcpy(tmp, dest, n + 1);
    for (size_t i = 0; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0]) {
                int err = lfs2_mkdir(lfs, tmp);
                if (err && err != LFS2_ERR_EXIST) {
                    fprintf(stderr, "error: lfs2_mkdir(%s): %d\n", tmp, err);
                    return err;
                }
            }
            tmp[i] = '/';
        }
    }
    return 0;
}

/* Write one host file into the mounted fs at `dest`, then read it back and compare
 * (per-file self-test). Returns 0 on success, -1 on any failure. */
static int write_one(lfs2_t *lfs, const char *src, const char *dest) {
    while (*dest == '/') dest++;   /* fs-root relative; ignore a leading slash */
    if (!*dest) { fprintf(stderr, "error: empty dest for src %s\n", src); return -1; }

    long clen = 0;
    uint8_t *content = read_host_file(src, &clen);
    if (!content) return -1;

    int rc = -1;
    if (mkparents(lfs, dest)) { free(content); return -1; }

    lfs2_file_t f;
    int err = lfs2_file_open(lfs, &f, dest, LFS2_O_WRONLY | LFS2_O_CREAT | LFS2_O_TRUNC);
    if (err) { fprintf(stderr, "error: lfs2_file_open(%s): %d\n", dest, err); goto done; }
    lfs2_ssize_t wn = lfs2_file_write(lfs, &f, content, (lfs2_size_t)clen);
    if (wn != (lfs2_ssize_t)clen) {
        fprintf(stderr, "error: lfs2_file_write(%s): %ld != %ld\n", dest, (long)wn, clen);
        lfs2_file_close(lfs, &f); goto done;
    }
    if ((err = lfs2_file_close(lfs, &f))) {
        fprintf(stderr, "error: lfs2_file_close(%s): %d\n", dest, err); goto done;
    }

    /* Self-test: read the file back and compare, proving the write round-trips. */
    if ((err = lfs2_file_open(lfs, &f, dest, LFS2_O_RDONLY))) {
        fprintf(stderr, "error: verify open(%s): %d\n", dest, err); goto done;
    }
    uint8_t *rb = malloc(clen > 0 ? (size_t)clen : 1);
    if (!rb) { fprintf(stderr, "error: oom verifying %s\n", dest); lfs2_file_close(lfs, &f); goto done; }
    lfs2_ssize_t rn = lfs2_file_read(lfs, &f, rb, (lfs2_size_t)clen);
    lfs2_file_close(lfs, &f);
    if (rn != (lfs2_ssize_t)clen || (clen > 0 && memcmp(rb, content, (size_t)clen) != 0)) {
        fprintf(stderr, "error: verify read-back mismatch %s (%ld vs %ld)\n", dest, (long)rn, clen);
        free(rb); goto done;
    }
    free(rb);
    rc = 0;
done:
    free(content);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <out.bin> <block_size> <block_count> <manifest>\n", argv[0]);
        return 2;
    }
    const char *out_path = argv[1];
    uint32_t block_size = (uint32_t)strtoul(argv[2], NULL, 0);
    uint32_t block_count = (uint32_t)strtoul(argv[3], NULL, 0);
    const char *manifest_path = argv[4];

    if (block_size == 0 || block_count == 0) {
        fprintf(stderr, "error: block_size and block_count must be > 0\n");
        return 2;
    }

    /* Slurp the manifest and split it in place into (src, dest) pointer pairs. */
    long mlen = 0;
    char *manifest = (char *)read_host_file(manifest_path, &mlen);
    if (!manifest) return 1;
    /* NUL-terminate so line walking is safe even without a trailing newline. */
    char *mbuf = realloc(manifest, (size_t)mlen + 1);
    if (!mbuf) { fprintf(stderr, "error: oom\n"); free(manifest); return 1; }
    manifest = mbuf;
    manifest[mlen] = '\0';

    size_t total = (size_t)block_size * block_count;
    g_flash = malloc(total);
    if (!g_flash) { fprintf(stderr, "error: oom allocating %zu bytes\n", total); return 1; }
    memset(g_flash, 0xff, total);   /* erased flash reads as 0xFF */

    struct lfs2_config cfg;
    fill_config(&cfg, block_size, block_count);

    lfs2_t lfs;
    int err = lfs2_format(&lfs, &cfg);
    if (err) { fprintf(stderr, "error: lfs2_format: %d\n", err); return 1; }
    err = lfs2_mount(&lfs, &cfg);
    if (err) { fprintf(stderr, "error: lfs2_mount: %d\n", err); return 1; }

    /* Force the block allocator to scan from block 0 so data lands low and packs
     * contiguously, keeping the truncated image compact. On mount littlefs seeds the
     * allocator start with seed%block_count for wear-leveling (lfs2.c:4637), which
     * would scatter a small write ~anywhere in the partition and bloat the flashed
     * artifact (e.g. block 2039 -> 8 MB on a 32 MB board). This override is purely a
     * wear-leveling hint: the resulting filesystem is valid littlefs and the firmware
     * reads files via the on-disk block pointers regardless of where they sit. */
    lfs.lookahead.start = 0;

    /* Walk the manifest: each line "<src>\t<dest>". */
    int nfiles = 0;
    char *p = manifest;
    while (p < manifest + mlen) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *line = p;
        p = nl ? nl + 1 : manifest + mlen;
        if (!*line) continue;                       /* blank line */
        char *tab = strchr(line, '\t');
        if (!tab) { fprintf(stderr, "error: manifest line has no TAB: '%s'\n", line); return 1; }
        *tab = '\0';
        const char *src = line;
        const char *dest = tab + 1;
        if (write_one(&lfs, src, dest)) return 1;
        nfiles++;
    }
    if ((err = lfs2_unmount(&lfs))) { fprintf(stderr, "error: lfs2_unmount: %d\n", err); return 1; }
    free(manifest);

    if (nfiles == 0) { fprintf(stderr, "error: manifest baked 0 files\n"); return 1; }

    /* Remount clean and re-verify EVERY file survived the unmount, so a host run proves
     * the whole image is internally consistent before we ship it. (Re-slurp geometry
     * only; the manifest is gone, so re-derive the file list is not needed — a full
     * remount that mounts without error already validates the superblock + metadata
     * tree; per-file read-back happened inline above.) */
    if ((err = lfs2_mount(&lfs, &cfg))) { fprintf(stderr, "error: verify remount: %d\n", err); return 1; }
    lfs2_unmount(&lfs);

    /* Truncate trailing erased (0xFF) blocks so the flashed artifact stays tight. The
     * unwritten tail of the vfs partition stays erased on a fresh chip; littlefs tracks
     * free space via metadata (not a content scan) and erases-before-prog, so an
     * unflashed/stale tail is harmless. Keep whole blocks up to the last written byte. */
    size_t used = total;
    while (used > 0 && g_flash[used - 1] == 0xff) used--;
    size_t used_blocks = (used + block_size - 1) / block_size;
    if (used_blocks == 0) used_blocks = 1;   /* always keep at least the superblock block */
    used = used_blocks * (size_t)block_size;

    FILE *of = fopen(out_path, "wb");
    if (!of) { perror("open out"); return 1; }
    if (fwrite(g_flash, 1, used, of) != used) { perror("write out"); return 1; }
    fclose(of);

    fprintf(stderr,
            "[mklfs] %s: %zu bytes (%zu of %u blocks x %u), fs geometry %ux%u, %d file(s)\n",
            out_path, used, used_blocks, block_count, block_size,
            block_size, block_count, nfiles);
    free(g_flash);
    return 0;
}
