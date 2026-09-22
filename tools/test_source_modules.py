"""Regression cases for the functional source-layout guard."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from check_source_modules import ROOT, includes, sources, violations


class SourceModuleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tiku-source-modules-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def source(self, path, text):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def test_independent_model(self):
        self.source("apps/editor/tiku_editor_buffer.c",
                    '#include "tiku_editor_buffer.h"\n')
        self.source("apps/editor/tiku_editor_buffer.h", '#include <stddef.h>\n')
        self.assertEqual(violations(self.root), [])

    def test_implementation_include(self):
        self.source("apps/ide/tiku_ide_app.c", '#include "tiku_ide_search.inl"\n')
        self.assertTrue(violations(self.root))

    def test_fragment_file(self):
        self.source("apps/reader/tiku_reader_search.inl", "")
        self.assertTrue(violations(self.root))

    def test_component_name(self):
        self.source("apps/editor/tiku_edit.c", "")
        self.assertTrue(violations(self.root))

    def test_test_must_link_module(self):
        self.source("tests/apps/test_ide.c", '#include "../../apps/ide/tiku_ide_app.c"\n')
        self.assertTrue(violations(self.root))

    def test_indirect_model_ui_dependency(self):
        self.source("apps/reader/tiku_reader_document.c", '#include "tiku_reader_document.h"\n')
        self.source("apps/reader/tiku_reader_document.h", '#include "tiku_window.h"\n')
        self.assertTrue(violations(self.root))

    def test_inventory_sees_fragments_and_objective_c(self):
        for name in ("apps/calculator/tiku_calc_ui.inl", "kits/interface/tiku_cocoa.m",
                     "system/desktop/tiku_desktop.c"):
            self.source(name, "")
        self.assertEqual(len(sources(self.root)), 3)
        self.assertEqual(violations(self.root), [])

    def test_bench_and_console_cannot_regress_to_fragments(self):
        for family in ("bench", "console"):
            with self.subTest(family=family):
                path = f"apps/{family}/tiku_{family}_ui.inl"
                self.source(path, "")
                self.assertTrue(any(path in e for e in violations(self.root)))

    def test_legacy_app_source_includes_are_rejected(self):
        for family in ("bench", "console"):
            with self.subTest(family=family):
                path = f"tests/apps/test_{family}.c"
                self.source(path, f'#include "../../apps/{family}/tiku_{family}.c"\n')
                self.assertTrue(any(path in e for e in violations(self.root)))

    def test_guard_handles_relative_header_cycles(self):
        self.source("apps/ide/tiku_ide_project.c", '#include "tiku_ide_project.h"\n')
        self.source("apps/ide/tiku_ide_project.h",
                    '#include "../ide/tiku_ide_project.h"\n')
        self.assertEqual(violations(self.root), [])

    def test_comments_and_strings_are_not_includes(self):
        self.assertEqual(includes('/*\n#include "bad.c"\n*/\n'
                                  'const char *s = "/* text */";\n'
                                  '#include "good.h"\n'), ["good.h"])


class BuildGraphTests(unittest.TestCase):
    def test_custom_flags_keep_test_entry_point_disabled(self):
        result = subprocess.run(
            ["make", "--no-print-directory", "-B", "-n", "BUILD=build",
             "CFLAGS=-O0", "build/obj/apps/ide/tiku_ide_app.o"],
            cwd=ROOT, text=True, capture_output=True, check=True)
        compile_lines = [line for line in result.stdout.splitlines()
                         if " -c " in line and "tiku_ide_app.c" in line]
        self.assertEqual(len(compile_lines), 1)
        self.assertIn("-DTIKU_APP_EMBED", compile_lines[0])


if __name__ == "__main__":
    unittest.main()
