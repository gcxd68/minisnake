XOR_KEY = 0x42
def xor_string(name, text, key):
    result = [ord(c) ^ key for c in text] + [0x00]
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
print("/* Auto-generated file - DO NOT EDIT */")
print("#ifndef KEYS_H")
print("# define KEYS_H\n")
print(f"# define XOR_KEY 0x{XOR_KEY:02x}\n")
xor_string("OBS_PUB_KEY", keys.get("PUBLIC_KEY", ""), XOR_KEY)
xor_string("OBS_PRIV_KEY", keys.get("PRIVATE_KEY", ""), XOR_KEY)
print("\n#endif")