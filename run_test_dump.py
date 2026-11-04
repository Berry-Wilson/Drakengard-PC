import os
import subprocess
import sys

frame_dir = os.path.abspath("scratch_frames")
import shutil
if os.path.exists(frame_dir):
    shutil.rmtree(frame_dir)
os.makedirs(frame_dir, exist_ok=True)

env = os.environ.copy()
env["PS2X_AUTOPAD"] = "4:0.2:4000;6:0.2:0008;10:0.2:0008;15:0.2:4000"
env["PS2X_DUMP_FRAMES_DIR"] = frame_dir
env["PS2X_DUMP_FRAMES_EVERY"] = "5"
env["PS2X_MAX_TICKS"] = "450"

exe = os.path.abspath("external/PS2Recomp/out/build/ps2xRuntime/Release/ps2EntryRunner.exe")
iso_dir = os.path.abspath("iso_extracted")
elf = os.path.abspath("iso_extracted/SLUS_207.32")

print(f"Running {exe} in {iso_dir}...")
with open("dump_stdout.log", "w") as out, open("dump_stderr.log", "w") as err:
    proc = subprocess.run([exe, elf], cwd=iso_dir, env=env, stdout=out, stderr=err, timeout=120)

print(f"Finished with exit code {proc.returncode}")
files = os.listdir(frame_dir)
print(f"Dumped {len(files)} files in {frame_dir}: {files[:10]}")
