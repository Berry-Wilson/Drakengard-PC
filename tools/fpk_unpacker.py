#!/usr/bin/env python3
"""
Drakengard (PS2 - SLUS_207.32) FPK & V3a (lz0+/LZO1X) Archive Unpacker and Cataloger.

Supports:
- Top-level and nested 'fpk\0' archives (IMAGE.BIN, FACEUC.BIN, SNDIMGUC.BIN, VOICEUC.BIN, MOVIE0UC.BIN, MOVIE1UC.BIN)
- Cavia '\0V3a' ('lz0+' / 'lz0c' / 'lz0l') LZO1X chunked compression
- Automatic filename recovery via embedded '.lst' manifest files and FPK header names
"""

import argparse
import json
import os
import struct
import sys
import time
from pathlib import Path


def lzo1x_decompress(src: bytes, uncomp_len: int) -> bytes:
    """Fast pure-Python LZO1X decompressor using slice copies where possible."""
    dst = bytearray(uncomp_len)
    ip = 0
    op = 0
    src_len = len(src)

    t = src[ip]
    if t > 17:
        ip += 1
        t -= 17
        dst[op : op + t] = src[ip : ip + t]
        op += t
        ip += t
        t = src[ip]
        if t < 16:
            raise ValueError("LZO1X error after initial literal run")

    while True:
        t = src[ip]
        if t < 16:
            ip += 1
            if t == 0:
                while src[ip] == 0:
                    t += 255
                    ip += 1
                t += 15 + src[ip]
                ip += 1
            t += 3
            dst[op : op + t] = src[ip : ip + t]
            op += t
            ip += t
            t = src[ip]
            if t < 16:
                ip += 1
                m_pos = op - 0x0801 - (t >> 2) - (src[ip] << 2)
                ip += 1
                dst[op] = dst[m_pos]
                dst[op + 1] = dst[m_pos + 1]
                dst[op + 2] = dst[m_pos + 2]
                op += 3
                t = src[ip - 2] & 3
                if t == 0:
                    continue
                dst[op : op + t] = src[ip : ip + t]
                op += t
                ip += t
                t = src[ip]
        ip += 1

        while True:
            if t >= 64:
                m_pos = op - 1 - ((t >> 2) & 7) - (src[ip] << 3)
                ip += 1
                m_len = (t >> 5) + 1
            elif t >= 32:
                m_len = t & 31
                if m_len == 0:
                    while src[ip] == 0:
                        m_len += 255
                        ip += 1
                    m_len += 31 + src[ip]
                    ip += 1
                m_len += 2
                m_pos = op - 1 - (src[ip] >> 2) - (src[ip + 1] << 6)
                ip += 2
            elif t >= 16:
                m_pos = op - ((t & 8) << 11)
                m_len = t & 7
                if m_len == 0:
                    while src[ip] == 0:
                        m_len += 255
                        ip += 1
                    m_len += 7 + src[ip]
                    ip += 1
                m_len += 2
                m_pos -= (src[ip] >> 2) + (src[ip + 1] << 6)
                ip += 2
                if m_pos == op:
                    if op != uncomp_len or ip != src_len:
                        raise ValueError(f"LZO1X EOF size mismatch: op={op}/{uncomp_len}, ip={ip}/{src_len}")
                    return bytes(dst)
                m_pos -= 0x4000
            else:
                m_pos = op - 1 - (t >> 2) - (src[ip] << 2)
                ip += 1
                dst[op] = dst[m_pos]
                dst[op + 1] = dst[m_pos + 1]
                op += 2
                t = src[ip - 2] & 3
                if t == 0:
                    break
                dst[op : op + t] = src[ip : ip + t]
                op += t
                ip += t
                t = src[ip]
                ip += 1
                continue

            # Copy match of length m_len from m_pos to op
            if op - m_pos >= m_len:
                dst[op : op + m_len] = dst[m_pos : m_pos + m_len]
                op += m_len
            else:
                for _ in range(m_len):
                    dst[op] = dst[m_pos]
                    op += 1
                    m_pos += 1

            t = src[ip - 2] & 3
            if t == 0:
                break
            dst[op : op + t] = src[ip : ip + t]
            op += t
            ip += t
            t = src[ip]
            ip += 1


def decompress_v3a(data: bytes) -> bytes:
    """Decompress a Cavia '\0V3a' ('lz0+' / 'lz0c' / 'lz0l') buffer."""
    if len(data) < 32 or data[:4] != b"\x00V3a":
        return data
    magic, crc_comp, uncomp_size, comp_size, tag, chunk_max, num_chunks, crc_uncomp = struct.unpack_from("<8I", data, 0)
    if data[16:20] != b"lz0+":
        raise ValueError(f"Unsupported V3a compression tag: {data[16:20]!r}")

    out = bytearray()
    pos = 32
    for _ in range(num_chunks):
        ctag = data[pos : pos + 4]
        c_comp, c_uncomp = struct.unpack_from("<2I", data, pos + 4)
        if ctag not in (b"lz0c", b"lz0l"):
            raise ValueError(f"Unexpected V3a chunk tag {ctag!r} at offset {hex(pos)}")
        pos += 12
        chunk_data = data[pos : pos + c_comp]
        pos += c_comp
        out.extend(lzo1x_decompress(chunk_data, c_uncomp))
        if ctag == b"lz0l":
            break

    if len(out) != uncomp_size:
        raise ValueError(f"V3a total size mismatch: got {len(out)}, expected {uncomp_size}")
    return bytes(out)


def parse_fpk_header(buf: bytes):
    """Parse an FPK header and entry table from bytes."""
    if len(buf) < 128 or buf[:4] != b"fpk\x00":
        return None
    hdr = struct.unpack_from("<16I", buf, 0)
    internal_name = buf[64:128].split(b"\x00")[0].decode("ascii", errors="replace")
    count = hdr[2]
    align = hdr[3]
    table_off = hdr[4]
    data_base = hdr[10]
    data_size = hdr[11]
    entries = []
    for i in range(count):
        u0, off, size, ext_int = struct.unpack_from("<4I", buf, table_off + i * 16)
        # ext_int is stored such that big-endian bytes are readable ASCII (e.g. 'lst\0' -> 0x0074736c or 'cads' -> 0x73646163)
        ext_be = struct.pack(">I", ext_int).lstrip(b"\x00").decode("ascii", errors="replace")
        entries.append({
            "index": i,
            "unknown0": u0,
            "offset": off,
            "size": size,
            "ext": ext_be,
        })
    return {
        "internal_name": internal_name,
        "count": count,
        "align": align,
        "table_off": table_off,
        "data_base": data_base,
        "data_size": data_size,
        "entries": entries,
    }


def detect_magic(buf: bytes) -> str:
    if not buf:
        return "empty"
    if buf[:4] == b"fpk\x00":
        return "fpk"
    if buf[:4] == b"\x00V3a":
        return "v3a_lz0+"
    if buf[:4] == b"wZIM":
        return "wZIM_texture"
    if buf[:4] == b"SPK0":
        return "SPK0_pack"
    if buf[:4] == b"KPS_":
        return "KPS_text"
    if buf[:4] == b"pBAX":
        return "pBAX_hd2_sound_header"
    if buf[:4] == b"\x00\x00\x01\xba":
        return "PSS_mpeg2_video"
    if buf[:19] == b"cavia stream format":
        return "cads_cavia_audio_stream"
    return f"raw_{buf[:4].hex()}"


def sanitize_relpath(p: str) -> str:
    p = p.strip().replace("\\", "/")
    while p.startswith("../") or p.startswith("./") or p.startswith("/"):
        if p.startswith("../"):
            p = p[3:]
        elif p.startswith("./"):
            p = p[2:]
        elif p.startswith("/"):
            p = p[1:]
    return p


def catalog_archive(bin_path: Path, decompress_all: bool = True):
    """Catalog a .BIN FPK archive, optionally decompressing V3a entries to inspect nested FPKs."""
    with open(bin_path, "rb") as f:
        top_hdr_bytes = f.read(0x10000)
        info = parse_fpk_header(top_hdr_bytes)
        if not info:
            raise ValueError(f"{bin_path} is not a valid FPK archive")

        # Check if entry 0 is a top-level .lst file (like MOVIE0UC.BIN / MOVIE1UC.BIN)
        top_lst_names = []
        if info["entries"] and info["entries"][0]["ext"] == "lst" and info["entries"][0]["size"] > 0:
            e0 = info["entries"][0]
            f.seek(info["data_base"] + e0["offset"])
            lst_data = f.read(e0["size"]).decode("ascii", errors="replace")
            top_lst_names = [line.strip() for line in lst_data.splitlines() if line.strip()]

        catalog_entries = []
        for e in info["entries"]:
            idx = e["index"]
            size = e["size"]
            off = e["offset"]
            ext = e["ext"]
            top_name = top_lst_names[idx] if idx < len(top_lst_names) else None

            if size == 0:
                catalog_entries.append({
                    "index": idx,
                    "offset": off,
                    "size": 0,
                    "compressed": False,
                    "type": "empty",
                    "ext": ext,
                    "name": top_name,
                })
                continue

            f.seek(info["data_base"] + off)
            if decompress_all:
                # Read up to 64 bytes first to check if it's V3a or FPK
                peek = f.read(min(128, size))
                is_comp = peek[:4] == b"\x00V3a"
                if is_comp:
                    f.seek(info["data_base"] + off)
                    raw_comp = f.read(size)
                    uncomp_data = decompress_v3a(raw_comp)
                    uncomp_size = len(uncomp_data)
                    sub_fpk = parse_fpk_header(uncomp_data)
                    magic_type = detect_magic(uncomp_data[:32])
                elif peek[:4] == b"fpk\x00":
                    uncomp_size = size
                    # Read enough for sub-fpk header + table + any .lst entry
                    f.seek(info["data_base"] + off)
                    uncomp_data = f.read(min(size, 0x20000))
                    sub_fpk = parse_fpk_header(uncomp_data)
                    magic_type = "fpk"
                else:
                    uncomp_size = size
                    uncomp_data = peek
                    sub_fpk = None
                    magic_type = detect_magic(peek[:32])
            else:
                peek = f.read(min(128, size))
                is_comp = peek[:4] == b"\x00V3a"
                uncomp_size = struct.unpack_from("<I", peek, 8)[0] if is_comp and len(peek) >= 12 else size
                sub_fpk = parse_fpk_header(peek) if peek[:4] == b"fpk\x00" else None
                magic_type = detect_magic(peek[:32])
                uncomp_data = peek

            entry_record = {
                "index": idx,
                "offset": off,
                "comp_size": size,
                "uncomp_size": uncomp_size,
                "compressed": is_comp,
                "type": magic_type,
                "ext": ext,
                "name": top_name,
            }

            if sub_fpk:
                entry_record["fpk_internal_name"] = sub_fpk["internal_name"]
                # Check if sub_fpk has a .lst entry (usually sub-entry 0)
                sub_lst = []
                for se in sub_fpk["entries"]:
                    if se["ext"] == "lst" and se["size"] > 0:
                        s_off = sub_fpk["data_base"] + se["offset"]
                        if s_off + se["size"] <= len(uncomp_data):
                            lst_txt = uncomp_data[s_off : s_off + se["size"]].decode("ascii", errors="replace")
                            sub_lst = [line.strip() for line in lst_txt.splitlines() if line.strip()]
                        break
                sub_items = []
                for se in sub_fpk["entries"]:
                    si = se["index"]
                    sname = sub_lst[si] if si < len(sub_lst) else None
                    sub_items.append({
                        "index": si,
                        "offset": se["offset"],
                        "size": se["size"],
                        "ext": se["ext"],
                        "path": sanitize_relpath(sname) if sname else None,
                    })
                entry_record["sub_entries"] = sub_items

            catalog_entries.append(entry_record)

    return {
        "archive_file": bin_path.name,
        "internal_name": info["internal_name"],
        "total_entries": info["count"],
        "align": info["align"],
        "data_base": info["data_base"],
        "entries": catalog_entries,
    }


def extract_archive(bin_path: Path, out_dir: Path, limit: int = 0):
    """Extract an FPK .BIN archive to out_dir, decompressing V3a and unpacking nested FPKs."""
    out_dir.mkdir(parents=True, exist_ok=True)
    with open(bin_path, "rb") as f:
        top_hdr = f.read(0x10000)
        info = parse_fpk_header(top_hdr)
        if not info:
            raise ValueError(f"{bin_path} is not a valid FPK archive")

        top_lst_names = []
        if info["entries"] and info["entries"][0]["ext"] == "lst" and info["entries"][0]["size"] > 0:
            e0 = info["entries"][0]
            f.seek(info["data_base"] + e0["offset"])
            lst_data = f.read(e0["size"]).decode("ascii", errors="replace")
            top_lst_names = [line.strip() for line in lst_data.splitlines() if line.strip()]

        extracted_count = 0
        for e in info["entries"]:
            idx = e["index"]
            size = e["size"]
            if size == 0:
                continue
            if limit and extracted_count >= limit:
                break

            f.seek(info["data_base"] + e["offset"])
            data = f.read(size)
            if data[:4] == b"\x00V3a":
                data = decompress_v3a(data)

            sub_fpk = parse_fpk_header(data)
            if sub_fpk:
                # Find .lst inside sub_fpk if any
                sub_lst = []
                for se in sub_fpk["entries"]:
                    if se["ext"] == "lst" and se["size"] > 0:
                        s_off = sub_fpk["data_base"] + se["offset"]
                        lst_txt = data[s_off : s_off + se["size"]].decode("ascii", errors="replace")
                        sub_lst = [line.strip() for line in lst_txt.splitlines() if line.strip()]
                        break

                pack_folder = out_dir / f"{idx:04d}_{Path(sub_fpk['internal_name']).stem}"
                pack_folder.mkdir(parents=True, exist_ok=True)
                for se in sub_fpk["entries"]:
                    if se["size"] == 0:
                        continue
                    si = se["index"]
                    s_off = sub_fpk["data_base"] + se["offset"]
                    s_bytes = data[s_off : s_off + se["size"]]
                    if si < len(sub_lst) and sub_lst[si] and sub_lst[si] != "nil":
                        rel = sanitize_relpath(sub_lst[si])
                        dest = pack_folder / f"{si:02d}_{Path(rel).name}"
                    else:
                        ext_str = f".{se['ext'].strip('/')}" if se["ext"] else ".bin"
                        dest = pack_folder / f"{si:02d}{ext_str}"
                    dest.write_bytes(s_bytes)
            else:
                ext_str = f".{e['ext'].strip('/')}" if e["ext"] else ".bin"
                if idx < len(top_lst_names) and top_lst_names[idx]:
                    rel = sanitize_relpath(top_lst_names[idx])
                    dest = out_dir / f"{idx:04d}_{Path(rel).name}"
                else:
                    dest = out_dir / f"{idx:04d}{ext_str}"
                dest.write_bytes(data)

            extracted_count += 1
    return extracted_count


def main():
    parser = argparse.ArgumentParser(description="Drakengard FPK & V3a Archive Tool")
    parser.add_argument("input", help="Path to a .BIN file or directory containing .BIN files")
    parser.add_argument("--catalog", help="Output JSON catalog path")
    parser.add_argument("--extract", help="Output directory to extract files into")
    parser.add_argument("--limit", type=int, default=0, help="Max top-level entries to extract (0 = all)")
    args = parser.parse_args()

    in_path = Path(args.input)
    bins = sorted(in_path.glob("*.BIN")) if in_path.is_dir() else [in_path]

    all_catalogs = {}
    for b in bins:
        t0 = time.time()
        print(f"Processing {b.name} ({b.stat().st_size / (1024*1024):.1f} MB)...")
        cat = catalog_archive(b, decompress_all=True)
        dt = time.time() - t0
        non_empty = sum(1 for e in cat["entries"] if e["type"] != "empty")
        comp_cnt = sum(1 for e in cat["entries"] if e.get("compressed"))
        print(f"  -> {cat['total_entries']} entries ({non_empty} non-empty, {comp_cnt} V3a-compressed) in {dt:.2f}s")
        all_catalogs[b.name] = cat

        if args.extract:
            ext_dir = Path(args.extract) / b.stem
            cnt = extract_archive(b, ext_dir, limit=args.limit)
            print(f"  -> Extracted {cnt} top-level entries to {ext_dir}")

    if args.catalog:
        cat_path = Path(args.catalog)
        cat_path.parent.mkdir(parents=True, exist_ok=True)
        cat_path.write_text(json.dumps(all_catalogs, indent=2), encoding="utf-8")
        print(f"Saved full asset catalog to {cat_path}")


if __name__ == "__main__":
    main()
