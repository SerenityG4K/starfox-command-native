/*
 * Table des overlays lue dans la ROM : adresses, tailles, images (pour
 * reconnaître lequel occupe un emplacement). Les overlays de ce jeu ne sont
 * pas compressés ; le drapeau est vérifié.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }

OverlayInfo *rt_load_overlay_table(const uint8_t *rom, size_t size, size_t *count) {
    uint32_t ovt_off = rd32(rom, 0x50), ovt_size = rd32(rom, 0x54);
    uint32_t fat_off = rd32(rom, 0x48);
    size_t n = ovt_size / 32;
    OverlayInfo *out = calloc(n, sizeof *out);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *e = rom + ovt_off + i * 32;
        out[i].id = (int)rd32(e, 0);
        out[i].ram_address = rd32(e, 4);
        out[i].ram_size = rd32(e, 8);
        out[i].bss_size = rd32(e, 12);
        uint32_t file_id = rd32(e, 24);
        uint32_t flags = rd32(e, 28);
        uint32_t start = rd32(rom, fat_off + file_id * 8), end = rd32(rom, fat_off + file_id * 8 + 4);
        if ((flags >> 24) & 1) rt_log("overlay %d : compressé, non pris en charge", out[i].id);
        out[i].rom_start = start;
        out[i].rom_end = end;
        if (end <= size && start < end) out[i].image = rom + start;
        else out[i].image = NULL;
    }
    *count = n;
    return out;
}
