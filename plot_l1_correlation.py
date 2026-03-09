import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import scipy.stats as spearmanr
import os

QUEUES = ["ms", "fc", "lprq", "faa"]
QUEUE_LABELS = ["MS", "FC", "LPRQ", "FAA"]
WORKLOADS = ["enqueueheavy", "dequeueheavy", "pair"]
WORKLOAD_LABELS = ["Enqueue-Heavy", "Dequeue-Heavy", "Enqueue-Dequeue Pair"]

def calc_correlation_coefficient(workload: str):
    corr_queues = {}
    for queue in QUEUES:
        df = pd.read_csv(f"results/{queue}_{workload}.csv")
        df = df[df['threads'] > 1]

        if workload == "pair":
            enq_avg = df.groupby('threads')['enq_mean_ovt'].mean()
            deq_avg = df.groupby('threads')['deq_mean_ovt'].mean()
            op_mean_ovt = (enq_avg + deq_avg) / 2
        elif workload == "enqueueheavy":
            op_mean_ovt = df.groupby('threads')['enq_mean_ovt'].mean()
        elif workload == "dequeueheavy":
            op_mean_ovt = df.groupby('threads')['deq_mean_ovt'].mean()

        l1_miss_rate = df.groupby('threads')['l1_missrate'].mean()
        corr_queues[queue] = np.corrcoef(op_mean_ovt, l1_miss_rate)[0, 1]
    return corr_queues

def generate_heatmap():
    heatmap_data = []
    for workload in WORKLOADS:
        workload_corrs = calc_correlation_coefficient(workload)
        row = [workload_corrs[q] for q in QUEUES]
        heatmap_data.append(row)

    heatmap_data = np.array(heatmap_data)

    fig, ax = plt.subplots(figsize=(10, 6))
    im = ax.imshow(heatmap_data, cmap="coolwarm", vmin=-1, vmax=1)

    cbar = ax.figure.colorbar(im, ax=ax)
    cbar.ax.set_ylabel("Pearson Correlation Coefficient", rotation=-90, va="bottom")

    ax.set_xticks(np.arange(len(QUEUES)))
    ax.set_yticks(np.arange(len(WORKLOADS)))
    ax.set_xticklabels(QUEUE_LABELS)
    ax.set_yticklabels(WORKLOAD_LABELS)

    plt.setp(ax.get_xticklabels(), rotation=0, ha="center", rotation_mode="anchor")

    for i in range(len(WORKLOADS)):
        for j in range(len(QUEUES)):
            ax.text(j, i, f"{heatmap_data[i, j]:.2f}",
                    ha="center", va="center",
                    color="black" if -0.5 < heatmap_data[i, j] < 0.5 else "white")

    fig.tight_layout()
    os.makedirs("results/graphs", exist_ok=True)
    fig.savefig("results/graphs/pearson_correlation_heatmap.png", dpi=300)
    print("Heatmap saved to results/graphs/pearson_correlation_heatmap.png")

if __name__ == "__main__":
    generate_heatmap()
