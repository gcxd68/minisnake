import random
import sys

# Generate a random target XOR key (the actual encoding key)
TARGET_KEY = random.randint(1, 255)

# Split TARGET_KEY into three parts to hide it in the compiled binary:
# base_key = (PART_A ^ PART_B) + PART_C == TARGET_KEY
# This makes it harder to recover by searching for a single constant.
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

# Read keys and configuration from the local 'net' file (never committed to git)
net = {}
try:
    with open('net') as f:
        for line in f:
            line = line.strip()
            # Ignore empty lines and comments
            if line and '=' in line and not line.startswith('#'):
                k, v = line.split('=', 1)
                net[k.strip()] = v.strip()
except FileNotFoundError:
    print("Error: 'net' file not found.", file=sys.stderr)
    print("Copy 'net.example' to 'net' and fill in your net configuration.", file=sys.stderr)
    sys.exit(1)

# Ensure essential Dreamlo/VPS keys are present
if not net.get("VPS_PUBLIC_KEY") or not net.get("VPS_PRIVATE_KEY"):
    print("Error: VPS_PUBLIC_KEY or VPS_PRIVATE_KEY missing from 'net' file.", file=sys.stderr)
    sys.exit(1)

# Default networking values if not specified in the net file
vps_host = net.get("VPS_HOST", "dreamlo.com")
vps_port = net.get("VPS_PORT", "80")

# Output a fresh net.h each time — constants are re-randomized on every run
# so the binary changes even if the keys don't (polymorphic obfuscation)
print("/* Auto-generated polymorphic file - DO NOT EDIT */")
print("#ifndef NET_H")
print("# define NET_H\n")

print(f"# define KEY_PART_A 0x{PART_A:02x}")
print(f"# define KEY_PART_B 0x{PART_B:02x}")
print(f"# define KEY_PART_C 0x{PART_C:02x}")
print(f"# define KEY_SALT   0x{SALT:02x}\n")

# Network configuration defines
print(f'# define SERVER_HOST    "{vps_host}"')
print(f'# define SERVER_PORT    {vps_port}\n')

# Obfuscate the keys into byte arrays
obfuscate_string("OBS_PUB_KEY", net["VPS_PUBLIC_KEY"])
obfuscate_string("OBS_PRIV_KEY", net["VPS_PRIVATE_KEY"])

print("\n#endif")
