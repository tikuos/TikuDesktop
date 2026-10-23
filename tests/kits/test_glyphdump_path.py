"""The comparison tool uses the documented glyphdump build output."""
import importlib.util
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from source_layout import source_path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "comparison", source_path(root, "kits/interface/tools/compare_freetype.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with patch.object(module.subprocess, "run", return_value=SimpleNamespace(
        returncode=0, stdout="FAMILY Test\n")) as run:
    module.ours("font.ttf", 12, [65])
    assert Path(run.call_args.args[0][0]).resolve() == root / "build/tools/glyphdump"
print("PASS: D-123")
