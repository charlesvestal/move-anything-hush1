import json
from pathlib import Path


class Sim:
    def __init__(self):
        self.held = []
        self.priority = "last"
        self.hold = False
        self.note = -1

    def _refresh(self):
        if self.held:
            if self.priority == "lowest":
                self.note = min(self.held)
            else:
                self.note = self.held[-1]
        elif not self.hold:
            self.note = -1

    def apply(self, ev):
        t = ev["type"]
        if t == "note_on":
            n = ev["note"]
            if n in self.held:
                self.held.remove(n)
            self.held.append(n)
            self._refresh()
        elif t == "note_off":
            n = ev["note"]
            if n in self.held:
                self.held.remove(n)
            self._refresh()
        elif t == "hold":
            self.hold = bool(ev["value"])
            self._refresh()
        elif t == "priority":
            self.priority = "lowest" if ev["value"] == "lowest" else "last"
            self._refresh()


def simulate_events(events):
    s = Sim()
    for ev in events:
        s.apply(ev)
    return {"final_note": s.note}


def test_hold_and_last_note_priority():
    vec = json.loads(Path("tests/fixtures/control_vectors.json").read_text())["hold_last_note"]
    out = simulate_events(vec["events"])
    assert out["final_note"] == vec["expected_final_note"]


def test_lowest_note_priority():
    vec = json.loads(Path("tests/fixtures/control_vectors.json").read_text())["lowest_priority"]
    out = simulate_events(vec["events"])
    assert out["final_note"] == vec["expected_final_note"]
