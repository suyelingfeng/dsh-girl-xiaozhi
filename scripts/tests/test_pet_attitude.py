"""Run board-local attitude/gesture math against physical synthetic trajectories."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PetAttitudeTest(unittest.TestCase):
    def test_physical_trajectories_bias_rejection_and_gesture_debounce(self):
        compiler = os.environ.get("PET_TEST_CXX") or shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("Set PET_TEST_CXX to a host C++ compiler")
        with tempfile.TemporaryDirectory(dir=ROOT / "scripts/tests") as directory:
            executable = Path(directory) / ("pet-test.exe" if os.name == "nt" else "pet-test")
            subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT), str(Path(__file__).with_name("pet_attitude_test.cc")),
                            "-o", str(executable)], check=True, timeout=60)
            subprocess.run([str(executable)], check=True, timeout=30)
