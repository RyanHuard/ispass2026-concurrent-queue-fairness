#!/usr/bin/env python3
"""
Triptych plots for insertion fairness metrics.

For each metric in:
  - ins_mean_all
  - ins_mean_ovt
  - ins_max
  - ins_pct_avg

Create one 1×3 figure:
  [ Enqueue-heavy | Dequeue-heavy | Balanced ]

Each panel overlays ALL CSVs belonging to that workload. CSVs can be named like:
  msq_enqueueheavy.csv, fcq_enqueueheavy.csv, ... (or any names containing those keywords)
You may also pass explicit --label per CSV (optional). If not provided, the label is
derived from the filename by removing the workload suffix.

Example:
  python plot_ins_triptych.py \
    --csv msq_enqueueheavy.csv --csv fcq_enqueueheavy.csv \
    --csv msq_dequeueheavy.csv --csv fcq_dequeueheavy.csv \
    --csv msq_balanced.csv     --csv fcq_balanced.csv     \
    --out ./graphs --show
"""

import argparse
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

INS_METRICS = [
    ("ins_mean_all", "Mean Depth (all)"),
    ("ins_mean_ovt", "Mean Depth (overtaken)"),
    ("ins_max",      "Max Overtake Depth"),
    ("ins_pct_avg",  "Overtaken %"),
]

REQUIRED_BASE = ["threads"]  # we check metric columns per-figure

WORKLOAD_KEYS = [
    ("enqueueheavy", "Enqueue-Heavy"),
    ("dequeueheavy", "Dequeue-Heavy"),
    ("balanced",     "Balanced"),
    ("pair", "Enqueue-Dequeue Pair")
]

def _norm(s: str) -> str:
    return "".join(c.lower() for c in s if c.isalnum() or c in ("_",))

def _derive_impl_label(path_or_label: str) -> str:
    """Make a readable implementation label from filename/label by stripping workload tokens."""
    stem = os.path.splitext(os.path.basename(path_or_label))[0]
    n = _norm(stem)
    for k, _ in WORKLOAD_KEYS:
        idx = n.rfind("_" + k)
        if idx != -1:
            stem = stem[:idx]  # drop trailing _workload
            break
        if n.endswith(k):
            # name like 'msqenqueueheavy' (no underscore)
            cut = len(stem) - len(k)
            stem = stem[:cut]
            break
    # Cleanup: remove trailing underscores/dashes/spaces
    stem = stem.strip(" _-")
    return stem if stem else path_or_label

def ensure_columns_present(df, need_cols, path):
    missing = [c for c in need_cols if c not in df.columns]
    if missing:
        raise ValueError(f"{path} missing columns: {missing}")

def savefig(fig, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print(f"Saved {path}")

def load_labeled(csvs, labels):
    out = []
    for i, p in enumerate(csvs):
        if not os.path.exists(p):
            print(f"CSV not found: {p}", file=sys.stderr); sys.exit(1)
        df = pd.read_csv(p)
        ensure_columns_present(df, REQUIRED_BASE, p)
        df = df.sort_values("threads")
        label = labels[i] if i < len(labels) else _derive_impl_label(p)
        out.append((label, df, p))
    return out

def group_by_workload(dfs):
    """
    Return dict: key -> {"pretty": str, "series": [(impl_label, df, path), ...]}
    Keys are ordered as WORKLOAD_KEYS.
    """
    groups = {k: {"pretty": pretty, "series": []} for k, pretty in WORKLOAD_KEYS}
    for label, df, path in dfs:
        bucket = None
        # try label then filename
        candidates = [label, os.path.basename(path)]
        for s in candidates:
            ns = _norm(s)
            for k, _ in WORKLOAD_KEYS:
                if k in ns:
                    bucket = k
                    break
            if bucket:
                break
        if not bucket:
            print(f"[warn] Could not detect workload for '{path}'. "
                  f"Name should include one of {[k for k,_ in WORKLOAD_KEYS]}. Skipping.", file=sys.stderr)
            continue
        groups[bucket]["series"].append((label, df, path))
    # Warn on empty groups
    for k, g in groups.items():
        if not g["series"]:
            print(f"[warn] No CSVs found for workload '{g['pretty']}'. Panel will be empty.", file=sys.stderr)
    return groups

def plot_triptychs(out_dir, groups, logy=False, xstep=4):
    """
    For each ins_* metric, make a 1x4 figure (enqueue | dequeue | balanced | pair).
    Each panel overlays all implementations for that workload.
    """
    for col, ylabel in INS_METRICS:
        # Skip entirely if none of the CSVs in any workload has the metric
        any_has = any(
            any(col in df.columns for _, df, _ in g["series"])
            for g in groups.values()
        )
        if not any_has:
            print(f"[skip] None of the CSVs contain '{col}'.", file=sys.stderr)
            continue

        fig, axes = plt.subplots(1, 4, sharey=True, figsize=(13, 4))
        for ax, (k, pretty) in zip(axes, WORKLOAD_KEYS):
            g = groups[k]
            ax.set_title(pretty)
            ax.set_xlabel("Threads")
            ax.xaxis.set_major_locator(MultipleLocator(xstep))
            if logy or col.endswith("max"): ax.set_yscale("log")
            if not g["series"]:
                ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
                ax.grid(True, which="both", axis="both", linestyle="--", alpha=0.2)
                continue

            plotted_any = False
            for impl_label, df, _ in g["series"]:
                if col not in df.columns:
                    continue
                ax.plot(df["threads"], df[col], marker="o", label=impl_label, markersize=3, linewidth=1.0)
                plotted_any = True

            if not plotted_any:
                ax.text(0.5, 0.5, f"'{col}' missing", ha="center", va="center", transform=ax.transAxes)

            ax.grid(True, which="both", axis="both", linestyle="--", alpha=0.2)

        axes[0].set_ylabel(ylabel)
        # Build a combined legend from all visible lines (use left panel's handles or gather from all)
        handles, labels = [], []
        for ax in axes:
            h, l = ax.get_legend_handles_labels()
            for hh, ll in zip(h, l):
                if ll not in labels:
                    handles.append(hh); labels.append(ll)
        if handles:
            legend = fig.legend(
                handles, labels,
                loc="upper center",
                bbox_to_anchor=(0.5, 1.05),   # 5% below the axes
                ncol=max(1, len(labels)),
                frameon=True
            )
            # only adjust top/wspace; no need for bottom guesswork
            fig.subplots_adjust(top=0.88, wspace=0.30)
        else:
            fig.subplots_adjust(top=0.88, wspace=0.25)


        out_path = os.path.join(out_dir, f"{col}_by_workload.png")
        savefig(fig, out_path)

def main():
    ap = argparse.ArgumentParser(description="Plot ins_* triptychs with multiple CSVs per workload.")
    ap.add_argument("--csv", action="append", required=True,
                    help="Path to a CSV (repeatable). Include 'enqueueheavy', 'dequeueheavy', 'pair', or 'balanced' in the name or provide labels accordingly.")
    ap.add_argument("--label", action="append", default=[],
                    help="Optional label per --csv (repeat; same order). Used for legend/impl name.")
    ap.add_argument("--out", default=None,
                    help="Output dir (default: ./graphs next to the FIRST CSV).")
    ap.add_argument("--show", action="store_true", help="Show figures after saving")
    ap.add_argument("--logy", action="store_true", help="Log-scale y-axis")
    ap.add_argument("--xstep", type=int, default=4, help="X tick step for 'threads' (default: 4)")
    args = ap.parse_args()

    dfs = load_labeled(args.csv, args.label)
    out_dir = args.out or os.path.join(os.path.dirname(os.path.abspath(args.csv[0])), "graphs")
    groups = group_by_workload(dfs)

    # Sanity: at least one ins_* present somewhere
    if not any(any(col in df.columns for col, _ in INS_METRICS) for _, df, _ in sum([g["series"] for g in groups.values()], [])):
        print("[error] No required ins_* metrics found in any CSV.", file=sys.stderr)
        sys.exit(1)

    plot_triptychs(out_dir, groups, logy=args.logy, xstep=args.xstep)

    if args.show:
        plt.show()

if __name__ == "__main__":
    main()
