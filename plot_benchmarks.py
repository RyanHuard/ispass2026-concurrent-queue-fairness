#!/usr/bin/env python3
"""
Triptych plots for enqueue/dequeue fairness metrics.

Reads all CSVs from ./results and groups them by workload keyword:
  - enqueueheavy
  - dequeueheavy
  - balanced
  - pair
"""

import os
import sys
import glob
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

# Updated metric sets
ENQ_METRICS = [
    ("enq_mean_all", "Mean Depth (all)"),
    ("enq_mean_ovt", "Mean Depth (overtaken)"),
    ("enq_max",      "Max Overtake Depth"),
    ("enq_pct_avg",  "Overtaken %"),
]

DEQ_METRICS = [
    ("deq_mean_all", "Mean Depth (all)"),
    ("deq_mean_ovt", "Mean Depth (overtaken)"),
    ("deq_max",      "Max Overtake Depth"),
    ("deq_pct_avg",  "Overtaken %"),
]

REQUIRED_BASE = ["threads"]

WORKLOAD_KEYS = [
    ("enqueueheavy", "Enqueue-Heavy"),
    ("dequeueheavy", "Dequeue-Heavy"),
    ("balanced",     "Balanced"),
    ("pair", "Enqueue-Dequeue Pair")
]

def _norm(s: str) -> str:
    return "".join(c.lower() for c in s if c.isalnum() or c in ("_",))

def _derive_impl_label(path: str) -> str:
    stem = os.path.splitext(os.path.basename(path))[0]
    n = _norm(stem)
    for k, _ in WORKLOAD_KEYS:
        idx = n.rfind("_" + k)
        if idx != -1:
            stem = stem[:idx]
            break
        if n.endswith(k):
            cut = len(stem) - len(k)
            stem = stem[:cut]
            break
    return stem.strip(" _-") or path

def ensure_columns_present(df, need_cols, path):
    missing = [c for c in need_cols if c not in df.columns]
    if missing:
        raise ValueError(f"{path} missing columns: {missing}")

def savefig(fig, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print(f"Saved {path}")

def load_all_csvs(folder="./results"):
    paths = glob.glob(os.path.join(folder, "*.csv"))
    if not paths:
        print(f"[error] No CSVs found in {folder}", file=sys.stderr)
        sys.exit(1)
    out = []
    for p in paths:
        df = pd.read_csv(p)
        ensure_columns_present(df, REQUIRED_BASE, p)
        df = df.sort_values("threads")
        label = _derive_impl_label(p)
        out.append((label, df, p))
    return out

def group_by_workload(dfs):
    groups = {k: {"pretty": pretty, "series": []} for k, pretty in WORKLOAD_KEYS}
    for label, df, path in dfs:
        bucket = None
        for s in [label, os.path.basename(path)]:
            ns = _norm(s)
            for k, _ in WORKLOAD_KEYS:
                if k in ns:
                    bucket = k
                    break
            if bucket:
                break
        if not bucket:
            print(f"[warn] Could not detect workload for '{path}'. Skipping.", file=sys.stderr)
            continue
        groups[bucket]["series"].append((label, df, path))
    return groups

def plot_triptychs(out_dir, groups, metrics, prefix, logy=False, xstep=4):
    for col, ylabel in metrics:
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
            if logy or col.endswith("max"):
                ax.set_yscale("log")
            if not g["series"]:
                ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
                ax.grid(True, linestyle="--", alpha=0.2)
                continue
            for impl_label, df, _ in g["series"]:
                if col in df.columns:
                    ax.plot(df["threads"], df[col], marker="o", label=impl_label, markersize=3, linewidth=1.0)
            ax.grid(True, linestyle="--", alpha=0.2)

        axes[0].set_ylabel(ylabel)
        handles, labels = [], []
        for ax in axes:
            h, l = ax.get_legend_handles_labels()
            for hh, ll in zip(h, l):
                if ll not in labels:
                    handles.append(hh)
                    labels.append(ll)
        if handles:
            fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 1.05), ncol=max(1, len(labels)))
            fig.subplots_adjust(top=0.88, wspace=0.30)
        else:
            fig.subplots_adjust(top=0.88, wspace=0.25)

        col_short = col.replace(f"{prefix}_", "")
        out_path = os.path.join(out_dir, f"{prefix}_{col_short}_by_workload.png")

        savefig(fig, out_path)

def main():
    dfs = load_all_csvs("./results")
    out_dir = os.path.join("./results", "graphs")
    groups = group_by_workload(dfs)

    # Check presence of at least one metric group
    if not any(any(col in df.columns for col, _ in ENQ_METRICS + DEQ_METRICS)
               for _, df, _ in sum([g["series"] for g in groups.values()], [])):
        print("[error] No required enq_* or deq_* metrics found in any CSV.", file=sys.stderr)
        sys.exit(1)

    plot_triptychs(out_dir, groups, ENQ_METRICS, prefix="enq", logy=False, xstep=4)
    plot_triptychs(out_dir, groups, DEQ_METRICS, prefix="deq", logy=False, xstep=4)

if __name__ == "__main__":
    main()
