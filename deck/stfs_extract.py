"""Minimal STFS (LIVE/PIRS/CON) extractor: lists files, extracts one by name."""
import struct
import sys


def i24le(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)


def i24be(b, o):
    return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]


class Stfs:
    def __init__(self, data, shift):
        self.d = data
        self.shift = shift
        header_size = struct.unpack_from(">I", data, 0x340)[0]
        self.base = (header_size + 0xFFF) & ~0xFFF
        self.sep = data[0x37B]
        self.ft_count = struct.unpack_from("<H", data, 0x37C)[0]
        self.ft_block = i24le(data, 0x37E)

    def phys(self, block):
        # One level-0 table before every 0xAA data blocks, one level-1 table
        # after the first 0xAA (and before every further 0x70E4).
        adj = ((block // 0xAA) + 1) << self.shift
        if block >= 0xAA:
            adj += ((block // 0x70E4) + 1) << self.shift
        return self.base + (block + adj) * 0x1000

    def hash_entry(self, block):
        # Level-0 hash table for this block's group sits just before its data.
        group = block // 0xAA
        first = group * 0xAA
        tbl = self.phys(first) - (1 << self.shift) * 0x1000
        if self.shift and not (self.sep & 1):
            tbl += 0x1000  # second (active) table
        return tbl + (block % 0xAA) * 0x18

    def next_block(self, block):
        return i24be(self.d, self.hash_entry(block) + 0x15)

    def blocks(self, start, n, contiguous):
        b = start
        for _ in range(n):
            yield b
            b = b + 1 if contiguous else self.next_block(b)

    def entries(self):
        out = []
        for blk in self.blocks(self.ft_block, self.ft_count, False):
            o = self.phys(blk)
            for i in range(0x1000 // 0x40):
                e = self.d[o + i * 0x40:o + (i + 1) * 0x40]
                flags = e[0x28]
                ln = flags & 0x3F
                if ln == 0:
                    continue
                out.append(dict(
                    name=e[:ln].decode("latin-1"),
                    dir=bool(flags & 0x80), contig=bool(flags & 0x40),
                    nblocks=i24le(e, 0x29), start=i24le(e, 0x2F),
                    size=struct.unpack_from(">I", e, 0x34)[0]))
        return out

    def read(self, ent):
        buf = bytearray()
        for b in self.blocks(ent["start"], ent["nblocks"], ent["contig"]):
            o = self.phys(b)
            buf += self.d[o:o + 0x1000]
        return bytes(buf[:ent["size"]])


def extract_all(pkg, outdir):
    import os
    data = open(pkg, "rb").read()
    s = Stfs(data, 0)
    ents = s.entries()
    if any(e["dir"] for e in ents):
        sys.exit("package has directories; not handled")
    os.makedirs(outdir, exist_ok=True)
    for e in ents:
        blob = s.read(e)
        open(os.path.join(outdir, e["name"]), "wb").write(blob)
        print(f"{e['name']} {len(blob)}")


def main():
    if sys.argv[1] == "--all":
        return extract_all(sys.argv[2], sys.argv[3])
    pkg, want, out = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(pkg, "rb").read()
    for shift in (0, 1):
        try:
            s = Stfs(data, shift)
            ents = s.entries()
        except Exception:
            continue
        hit = [e for e in ents if e["name"] == want]
        if not hit:
            continue
        blob = s.read(hit[0])
        if not blob.startswith(b"XEX2"):
            continue
        open(out, "wb").write(blob)
        print(f"shift={shift} files={[e['name'] for e in ents]}")
        print(f"wrote {out} ({len(blob)} bytes)")
        return
    sys.exit("could not find/extract " + want)


if __name__ == "__main__":
    main()
