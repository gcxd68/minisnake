import random
import sys

# Base keys for the polymorphic XOR algorithm (re-randomized on every run)
TARGET_KEY = random.randint(1, 255)
PART_A = random.randint(1, 255)
PART_B = random.randint(1, 255)
PART_C = (TARGET_KEY - (PART_A ^ PART_B)) % 256
SALT = random.randint(1, 255)

def obfuscate_string(name, text):
    """
    Encodes a string into a byte array using a polymorphic XOR scheme.
    Encoding: val = (plaintext + base_key + index) % 256 ^ SALT
    Decoding (in C): c ^ SALT - (base_key + i)
    """
    result = []
    for i, c in enumerate(text):
        val = (ord(c) + TARGET_KEY + i) % 256
        val = val ^ SALT
        result.append(val)
    result.append(0x00)  # Null terminator sentinel
    array_str = ", ".join(f"0x{b:02x}" for b in result)
    print(f"# define {name} {{{array_str}}}")

# Read configuration from the local 'net' file
net = {}
try:
    with open('net') as f:
        for line in f:
            line = line.strip()
            if line and '=' in line and not line.startswith('#'):
                k, v = line.split('=', 1)
                net[k.strip()] = v.strip()
except FileNotFoundError:
    print("Error: 'net' file not found.", file=sys.stderr)
    sys.exit(1)

print("/* Auto-generated polymorphic file - DO NOT EDIT */")
print("#ifndef NET_H")
print("# define NET_H\n")

# Header guards and obfuscation constants
print(f"# define KEY_PART_A 0x{PART_A:02x}")
print(f"# define KEY_PART_B 0x{PART_B:02x}")
print(f"# define KEY_PART_C 0x{PART_C:02x}")
print(f"# define KEY_SALT   0x{SALT:02x}\n")

# Obfuscate ALL sensitive data (Keys, Host, and Port)
obfuscate_string("OBS_PUB_KEY", net.get("VPS_PUBLIC_KEY", ""))
obfuscate_string("OBS_PRIV_KEY", net.get("VPS_PRIVATE_KEY", ""))
obfuscate_string("OBS_SERVER_HOST", net.get("VPS_HOST", "127.0.0.1"))
obfuscate_string("OBS_SERVER_PORT", net.get("VPS_PORT", "8000"))

print("\n#endif")
