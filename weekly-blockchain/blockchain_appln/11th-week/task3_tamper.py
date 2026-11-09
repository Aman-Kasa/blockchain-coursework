"""Task 3: change one character in the file and compare hashes."""
import shutil
from hashing import sha256_file, bits_changed

shutil.copy("original_work.py", "modified_work.py")
with open("modified_work.py") as f:
    src = f.read()
# smallest possible change: one variable's initial value, 0 -> 1
with open("modified_work.py", "w") as f:
    f.write(src.replace("a, b = 0, 1", "a, b = 1, 1", 1))

h1 = sha256_file("original_work.py")
h2 = sha256_file("modified_work.py")
print("Change made:   'a, b = 0, 1'  ->  'a, b = 1, 1'  (1 character)")
print(f"Original hash: {h1}")
print(f"Modified hash: {h2}")
print(f"Identical?     {h1 == h2}")
print(f"Bits differing: {bits_changed(h1, h2)} / 256 "
      f"({bits_changed(h1, h2) / 256:.0%}) -> avalanche effect")
