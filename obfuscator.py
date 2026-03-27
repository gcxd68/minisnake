import random
TARGET_KEY = random.randint(1, 255)
PART_A = random.randint(1, 255)
PART_B = random.randint(1, 255)
PART_C = (TARGET_KEY - (PART_A ^ PART_B)) % 256
SALT = random.randint(1, 255)
def obfuscate_string(name, text):
    result = []
    for i, c in enumerate(text):
        val = (ord(c) + TARGET_KEY + i) % 256
        val = val ^ SALT
        result.append(val)
    result.append(0x00)
    array_str = ", ".join(f"0x{b:02x}" for b in result)
    print(f"# define {name} {{{array_str}}}")
keys = {}
try:
    with open('keys') as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                k, v = line.split('=', 1)
                keys[k.strip()] = v.strip()
except FileNotFoundError:
    print("/* Warning: 'keys' file not found */")
print("/* Auto-generated polymorph file - DO NOT EDIT */")
print("#ifndef KEYS_H")
print("# define KEYS_H\n")
print(f"# define KEY_PART_A 0x{PART_A:02x}")
print(f"# define KEY_PART_B 0x{PART_B:02x}")
print(f"# define KEY_PART_C 0x{PART_C:02x}")
print(f"# define KEY_SALT   0x{SALT:02x}\n")
obfuscate_string("OBS_PUB_KEY", keys.get("PUBLIC_KEY", ""))
obfuscate_string("OBS_PRIV_KEY", keys.get("PRIVATE_KEY", ""))
print("\n#endif")