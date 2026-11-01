"""Paths across a room's navigation mesh, written as autopilot script lines (HG-DIAG-123).

A* over the slot-0 triangles (neighbour links, blocked/unlinked triangles excluded) from
the triangle under a start point to a goal: a door (its slot-7 trigger triangle) or a
point. The corridor is shortened by string pulling across the shared edges, each pulled in by `margin` so Fiona
keeps off walls.
"""
import heapq
import math

from . import layout
from .pac import RoomPac, room_path


def _xz(v):
    return (v[0], v[2])


def _cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def _inside(tri, p):
    a, b, c = (_xz(v) for v in tri['v'])
    d1, d2, d3 = _cross(a, b, p), _cross(b, c, p), _cross(c, a, p)
    return (d1 <= 0 and d2 <= 0 and d3 <= 0) or (d1 >= 0 and d2 >= 0 and d3 >= 0)


def _centroid(tri):
    return (sum(v[0] for v in tri['v']) / 3, sum(v[2] for v in tri['v']) / 3)


def load_room(archive, room):
    pac = RoomPac(archive.read(room_path(room)))
    tris, _ = layout.navigation(pac.section(0), pac.section(1), pac.section(16))
    return tris, layout.portals(pac.section(7))


def locate(tris, p, y=None):
    best, best_d = None, 1e30
    for i, t in enumerate(tris):
        if _inside(t, p):
            dy = 0 if y is None else abs(sum(v[1] for v in t['v']) / 3 - y)
            if dy < best_d:
                best, best_d = i, dy
    if best is None:   # nearest centroid
        best = min(range(len(tris)), key=lambda i: math.dist(_centroid(tris[i]), p))
    return best


def corridor(tris, start, goal):
    open_ = [(0.0, start)]
    came, cost = {start: None}, {start: 0.0}
    goal_c = _centroid(tris[goal])
    while open_:
        _, i = heapq.heappop(open_)
        if i == goal:
            break
        for k, n in enumerate(tris[i]['neighbour']):
            if n < 0 or n >= len(tris):
                continue
            c = cost[i] + math.dist(_centroid(tris[i]), _centroid(tris[n]))
            if c < cost.get(n, 1e30):
                cost[n], came[n] = c, (i, k)
                heapq.heappush(open_, (c + math.dist(_centroid(tris[n]), goal_c), n))
    if goal not in came:
        raise ValueError('no walkable path to the goal')
    path, i = [], goal
    while came[i] is not None:
        prev, k = came[i]
        path.append((prev, k))
        i = prev
    return list(reversed(path))


def _segments_cross(p, q, a, b):
    d1, d2 = _cross(p, q, a), _cross(p, q, b)
    d3, d4 = _cross(a, b, p), _cross(a, b, q)
    return d1 * d2 <= 0 and d3 * d4 <= 0


def funnel(tris, steps, start_p, goal_p, margin=1.5):
    """Waypoints: portal midpoints, then string-pulled (a straight line from a waypoint to a
    later one is kept when it crosses every shrunken portal edge in between)."""
    portals = []
    for i, k in steps:
        v = tris[i]['v']
        a, b = _xz(v[k]), _xz(v[(k + 1) % 3])
        length = math.dist(a, b)
        t = min(0.45, margin / length) if length > 0 else 0.45
        portals.append(((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t),
                        (b[0] + (a[0] - b[0]) * t, b[1] + (a[1] - b[1]) * t)))
    mids = [((a[0] + b[0]) / 2, (a[1] + b[1]) / 2) for a, b in portals]
    chain = [start_p] + mids + [goal_p]          # chain[j] lies on portal j-1 (1..len(portals))
    out, cur = [], 0
    while cur < len(chain) - 1:
        nxt = cur + 1
        for j in range(len(chain) - 1, cur + 1, -1):
            if all(_segments_cross(chain[cur], chain[j], *portals[m - 1]) for m in range(cur + 1, j)):
                nxt = j
                break
        out.append(chain[nxt])
        cur = nxt
    return out


def script(archive, room, start_p, door=None, point=None, y=None):
    tris, portals = load_room(archive, room)
    s = locate(tris, start_p, y)
    if door is not None:
        if door >= len(portals):
            raise ValueError(f'room {room:X} has {len(portals)} portals')
        g = portals[door]['triangle']
        goal_p = _centroid(tris[g])
    else:
        goal_p = point
        g = locate(tris, point, y)
    steps = corridor(tris, s, g)
    pts = funnel(tris, steps, start_p, goal_p)
    lines = [f'goto {x:.2f} {z:.2f}' for x, z in pts[:-1]]
    x, z = pts[-1]
    lines.append(f'door {x:.2f} {z:.2f}' if door is not None else f'goto {x:.2f} {z:.2f}')
    return lines
