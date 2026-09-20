import json, re

f = r'C:/Users/jam19/.claude/projects/d--workspace-esp32-motor-aed-xiao/2b8fad8e-9097-4984-865e-a4e3e35c8612.jsonl'

def get_text(obj):
    msg = obj.get('message', {})
    content = msg.get('content', '')
    if isinstance(content, list):
        parts = []
        for c in content:
            if isinstance(c, dict) and c.get('type') == 'text':
                parts.append(c.get('text', ''))
        return ' '.join(parts)
    return str(content)

# 找用户贴的 v1.13 输出 (含 SDA=/SCL= 和 WHO_AM_I=0x6A 且含 v1.13)
out = []
with open(f, encoding='utf-8') as fh:
    for i, line in enumerate(fh):
        try:
            obj = json.loads(line)
        except Exception:
            continue
        s = get_text(obj)
        if not s:
            continue
        if 'v1.13' in s and ('SDA=' in s) and ('WHO_AM_I' in s or '0x6A' in s) and ('===' in s or 'FW' in s):
            out.append((i, s))

print("found %d messages with v1.13 output" % len(out))
with open(r'd:\workspace_esp32\motor-aed-xiao\v113_output.txt', 'w', encoding='utf-8') as fh:
    for i, s in out:
        fh.write("===== message %d =====\n" % i)
        fh.write(s[:4000] + "\n\n")
print("written v113_output.txt")
