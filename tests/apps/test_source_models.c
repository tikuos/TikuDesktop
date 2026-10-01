/* The project, editor buffer and reader document link without window controllers.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../apps/ide/tiku_ide_project.h"
#include "../../apps/editor/tiku_editor_buffer.h"
#include "../../apps/reader/tiku_reader_document.h"

/** Project parsing preserves the legacy fields without requiring a device. */
static void project_model(void)
{
    project_t p;
    const char *source = "title Sensor tools\nboard old-board\ntarget host\n"
                         "group Sources\nfile main.bas\nfuture ignored\n";
    assert(tiku_ide_project_parse(&p, source, "/tmp/project") == 2);
    assert(!strcmp(p.title, "Sensor tools") && !strcmp(p.board, "old-board"));
    assert(!strcmp(p.target, "host") && p.unknown == 1);
    assert(p.row[0].heading && !p.row[1].heading);
    assert(!strcmp(p.row[1].path, "/tmp/project/main.bas"));
    assert(tiku_ide_project_parse(&p, "file my program.bas \r\n", "/tmp/project") == 1);
    assert(!strcmp(p.row[0].name, "my program.bas"));
    assert(!strcmp(p.row[0].path, "/tmp/project/my program.bas"));
    assert(!tiku_ide_project_parse(&p, NULL, NULL));
    assert(!p.nrow && !strcmp(p.dir, "."));
}

/** UTF-8 and undo semantics belong to the document, not its painter. */
static void editor_model(void)
{
    edit_buffer_t b;
    char text[128];
    assert(tiku_editor_buffer_init(&b));
    assert(tiku_editor_buffer_set(&b, "café\nlast\n"));
    assert(b.nline == 3);
    tiku_editor_buffer_saved(&b);
    tiku_editor_buffer_place(&b, 0, 5);
    assert(tiku_editor_buffer_replace(&b, "!"));
    assert(tiku_editor_buffer_modified(&b));
    assert(tiku_editor_buffer_undo(&b));
    assert(!tiku_editor_buffer_modified(&b));
    tiku_editor_buffer_text(&b, text, sizeof text);
    assert(!strcmp(text, "café\nlast\n"));
    assert(tiku_editor_buffer_redo(&b));
    assert(!tiku_editor_buffer_replace(&b, "\xff"));
    tiku_editor_buffer_text(&b, text, sizeof text);
    assert(!strcmp(text, "café!\nlast\n"));
    tiku_editor_buffer_free(&b);
}

/** Desktop parsing grows beyond embedded limits without any window state. */
static void reader_model(void)
{
    const size_t length = 48 * 1024;
    char *source = malloc(length + 1);
    assert(source);
    memset(source, 'a', length);
    source[length] = 0;
    tiku_doc_t *doc = tiku_reader_document("large.txt", source);
    assert(doc && doc->ntext >= (int)length);
    tiku_reader_doc_free(doc);
    free(source);

    assert(tiku_reader_markdown("sensor.MD"));
    doc = tiku_reader_document("sensor.md", "# Sensor\n\nReading **42**.\n");
    assert(doc && doc->nblock > 0);
    tiku_reader_doc_free(doc);

    char path[] = "/tmp/tiku-reader-model-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, "a\0b", 3) == 3);
    assert(!close(fd));
    const char *why = NULL;
    assert(!tiku_reader_source(path, &why) && why);
    assert(!unlink(path));
}

int main(void)
{
    project_model();
    editor_model();
    reader_model();
    puts("PASS: separately linked project, editor buffer and reader document models");
    return 0;
}
