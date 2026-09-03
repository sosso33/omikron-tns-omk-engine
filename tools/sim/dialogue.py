#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The conversation runtime - phase 6, the last stubbed subsystem in the
decision path.

`dialog.start` loaded a conversation and the harness then declared it over.
This runs it: node by node, evaluating each branch's condition and executing
the chosen branch's action against the real `GameState`, so a conversation
becomes an execution rather than an event.

The layout is `FILE_FORMATS` "DialogNode (64 bytes)", and the two halves of the
nine pointers were **proven by tracing**, not guessed (FILE_FORMATS 1907):

    ptr[0..3]   condition script for branch k - `Game_HandleEvent` event 55,
                fired while `Dialog_TickUI` builds the reply menu, EVALUATES
                it through `Dialog_EvalBranchCondition` and takes the value
    ptr[4..7]   action script for branch k - event 59, fired when a reply is
                chosen, EXECUTES it in a throwaway context through
                `Dialog_GetBranchAction`
    param[0..3] the node to go to if branch k is taken, -1 = unused

So conditions gate and actions run, and a branch with no condition script is
simply available.

    python3 tools/sim/dialogue.py            # every shipped conversation
    python3 tools/sim/dialogue.py 387        # one, traced
"""
import os, sys, struct

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import dialog_disasm as D
import dialog_triggers as T
import gamestate
import omkpaths
from vm import VM, Trace


class Conversation:
    """One `IAM\\DIALOG` chunk, read as a graph of nodes."""

    def __init__(self, cid, blob):
        self.id = cid
        self.b = blob
        p = D.parse(blob)
        if not p: raise ValueError("dialog %d does not parse" % cid)
        self.speaker, self.n, self.ncam = p
        self.nodes = []
        for j in range(self.n):
            o = 8 + 64 * j
            self.nodes.append({
                "i": j,
                "ptr": struct.unpack_from("<9I", blob, o),
                "param": struct.unpack_from("<4h", blob, o + 36),
                "id": struct.unpack_from("<h", blob, o + 44)[0],
            })

    def branches(self, node):
        """The four branches as (k, condition ptr, action ptr, target)."""
        return [(k, node["ptr"][k], node["ptr"][4 + k], node["param"][k])
                for k in range(4)]


def run(conv, state=None, choose=None, limit=200, trace=None):
    r"""Walk the conversation from node 0. -> the trace of what it decided.

    `choose` picks among the branches whose condition passed; the default takes
    the first, which is the least interesting policy that still exercises every
    condition. Which reply a person picks is player input and is not in the
    data - what is tested is that the conditions evaluate, the actions run, and
    the graph terminates.

    A visited guard is deliberate rather than incidental: a conversation is a
    graph and nothing in the format forbids a cycle, so "it terminates" has to
    be something this can observe rather than assume.
    """
    state = state or gamestate.load()
    trace = trace if trace is not None else Trace(keep=True)
    at, steps, seen, path = 0, 0, set(), []
    while 0 <= at < conv.n and steps < limit:
        steps += 1
        node = conv.nodes[at]
        if at in seen: return {"path": path, "end": "cycle at %d" % at,
                               "steps": steps, "trace": trace}
        seen.add(at)
        avail = []
        for k, cond, act, tgt in conv.branches(node):
            if tgt < 0 or tgt >= conv.n: continue
            ok = True
            if cond:
                vm = VM(state, trace)
                why, _pc = vm.run(conv.b, cond)
                # `Dialog_EvalBranchCondition` takes the top of stack
                ok = bool(vm.stack[-1]) if vm.stack else False
                if why not in ("end", "dialog"): ok = False
            if ok: avail.append((k, act, tgt))
        path.append((at, node["id"], [a[0] for a in avail]))
        if not avail:
            return {"path": path, "end": "leaf", "steps": steps, "trace": trace}
        k, act, tgt = (choose(conv, at, avail) if choose else avail[0])
        if act:                       # the chosen branch's action runs
            VM(state, trace).run(conv.b, act)
        at = tgt
    return {"path": path, "end": "limit" if steps >= limit else "out of range",
            "steps": steps, "trace": trace}


def reconstruct(conv, want, state, cap=8, menu=True):
    r"""Which reply choices reproduce a captured announcement stream.

    The engine announces more than the actions: `Dialog_TickUI` evaluates
    EVERY branch's condition to build the reply menu, and a condition that
    reads a variable does so with `push.var`, which logs. So a capture records
    what the menu OFFERED as well as what the player took.

    That is not a guess - `traces/telis-dialog.log` shows it. Conversation
    402's node 12 has three conditions reading, in branch order, {669, 668,
    667}, {671} and {670}, and the capture holds exactly
    `669 668 667 671 670` as one batch at t=167.5 s. A model that logged only
    the chosen action cannot produce that run at all, and did not: searching
    with actions alone found **no** path, searching with the menu build found
    the engine's stream exactly.

    -> the list of (node, branch) choice sequences whose stream equals `want`.
    Several can match: two branches whose actions announce nothing are
    indistinguishable to the oracle, which is a fact about the evidence rather
    than a weakness of the search.
    """
    import copy
    found = []

    def tap(store):
        import goldentrace as G
        base = VM.step
        def f(self, op, raw, code, start, pc):
            for e in G.loggable(op, raw): store.append(e)
            return base(self, op, raw, code, start, pc)
        return f

    base = VM.step

    def rec(at, st, seen, picks, out):
        if len(found) >= cap or len(out) > len(want): return
        if not (0 <= at < conv.n) or at in seen:
            if out == want: found.append(list(picks))
            return
        ev, avail = [], []
        VM.step = tap(ev)
        for k, cond, act, tgt in conv.branches(conv.nodes[at]):
            if tgt < 0 or tgt >= conv.n: continue
            ok = True
            if cond:
                vm = VM(st, None); vm.run(conv.b, cond)
                ok = bool(vm.stack[-1]) if vm.stack else False
            if ok: avail.append((k, act, tgt))
        VM.step = base
        if not menu: ev = []      # the control: score the actions alone
        n = len(out)
        if want[n:n + len(ev)] != ev: return          # the menu build must match
        out = out + ev
        if not avail:
            if out == want: found.append(list(picks))
            return
        for k, act, tgt in avail:
            st2 = gamestate.GameState(bytes(st.raw))
            aev = []
            if act:
                VM.step = tap(aev)
                VM(st2, None).run(conv.b, act)
                VM.step = base
            m = len(out)
            if want[m:m + len(aev)] != aev: continue
            rec(tgt, st2, seen | {at}, picks + [(at, k)], out + aev)

    rec(0, state, frozenset(), [], [])
    VM.step = base
    return found


def corpus(limit=200):
    """Run every shipped conversation. The invariant is that they all end."""
    arch = T.archive(omkpaths.data("IAM/DIALOG"))
    tot = leaf = cyc = lim = bad = 0
    nodes = 0
    for cid, b in sorted(arch.items()):
        try: c = Conversation(cid, b)
        except ValueError: continue
        tot += 1
        st = gamestate.load()
        try:
            r = run(c, st, limit=limit)
        except Exception:
            bad += 1; continue
        nodes += len(r["path"])
        if r["end"] == "leaf": leaf += 1
        elif r["end"].startswith("cycle"): cyc += 1
        else: lim += 1
    return {"conversations": tot, "ended": leaf, "cycles": cyc,
            "hit_limit": lim, "failed": bad, "nodes": nodes}


def scripts():
    r"""Execute every condition and action in the corpus, standalone.

    The dialogue analogue of stage 1's `sim: VM executes`: the world scripts
    are one corpus and these are another, on the same VM. A condition that
    cannot be evaluated is a condition the reply menu could not be built from,
    so "they all run" is the invariant with teeth.
    """
    arch = T.archive(omkpaths.data("IAM/DIALOG"))
    st, tr = gamestate.load(), Trace(keep=False)
    tot = ok = conds = acts = 0
    targets = valid = 0
    for cid, b in sorted(arch.items()):
        p = D.parse(b)
        if not p: continue
        for j in range(p[1]):
            for t in struct.unpack_from("<4h", b, 8 + 64 * j + 36):
                if t == -1: continue
                targets += 1
                if 0 <= t < p[1]: valid += 1
        for _j, _nid, k, off in D.scripts_of(b, p[1]):
            tot += 1
            if k < 4: conds += 1
            else: acts += 1
            try:
                why, _pc = VM(st, tr).run(b, off)
            except Exception:
                continue
            if why in ("end", "dialog"): ok += 1
    return {"scripts": tot, "conditions": conds, "actions": acts,
            "executed": ok, "targets": targets, "validTargets": valid}


def main():
    a = sys.argv[1:]
    arch = T.archive(omkpaths.data("IAM/DIALOG"))
    if a:
        cid = int(a[0])
        c = Conversation(cid, arch[cid])
        r = run(c)
        print("dialog %d: %d nodes, speaker obj %d" % (cid, c.n, c.speaker))
        for at, nid, av in r["path"]:
            print("   node %-3d id %-5d branches available: %s" % (at, nid, av))
        print("   ended: %s after %d steps" % (r["end"], r["steps"]))
        return 0
    r = corpus()
    print("the conversation runtime, over the whole corpus")
    for k in ("conversations", "ended", "cycles", "hit_limit", "failed", "nodes"):
        print("   %-14s %d" % (k, r[k]))
    sc = scripts()
    for k in ("scripts", "conditions", "actions", "executed",
              "targets", "validTargets"):
        print("   %-14s %d" % (k, sc[k]))
    ok = (r["failed"] == 0 and r["hit_limit"] == 0 and r["cycles"] == 0
          and sc["executed"] == sc["scripts"]
          and sc["validTargets"] == sc["targets"])
    print("corpus: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
