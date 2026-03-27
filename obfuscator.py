import random
TARGET_KEY = random.randint(1, 255)
NET_TICK = random.randint(1, 255)
CONN_TIMEOUT = random.randint(1, 255)
PACKET_ALIGN = (TARGET_KEY - (NET_TICK ^ CONN_TIMEOUT)) % 256
PAYLOAD_SHIFT = random.randint(1, 255)
def obfuscate_string(name, text):
    result = []
    for i, c in enumerate(text):
        val = (ord(c) + TARGET_KEY + i) % 256
        val = val ^ PAYLOAD_SHIFT
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
print("/* Auto-generated network config - DO NOT EDIT */")
print("#ifndef KEYS_H")
print("# define KEYS_H\n")
print(f"# define NET_TICK 0x{NET_TICK:02x}")
print(f"# define CONN_TIMEOUT 0x{CONN_TIMEOUT:02x}")
print(f"# define PACKET_ALIGN 0x{PACKET_ALIGN:02x}")
print(f"# define PAYLOAD_SHIFT 0x{PAYLOAD_SHIFT:02x}\n")
obfuscate_string("NET_BUFFER_TX", keys.get("PUBLIC_KEY", ""))
obfuscate_string("NET_BUFFER_RX", keys.get("PRIVATE_KEY", ""))
print("\n#endif")