#!/usr/bin/env python3
"""Pre-render the real horizon around a place, for missingno's shore.

    python3 tools/horizon.py --lat 47.61 --lon -122.33
    python3 tools/horizon.py --town "Innsbruck" --radius-km 6

For a grid of points around the place it casts 720 rays out to 200 km over a
digital elevation model and records, for each degree of azimuth, how high the
land stands above the level line: once for the near ground, once for the
middle distance and once for the far ranges. Earth curvature and ordinary
atmospheric refraction are taken into account.

The silhouettes are then flattened into the shore's own vertical scale (the
square-root curve the sun and moon are placed on), one byte per degree, so the
device only has to look up a column and draw it. Because the sun uses the
same curve, it goes down behind the real ridge at the real minute.

Elevation comes from
  * USGS 3DEP, 1 arc-second (about 30 m, bare earth), where it exists (the US),
  * Copernicus GLO-30 (about 30 m, surface model) everywhere else.
Both are public cloud-optimised GeoTIFFs. Only the tiles and the resolution
each distance needs are fetched (by HTTP range request), and kept in a cache,
so a first run downloads some tens of megabytes and later runs none.

Writes, into --out (default tools/horizon_out/, kept out of git):
  horizon.bin        copy to the card as /missingno/horizon.bin, or
  horizon_local.h    which is also copied to missingno/ and built in, so an
                     update over Wi-Fi carries it (also kept out of git)
  horizon.png        a preview panorama of the centre point

Both outputs say where the place is. Neither belongs in a public repository.
Needs numpy; the preview needs Pillow.
"""
import argparse, hashlib, json, math, os, struct, sys, time, urllib.parse, urllib.request, zlib
from concurrent.futures import ThreadPoolExecutor

try:
    import numpy as np
except ImportError:
    sys.exit("horizon.py needs numpy (pip install numpy)")

R_EARTH = 6371000.0
REFRACTION = 0.13            # the usual coefficient: light bends a seventh of the way round
NAZ = 360                    # stored azimuth bins, one per degree
NRAYS = 720
SCALE_MAX = 255              # byte value for a silhouette at 90 degrees
UA = "missingno-horizon/1"

# ----------------------------------------------------------------------- tiff
TSIZE = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1, 8: 2, 9: 4, 10: 8, 11: 4, 12: 8, 16: 8, 17: 8, 18: 8}
TFMT = {1: "B", 3: "H", 4: "I", 16: "Q", 12: "d", 11: "f", 8: "h", 9: "i", 17: "q", 6: "b", 7: "B", 2: "s"}


class Missing(Exception):
    pass


def fetch(url, start=None, length=None, tries=4):
    hdr = {"User-Agent": UA}
    if start is not None:
        hdr["Range"] = "bytes=%d-%d" % (start, start + length - 1)
    for k in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=60) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code in (403, 404):
                raise Missing(url)
            if k == tries - 1:
                raise
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            if k == tries - 1:
                raise
        time.sleep(1.5 * (k + 1))


class Cog:
    """Just enough of a cloud-optimised GeoTIFF reader: tiled float32, deflate or LZW,
    any predictor, with its overviews. Tiles are fetched by range and cached."""

    def __init__(self, url, cache):
        self.url, self.cache = url, cache
        key = hashlib.sha1(url.encode()).hexdigest()[:16]
        self.dir = os.path.join(cache, key)
        os.makedirs(self.dir, exist_ok=True)
        hp = os.path.join(self.dir, "head")
        if os.path.exists(hp):
            head = open(hp, "rb").read()
        else:
            head = fetch(url, 0, 1 << 16)
            open(hp, "wb").write(head)
        self.levels = self._parse(head)

    def _parse(self, head):
        e = "<" if head[:2] == b"II" else ">"
        big = struct.unpack(e + "H", head[2:4])[0] == 43
        off = struct.unpack(e + "Q", head[8:16])[0] if big else struct.unpack(e + "I", head[4:8])[0]
        levels = []
        while off:
            if off + 16 > len(head):
                raise ValueError("TIFF header runs past the first 64 KB")
            if big:
                n = struct.unpack(e + "Q", head[off:off + 8])[0]; p = off + 8; es = 20
            else:
                n = struct.unpack(e + "H", head[off:off + 2])[0]; p = off + 2; es = 12
            tags = {}
            for i in range(n):
                b = head[p + i * es:p + (i + 1) * es]
                tag, typ = struct.unpack(e + "HH", b[:4])
                cnt = struct.unpack(e + ("Q" if big else "I"), b[4:12] if big else b[4:8])[0]
                size = TSIZE.get(typ, 1) * cnt
                vb = b[12:20] if big else b[8:12]
                if size <= len(vb):
                    raw = vb[:size]
                else:
                    vo = struct.unpack(e + ("Q" if big else "I"), vb)[0]
                    raw = head[vo:vo + size]
                    if len(raw) < size:
                        raw = fetch(self.url, vo, size)
                if typ == 2:
                    tags[tag] = raw.rstrip(b"\0").decode("latin1")
                else:
                    tags[tag] = struct.unpack(e + TFMT[typ] * cnt, raw)
            q = p + n * es
            off = struct.unpack(e + ("Q" if big else "I"), head[q:q + (8 if big else 4)])[0]
            if 322 not in tags:
                raise ValueError("not a tiled TIFF")
            levels.append(dict(
                w=tags[256][0], h=tags[257][0], tw=tags[322][0], th=tags[323][0],
                offs=tags[324], cnts=tags[325], comp=tags.get(259, (1,))[0],
                pred=tags.get(317, (1,))[0], bits=tags.get(258, (32,))[0], fmt=tags.get(339, (1,))[0],
                nodata=float(tags[42113]) if 42113 in tags else None,
                scale=tags.get(33550), tie=tags.get(33922), endian=e))
        base = levels[0]
        if not base["scale"] or not base["tie"]:
            raise ValueError("no georeferencing")
        x0, y0 = base["tie"][3] - base["tie"][0] * base["scale"][0], base["tie"][4] + base["tie"][1] * base["scale"][1]
        for lv in levels:
            lv["sx"] = base["scale"][0] * base["w"] / lv["w"]
            lv["sy"] = base["scale"][1] * base["h"] / lv["h"]
            lv["x0"], lv["y0"] = x0, y0
            if lv["nodata"] is None:
                lv["nodata"] = base["nodata"]
        return levels

    def tile(self, li, tx, ty):
        lv = self.levels[li]
        across = (lv["w"] + lv["tw"] - 1) // lv["tw"]
        i = ty * across + tx
        cp = os.path.join(self.dir, "%d_%d" % (li, i))
        if os.path.exists(cp):
            data = open(cp, "rb").read()
        else:
            data = fetch(self.url, lv["offs"][i], lv["cnts"][i]) if lv["cnts"][i] else b""
            open(cp, "wb").write(data)
        tw, th = lv["tw"], lv["th"]
        if not data:
            return np.full((th, tw), np.nan, np.float32)
        if lv["comp"] in (8, 32946):
            raw = zlib.decompress(data)
        elif lv["comp"] == 5:
            raw = lzw_decode(data)
        elif lv["comp"] == 1:
            raw = data
        else:
            raise ValueError("compression %d not supported" % lv["comp"])
        bps = lv["bits"] // 8
        need = tw * th * bps
        raw = np.frombuffer(raw[:need].ljust(need, b"\0"), np.uint8).reshape(th, tw * bps)
        if lv["pred"] == 3:                                 # floating point: byte planes, differenced
            raw = np.cumsum(raw, axis=1, dtype=np.uint8)
            planes = raw.reshape(th, bps, tw).transpose(0, 2, 1)   # MSB plane first
            arr = np.ascontiguousarray(planes).view(">f%d" % bps).reshape(th, tw).astype(np.float32)
        else:
            dt = {(3, 4): "f4", (3, 8): "f8", (2, 2): "i2", (2, 4): "i4", (1, 2): "u2"}[(lv["fmt"], bps)]
            arr = raw.view(lv["endian"] + dt).reshape(th, tw)
            if lv["pred"] == 2:
                arr = np.cumsum(arr, axis=1, dtype=arr.dtype)
            arr = arr.astype(np.float32)
        if lv["nodata"] is not None:
            arr[arr == lv["nodata"]] = np.nan
        arr[(arr < -500) | (arr > 9000)] = np.nan
        return arr

    def window(self, li, lat0, lat1, lon0, lon1):
        """Pixels covering [lat0,lat1] x [lon0,lon1] at level li, with their georeference."""
        lv = self.levels[li]
        c0 = max(0, int(math.floor((lon0 - lv["x0"]) / lv["sx"])) - 1)
        c1 = min(lv["w"], int(math.ceil((lon1 - lv["x0"]) / lv["sx"])) + 1)
        r0 = max(0, int(math.floor((lv["y0"] - lat1) / lv["sy"])) - 1)
        r1 = min(lv["h"], int(math.ceil((lv["y0"] - lat0) / lv["sy"])) + 1)
        if c1 <= c0 or r1 <= r0:
            return None
        out = np.full((r1 - r0, c1 - c0), np.nan, np.float32)
        tw, th = lv["tw"], lv["th"]
        for ty in range(r0 // th, (r1 - 1) // th + 1):
            for tx in range(c0 // tw, (c1 - 1) // tw + 1):
                t = self.tile(li, tx, ty)
                ys, xs = ty * th, tx * tw
                a0, a1 = max(r0, ys), min(r1, ys + th)
                b0, b1 = max(c0, xs), min(c1, xs + tw)
                out[a0 - r0:a1 - r0, b0 - c0:b1 - c0] = t[a0 - ys:a1 - ys, b0 - xs:b1 - xs]
        # georeference of pixel centres
        return out, lv["y0"] - (r0 + 0.5) * lv["sy"], lv["x0"] + (c0 + 0.5) * lv["sx"], lv["sy"], lv["sx"]


def lzw_decode(data):
    """TIFF's LZW: MSB-first codes of 9 to 12 bits, with the early change."""
    data = bytes(data) + b"\0\0\0"
    nbits_total = (len(data) - 3) * 8
    out = bytearray()
    table = [bytes((i,)) for i in range(256)] + [b"", b""]
    pos, nbits, prev = 0, 9, None
    frombytes = int.from_bytes
    while pos + nbits <= nbits_total:
        byte = pos >> 3
        code = (frombytes(data[byte:byte + 3], "big") >> (24 - nbits - (pos & 7))) & ((1 << nbits) - 1)
        pos += nbits
        if code == 256:
            del table[258:]
            nbits, prev = 9, None
            continue
        if code == 257:
            break
        if prev is None:
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            table.append(prev + entry[:1])
        else:
            entry = prev + prev[:1]
            table.append(entry)
        out += entry
        prev = entry
        if len(table) >= (1 << nbits) - 1 and nbits < 12:
            nbits += 1
    return bytes(out)


# ------------------------------------------------------------------- sources
def tile_urls(lat, lon, source):
    """The 1-degree tile holding (lat, lon) in one source."""
    la, lo = math.floor(lat), math.floor(lon)
    if source == "3dep":
        ns = "n%02d" % (la + 1) if la + 1 >= 0 else "s%02d" % -(la + 1)
        ew = "w%03d" % -lo if lo < 0 else "e%03d" % lo
        return "https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/1/TIFF/current/%s%s/USGS_1_%s%s.tif" % (ns, ew, ns, ew)
    ns = "N%02d_00" % la if la >= 0 else "S%02d_00" % -la
    ew = "E%03d_00" % lo if lo >= 0 else "W%03d_00" % -lo
    name = "Copernicus_DSM_COG_10_%s_%s_DEM" % (ns, ew)
    return "https://copernicus-dem-30m.s3.amazonaws.com/%s/%s.tif" % (name, name)


def in_us(lat, lon):
    boxes = [(24, 50, -125, -66), (51, 72, -180, -129), (18, 23, -161, -154), (17, 19, -68, -64)]
    return any(a <= lat < b and c <= lon < d for a, b, c, d in boxes)


class Mosaic:
    """One resolution ring: a regular lat/lon grid assembled from whichever tiles cover it."""

    def __init__(self, lat0, lat1, lon0, lon1, level, sources, cache, log):
        res = (1.0 / 3600.0) * (1 << level)
        self.dlat = self.dlon = res
        self.lat_top, self.lon_left = lat1, lon0
        self.h = int(math.ceil((lat1 - lat0) / res)) + 1
        self.w = int(math.ceil((lon1 - lon0) / res)) + 1
        self.z = np.full((self.h, self.w), np.nan, np.float32)
        self.used = set()
        jobs = []
        for la in range(int(math.floor(lat0)), int(math.floor(lat1)) + 1):
            for lo in range(int(math.floor(lon0)), int(math.floor(lon1)) + 1):
                jobs.append((la, lo))

        def one(job):
            la, lo = job
            for src in sources:
                if src == "3dep" and not in_us(la + 0.5, lo + 0.5):
                    continue
                url = tile_urls(la + 0.5, lo + 0.5, src)
                try:
                    cog = Cog(url, cache)
                except Missing:
                    continue
                li = min(level, len(cog.levels) - 1)
                win = cog.window(li, max(lat0, la), min(lat1, la + 1), max(lon0, lo), min(lon1, lo + 1))
                return src, win
            return None, None

        with ThreadPoolExecutor(6) as ex:
            for (la, lo), (src, win) in zip(jobs, ex.map(one, jobs)):
                if win is None:
                    continue
                self.used.add(src)
                arr, top, left, sy, sx = win
                # nearest-neighbour into the ring grid
                rows = np.arange(self.h)
                cols = np.arange(self.w)
                lat = self.lat_top - rows * self.dlat
                lon = self.lon_left + cols * self.dlon
                rr = np.round((top - lat) / sy).astype(int)
                cc = np.round((lon - left) / sx).astype(int)
                okr = (lat >= la) & (lat < la + 1) & (rr >= 0) & (rr < arr.shape[0])
                okc = (lon >= lo) & (lon < lo + 1) & (cc >= 0) & (cc < arr.shape[1])
                if not okr.any() or not okc.any():
                    continue
                ri, ci = np.where(okr)[0], np.where(okc)[0]
                block = arr[np.ix_(rr[ri], cc[ci])]
                dst = self.z[np.ix_(ri, ci)]
                take = np.isnan(dst) & ~np.isnan(block)
                dst[take] = block[take]
                self.z[np.ix_(ri, ci)] = dst
        missing = np.isnan(self.z)
        if missing.all():
            log("  no elevation data in this ring: treating it as sea level")
        self.z[missing] = 0.0                                # open sea, or a hole

    def sample(self, lat, lon):
        y = (self.lat_top - lat) / self.dlat
        x = (lon - self.lon_left) / self.dlon
        y = np.clip(y, 0, self.h - 1.001); x = np.clip(x, 0, self.w - 1.001)
        y0 = np.floor(y).astype(int); x0 = np.floor(x).astype(int)
        fy = y - y0; fx = x - x0
        z = self.z
        return (z[y0, x0] * (1 - fx) * (1 - fy) + z[y0, x0 + 1] * fx * (1 - fy) +
                z[y0 + 1, x0] * (1 - fx) * fy + z[y0 + 1, x0 + 1] * fx * fy)


# ------------------------------------------------------------------- horizon
def distances(dmax):
    d, out = 250.0, []                               # nearer than this is the street, not the land
    while d < 3000:
        out.append(d); d += 30.0
    while d < dmax:
        out.append(d); d *= 1.006
    out.append(dmax)
    return np.array(out)


def cast(lat, lon, rings, edges, dist, eye):
    """Per band, per degree: the highest elevation angle; plus where the highest point is."""
    h0 = float(rings[0][1].sample(np.array([lat]), np.array([lon]))[0]) + eye
    az = (np.arange(NRAYS) + 0.5) * (360.0 / NRAYS)
    th = np.radians(az)[:, None]
    dl = (dist / R_EARTH)[None, :]
    p1, l1 = math.radians(lat), math.radians(lon)
    sp2 = math.sin(p1) * np.cos(dl) + math.cos(p1) * np.sin(dl) * np.cos(th)
    p2 = np.arcsin(np.clip(sp2, -1, 1))
    l2 = l1 + np.arctan2(np.sin(th) * np.sin(dl) * math.cos(p1), np.cos(dl) - math.sin(p1) * sp2)
    lat2, lon2 = np.degrees(p2), (np.degrees(l2) + 540.0) % 360.0 - 180.0
    z = np.empty_like(lat2)
    lo = 0.0
    for reach, ring in rings:
        m = (dist >= lo) & (dist < reach) if reach != rings[-1][0] else (dist >= lo)
        if m.any():
            z[:, m] = ring.sample(lat2[:, m], lon2[:, m])
        lo = reach
    drop = dist ** 2 / (2 * R_EARTH) * (1 - REFRACTION)
    ang = np.degrees(np.arctan2(z - drop[None, :] - h0, dist[None, :]))
    bands = []
    lo = 0.0
    for hi in edges:
        m = (dist >= lo) & (dist < hi * 1000.0)
        a = ang[:, m].max(axis=1) if m.any() else np.full(NRAYS, -90.0)
        bands.append(a.reshape(NAZ, NRAYS // NAZ).max(axis=1))
        lo = hi * 1000.0
    # The mountain: the highest point beyond the near ground (a hill next door is not a range).
    far = dist >= edges[0] * 1000.0 if len(edges) > 1 else dist >= 1000.0
    if ang[:, far].max() <= 0:
        far = dist >= 1000.0
    sub = ang[:, far]
    k = np.unravel_index(np.argmax(sub), sub.shape)
    peak = (float(az[k[0]]), float(sub[k]), float(dist[far][k[1]]) / 1000.0)
    return h0 - eye, np.array(bands), peak


def encode(a):
    """Angle (degrees) -> the shore's flattened scale: 0 = nothing above the level line."""
    v = np.round(np.sqrt(np.clip(a, 0, 90) / 90.0) * SCALE_MAX)
    v[a <= 0] = 0
    return np.clip(v, 0, 255).astype(np.uint8)


def geocode(town, log):
    q = urllib.parse.urlencode({"name": town, "count": 1, "format": "json"})
    js = json.loads(fetch("https://geocoding-api.open-meteo.com/v1/search?" + q))
    if not js.get("results"):
        sys.exit("no place called %r" % town)
    r = js["results"][0]
    log("%s, %s: %.4f %.4f" % (r.get("name"), r.get("country", ""), r["latitude"], r["longitude"]))
    return r["latitude"], r["longitude"]


DIRS = ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"]


def compass(az):
    return DIRS[int((az + 22.5) % 360 // 45)]


def preview(path, prof, edges, peak, ground, label):
    from PIL import Image, ImageDraw
    W, H, top = 1440, 260, 30
    K = 200.0                                        # px for the full 90 degrees, as on the device but larger
    im = Image.new("RGB", (W, H), (20, 17, 40))
    d = ImageDraw.Draw(im)
    for y in range(H - 40):
        k = y / (H - 40.0)
        d.line([(0, y), (W, y)], fill=(int(5 + 15 * k), int(4 + 13 * k), int(10 + 30 * k)))
    base = H - 40
    cols = [(10, 10, 16), (40, 36, 64), (86, 76, 112)][:len(prof)]
    for b in range(len(prof) - 1, -1, -1):
        pts = [(0, base)]
        for x in range(W):
            az = x * 360.0 / W
            v = np.interp(az, np.arange(NAZ) + 0.5, prof[b], period=360) / SCALE_MAX
            pts.append((x, base - v * K))
        pts.append((W - 1, base))
        d.polygon(pts, fill=cols[b])
    d.rectangle([0, base, W, H], fill=(8, 8, 12))
    for i, n in enumerate(["N", "NE", "E", "SE", "S", "SW", "W", "NW"]):
        x = i * W / 8
        d.line([(x, base), (x, base + 6)], fill=(140, 140, 140))
        d.text((x + 3, base + 8), n, fill=(200, 200, 200))
    px = peak[0] * W / 360.0
    d.line([(px, base - 6 - math.sqrt(max(peak[1], 0) / 90) * K), (px, top)], fill=(216, 90, 40))
    d.text((min(px + 4, W - 260), top), "%.1f deg, %.0f km" % (peak[1], peak[2]), fill=(216, 90, 40))
    d.text((6, 6), "%s   ground %d m   bands to %s km" % (label, ground, "/".join(str(e) for e in edges)), fill=(230, 230, 230))
    d.text((6, H - 16), "the shore shows east to west through the south (north of the equator), or west to east through the north", fill=(130, 130, 130))
    im.save(path)


def write_header(path, blob, note):
    with open(path, "w") as f:
        f.write("// Made by tools/horizon.py: %s\n" % note)
        f.write("// It says where this device lives, so it is kept out of git.\n#pragma once\n#include <stdint.h>\n")
        f.write("alignas(4) static const uint8_t MN_HORIZON_BLOB[%d] = {\n" % len(blob))
        for i in range(0, len(blob), 24):
            f.write(",".join("0x%02x" % b for b in blob[i:i + 24]) + ",\n")
        f.write("};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--lat", type=float); ap.add_argument("--lon", type=float)
    ap.add_argument("--town", help="a place name instead of --lat/--lon (looked up at Open-Meteo)")
    ap.add_argument("--radius-km", type=float, default=8.0, help="how far around the place to pre-render (0: just the place)")
    ap.add_argument("--step-km", type=float, default=1.0, help="grid spacing")
    ap.add_argument("--dem", choices=["auto", "3dep", "copernicus"], default="auto")
    ap.add_argument("--bands", default="4,25,200", help="outer edge of the near, middle and far ranges, km")
    ap.add_argument("--eye", type=float, default=1.6, help="eye height above the ground, m")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--out", default=os.path.join(root, "tools", "horizon_out"))
    ap.add_argument("--cache", default=os.path.join(os.path.expanduser("~"), ".cache", "missingno-dem"))
    ap.add_argument("--no-install", action="store_true", help="don't copy horizon_local.h into missingno/")
    a = ap.parse_args()
    log = lambda s: print(s, flush=True)

    if a.town:
        a.lat, a.lon = geocode(a.town, log)
    if a.lat is None or a.lon is None:
        ap.error("give --lat and --lon, or --town")
    edges = [int(x) for x in a.bands.split(",")]
    if not 1 <= len(edges) <= 4 or edges != sorted(edges):
        ap.error("--bands wants one to four increasing distances")
    dmax = edges[-1] * 1000.0
    sources = {"auto": ["3dep", "copernicus"] if in_us(a.lat, a.lon) else ["copernicus"],
               "3dep": ["3dep", "copernicus"], "copernicus": ["copernicus"]}[a.dem]

    n = int(round(a.radius_km / a.step_km)) if a.radius_km > 0 else 0
    side = 2 * n + 1
    if side * side > 4000:
        ap.error("that grid is %d points; keep it under 4000" % (side * side))
    dlat = a.step_km / 111.195
    dlon = a.step_km / (111.195 * math.cos(math.radians(a.lat)))
    lat0, lon0 = a.lat - n * dlat, a.lon - n * dlon

    # three rings of detail: full resolution close by, coarser further out
    span = n * a.step_km
    plan = [(min(10.0, edges[-1]) * 1000.0, 0), (min(50.0, edges[-1]) * 1000.0, 2), (dmax, 3)]
    rings = []
    os.makedirs(a.cache, exist_ok=True)
    for reach, level in plan:
        if rings and reach <= rings[-1][0]:
            continue
        r_km = reach / 1000.0 + span + 1.0
        la_r = r_km / 111.195
        lo_r = r_km / (111.195 * max(0.05, math.cos(math.radians(min(89.0, abs(a.lat) + la_r)))))
        log("elevation to %3d km at %d m (%s)..." % (reach / 1000, 30 * (1 << level), "+".join(sources)))
        rings.append((reach, Mosaic(a.lat - la_r, a.lat + la_r, a.lon - lo_r, a.lon + lo_r, level, sources, a.cache, log)))
    used = set().union(*(r.used for _, r in rings))

    dist = distances(dmax)
    L = len(edges)
    rec = 12 + L * NAZ
    blob = bytearray(64)
    struct.pack_into("<4sBBHiiiiHHH4HBBI", blob, 0, b"MNHZ", 1, L, NAZ,
                     int(round(lat0 * 1e6)), int(round(lon0 * 1e6)), int(round(dlat * 1e6)), int(round(dlon * 1e6)),
                     side, side, rec, *(edges + [0] * (4 - L)),
                     (1 if "3dep" in used else 0) | (2 if "copernicus" in used else 0), int(round(a.eye * 10)),
                     int(time.time()))
    t0 = time.time()
    centre = None
    for r in range(side):
        for c in range(side):
            la, lo = lat0 + r * dlat, lon0 + c * dlon
            ground, bands, peak = cast(la, lo, rings, edges, dist, a.eye)
            enc = [encode(b) for b in bands]
            blob += struct.pack("<hBBHhHH", int(round(ground)), 1, 0, int(round(peak[0] * 10)) % 3600,
                                int(round(max(-90, min(90, peak[1])) * 100)), int(round(min(6553, peak[2] * 10))), 0)
            for e in enc:
                blob += e.tobytes()
            if r == n and c == n:
                centre = (ground, enc, peak, bands)
        done = (r + 1) * side
        el = time.time() - t0
        print("\r  %d / %d points, %.0f s left   " % (done, side * side, el / done * (side * side - done)), end="", flush=True)
    print()

    os.makedirs(a.out, exist_ok=True)
    binp = os.path.join(a.out, "horizon.bin")
    open(binp, "wb").write(blob)
    note = "%d x %d points, %.1f km apart, to %d km, from %s" % (side, side, a.step_km, edges[-1], "+".join(sorted(used)) or "nothing")
    hp = os.path.join(a.out, "horizon_local.h")
    write_header(hp, blob, note)
    installed = None
    if not a.no_install:
        installed = os.path.join(root, "missingno", "horizon_local.h")
        write_header(installed, blob, note)
    ground, enc, peak, bands = centre
    try:
        preview(os.path.join(a.out, "horizon.png"), enc, edges, peak, int(ground), "%.3f %.3f" % (a.lat, a.lon))
    except ImportError:
        pass
    log("ground %d m. highest point on the horizon: %s (%.0f deg), %.2f deg up, %.0f km off." %
        (ground, compass(peak[0]), peak[0], peak[1], peak[2]))
    log("wrote %s (%d bytes) and %s" % (binp, len(blob), hp))
    if installed:
        log("installed %s: build and upload, and it is part of the firmware." % installed)
    log("or copy horizon.bin to the card as /missingno/horizon.bin (the card wins over the built-in one).")


if __name__ == "__main__":
    main()
