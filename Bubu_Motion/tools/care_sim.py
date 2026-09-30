#!/usr/bin/env python3
"""Simulate the proposed care loop against weeks of made-up children.

Runs the rules in docs/care-system-plan.md at one-minute resolution while a
child is with Bubu, and in closed form while Bubu sleeps. It answers what a
table of rates cannot: does an engaged child earn the weekly HUY HIỆU, how
often does Bubu ask out loud, how long a game sitting lasts before Bubu is
tired, how many weeks to each TÌNH BẠN stage.

Nothing here runs on the device. Every number in Rules is a proposal: change
one, re-run, compare. The children are invented schedules, not measured ones;
treat the output as a check on the rules, not a forecast.

Usage:
  python3 tools/care_sim.py                  # 4 children x 100 runs x 28 days
  python3 tools/care_sim.py --runs 300 --days 56 --seed 7
  python3 tools/care_sim.py --child engaged
"""
import argparse
import random
import statistics
from dataclasses import dataclass, field

MIN_PER_DAY = 24 * 60
NEEDS = ("hungry", "dirty", "tired", "lonely")


def hm(h, m=0):
    return h * 60 + m


def in_window(m, window):
    return window[0] <= m < window[1]


@dataclass(frozen=True)
class Rules:
    # Bubu's day. Defaults; parents will set them in the portal later.
    wake: int = hm(6, 30)
    sleepy_from: int = hm(21)            # sleepy eyes + yawn; Bubu still talks
    auto_night: int = hm(21, 30)         # idle after this = asleep, no bed credit
    breakfast: tuple = (hm(6), hm(9, 30))
    lunch: tuple = (hm(11), hm(13, 30))
    dinner: tuple = (hm(17), hm(20))
    bath_window: tuple = (hm(15), hm(21, 30))
    bed_window: tuple = (hm(19, 30), hm(21, 30))

    doze_after_idle: int = 5             # existing kSleepIdleTimeoutMs
    long_nap: int = 120                  # waking from this long a nap = hungry

    # Rates per hour, by what Bubu is doing.
    awake_full: float = -3.5
    awake_clean: float = -1.5
    awake_mood: float = -2.5
    awake_energy: float = -2.0
    doze_full: float = -1.0              # dozing = the child is away
    doze_clean: float = -0.5
    doze_mood: float = -1.0
    doze_energy: float = +120.0          # +2 per minute
    night_full: float = -1.0
    night_energy: float = +15.0

    wake_hunger_cap: int = 35            # "Bubu ngủ dậy là đói"

    # Actions.
    feed: int = 30                       # 3 bites x kFeedBiteBoost (10)
    feed_refuse: int = 85                # existing kFeedFullThreshold
    feed_clean: int = -3                 # crumbs
    bath: int = 90                       # existing kBathBoost
    chat_mood: int = 10                  # kept, user decision 2026-09-30
    chat_minutes: int = 3
    game_minutes: int = 5                # one game
    game_mood: int = 12                  # typical of today's per-game rewards
    game_energy_per_min: float = -1.2
    game_clean_per_min: float = -0.7     # playing gets Bubu grubby
    care_bonus_mood: int = 5             # a need met while it was showing

    # A need "shows" (bubble, face) below these.
    hungry: int = 40
    dirty: int = 50                      # smudges already show below 60
    tired: int = 30                      # games pay half
    exhausted: int = 10                  # games pay nothing; Bubu asks for a nap
    lonely: int = 40
    # "Bubu đói thì không vui được": each need showing lowers how high CẢM XÚC
    # can go, and CẢM XÚC above that ceiling sinks toward it while awake.
    need_ceiling_step: int = 20
    ceiling_pull: float = -20.0          # per hour

    floor_full: int = 10
    floor_clean: int = 10
    floor_mood: int = 20
    no_smudge: int = 60                  # EyeAnimation::kDirtyLight

    # Voice asks: local non-verbal clips on the overlay lane.
    ask_max_per_day: int = 4
    ask_cooldown: int = 45

    # HUY HIỆU tuần: routine points per week, one per anchor per day (max 21).
    week_bronze: int = 10                # Đồng
    week_silver: int = 15                # Bạc
    week_gold: int = 20                  # Vàng: at most one slip
    week_perfect: int = 21               # Tuần hoàn hảo, its own badge

    # TÌNH BẠN.
    xp_per_anchor: int = 10              # breakfast, clean at bedtime, bed on time
    xp_happy_bonus: int = 5
    happy_bedtime: int = 60
    stage_levels: tuple = (3, 6, 10)     # Hiểu bạn, Có cá tính, Bạn thân


def xp_for_next(level):
    return 50 + 25 * level              # existing LevelSystem curve


# ---------------------------------------------------------------------------
# Children
# ---------------------------------------------------------------------------

@dataclass
class Visit:
    start: int
    plan: list                           # one slot per minute: an action or "idle"

    @property
    def end(self):
        return self.start + len(self.plan)


def make_visit(r, start, length, chats=0, games=0, feed=False, bath=False, bed=False):
    need = 2 + chats * r.chat_minutes + games + (2 if bath else 0) + (1 if bed else 0)
    plan = ["idle"] * max(length, need)
    k = 2
    if feed:
        plan[1] = "feed"
    for _ in range(int(chats)):
        plan[k] = "chat"
        for j in range(1, r.chat_minutes):
            plan[k + j] = "chat+"
        k += r.chat_minutes
    for _ in range(games):
        plan[k] = "game"
        k += 1
    if bath:
        plan[len(plan) - (2 if bed else 1)] = "bath"
    if bed:
        plan[-1] = "bed"
    return Visit(start, plan)


def settle(visits):
    """Sort and push overlapping visits later; drop any past midnight."""
    visits.sort(key=lambda v: v.start)
    out = []
    for v in visits:
        if out and v.start < out[-1].end + 1:
            v.start = out[-1].end + 1
        if v.end < MIN_PER_DAY:
            out.append(v)
    return out


class Child:
    name = ""
    p_resp = 0.5    # taps the bubble / reacts to an ask
    p_nap = 0.5     # lets a tired Bubu nap instead of playing on

    def __init__(self, r):
        self.r = r

    def v(self, *a, **k):
        return make_visit(self.r, *a, **k)

    @staticmethod
    def jit(rng, center, spread):
        return max(0, int(rng.gauss(center, spread / 2)))


class Engaged(Child):
    """Does most of the routine most days, plays 20-45 min."""
    name, p_resp, p_nap = "engaged", 0.75, 0.6

    def plan(self, wd, rng):
        V, p = [], rng.random
        if wd < 5:
            if p() < 0.9:
                V.append(self.v(self.jit(rng, hm(6, 35), 10), 8, chats=p() < 0.2, feed=p() < 0.9))
            if p() < 0.95:
                V.append(self.v(self.jit(rng, hm(16, 45), 20), 45, chats=rng.choice([1, 2]),
                                games=rng.randint(15, 30), feed=p() < 0.5))
            if p() < 0.8:
                V.append(self.v(self.jit(rng, hm(19, 15), 30), 30, chats=p() < 0.5,
                                games=rng.randint(0, 15), feed=p() < 0.7, bath=p() < 0.8))
            if p() < 0.85:
                V.append(self.v(self.jit(rng, hm(20, 50), 15), 5, bed=p() < 0.9))
        else:
            V.append(self.v(self.jit(rng, hm(7, 45), 30), 20, chats=1, games=rng.randint(0, 20), feed=p() < 0.9))
            if p() < 0.7:
                V.append(self.v(self.jit(rng, hm(10, 30), 30), 40, chats=1, games=rng.randint(20, 40)))
                V.append(self.v(self.jit(rng, hm(12, 0), 20), 10, feed=p() < 0.6))
            if p() < 0.7:
                V.append(self.v(self.jit(rng, hm(15, 30), 40), 40, chats=1, games=rng.randint(20, 40)))
            V.append(self.v(self.jit(rng, hm(19, 0), 30), 30, chats=p() < 0.5,
                            games=rng.randint(0, 15), feed=p() < 0.7, bath=p() < 0.8))
            if p() < 0.85:
                V.append(self.v(self.jit(rng, hm(20, 50), 15), 5, bed=p() < 0.9))
        return settle(V)


class Casual(Child):
    """One evening visit most days, half the routine."""
    name, p_resp, p_nap = "casual", 0.5, 0.5

    def plan(self, wd, rng):
        V, p = [], rng.random
        if wd < 5:
            if p() < 0.5:
                V.append(self.v(self.jit(rng, hm(6, 40), 10), 5, feed=p() < 0.7))
            if p() < 0.85:
                V.append(self.v(self.jit(rng, hm(18, 0), 60), 35, chats=1, games=rng.randint(20, 35),
                                feed=p() < 0.5, bath=p() < 0.4))
        else:
            for c in (hm(10, 0), hm(17, 0)):
                if p() < 0.8:
                    V.append(self.v(self.jit(rng, c, 60), 40, chats=1, games=rng.randint(20, 40),
                                    feed=p() < 0.5, bath=c > hm(15) and p() < 0.4))
        if p() < 0.4:
            V.append(self.v(self.jit(rng, hm(20, 55), 20), 3, bed=p() < 0.8))
        return settle(V)


class Forgetful(Child):
    """Two or three evenings a week."""
    name, p_resp, p_nap = "forgetful", 0.4, 0.4

    def plan(self, wd, rng):
        V, p = [], rng.random
        if p() < 0.4:
            V.append(self.v(self.jit(rng, hm(18, 0), 90), 25, chats=1, games=rng.randint(10, 25),
                            feed=p() < 0.4, bath=p() < 0.3))
            if p() < 0.15:
                V.append(self.v(self.jit(rng, hm(20, 55), 20), 3, bed=True))
        return settle(V)


class Gamer(Child):
    """Long game sittings, little care. Tests the soft play budget."""
    name, p_resp, p_nap = "gamer", 0.4, 0.2

    def plan(self, wd, rng):
        V, p = [], rng.random
        if wd < 5:
            if p() < 0.6:
                V.append(self.v(self.jit(rng, hm(6, 40), 10), 5, feed=p() < 0.6))
            V.append(self.v(self.jit(rng, hm(16, 45), 20), 100, chats=p() < 0.5,
                            games=rng.randint(80, 100), feed=p() < 0.6, bath=p() < 0.3))
            V.append(self.v(self.jit(rng, hm(19, 30), 20), 30, games=rng.randint(20, 30)))
        else:
            for c in (hm(9, 30), hm(15, 0)):
                V.append(self.v(self.jit(rng, c, 40), 95, chats=p() < 0.5, games=rng.randint(75, 90),
                                feed=p() < 0.6, bath=c > hm(15) and p() < 0.3))
        if p() < 0.4:
            V.append(self.v(self.jit(rng, hm(21, 0), 20), 3, bed=p() < 0.8))
        return settle(V)


CHILDREN = {c.name: c for c in (Engaged, Casual, Forgetful, Gamer)}


# ---------------------------------------------------------------------------
# Simulation
# ---------------------------------------------------------------------------

@dataclass
class Day:
    fed_windows: set = field(default_factory=set)
    bed: bool = False
    bedtime_mood: float = None
    bedtime_clean: float = None
    min_clean: float = 100.0
    together: int = 0
    need_min: dict = field(default_factory=lambda: {n: 0 for n in NEEDS})
    asks: int = 0
    last_ask: int = -10 ** 9
    feeds: int = 0
    refused: int = 0
    baths: int = 0
    naps: int = 0
    chats: int = 0
    game_min: int = 0
    game_min_tired: int = 0
    game_min_exhausted: int = 0
    nap_asks: int = 0

    @property
    def care_actions(self):
        return self.feeds + self.baths + self.naps + (1 if self.bed else 0)


class Sim:
    def __init__(self, r, child, rng):
        self.r, self.child, self.rng = r, child, rng
        # A new Bubu, just hatched in the evening: content, a little hungry.
        self.full, self.energy, self.clean, self.mood = 35.0, 100.0, 85.0, 70.0
        self.mode, self.mode_since, self.last_touch = "awake", 0, 0
        self.game_left = 0          # minutes left in the current game
        self.xp, self.level = 0, 1
        self.stage_day = {}
        self.days = []

    # -- state -----------------------------------------------------------
    def clamp(self):
        r = self.r
        self.full = min(100.0, max(r.floor_full, self.full))
        self.clean = min(100.0, max(r.floor_clean, self.clean))
        self.mood = min(100.0, max(r.floor_mood, self.mood))
        self.energy = min(100.0, max(0.0, self.energy))

    def drift(self, minutes, mode):
        r, h = self.r, minutes / 60.0
        if mode == "awake":
            self.full += r.awake_full * h
            self.clean += r.awake_clean * h
            self.mood += r.awake_mood * h
            self.energy += r.awake_energy * h
        elif mode == "doze":
            self.full += r.doze_full * h
            self.clean += r.doze_clean * h
            self.mood += r.doze_mood * h
            self.energy += r.doze_energy * h
        else:
            self.full += r.night_full * h
            self.energy += r.night_energy * h
        self.clamp()

    def ceiling(self):
        return 100 - self.r.need_ceiling_step * len(self.showing() - {"lonely"})

    def gain_mood(self, v):
        """Rewards cannot lift CẢM XÚC past what Bubu's needs allow."""
        self.mood = max(self.mood, min(self.mood + v, self.ceiling()))
        self.clamp()

    def showing(self):
        r, s = self.r, set()
        if self.full < r.hungry:
            s.add("hungry")
        if self.clean < r.dirty:
            s.add("dirty")
        if self.energy < r.tired:
            s.add("tired")
        if self.mood < r.lonely:
            s.add("lonely")
        return s

    def sleep(self, t, m, day, mode):
        self.mode, self.mode_since = mode, t
        if mode == "night" and m >= hm(12) and day.bedtime_mood is None:
            day.bedtime_mood = self.mood
            day.bedtime_clean = self.clean

    def wake(self, t, m, day):
        slept = t - self.mode_since
        if self.mode == "night" or slept >= self.r.long_nap:
            self.full = min(self.full, self.r.wake_hunger_cap)
        if self.mode == "night" and m >= hm(12):
            day.bed = False                 # woken again the same evening
            day.bedtime_mood = None
            day.bedtime_clean = None
        self.mode, self.mode_since = "awake", t

    # -- actions ---------------------------------------------------------
    def do_feed(self, m, day):
        r = self.r
        if self.full >= r.feed_refuse:
            day.refused += 1
            return
        was_hungry = self.full < r.hungry
        self.full += r.feed
        self.clean += r.feed_clean
        self.clamp()
        if was_hungry:
            self.gain_mood(r.care_bonus_mood)
        day.feeds += 1
        for name, w in (("breakfast", r.breakfast), ("lunch", r.lunch), ("dinner", r.dinner)):
            if in_window(m, w):
                day.fed_windows.add(name)
        self.clamp()

    def do_bath(self, m, day):
        r = self.r
        was_dirty = self.clean < r.dirty
        self.clean += r.bath
        self.clamp()
        if was_dirty:
            self.gain_mood(r.care_bonus_mood)
        day.baths += 1

    def do_chat(self, day):
        self.gain_mood(self.r.chat_mood)
        day.chats += 1

    def respond(self, need, t, m, day, visit):
        """The child reacts to the bubble or an ask. Returns True if the visit ends."""
        rng = self.rng
        if need == "tired":
            if rng.random() < self.child.p_nap:
                day.naps += 1
                self.gain_mood(self.r.care_bonus_mood)
                self.sleep(t, m, day, "doze")
                return True
            return False
        if rng.random() >= self.child.p_resp:
            return False
        if need == "hungry":
            self.do_feed(m, day)
        elif need == "dirty":
            self.do_bath(m, day)
        elif need == "lonely":
            self.do_chat(day)
        elif need == "sleepy" and in_window(m, self.r.bed_window):
            day.bed = True
            self.sleep(t, m, day, "night")
            return True
        return False

    def ask_candidates(self, m):
        r, s, out = self.r, self.showing(), []
        if "hungry" in s and (in_window(m, r.breakfast) or in_window(m, r.lunch) or in_window(m, r.dinner)):
            out.append("hungry")
        if "dirty" in s and in_window(m, r.bath_window):
            out.append("dirty")
        if m >= r.sleepy_from:
            out.append("sleepy")
        return out

    def maybe_ask(self, t, m, day, visit, need=None):
        r = self.r
        if day.asks >= r.ask_max_per_day or t - day.last_ask < r.ask_cooldown:
            return False
        cands = [need] if need else self.ask_candidates(m)
        if not cands:
            return False
        day.asks += 1
        day.last_ask = t
        return self.respond(cands[0], t, m, day, visit)

    # -- one minute with the child -----------------------------------------
    def minute_with_child(self, t, m, day, visit):
        r = self.r
        self.last_touch = t
        if self.mode != "awake":
            self.wake(t, m, day)
        k = m - visit.start
        act = visit.plan[k]
        self.drift(1, "awake")
        s = self.showing()
        for n in s:
            day.need_min[n] += 1
        ceiling = self.ceiling()
        if self.mood > ceiling:
            self.mood = max(ceiling, self.mood + r.ceiling_pull / 60.0)
        day.together += 1
        day.min_clean = min(day.min_clean, self.clean)

        ends = False
        if act == "game":
            if self.game_left == 0:
                self.game_left = r.game_minutes
                if self.energy < r.exhausted:
                    day.nap_asks += 1
                    if self.maybe_ask(t, m, day, visit, need="tired") or self.mode != "awake":
                        del visit.plan[k + 1:]
                        self.game_left = 0
                        return
            self.energy += r.game_energy_per_min
            self.clean += r.game_clean_per_min
            day.game_min += 1
            if self.energy < r.exhausted:
                day.game_min_exhausted += 1
            elif self.energy < r.tired:
                day.game_min_tired += 1
            self.game_left -= 1
            if self.game_left == 0:
                factor = 1.0 if self.energy >= r.tired else (0.5 if self.energy >= r.exhausted else 0.0)
                self.clamp()
                self.gain_mood(r.game_mood * factor)
            self.clamp()
            return
        self.game_left = 0
        if act == "feed":
            self.do_feed(m, day)
        elif act == "bath":
            self.do_bath(m, day)
        elif act == "chat":
            self.do_chat(day)
        elif act == "bed":
            if in_window(m, r.bed_window):
                day.bed = True
                self.sleep(t, m, day, "night")
            else:
                day.naps += 1
                self.sleep(t, m, day, "doze")
            ends = True
        if act in ("chat+",) or ends:
            return
        # Every 10 minutes the child may notice the bubble.
        if k % 10 == 0:
            need = self.most_urgent()
            if need and self.respond(need, t, m, day, visit):
                ends = True
        if not ends and self.mode == "awake":
            ends = self.maybe_ask(t, m, day, visit)
        if ends:
            del visit.plan[k + 1:]          # the child leaves Bubu to sleep

    def most_urgent(self):
        r, s = self.r, self.showing()
        order = [("hungry", self.full / r.hungry), ("dirty", self.clean / r.dirty),
                 ("tired", self.energy / max(1, r.tired)), ("lonely", self.mood / r.lonely)]
        order = [(n, v) for n, v in order if n in s]
        return min(order, key=lambda x: x[1])[0] if order else None

    # -- a day -------------------------------------------------------------
    def run_day(self, d):
        r = self.r
        visits = self.child.plan(d % 7, self.rng)
        day, m = Day(), 0
        while m < MIN_PER_DAY:
            t = d * MIN_PER_DAY + m
            v = next((x for x in visits if x.start <= m < x.end), None)
            if v is not None:
                self.minute_with_child(t, m, day, v)
                m += 1
                continue
            if self.mode == "awake":
                if t - max(self.last_touch, self.mode_since) >= r.doze_after_idle:
                    night = m >= r.auto_night or m < r.wake
                    self.sleep(t, m, day, "night" if night else "doze")
                    continue
                self.drift(1, "awake")
                day.min_clean = min(day.min_clean, self.clean)
                m += 1
                continue
            # Asleep: jump to the next thing that can change that.
            nxt = [x.start for x in visits if x.start > m]
            if m < r.wake and self.mode == "night":
                nxt.append(r.wake)
            if m < r.auto_night and self.mode == "doze":
                nxt.append(r.auto_night)
            nxt = min(nxt + [MIN_PER_DAY])
            self.drift(nxt - m, self.mode)
            m = nxt
            t = d * MIN_PER_DAY + m
            if m == r.wake and self.mode == "night" and m < MIN_PER_DAY:
                self.wake(t, m, day)          # wakes by itself, hungry
            elif m == r.auto_night and self.mode == "doze":
                self.sleep(t, m, day, "night")
        if day.bedtime_mood is None:
            day.bedtime_mood = self.mood
            day.bedtime_clean = self.clean
        self.close_day(d, day)

    def close_day(self, d, day):
        r = self.r
        day.breakfast = "breakfast" in day.fed_windows
        # Only a day spent together counts: a Bubu nobody played with stays
        # clean on its own, and that is not care.
        day.clean_bed = day.together > 0 and day.bedtime_clean >= r.no_smudge
        day.anchors = int(day.breakfast) + int(day.clean_bed) + int(day.bed)
        day.complete = day.anchors == 3
        happy = day.together > 0 and day.bedtime_mood >= r.happy_bedtime
        day.xp = r.xp_per_anchor * day.anchors + (r.xp_happy_bonus if happy else 0)
        self.xp += day.xp
        while self.xp >= xp_for_next(self.level):
            self.xp -= xp_for_next(self.level)
            self.level += 1
        for i, lv in enumerate(r.stage_levels):
            if self.level >= lv and i not in self.stage_day:
                self.stage_day[i] = d + 1
        self.days.append(day)

    def run(self, days):
        for d in range(days):
            self.run_day(d)
        return self


# ---------------------------------------------------------------------------
# Badges and report
# ---------------------------------------------------------------------------

def badges(sim):
    """First-set HUY HIỆU earned over the run (see the plan)."""
    days, r = sim.days, sim.r
    out = {"week_bronze": 0, "week_silver": 0, "week_gold": 0, "week_perfect": 0, "bed_7": 0,
           "clean_7": 0, "breakfast_7": 0, "chef": 0}
    for w in range(len(days) // 7):
        n = sum(d.anchors for d in days[w * 7:(w + 1) * 7])
        if n >= r.week_perfect:
            out["week_perfect"] += 1
        if n >= r.week_gold:
            out["week_gold"] += 1
        elif n >= r.week_silver:
            out["week_silver"] += 1
        elif n >= r.week_bronze:
            out["week_bronze"] += 1
    run = {"bed_7": 0, "clean_7": 0, "breakfast_7": 0}
    for d in days:
        for key, ok in (("bed_7", d.bed), ("clean_7", d.clean_bed), ("breakfast_7", d.breakfast)):
            run[key] = run[key] + 1 if ok else 0
            if run[key] == 7:
                out[key] += 1
                run[key] = 0
        if len(d.fed_windows) == 3:
            out["chef"] += 1
    out["days_together"] = sum(1 for d in days if d.care_actions > 0)
    return out


def pct(values, q):
    values = sorted(values)
    if not values:
        return 0
    i = min(len(values) - 1, max(0, int(round(q * (len(values) - 1)))))
    return values[i]


def report(name, sims, days):
    all_days = [d for s in sims for d in s.days]
    n = len(all_days)
    together = sum(d.together for d in all_days) or 1
    per_day = lambda f: sum(f(d) for d in all_days) / n
    game_min = sum(d.game_min for d in all_days) or 1
    weeks = days // 7
    b = [badges(s) for s in sims]
    tiers = {k: sum(x[k] for x in b) / (len(sims) * weeks) for k in ("week_bronze", "week_silver", "week_gold")}
    none = 1 - sum(tiers.values())

    print(f"\n== {name} ({len(sims)} runs x {days} days)")
    print(f"  with Bubu         {per_day(lambda d: d.together):5.0f} min/day")
    print("  need showing      " + "  ".join(
        f"{k} {100 * sum(d.need_min[k] for d in all_days) / together:4.1f}%" for k in NEEDS)
          + "   (share of time together)")
    asks = [d.asks for d in all_days]
    print(f"  voice asks        {statistics.mean(asks):4.2f}/day   p90 {pct(asks, 0.9)}   max {max(asks)}")
    print(f"  care actions      {per_day(lambda d: d.care_actions):4.2f}/day   "
          f"(feed {per_day(lambda d: d.feeds):.2f}, refused {per_day(lambda d: d.refused):.2f}, "
          f"bath {per_day(lambda d: d.baths):.2f}, bed on time {per_day(lambda d: d.bed):.2f}, "
          f"nap {per_day(lambda d: d.naps):.2f})")
    print(f"  games             {per_day(lambda d: d.game_min):5.1f} min/day   "
          f"tired {100 * sum(d.game_min_tired for d in all_days) / game_min:4.1f}%   "
          f"exhausted {100 * sum(d.game_min_exhausted for d in all_days) / game_min:4.1f}%   "
          f"nap asks {per_day(lambda d: d.nap_asks):.2f}/day")
    moods = [d.bedtime_mood for d in all_days]
    print(f"  bedtime CẢM XÚC   mean {statistics.mean(moods):4.0f}   p10 {pct(moods, 0.1):4.0f}   p90 {pct(moods, 0.9):4.0f}")
    pts = [sum(d.anchors for d in s.days[w * 7:(w + 1) * 7]) for s in sims for w in range(weeks)]
    comp = [sum(d.complete for d in s.days[w * 7:(w + 1) * 7]) for s in sims for w in range(weeks)]
    print(f"  routine points    {statistics.mean(pts):4.1f}/21 a week (p10 {pct(pts, 0.1)}, p90 {pct(pts, 0.9)})   "
          f"full days {statistics.mean(comp):.1f}/7   "
          f"(breakfast {per_day(lambda d: d.breakfast):.2f}, clean at bed {per_day(lambda d: d.clean_bed):.2f}, "
          f"bed on time {per_day(lambda d: d.bed):.2f} per day)")
    print("  week points       " + "  ".join(f">={k} {100 * sum(p >= k for p in pts) / len(pts):3.0f}%"
                                             for k in (8, 10, 12, 15, 18, 19, 20, 21)))
    print(f"  weekly badge      none {100 * none:3.0f}%   Đồng {100 * tiers['week_bronze']:3.0f}%   "
          f"Bạc {100 * tiers['week_silver']:3.0f}%   Vàng {100 * tiers['week_gold']:3.0f}%   "
          f"(+ Tuần hoàn hảo {100 * statistics.mean(x['week_perfect'] for x in b) / weeks:3.0f}%)")
    print(f"  streak badges     bed x7 {statistics.mean(x['bed_7'] for x in b):.2f}   "
          f"clean x7 {statistics.mean(x['clean_7'] for x in b):.2f}   "
          f"breakfast x7 {statistics.mean(x['breakfast_7'] for x in b):.2f}   "
          f"chef {statistics.mean(x['chef'] for x in b):.2f}   per run; "
          f"days together {statistics.mean(x['days_together'] for x in b):.1f}/{days}")
    xp = [d.xp for d in all_days]
    levels = [s.level for s in sims]
    print(f"  TÌNH BẠN          {statistics.mean(xp):4.1f} XP/day   level at day {days}: "
          f"median {statistics.median(levels):.0f} (p10 {pct(levels, 0.1)}, p90 {pct(levels, 0.9)})")
    names = ("Hiểu bạn", "Có cá tính", "Bạn thân")
    parts = []
    for i, nm in enumerate(names):
        reached = [s.stage_day[i] for s in sims if i in s.stage_day]
        share = 100 * len(reached) / len(sims)
        parts.append(f"{nm}: day {statistics.median(reached):.0f} ({share:.0f}% reach)" if reached
                     else f"{nm}: not reached")
    print("  stages            " + "   ".join(parts))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", type=int, default=100)
    ap.add_argument("--days", type=int, default=28)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--child", choices=sorted(CHILDREN), action="append")
    a = ap.parse_args()
    r = Rules()
    for name in a.child or list(CHILDREN):
        rng = random.Random(f"{a.seed}:{name}")
        sims = [Sim(r, CHILDREN[name](r), rng).run(a.days) for _ in range(a.runs)]
        report(name, sims, a.days)


if __name__ == "__main__":
    main()
