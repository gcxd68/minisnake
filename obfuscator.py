import random

# Generate a random target XOR key (the actual encoding key)
TARGET_KEY = random.randint(1, 255)

# Split TARGET_KEY into three parts to hide it in the compiled binary:
# base_key = (PART_A ^ PART_B) + PART_C == TARGET_KEY
# This makes it harder to recover by just searching for a single constant.
PART_A = random.randint(1, 255)
PART_B = random.randint(1, 255)
PART_C = (TARGET_KEY - (PART_A ^ PART_B)) % 256

# Additional XOR salt applied before the key, adds another layer of obfuscation
SALT = random.randint(1, 255)

def obfuscate_string(name, text):
    result = []
    for i, c in enumerate(text):
        # Encoding: val = (plaintext + base_key + index) % 256 ^ SALT
        # Decoding (in C): c ^ SALT - (base_key + i)
        val = (ord(c) + TARGET_KEY + i) % 256
        val = val ^ SALT
        result.append(val)
    result.append(0x00)  # null terminator sentinel
    array_str = ", ".join(f"0x{b:02x}" for b in result)
    print(f"# define {name} {{{array_str}}}")

# Read Dreamlo keys from the 'keys' file (never committed to git)
keys = {}
try:
    with open('keys') as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                k, v = line.split('=', 1)
                keys[k.strip()] = v.strip()
except FileNotFoundError:
    import sys
    print("Error: 'keys' file not found.", file=sys.stderr)
    print("Copy 'keys.example' to 'keys' and fill in your Dreamlo keys.", file=sys.stderr)
    sys.exit(1)

if not keys.get("PUBLIC_KEY") or not keys.get("PRIVATE_KEY"):
    import sys
    print("Error: PUBLIC_KEY or PRIVATE_KEY missing from 'keys' file.", file=sys.stderr)
    print("See 'keys.example' for the expected format.", file=sys.stderr)
    sys.exit(1)

# Output a fresh keys.h each time — constants are re-randomized on every run
# so the binary changes even if the keys don't (polymorphic obfuscation)
print("/* Auto-generated polymorphic file - DO NOT EDIT */")
print("#ifndef KEYS_H")
print("# define KEYS_H\n")
print(f"# define KEY_PART_A 0x{PART_A:02x}")
print(f"# define KEY_PART_B 0x{PART_B:02x}")
print(f"# define KEY_PART_C 0x{PART_C:02x}")
print(f"# define KEY_SALT   0x{SALT:02x}\n")
obfuscate_string("OBS_PUB_KEY", keys["PUBLIC_KEY"])
obfuscate_string("OBS_PRIV_KEY", keys["PRIVATE_KEY"])
print("\n#endif")
