"""Validate the YAKR grip against real OGF/OMF assets; no game files are modified."""
import argparse
import struct, math
from pathlib import Path

class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def get(self, fmt):
        fmt = '<' + fmt
        value = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += struct.calcsize(fmt)
        return value[0] if len(value) == 1 else value
    def z(self):
        end = self.data.index(0, self.pos)
        value = self.data[self.pos:end].decode()
        self.pos = end + 1
        return value

def chunks(data):
    r = Reader(data)
    while r.pos < len(data):
        key, size = r.get('II')
        yield key, data[r.pos:r.pos+size]
        r.pos += size
    assert r.pos == len(data)

def motions(path):
    c = dict(chunks(path.read_bytes()))
    p = Reader(c[15]); ver, np = p.get('HH'); names = {}; parts = {}
    for _ in range(np):
        part = p.z(); count = p.get('H'); parts[part] = []
        for _ in range(count):
            name = p.z(); idx = p.get('I'); names[idx] = name; parts[part].append(name)
    result = {}
    for idx, data in chunks(c[14]):
        if idx == 0: continue
        r = Reader(data); name = r.z(); frames = r.get('I'); tracks = {}
        for i in range(len(names)):
            flags = r.get('B')
            if flags & 2: qs = [r.get('4h')] * frames
            else:
                r.get('I'); qs = [r.get('4h') for _ in range(frames)]
            if flags & 1:
                r.get('I'); ts = [r.get('3h' if flags & 4 else '3b') for _ in range(frames)]
                size = r.get('3f'); init = r.get('3f')
                ts = [tuple(t[j] * size[j] + init[j] for j in range(3)) for t in ts]
            else: ts = [r.get('3f')] * frames
            qs = [conj(tuple(x/math.sqrt(sum(y*y for y in q)) for x in q)) for q in qs]
            tracks[names[i]] = list(zip(qs, ts))
        assert r.pos == len(data), (name, r.pos, len(data))
        result[name] = tracks
    return result, parts

def qmul(a, b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X, w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z)
def conj(q): return (-q[0],-q[1],-q[2],q[3])
def rotate(q,t): return qmul(qmul(q,(*t,0)),conj(q))[:3]
def compose(a,b):
    q,t=a; Q,T=b; v=rotate(q,T)
    return qmul(q,Q),tuple(t[i]+v[i] for i in range(3))

def blend(a, b, t):
    q, p = a; Q, P = b
    dot = sum(x*y for x, y in zip(q, Q))
    if dot < 0:
        Q = tuple(-x for x in Q); dot = -dot
    theta = math.acos(min(dot, 1))
    f, g = ((1-t, t) if theta < 1e-5 else
            (math.sin((1-t)*theta)/math.sin(theta), math.sin(t*theta)/math.sin(theta)))
    return (tuple(f*q[i]+g*Q[i] for i in range(4)),
            tuple((1-t)*p[i]+t*P[i] for i in range(3)))


def config(path):
    sections = {}; current = None
    for line in path.read_text(encoding="cp1251").splitlines():
        line = line.split(";", 1)[0].strip()
        if not line: continue
        if line.startswith("["):
            end = line.index("]"); current = line[1:end]
            bases = line[end+1:].lstrip(":").strip()
            sections[current] = ([b.strip() for b in bases.split(",") if b.strip()], {})
        elif "=" in line and current:
            k, v = line.split("=", 1); sections[current][1][k.strip()] = v.strip()
    def resolve(name):
        bases, own = sections.get(name, ([], {})); result = {}
        for base in bases: result.update(resolve(base))
        result.update(own)
        return result
    return resolve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arms", type=Path, required=True)
    parser.add_argument("--motions", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    resolve = config(args.config)
    cfg = resolve("wpn_knife_hud")
    for section in ["wpn_knife_hud"] + [f"wpn_knife{i}_hud" for i in range(2, 6)]:
        assert resolve(section).get("item_attach_bone") == "r_hand", section
    for section in ["wpn_knife8_hud", "wpn_knife9_hud", "wpn_knife_m1_hud"]:
        assert not resolve(section).get("item_attach_bone"), section
    position = tuple(float(x) for x in cfg["item_position"].split(","))
    h, p, b = [math.radians(float(x)) for x in cfg["item_orientation"].split(",")]
    ch, cp, cb, sh, sp, sb = math.cos(h), math.cos(p), math.cos(b), math.sin(h), math.sin(p), math.sin(b)
    # Columns of the configured setHPB transform, as used by transform_tiny.
    axes = [(ch*cb-sp*sh*sb, -cp*sb, sp*ch*sb+sh*cb),
            (sp*sh*cb+ch*sb, cp*cb, sh*sb-sp*ch*cb), (-cp*sh, sp, cp*ch)]
    r = Reader(dict(chunks(args.arms.read_bytes()))[13]); count = r.get("I"); parents = {}
    for _ in range(count):
        name = r.z(); parents[name] = r.z(); r.pos += 60
    ms, _ = motions(args.motions)
    max_position = max_axis = third_drift = 0.0; total_frames = 0
    for name, tracks in ms.items():
        count = len(tracks["lead_gun"]); total_frames += count
        def pose(frame, fraction=0):
            world = {}
            def bone(n):
                if n not in world:
                    local = blend(tracks[n][frame], tracks[n][min(frame+1, count-1)], fraction)
                    world[n] = compose(bone(parents[n]), local) if parents[n] else local
                return world[n]
            return bone("r_hand"), bone("lead_gun")
        for frame in range(count):
            (hand_q, hand_t), (gun_q, gun_t) = pose(frame)
            local_t = rotate(conj(hand_q), tuple(gun_t[i]-hand_t[i] for i in range(3)))
            local_q = qmul(conj(hand_q), gun_q)
            max_position = max(max_position, math.dist(local_t, position))
            for unit, expected in zip([(1,0,0),(0,1,0),(0,0,1)], axes):
                max_axis = max(max_axis, math.dist(rotate(local_q, unit), expected))
        if name == "liz_knife_hit3_start":
            for frame in range(count-1):
                for fraction in [.25, .5, .75]:
                    (hand_q, hand_t), (_, gun_t) = pose(frame, fraction)
                    grip_t = rotate(hand_q, position)
                    corrected_t = tuple(hand_t[i]+grip_t[i] for i in range(3))
                    third_drift = max(third_drift, math.dist(corrected_t, gun_t))
    assert max_position < 0.0002, f"Configured grip displaced original poses: {max_position} m"
    assert max_axis < 0.0005, f"Configured orientation changed original poses: {max_axis}"
    assert third_drift > 0.01, "The input assets did not reproduce the third attack drift"
    print(f"PASS: {len(ms)} motions / {total_frames} frames preserve authored grip within {max_position*1000:.3f} mm.")
    print(f"PASS: orientation axis error {max_axis:.6f}; only knife variants 1-5 opt in.")
    print(f"Original third-attack interpolation deviates from the hand attachment by up to {third_drift*1000:.2f} mm.")


if __name__ == "__main__": main()
