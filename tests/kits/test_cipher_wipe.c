/* RC2 and RC4 schedules are explicitly erased before returning. */
#include "tiku_cms_private.h"
#include "tiku_crypto.h"
#include <stddef.h>
static int rc2_wiped, rc4_wiped;
static void audit_wipe(void *ptr, size_t n);
#define cms_wipe audit_wipe
#include <tiku_cms.c>
#undef cms_wipe
static void audit_wipe(void *ptr, size_t n)
{
    volatile unsigned char *p = ptr;
    if (n == sizeof(tiku_rc2_t)) rc2_wiped++;
    if (n == sizeof(tiku_rc4_t)) rc4_wiped++;
    while (n--) *p++ = 0;
}
int main(void)
{
    unsigned char key[16] = {1}, iv[8] = {0}, data[8] = {0};
    cms_cbc(CMS_RC2, key, sizeof key, 128, iv, data, sizeof data);
    cms_cbc(CMS_RC4, key, sizeof key, 128, NULL, data, sizeof data);
    int ok = rc2_wiped && rc4_wiped;
    puts(ok ? "PASS D-162" : "FAIL D-162"); return !ok;
}
