/* The recursion guard rejects input before parsing signed data. */
#include "../../kits/support/tiku_cms.c"
#include <stdio.h>
int main(void)
{
    tiku_cms_sig_t result;
    int status = verify(NULL, 0, NULL, 0, &result, CMS_DEPTH + 1);
    int ok = status == TIKU_SIG_BROKEN && result.status == TIKU_SIG_BROKEN;
    puts(ok ? "PASS: D-163" : "FAIL: D-163");
    return !ok;
}
