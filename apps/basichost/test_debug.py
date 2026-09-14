"""Exercise the actual firmware interpreter through its host stream adapter."""
import os
from pathlib import Path
import select
import subprocess
import time
import unittest


class DebuggerTests(unittest.TestCase):
    def setUp(self):
        binary = Path(__file__).resolve().parents[2] / "build/apps/basic/tiku-basic"
        self.proc = subprocess.Popen([str(binary), "--interactive", "/tmp"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)
        self.buf = b""
        self.until(b"ok> ")

    def tearDown(self):
        self.proc.terminate()
        self.proc.communicate(timeout=5)

    def send(self, text):
        self.proc.stdin.write(text.encode())
        self.proc.stdin.flush()

    def until(self, marker):
        deadline = time.monotonic() + 4
        while marker not in self.buf:
            remaining = deadline - time.monotonic()
            self.assertGreater(remaining, 0, self.buf.decode(errors="replace"))
            if select.select([self.proc.stdout], [], [], remaining)[0]:
                block = os.read(self.proc.stdout.fileno(), 4096)
                self.assertTrue(block, self.buf)
                self.buf += block
        end = self.buf.index(marker) + len(marker)
        result, self.buf = self.buf[:end], self.buf[end:]
        return result.decode()

    def command(self, text):
        self.send(text + "\n")
        return self.until(b"ok> ")

    def setup_program(self, lines):
        self.command("NEW")
        self.assertIn("VERSION 1", self.command("DEBUG ON"))
        self.command("DEBUG WATCH A")
        for line in lines:
            self.command(line)

    def pause(self, command="RUN", line=10):
        self.send(command + "\n")
        result = self.until(b"[TDBG READY]")
        self.assertIn(f"[TDBG PAUSED {line}]", result)
        return result

    def test_step_break_continue_end(self):
        self.setup_program(["10 A=1", "20 A=A+2", "30 PRINT A", "40 END"])
        self.command("DEBUG BREAK 30")
        self.assertIn("VAR A 0", self.pause())
        self.assertIn("VAR A 1", self.pause("DEBUG STEP", 20))
        self.assertIn("VAR A 3", self.pause("DEBUG CONT", 30))
        self.pause("DEBUG STEP", 40)
        self.send("DEBUG STEP\n")
        result = self.until(b"ok> ")
        self.assertIn("[TDBG END]", result)
        self.assertIn("VAR A 3", result)

    def test_breakpoint_retriggers_on_loop(self):
        self.setup_program(["10 A=A+1", "20 GOTO 10"])
        self.command("DEBUG BREAK 10")
        self.pause()
        self.assertIn("VAR A 1", self.pause("DEBUG CONT", 10))
        self.assertIn("VAR A 2", self.pause("DEBUG CONT", 10))
        self.send("\x03")
        self.until(b"ok> ")

    def test_strings_named_variables_and_invalid_watch(self):
        self.setup_program(['10 TEMP=42:A$="ok> ? [TDBG READY]"', "20 END"])
        self.command("DEBUG WATCH TEMP")
        self.command("DEBUG WATCH A$")
        self.assertIn("ERROR", self.command("DEBUG WATCH VFSREAD(1)"))
        self.assertIn("MISSING TEMP", self.pause())
        result = self.pause("DEBUG STEP", 20)
        self.assertIn("VAR TEMP 42", result)
        self.assertIn("STR A$ 6F6B3E", result)
        self.send("DEBUG WATCH A+1\n")
        self.until(b"[TDBG ERROR invalid-command-or-limit]")
        self.pause("DEBUG SNAP", 20)

    def test_delay_and_input(self):
        self.setup_program(["10 A=1:DELAY 20:A=2", "20 INPUT A", "30 END"])
        self.pause()
        self.assertIn("VAR A 2", self.pause("DEBUG STEP", 20))
        self.send("DEBUG STEP\n")
        self.until(b"? ")
        self.send("17\n")
        result = self.until(b"[TDBG READY]")
        self.assertIn("PAUSED 30", result)
        self.assertIn("VAR A 17", result)

    def test_new_clears_debug_mode(self):
        self.setup_program(["10 END"])
        self.command("NEW")
        self.command("10 PRINT 42")
        self.send("RUN\n")
        result = self.until(b"ok> ")
        self.assertIn("42", result)
        self.assertNotIn("TDBG", result)


if __name__ == "__main__":
    unittest.main()
