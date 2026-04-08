#!/usr/bin/env python3
"""Summarize a samply (Firefox Profiler) capture as a flat top-N profile.

Usage:
  scripts/samply-summary.py build-profile/samply.json.gz [--top N] [--thread NAME]

Prints, for each thread (or just the filtered one), the top-N functions by
self samples and by total (inclusive) samples. Designed to produce output
small enough to paste into a conversation for analysis.

Symbol resolution: samply's JSON profile itself is not symbolicated — the
funcTable.name entries are hex offsets. If the capture was recorded with
`--unstable-presymbolicate`, a `<profile>.syms.json` sidecar sits next to
the profile and contains the address -> symbol map; this script loads it
automatically. Without the sidecar, function names fall back to hex.
"""

import argparse
import gzip
import json
import os
import sys
from collections import Counter


def load_json_maybe_gz(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        return json.load(f)


def sidecar_path_for(profile_path):
    # samply writes foo.json.gz -> foo.json.syms.json (strips .gz, appends .syms.json)
    # and also supports the older foo.syms.json layout.
    stem_no_gz = profile_path[:-3] if profile_path.endswith(".gz") else profile_path
    stem_no_json = stem_no_gz[:-5] if stem_no_gz.endswith(".json") else stem_no_gz
    candidates = [
        stem_no_gz + ".syms.json",      # foo.json.syms.json  (current samply)
        stem_no_gz + ".syms.json.gz",
        stem_no_json + ".syms.json",    # foo.syms.json       (older / alt)
        stem_no_json + ".syms.json.gz",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def _normalize_id(s):
    if not s:
        return ""
    return s.replace("-", "").lower()


def build_symbol_resolver(profile, sidecar):
    """Return a function addr_to_name(lib_index, rva) -> str or None.

    Samply's --unstable-presymbolicate sidecar has the shape:
      {
        "string_table": ["UNKNOWN", "name1", "name2", ...],
        "data": [
          {
            "debug_name": "libc.so.6",
            "debug_id":   "33309176-2ae8-453d-b50f-db9773ae48a9",
            "code_id":    "...",
            "symbol_table": [
              {"rva": 176656, "size": 159, "symbol": 1},  // index into string_table
              ...
            ]
          },
          ...
        ]
      }

    The profile's libs[i].breakpadId is the debug_id with dashes stripped,
    uppercased, and with a trailing `age` nibble (usually 0) appended.
    """
    if sidecar is None:
        return lambda lib_idx, addr: None

    string_table = sidecar.get("string_table", [])
    data = sidecar.get("data", [])
    libs = profile.get("libs", [])

    # Pre-sort each lib's symbol table by rva for bisect lookup.
    # lib_maps[lib_idx] = (sorted_rvas, sizes, symbol_names)
    import bisect

    lib_maps = [None] * len(libs)

    for entry in data:
        dbg_id = _normalize_id(entry.get("debug_id", ""))
        dbg_name = entry.get("debug_name", "")

        matching = None
        # Primary match: debug_id prefix of breakpadId.
        for i, lib in enumerate(libs):
            bp = _normalize_id(lib.get("breakpadId", ""))
            if dbg_id and bp.startswith(dbg_id):
                matching = i
                break
        # Fallback: debug_name match.
        if matching is None and dbg_name:
            for i, lib in enumerate(libs):
                if lib.get("debugName") == dbg_name or lib.get("name") == dbg_name:
                    matching = i
                    break
        if matching is None:
            continue

        syms = sorted(entry.get("symbol_table", []), key=lambda s: s["rva"])
        rvas = [s["rva"] for s in syms]
        sizes = [s["size"] for s in syms]
        names = [
            string_table[s["symbol"]] if 0 <= s["symbol"] < len(string_table) else "?"
            for s in syms
        ]
        lib_maps[matching] = (rvas, sizes, names)

    def resolve(lib_idx, addr):
        if lib_idx is None or lib_idx < 0 or lib_idx >= len(lib_maps):
            return None
        entry = lib_maps[lib_idx]
        if entry is None:
            return None
        rvas, sizes, names = entry
        # Find rightmost rva <= addr.
        i = bisect.bisect_right(rvas, addr) - 1
        if i < 0:
            return None
        start = rvas[i]
        size = sizes[i]
        if size > 0 and addr >= start + size:
            return None
        return names[i]

    return resolve


def thread_func_to_lib_addr(thread):
    """For each func index, return (lib_index, address) or (None, None).

    In the processed Firefox Profiler format, funcTable.resource indexes
    resourceTable, whose `lib` entry indexes the top-level libs table.
    The per-frame address lives in frameTable.address; we use the first
    frame we see that points at each func as that func's representative
    address. Good enough for flat-profile symbolication.
    """
    n_funcs = len(thread["funcTable"]["name"])
    func_resource = thread["funcTable"].get("resource", [-1] * n_funcs)
    resource_lib = thread.get("resourceTable", {}).get("lib", [])

    func_lib = [-1] * n_funcs
    for fi in range(n_funcs):
        r = func_resource[fi]
        if 0 <= r < len(resource_lib):
            func_lib[fi] = resource_lib[r]

    # Representative address per func: first frame we see with this func.
    func_addr = [None] * n_funcs
    frame_func = thread["frameTable"]["func"]
    frame_addr = thread["frameTable"].get("address", [-1] * len(frame_func))
    for frame_idx, fi in enumerate(frame_func):
        if func_addr[fi] is None and frame_addr[frame_idx] != -1:
            func_addr[fi] = frame_addr[frame_idx]

    return func_lib, func_addr


def flat_profile(thread):
    """Flat profile keyed by funcTable index.

    Native sampling can produce multiple funcTable entries with the same
    underlying symbol name (different frame addresses inside the same
    function). We first count by funcTable index, then in the caller we
    group by the *resolved* symbol name so a single function only appears
    once in the output.
    """
    strings = thread.get("stringArray")
    if strings is None:
        strings = thread["stringTable"]["_array"]

    frame_func = thread["frameTable"]["func"]
    stack_frame = thread["stackTable"]["frame"]
    stack_prefix = thread["stackTable"]["prefix"]

    samples = thread["samples"]
    sample_stacks = samples["stack"]
    weights = samples.get("weight") or [1] * len(sample_stacks)

    def func_of_stack(s):
        return frame_func[stack_frame[s]]  # funcTable index

    self_counts = Counter()   # funcTable index -> self samples
    total_counts = Counter()  # funcTable index -> inclusive samples
    total_samples = 0

    for s, w in zip(sample_stacks, weights):
        if s is None:
            continue
        total_samples += w
        self_counts[func_of_stack(s)] += w
        seen = set()
        cur = s
        while cur is not None:
            f = func_of_stack(cur)
            if f not in seen:
                seen.add(f)
                total_counts[f] += w
            cur = stack_prefix[cur]

    return strings, self_counts, total_counts, total_samples


def format_row(rank, count, pct, name):
    name = name if name else "(anonymous)"
    if len(name) > 120:
        name = name[:117] + "..."
    return f"  {rank:>3}. {count:>6}  {pct:>5.1f}%  {name}"


def pretty_name(func_idx, thread_strings, thread_func_name, resolver, func_lib, func_addr):
    """Resolve a funcTable index to a human-readable symbol name."""
    raw_str_idx = thread_func_name[func_idx]
    raw = thread_strings[raw_str_idx] if 0 <= raw_str_idx < len(thread_strings) else ""
    raw = raw or ""
    # If the profile already has a real name, keep it.
    if raw and not raw.startswith("0x"):
        return raw
    addr = func_addr[func_idx] if func_idx < len(func_addr) else None
    lib = func_lib[func_idx] if func_idx < len(func_lib) else -1
    if addr is not None and lib >= 0:
        resolved = resolver(lib, addr)
        if resolved:
            return resolved
    return raw or "(unknown)"


def collapse_by_name(counter, thread_strings, thread_func_name, resolver, func_lib, func_addr):
    """Merge funcTable entries that resolve to the same symbol name."""
    merged = Counter()
    for func_idx, count in counter.items():
        name = pretty_name(func_idx, thread_strings, thread_func_name, resolver, func_lib, func_addr)
        merged[name] += count
    return merged


def print_top(title, counter, total, n):
    print(f"\n{title}")
    print(f"  {'#':>3}  {'samples':>6}  {'pct':>6}  function")
    print(f"  {'-'*3}  {'-'*6}  {'-'*6}  {'-'*80}")
    for rank, (name, count) in enumerate(counter.most_common(n), 1):
        pct = 100.0 * count / total if total else 0.0
        print(format_row(rank, count, pct, name))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", help="path to samply.json(.gz)")
    ap.add_argument("--top", type=int, default=25, help="top-N functions (default 25)")
    ap.add_argument("--thread", default=None, help="only this thread name (substring match)")
    args = ap.parse_args()

    profile = load_json_maybe_gz(args.path)
    meta = profile.get("meta", {})
    threads = profile["threads"]

    sidecar_path = sidecar_path_for(args.path)
    sidecar = load_json_maybe_gz(sidecar_path) if sidecar_path else None
    resolver = build_symbol_resolver(profile, sidecar)

    print(f"Samply capture: {args.path}")
    print(f"  product:    {meta.get('product', '?')}")
    print(f"  interval:   {meta.get('interval', '?')} ms")
    print(f"  threads:    {len(threads)}")
    print(f"  sidecar:    {sidecar_path or '(none — names will be hex)'}")

    thread_info = sorted(
        ((len(t.get("samples", {}).get("stack", [])), t) for t in threads),
        key=lambda x: -x[0],
    )

    shown = 0
    for n_samples, t in thread_info:
        name = t.get("name", "?")
        if args.thread and args.thread not in name:
            continue
        if n_samples == 0:
            continue
        strings, self_c, total_c, total = flat_profile(t)
        func_lib, func_addr = thread_func_to_lib_addr(t)
        func_name = t["funcTable"]["name"]
        self_named = collapse_by_name(self_c, strings, func_name, resolver, func_lib, func_addr)
        total_named = collapse_by_name(total_c, strings, func_name, resolver, func_lib, func_addr)
        print(f"\n{'='*100}")
        print(f"Thread: {name!r}  (pid={t.get('pid', '?')}, tid={t.get('tid', '?')}, samples={total})")
        print(f"{'='*100}")
        print_top("Top by SELF samples", self_named, total, args.top)
        print_top("Top by TOTAL (inclusive) samples", total_named, total, args.top)
        shown += 1

    if shown == 0:
        print("No matching threads.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
