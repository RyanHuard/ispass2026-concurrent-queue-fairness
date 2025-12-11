#!/usr/bin/env python3
"""
Triptych plots for enqueue/dequeue fairness metrics.
Combined unfairness triptych added:
    combined_mean_ovt = enq_mean_ovt + deq_mean_ovt
"""

import os
import sys
import glob
import argparse
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

# =======================
# METRICS
# =======================

ENQ_METRICS = [
    ("enq_mean_all", "Mean Depth (all)"),
    ("enq_mean_ovt", "Mean Overtake Depth (overtaken)"),
    ("enq_count",    "Enqueue Count"),
    ("enq_pct",      "Overtake Percentage"),
]

DEQ_METRICS = [
    ("deq_mean_all", "Mean Depth (all)"),
    ("deq_mean_ovt", "Mean Overtake Depth (overtaken)"),
    ("deq_count",    "Dequeue Count"),
    ("deq_pct",      "Overtake Percentage"),
]

TIME_METRICS = [
    ("ms", "Execution Time (ms)"),
]

REQUIRED_BASE = ["threads"]

WORKLOAD_KEYS = [
    ("enqueueheavy", "Enqueue-Heavy"),
    ("dequeueheavy", "Dequeue-Heavy"),
    ("pair",         "Enqueue-Dequeue Pair"),
]

# =======================
# HELPERS
# =======================

def apply_grid(ax):
    ax.grid(True, linestyle="--", linewidth=1, alpha=0.35)

def _norm(s: str) -> str:
    return "".join(c.lower() for c in s if c.isalnum() or c == "_")

def _derive_impl_label(path: str) -> str:
    base = os.path.splitext(os.path.basename(path))[0]
    n = _norm(base)
    for wk, _ in WORKLOAD_KEYS:
        wk_norm = _norm(wk)
        if n.endswith("_" + wk_norm):
            return base[:len(base) - (len(wk_norm) + 1)]
        if n.endswith(wk_norm):
            return base[:len(base) - len(wk_norm)]
    return base

def legend_label(label: str) -> str:
    key = _norm(label).upper()
    for k in ["LCRQ","LPRQ","MS","FC","FAA"]:
        if k in key:
            return k
    return _norm(label).split("_")[0].upper()

def ensure_columns_present(df, cols, path):
    miss = [c for c in cols if c not in df.columns]
    if miss:
        raise ValueError(f"{path} missing required columns: {miss}")

def savefig(fig, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print(f"Saved {path}")

# =======================
# CSV LOADING
# =======================

def load_csv_files(csv_list, default_folder="./results"):
    paths = csv_list if csv_list else glob.glob(os.path.join(default_folder, "*.csv"))
    if not paths:
        print("[error] No CSV files found.", file=sys.stderr)
        sys.exit(1)

    out = []
    for p in paths:
        if not os.path.exists(p) or os.path.getsize(p) == 0:
            continue

        try:
            df = pd.read_csv(p)
        except Exception:
            continue

        try:
            ensure_columns_present(df, REQUIRED_BASE, p)
        except ValueError:
            continue

        df = df.sort_values("threads")
        impl_label = _derive_impl_label(p)
        out.append((impl_label, df, p))

    if not out:
        print("[error] No valid CSVs loaded.", file=sys.stderr)
        sys.exit(1)

    return out

# =======================
# GROUPING
# =======================

def group_by_workload(dfs):
    groups = {wk: {"pretty": pretty, "series": []} for wk, pretty in WORKLOAD_KEYS}
    for label, df, path in dfs:
        base = os.path.splitext(os.path.basename(path))[0]
        n = _norm(base)
        for wk, _ in WORKLOAD_KEYS:
            if _norm(wk) in n:
                groups[wk]["series"].append((label, df, path))
                break
    return groups

# =======================
# STYLE
# =======================

style_map = {
    "LPRQ": {"color": "tab:red",    "marker": "D"},
    "MS":   {"color": "tab:blue",   "marker": "o"},
    "LCRQ": {"color": "tab:orange", "marker": "s"},
    "FC":   {"color": "tab:green",  "marker": "^"},
    "FAA":  {"color": "tab:purple", "marker": "x"},
}

fallback_colors = ["tab:purple", "tab:brown", "tab:pink", "tab:gray"]
fallback_markers = ["v", "p", "X", "*"]

def pick_style(label, fallback_i):
    key = _norm(label).upper()
    for k, style in style_map.items():
        if k in key:
            return style, fallback_i
    style = {
        "color": fallback_colors[fallback_i % len(fallback_colors)],
        "marker": fallback_markers[fallback_i % len(fallback_markers)],
    }
    return style, fallback_i + 1

# =======================
# TRIPTYCH HELPERS
# =======================

def set_left_yaxis_only(ax, i, ylabel):
    if i == 0:
        ax.set_ylabel(ylabel, fontsize=8)
    else:
        ax.set_ylabel("")

# =======================
# TRIPTYCH PLOT FUNCTIONS
# =======================

def plot_metric_triptych_per_metric(out_dir, groups, metrics, prefix, logy=False, xstep=4):
    present_metrics = [
        (col, ylabel)
        for col, ylabel in metrics
        if any(any(col in df.columns for _, df, _ in g["series"]) for g in groups.values())
    ]

    if not present_metrics:
        return

    for col, ylabel in present_metrics:
        fig, axes = plt.subplots(1, len(WORKLOAD_KEYS), figsize=(len(WORKLOAD_KEYS)*4.2,3.6), squeeze=False)
        fig.suptitle(ylabel, fontsize=12)

        for i, (wk, pretty) in enumerate(WORKLOAD_KEYS):
            ax = axes[0][i]
            g = groups[wk]

            ax.set_title(pretty, fontsize=9)
            ax.set_xlabel("Threads")
            set_left_yaxis_only(ax, i, ylabel)

            ax.xaxis.set_major_locator(MultipleLocator(xstep))
            ax.set_xlim(0,20)

            fallback_i = 0
            for impl_label, df, _ in g["series"]:
                if col not in df.columns:
                    continue
                style, fallback_i = pick_style(impl_label, fallback_i)
                stats = df.groupby("threads")[col].agg(["mean","std"]).reset_index()

                ax.errorbar(
                    stats["threads"], stats["mean"],
                    yerr=stats["std"].fillna(0),
                    label=legend_label(impl_label),
                    color=style["color"],
                    marker=style["marker"],
                    markersize=4,
                    linewidth=1,
                    capsize=3,
                )

            apply_grid(ax)
            ax.legend(fontsize=7)

        savefig(fig, os.path.join(out_dir, f"{col}_triptych.png"))
        plt.close(fig)


def plot_l1_missrate_triptych(out_dir, groups):
    col = "l1_missrate"
    if not any(any(col in df.columns for _, df, _ in g["series"]) for g in groups.values()):
        return

    fig, axes = plt.subplots(1, len(WORKLOAD_KEYS), figsize=(len(WORKLOAD_KEYS)*4.2,3.6), squeeze=False)
    fig.suptitle("L1 Miss Rate", fontsize=12)

    for i, (wk, pretty) in enumerate(WORKLOAD_KEYS):
        g = groups[wk]
        ax = axes[0][i]

        ax.set_title(pretty)
        ax.set_xlabel("Threads")
        set_left_yaxis_only(ax, i, "L1 Miss Rate")

        ax.xaxis.set_major_locator(MultipleLocator(4))
        ax.set_xlim(0,20)

        fallback_i = 0
        for impl_label, df, _ in g["series"]:
            if col not in df.columns:
                continue
            style, fallback_i = pick_style(impl_label, fallback_i)
            stats = df.groupby("threads")[col].agg(["mean","std"]).reset_index()

            ax.errorbar(
                stats["threads"], stats["mean"],
                yerr=stats["std"].fillna(0),
                label=legend_label(impl_label),
                color=style["color"],
                marker=style["marker"],
                markersize=4,
                linewidth=1,
                capsize=3,
            )

        apply_grid(ax)
        ax.legend(fontsize=7)

    savefig(fig, os.path.join(out_dir, f"{col}_triptych.png"))
    plt.close(fig)


# =======================
# COMBINED FAIRNESS TRIPTYCH
# =======================

def plot_combined_overtake_triptych(out_dir, groups, xstep=4):
    col_enq = "enq_mean_ovt"
    col_deq = "deq_mean_ovt"

    any_has = any(
        any((col_enq in df.columns) and (col_deq in df.columns)
            for _, df, _ in g["series"])
        for g in groups.values()
    )
    if not any_has:
        print("[warn] no datasets have both enqueue+dequeue mean ovt")
        return

    fig, axes = plt.subplots(1, len(WORKLOAD_KEYS), figsize=(len(WORKLOAD_KEYS)*4.2, 3.6), squeeze=False)
    fig.suptitle("Combined Mean Overtake Depth", fontsize=12)

    for i, (wk, pretty) in enumerate(WORKLOAD_KEYS):
        ax = axes[0][i]
        g = groups[wk]

        ax.set_title(pretty)
        ax.set_xlabel("Threads")
        set_left_yaxis_only(ax, i, "Combined Mean Overtake Depth")

        ax.xaxis.set_major_locator(MultipleLocator(xstep))
        ax.set_xlim(0,20)

        fallback_i = 0
        for impl_label, df, _ in g["series"]:
            if col_enq not in df.columns or col_deq not in df.columns:
                continue

            df = df.copy()
            df["combined_mean_ovt"] = df[col_enq] + df[col_deq]

            style, fallback_i = pick_style(impl_label, fallback_i)
            stats = df.groupby("threads")["combined_mean_ovt"].agg(["mean","std"]).reset_index()

            ax.errorbar(
                stats["threads"], stats["mean"],
                yerr=stats["std"].fillna(0),
                label=legend_label(impl_label),
                color=style["color"],
                marker=style["marker"],
                markersize=4,
                linewidth=1,
                capsize=3,
            )

        apply_grid(ax)
        ax.legend(fontsize=7)

    savefig(fig, os.path.join(out_dir, "combined_mean_ovt_triptych.png"))
    plt.close(fig)

# =======================
# MAIN
# =======================

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", action="append", help="CSV paths", default=None)
    ap.add_argument("--out", default="./results/graphs", help="Output directory")
    args = ap.parse_args()

    dfs = load_csv_files(args.csv)
    groups = group_by_workload(dfs)

    # Triptychs
    plot_metric_triptych_per_metric(args.out, groups, ENQ_METRICS, prefix="enq")
    plot_metric_triptych_per_metric(args.out, groups, DEQ_METRICS, prefix="deq")
    plot_metric_triptych_per_metric(args.out, groups, TIME_METRICS, prefix="time")
    plot_l1_missrate_triptych(args.out, groups)

    # Combined unfairness
    plot_combined_overtake_triptych(args.out, groups)

if __name__ == "__main__":
    main()
