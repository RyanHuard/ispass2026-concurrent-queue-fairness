#!/usr/bin/env python3
"""
Plot fairness metrics from one or more CSVs (same schema).

Each CSV must have columns:
  threads,avg_ms,
  ins_mean_all,ins_mean_ovt,ins_max,ins_count_avg,ins_pct_avg,
  deq_mean_all,deq_mean_ovt,deq_max,deq_count_avg,deq_pct_avg,
  e2e_mean_all,e2e_mean_ovt,e2e_max,e2e_count_avg,e2e_pct_avg
"""

import argparse
import os
import sys
import pandas as pd
import matplotlib.pyplot as plt

REQUIRED_COLS = [
    "threads","avg_ms",
    "ins_mean_all","ins_mean_ovt","ins_max","ins_count_avg","ins_pct_avg",
    "deq_mean_all","deq_mean_ovt","deq_max","deq_count_avg","deq_pct_avg",
    "e2e_mean_all","e2e_mean_ovt","e2e_max","e2e_count_avg","e2e_pct_avg",
]

PLOTS = []
PLOTS = [
   # (filename, ycol, ylabel, title)
    ("avg_time_vs_threads.png", "avg_ms",      "Avg Time (ms)",            "Average Time vs Threads"),
    ("insertion_mean_depth_vs_threads.png", "ins_mean_all", "Mean Overtake Depth", "Insertion Fairness — Mean Depth vs Threads"),
    ("insertion_max_depth_vs_threads.png",  "ins_max",  "Max Overtake Depth",  "Insertion Fairness — Max Depth vs Threads"),
    ("insertion_pct_vs_threads.png",        "ins_pct_avg","Overtaken %",       "Insertion Fairness"),
    ("service_mean_depth_vs_threads.png",   "deq_mean_all", "Mean Overtake Depth", "Service Fairness — Mean Depth vs Threads"),
    ("service_max_depth_vs_threads.png",    "deq_max",  "Max Overtake Depth",  "Service Fairness — Max Depth vs Threads"),
    ("service_pct_vs_threads.png",          "deq_pct_avg","Overtaken %",       "Service Fairness — % Overtaken vs Threads"),
    ("e2e_mean_depth_vs_threads.png",       "e2e_mean_all", "Mean Overtake Depth", "End-to-End Fairness — Mean Depth vs Threads"),
    ("e2e_max_depth_vs_threads.png",        "e2e_max",  "Max Overtake Depth",  "End-to-End Fairness — Max Depth vs Threads"),
    ("e2e_pct_vs_threads.png",              "e2e_pct_avg","Overtaken %",       "End-to-End Fairness — % Overtaken vs Threads"),
]

COMBINED = [
    # (filename, cols, ylabel, titles)
    ("mean_all_depth_comparison.png",
     ["ins_mean_all", "deq_mean_all", "e2e_mean_all"],
     "All Elements Mean Overtake Depth",
     ["Insertion", "Service", "End-to-End"]),
    ("mean_ovt_depth_comparison.png",
     ["ins_mean_ovt", "deq_mean_ovt", "e2e_mean_ovt"],
     "Overtaken Elements Mean Overtake Depth",
     ["Insertion", "Service", "End-to-End"]),
    ("overtaken_pct_comparison.png",
     ["ins_pct_avg", "deq_pct_avg", "e2e_pct_avg"],
     "Overtaken %",
     ["Insertion", "Service", "End-to-End"]),
    ("max_overtake_depth_comparison.png",
     ["ins_max", "deq_max", "e2e_max"],
     "Max Overtake Depth",
     ["Insertion", "Service", "End-to-End"]),
]

def ensure_columns(df, path):
    missing = [c for c in REQUIRED_COLS if c not in df.columns]
    if missing:
        raise ValueError(f"{path} missing columns: {missing}")

def savefig(fig, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    print(f"Saved {path}")

def load_labeled(csvs, labels):
    dfs = []
    for i, p in enumerate(csvs):
        if not os.path.exists(p):
            print(f"CSV not found: {p}", file=sys.stderr); sys.exit(1)
        df = pd.read_csv(p)
        ensure_columns(df, p)
        df = df.sort_values("threads")
        label = labels[i] if i < len(labels) else os.path.splitext(os.path.basename(p))[0]
        dfs.append((label, df))
    return dfs

def plot_single_series(out_dir, dfs):
    # For each metric, overlay lines from all CSVs
    for fname, ycol, ylabel, title in PLOTS:
        fig = plt.figure()
        for label, df in dfs:
            plt.plot(df["threads"], df[ycol], marker="o", label=label)
        plt.xlabel("Threads")
        plt.ylabel(ylabel)
        plt.title(title)
        plt.legend()
        savefig(fig, os.path.join(out_dir, fname))

def plot_combined_panels(out_dir, dfs):
    for fname, cols, ylabel, titles in COMBINED:
        fig, axes = plt.subplots(1, 3, sharey=True, figsize=(12,4))
        for ax, col, title in zip(axes, cols, titles):
            for label, df in dfs:
                ax.plot(df["threads"], df[col], marker="o", label=label)
            ax.set_title(title)
            ax.set_xlabel("Threads")
        axes[0].set_ylabel(ylabel)
        # Put one legend for the whole figure
        handles, labels = axes[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="upper center", ncol=max(1, len(dfs)))
        fig.subplots_adjust(top=0.82, wspace=0.25)
        savefig(fig, os.path.join(out_dir, fname))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", action="append", required=True,
                    help="Path to a CSV exported by the benchmark. Repeat for multiple.")
    ap.add_argument("--label", action="append", default=[],
                    help="Legend label for the preceding --csv (repeat; same order).")
    ap.add_argument("--out", default=None,
                    help="Output directory (default: ./graphs next to the FIRST CSV).")
    ap.add_argument("--show", action="store_true",
                    help="Call plt.show() after saving figures")
    args = ap.parse_args()

    dfs = load_labeled(args.csv, args.label)

    # Default output dir: <dir_of_first_csv>/graphs
    out_dir = args.out
    if not out_dir:
        first_dir = os.path.dirname(os.path.abspath(args.csv[0]))
        out_dir = os.path.join(first_dir, "graphs")

    plot_single_series(out_dir, dfs)
    plot_combined_panels(out_dir, dfs)

    if args.show:
        plt.show()

if __name__ == "__main__":
    main()
