def xor_string(name, text, key=0x42):
    result = [ord(c) ^ key for c in text] + [0x00]
    array_str = ", ".join(f"0x{b:02x}" for b in result)
    print(f"# define {name} {{{array_str}}}")

keys = {}
with open('keys') as f:
    for line in f:
        line = line.strip()
        if '=' in line:
            k, v = line.split('=', 1)
            keys[k.strip()] = v.strip()

print("/* Auto-generated file - DO NOT EDIT */")
print("#ifndef KEYS_H")
print("# define KEYS_H\n")
print("# define XOR_KEY 0x42\n")
xor_string("OBS_PUB_KEY", keys.get("PUBLIC_KEY", ""))
xor_string("OBS_PRIV_KEY", keys.get("PRIVATE_KEY", ""))
print("\n#endif")