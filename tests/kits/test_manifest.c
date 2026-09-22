/* Bundle discovery reads data, not executable code.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_manifest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Each fixture owns its manifest and cannot touch installed applications. */
static int parse(const char *text, tiku_manifest_t *m)
{
    char path[] = "/tmp/tiku-manifest-XXXXXX", error[100];
    int fd = mkstemp(path); assert(fd >= 0);
    FILE *f = fdopen(fd, "w"); assert(f);
    assert(fputs(text, f) >= 0); assert(fclose(f) == 0);
    int rc = tiku_manifest_read(path, m, error, sizeof error);
    assert((rc == 0) == !error[0]); unlink(path);
    return rc;
}

int main(void)
{
    const char *valid = "version=1\nid=basic\nname=TikuBasic\n"
                        "module=TikuBasic.so\ncategory=programming\n";
    tiku_manifest_t m;
    assert(parse(valid, &m) == 0);
    assert(!strcmp(m.id, "basic") && !strcmp(m.icon, "application"));
    char text[1024];
    const char *bad[] = {"version=1\n", "id=duplicate\n", "unknown=1\n",
                         "module=../escape.so\n", "name=bad\tvalue\n"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        snprintf(text, sizeof text, "%s%s", valid, bad[i]);
        tiku_manifest_t before = m;
        assert(parse(text, &m) < 0 && !memcmp(&m, &before, sizeof m));
    }
    assert(parse("version=2\nid=x\nname=X\nmodule=x.so\n"
                 "category=applications\n", &m) < 0);
    assert(parse("version=1\nid=x\nname=X\nmodule=../x.so\n"
                 "category=applications\n", &m) < 0);
    assert(parse("version=1\nid=x\nname=X\nmodule=x.so\n"
                 "category=trusted\n", &m) < 0);
    assert(parse("version=1\nid=x\nname=X\n", &m) < 0);
    memset(text, 'x', sizeof text - 1); text[sizeof text - 1] = 0;
    assert(parse(text, &m) < 0);
    assert(tiku_manifest_read("apps/ide/basic.app", &m, NULL, 0) == 0);
    assert(!strcmp(m.types, "text/x-basic,application/x-tiku-project"));
    puts("PASS: manifest validation, safe leaf modules, atomic parse, app-owned types");
    return 0;
}
