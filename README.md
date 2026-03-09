# Concurrent FIFO Queue Fairness Benchmark

Artifact for "Linearizability Alone Is Not Enough for Strict FIFO Semantics".
Benchmarks the fairness behavior of four concurrent FIFO queue implementations
under various workloads, measuring overtake percentage, overtake depth, and L1
cache miss rate.

## Requirements

- Ubuntu 22.04+
- C++20 (gcc)w to 
- Python 3.8+
- libpapi-dev
- linux-tools-common, linux-tools-generic (for `perf`)

## Installation

1. Clone the repository:
```bash
   git clone https://github.com/TODO/TODO.git
   cd TODO
```

2. Install dependencies:
```bash
   sudo apt install libpapi-dev linux-tools-common linux-tools-generic
```

3. Install Python dependencies:
```bash
   pip install -r requirements.txt
```

4. Build the benchmark:
```bash
   make
```

## Usage

Run all queues and workloads with default settings:
```bash
./bin/benchmark
```

Results are written as CSV files to `results/`.

### Command-line arguments

| Argument | Description | Default |
|---|---|---|
| `--queue=<name>` | Queue(s) to benchmark: `ms`, `fc`, `lprq`, `faa`, or `all` | all |
| `--trials=N` | Number of trials per configuration | 100 |
| `--ops=N` | Number of operations per thread per trial | 10000 |
| `--threads=N` | Maximum thread count | 16 |

Multiple queues can be specified as a comma-separated list:
```bash
./bin/benchmark --queue=ms,faa --trials=10 --threads=8
```


## Generating Figures

After running the benchmark, generate all figures with:
```bash
make plot
```

Figures are saved as PNG files to `results/graphs/`, corresponding to the
figures in the paper.

## Notes

Collecting L1 cache miss rates via Linux `perf` may require elevated
privileges:
```bash
sudo sysctl -w kernel.perf_event_paranoid=0
```
