import zlib, struct, sys, os

path = sys.argv[1]
raw = open(path, "rb").read()
sig = raw[:3]
print("sig:", sig, "size:", len(raw))
if sig == b"CWS":
    body = zlib.decompress(raw[8:])
elif sig == b"FWS":
    body = raw[8:]
else:
    import lzma
    compressed_size = struct.unpack_from("<I", raw, 8)[0]
    props = raw[12:17]
    lzma_data = raw[17:]
    stream = props + struct.pack("<Q", 0xFFFFFFFFFFFFFFFF) + lzma_data
    body = lzma.decompress(stream, format=lzma.FORMAT_ALONE)
print("body len:", len(body))
pos = 0
nbits = body[pos] >> 3
total_bits = 5 + 4*nbits
total_bytes = (total_bits + 7) // 8
pos += total_bytes
pos += 2 + 2
filter_s = sys.argv[2] if len(sys.argv) > 2 else ""
while pos < len(body):
    tch = struct.unpack_from("<H", body, pos)[0]
    pos += 2
    code = tch >> 6
    length = tch & 0x3f
    if length == 0x3f:
        length = struct.unpack_from("<I", body, pos)[0]
        pos += 4
    tag_body = body[pos:pos+length]
    pos += length
    if code == 76:
        cnt = struct.unpack_from("<H", tag_body, 0)[0]
        p = 2
        print(f"SymbolClass count={cnt}")
        for _ in range(cnt):
            tid = struct.unpack_from("<H", tag_body, p)[0]
            p += 2
            end = tag_body.index(b"\x00", p)
            name = tag_body[p:end].decode('utf-8','replace')
            p = end + 1
            if not filter_s or filter_s in name:
                print(f"  chid={tid:4d}  {name}")
    if code == 0:
        break
