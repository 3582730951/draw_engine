#!/usr/bin/env python3
import struct
import sys


SPARSE_HEADER_MAGIC = 0xED26FF3A
CHUNK_TYPE_RAW = 0xCAC1
CHUNK_TYPE_FILL = 0xCAC2
CHUNK_TYPE_DONT_CARE = 0xCAC3
CHUNK_TYPE_CRC32 = 0xCAC4


def read_exact(f, size):
    data = f.read(size)
    if len(data) != size:
        raise IOError("unexpected EOF")
    return data


def main(argv):
    if len(argv) != 3:
        print("Usage: sparse_img_to_raw.py <input.sparse.img> <output.raw.img>", file=sys.stderr)
        return 2

    in_path = argv[1]
    out_path = argv[2]

    with open(in_path, "rb") as fin:
        header = read_exact(fin, 28)
        (magic, major_version, minor_version, file_hdr_sz, chunk_hdr_sz, blk_sz, total_blks,
         total_chunks, checksum) = struct.unpack("<IHHHHIIII", header)
        if magic != SPARSE_HEADER_MAGIC:
            raise ValueError("invalid sparse image magic")
        if file_hdr_sz > 28:
            read_exact(fin, file_hdr_sz - 28)

        total_size = total_blks * blk_sz
        with open(out_path, "wb") as fout:
            fout.truncate(total_size)
            for _ in range(total_chunks):
                chunk_header = read_exact(fin, chunk_hdr_sz)
                chunk_type, _, chunk_sz, total_sz = struct.unpack("<HHII", chunk_header[:12])
                data_sz = total_sz - chunk_hdr_sz
                out_bytes = chunk_sz * blk_sz

                if chunk_type == CHUNK_TYPE_RAW:
                    data = read_exact(fin, data_sz)
                    fout.write(data)
                elif chunk_type == CHUNK_TYPE_FILL:
                    fill = read_exact(fin, 4)
                    read_exact(fin, data_sz - 4) if data_sz > 4 else None
                    if out_bytes:
                        pattern = fill * (4096 // 4)
                        remaining = out_bytes
                        while remaining > 0:
                            chunk = pattern if remaining >= len(pattern) else pattern[:remaining]
                            fout.write(chunk)
                            remaining -= len(chunk)
                elif chunk_type == CHUNK_TYPE_DONT_CARE:
                    if data_sz:
                        read_exact(fin, data_sz)
                    fout.seek(out_bytes, 1)
                elif chunk_type == CHUNK_TYPE_CRC32:
                    if data_sz:
                        read_exact(fin, data_sz)
                else:
                    raise ValueError("unknown chunk type: 0x%04x" % chunk_type)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
