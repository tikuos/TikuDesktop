"""Regression checks for style discovery and icon regeneration."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("style", ROOT / "tools/check_style.py")
style = importlib.util.module_from_spec(spec)
spec.loader.exec_module(style)


class AuditTools(unittest.TestCase):
    def test_audited_function_sizes(self):
        targets = {
            "kits/interface/tiku_cff.c": ["run_op"],
            "kits/interface/tiku_dl.c": ["tiku_dl_play"],
            "kits/interface/tiku_gfx.c": ["round_corners", "tiku_blend_scaled", "tiku_shadow", "tiku_surface_png"],
            "kits/interface/tiku_logo.c": ["segment"],
            "kits/translation/tiku_image.c": ["load_png"],
            "kits/storage/tiku_fs_step.c": ["step_one"],
            "kits/storage/tiku_config_outbox.c": ["ob_resolve"],
            "kits/application/tiku_remote.c": ["session_message"],
            "kits/editor/tiku_codeview_input.c": ["bar_drag"],
            "apps/ide/tiku_ide_commands.c": ["tiku_ide_pick"],
            "apps/ide/tiku_ide_events.c": ["tiku_ide_event"],
        }
        for path, names in targets.items():
            functions = style.functions((ROOT / path).read_text().splitlines())
            for name in names:
                with self.subTest(path=path, function=name):
                    found = [f for f in functions if f[0] == name]
                    self.assertEqual(len(found), 1)
                    _, _, _, start, end, count = found[0]
                    self.assertLessEqual(end - start + 1, style.FUNC_MAX)
                    self.assertLessEqual(count, style.LOCALS_MAX)

    def test_desktop_internal_contracts(self):
        header = (ROOT / "system/desktop/tiku_trk_desktop_private.h").read_text()
        self.assertNotIn("int mount_one(", header)
        draw = (ROOT / "system/desktop/tiku_trk_desktop_draw.c").read_text()
        self.assertNotIn("20 /* TITLE_HEIGHT */", draw)
        self.assertIn("TIKU_TRK_TITLE_HEIGHT", draw)

    def test_divider_is_not_documentation(self):
        self.assertFalse(style.is_doc_end("/* ---- wire */"))
        self.assertTrue(style.is_doc_end("/** @brief Send one frame. */"))

    def test_multiline_signature(self):
        source = "/** @brief Add two values. */\nstatic int add(int a,\n               int b)\n{\n    int c = 0;\n    return a + b + c;\n}\n"
        functions = style.functions(source.splitlines())
        self.assertEqual(len(functions), 1)
        self.assertEqual(functions[0][:3], ("add", True, True))
        self.assertEqual(functions[0][-1], 1)
        self.assertEqual(style.functions("static int add(int a,\n int b);\n".splitlines()), [])

    def test_generator_keeps_runtime(self):
        with tempfile.TemporaryDirectory(prefix="desktop49-icons-") as temp:
            root = Path(temp)
            for path in ("tools/icons", "kits/translation"):
                (root / path).mkdir(parents=True)
            shutil.copytree(ROOT / "assets/icons/hvif", root / "assets/icons/hvif")
            script = root / "tools/icons/gen_icons.py"
            output = root / "kits/translation/tiku_icons.c"
            shutil.copyfile(ROOT / "tools/icons/gen_icons.py", script)
            shutil.copyfile(ROOT / "kits/translation/tiku_icons.c", output)
            original = output.read_text()
            subprocess.run(["python3", str(script)], check=True, capture_output=True)
            result = output.read_text()
            marker = "static tiku_icon_t tiku_icon_live"
            self.assertTrue(marker in result, "generated output lost the live icon runtime")
            self.assertEqual(original[original.index(marker):], result[result.index(marker):])
            subprocess.run(["python3", str(script)], check=True, capture_output=True)
            self.assertEqual(result, output.read_text())


if __name__ == "__main__":
    unittest.main()
