"""Check that invalid cache input fails before attaching to any process."""
import subprocess
import sys
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
missing = binary.with_name("does-not-exist.uedproj")
assert not missing.exists()
run = subprocess.run([str(binary), "--headless", "--pid", "1", "--cache", str(missing)],
                     capture_output=True, text=True)
assert run.returncode != 0
assert "Cache file does not exist" in run.stdout, run.stdout
assert "Attached PID" not in run.stdout
print("PASS: invalid cache fails before process attachment")
