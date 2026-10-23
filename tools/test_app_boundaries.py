"""Regression tests for app/system source separation."""
from pathlib import Path
import tempfile
import unittest

from check_app_boundaries import violations


class BoundaryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="tiku-boundaries-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def source(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def test_sdk_only_app(self):
        self.source("apps/editor/main.c", '#include "tiku_app.h"\n')
        self.assertEqual(violations(self.root, ["system/main.c"]), [])

    def test_app_imports_private_core(self):
        self.source("apps/editor/main.c", '#include "tiku_trk_desktop_private.h"\n')
        self.assertTrue(violations(self.root, []))

    def test_system_imports_app(self):
        self.source("system/runner/main.c", '#include "../../apps/editor/main.c"\n')
        self.assertTrue(violations(self.root, []))

    def test_kit_imports_app(self):
        self.source("kits/application/host.c", '#include "apps/editor/main.h"\n')
        self.assertTrue(violations(self.root, []))

    def test_shell_loads_module(self):
        self.source("system/shared/shell/main.c", "tiku_app_load(path, why, cap);")
        self.assertTrue(violations(self.root, []))

    def test_app_linked_into_shell(self):
        self.assertTrue(violations(self.root, ["apps/editor/main.c"]))

    def test_examples_may_share_example_headers(self):
        self.source("examples/diagnostics/main.c", '#include "../apps/demo.h"\n')
        self.assertEqual(violations(self.root, []), [])

    def test_examples_may_not_import_desktop_headers(self):
        self.source("examples/apps/demo.c", '#include "tiku_trk_desktop_private.h"\n')
        self.assertTrue(violations(self.root, []))

    def test_shared_system_headers_are_allowed_in_shell(self):
        self.source("system/desktop/main.c", '#include "tiku_trk_desktop_private.h"\n')
        self.assertEqual(violations(self.root, []), [])

    def test_active_shell_view_is_checked(self):
        """A composed shell cannot hide an app import behind an inactive copy."""
        self.source("system/desktop/main.c", "")
        self.source("build/foundation/system/desktop/main.c", '#include "apps/editor/main.c"\n')
        self.assertTrue(violations(self.root, []))


if __name__ == "__main__":
    unittest.main()
