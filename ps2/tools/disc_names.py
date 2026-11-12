"""ISO9660 name mapping for the PS2 disc. Must match ps2/src/disc_names.c."""

NAME_MAX = 31


def _fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def _map(text: str) -> str:
    out = []
    for c in text:
        if "a" <= c <= "z":
            c = c.upper()
        if not (("A" <= c <= "Z") or ("0" <= c <= "9") or c in "_."):
            c = "_"
        out.append(c)
    return "".join(out)


def component(name: str) -> str:
    raw = name.encode("utf-8")
    if len(raw) <= NAME_MAX:
        return _map(name)
    dot = name.rfind(".")
    ext = name[dot:] if dot >= 0 and len(name) - dot <= 5 else ""
    keep = NAME_MAX - len(ext) - 9
    return _map(name[:keep]) + "_%08X" % _fnv1a(raw) + _map(ext)


def path(rel: str) -> str:
    return "/".join(component(part) for part in rel.split("/") if part)
