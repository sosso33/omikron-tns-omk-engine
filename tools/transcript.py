#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render a Claude Code session .jsonl into a readable Markdown transcript.

The raw log is one JSON object per line and is mostly tool traffic - on this
session, 16.5 MB of which the actual conversation is a small fraction. This
keeps what a person would want to re-read: the user's messages, the assistant's
prose, and a one-line trace of each tool call so the reasoning stays followable.

Full tool output and the assistant's thinking blocks are dropped by default;
pass --thinking to keep the thinking, --full to keep tool results too.
"""
import json, sys, os, re, datetime

def text_of(content):
    if isinstance(content, str): return content
    out = []
    if isinstance(content, list):
        for b in content:
            if not isinstance(b, dict): continue
            if b.get("type") == "text": out.append(b.get("text", ""))
    return "\n".join(out)

def blocks(content, want):
    if not isinstance(content, list): return []
    return [b for b in content if isinstance(b, dict) and b.get("type") == want]

def one_line(s, n=110):
    s = " ".join((s or "").split())
    return s if len(s) <= n else s[:n-1] + "…"

def render(path, keep_thinking=False, keep_results=False):
    out, turn = [], 0
    for line in open(path, encoding="utf-8", errors="replace"):
        try: d = json.loads(line)
        except Exception: continue
        t = d.get("type")
        if t not in ("user", "assistant"): continue
        msg = d.get("message", {})
        content = msg.get("content")

        if t == "user":
            # tool results come back as `user` records; skip unless asked
            if isinstance(content, list) and blocks(content, "tool_result"):
                if keep_results:
                    for b in blocks(content, "tool_result"):
                        body = b.get("content")
                        body = text_of(body) if not isinstance(body, str) else body
                        out.append("```\n%s\n```\n" % one_line(body, 4000))
                continue
            body = text_of(content).strip()
            if not body: continue
            turn += 1
            ts = d.get("timestamp", "")[:19].replace("T", " ")
            out.append("\n---\n\n## %d. User%s\n\n%s\n" % (turn, "  ·  " + ts if ts else "", body))
            continue

        if keep_thinking:
            for b in blocks(content, "thinking"):
                th = (b.get("thinking") or "").strip()
                if th: out.append("<details><summary>thinking</summary>\n\n%s\n\n</details>\n" % th)
        body = text_of(content).strip()
        if body: out.append("\n**Claude**\n\n%s\n" % body)
        for b in blocks(content, "tool_use"):
            name = b.get("name", "?")
            inp = b.get("input", {}) or {}
            hint = (inp.get("description") or inp.get("command") or
                    inp.get("file_path") or inp.get("prompt") or "")
            out.append("> `%s` — %s\n" % (name, one_line(hint)))
    return "\n".join(out)

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    src = args[0] if args else "transcript/session-raw.jsonl"
    dst = args[1] if len(args) > 1 else "transcript/session.md"
    body = render(src, "--thinking" in sys.argv, "--full" in sys.argv)
    head = ("# Session transcript\n\n"
            "Rendered from `%s` by `tools/transcript.py` on %s.\n"
            "User messages and Claude's replies in full; each tool call as a single\n"
            "line. Re-render with `--thinking` or `--full` for more.\n"
            % (os.path.basename(src), datetime.date.today().isoformat()))
    open(dst, "w", encoding="utf-8").write(head + body)
    print("%s: %d lines, %d KB" % (dst, body.count("\n") + 4, (len(head) + len(body)) // 1024))
