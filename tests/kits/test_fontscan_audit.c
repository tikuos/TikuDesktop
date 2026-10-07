/* Font names and fallback coverage follow the encoded mappings. */
#include "../../kits/translation/tiku_fontset_scan.c"
static void put16(unsigned char *p, unsigned n) { p[0] = n >> 8; p[1] = n; }
int main(void)
{
    char name[32];
    const unsigned char raw[] = {0x80, 0x8e};
    name_utf8(raw, sizeof raw, 0, name, sizeof name);
    int fails = strcmp(name, "\303\204\303\251") != 0;
    if (fails) puts("FAIL D-096");
    unsigned char cmap[30] = {0};
    put16(cmap, 4); put16(cmap + 2, sizeof cmap); put16(cmap + 6, 2);
    put16(cmap + 14, 'C'); put16(cmap + 18, 'A'); put16(cmap + 22, 2);
    put16(cmap + 24, 1); put16(cmap + 28, 3);
    fs_face_t f = {0}; read_subtable(cmap, sizeof cmap, &f);
    int ok = f.ncover == 2 && f.cover[0] == 'A' && f.cover[1] == 'A' &&
             f.cover[2] == 'C' && f.cover[3] == 'C';
    if (!ok) { puts("FAIL D-097"); fails++; }
    free(f.cover);
    return fails != 0;
}
