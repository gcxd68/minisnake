import sys

def xor_string(name, text, key=0x42):
    result = [ord(c) ^ key for c in text] + [0x00]
    array_str = ", ".join(f"0x{b:02x}" for b in result)
    print(f"# define {name} {{{array_str}}}")

print("/* Fichier généré automatiquement - NE PAS EDITER */")
print("#ifndef KEYS_H")
print("# define KEYS_H\n")
print("# define XOR_KEY 0x42\n")

# --- PUT YOUR DREAMLO KEYS HERE ---
xor_string("OBS_PUB_KEY", "PUT_YOUR_DREAMLO_PUBLIC_KEY_HERE")
xor_string("OBS_PRIV_KEY", "PUT_YOUR_DREAMLO_PRIVATE_KEY_HERE")
# ----------------------------------

print("\n#endif")