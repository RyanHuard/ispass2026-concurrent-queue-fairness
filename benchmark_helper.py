
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

STATE_FILE = Path('/tmp/bench_helper_state.json')


def run(cmd):
    print(f"Running: {' '.join(cmd)}")
    subprocess.check_call(cmd)


def read_file(path):
    with open(path) as f:
        return f.read().strip()

def write_file(path, data):
    with open(path, 'w') as f:
        f.write(data)


def disable_aslr(state):
    # ASLR controlled by kernel.randomize_va_space
    cur = subprocess.check_output(['sysctl', '-n', 'kernel.randomize_va_space']).decode().strip()
    state['aslr'] = cur
    run(['sysctl', 'kernel.randomize_va_space=0'])


def restore_aslr(state):
    orig = state.get('aslr', '2')
    run(['sysctl', f'kernel.randomize_va_space={orig}'])


def disable_hyperthreading(state):
    # offline sibling threads, keep one per core
    siblings = []
    for cpu_dir in Path('/sys/devices/system/cpu').glob('cpu[0-9]*'):
        cpu = cpu_dir.name
        sib_file = cpu_dir / 'topology' / 'thread_siblings_list'
        if not sib_file.exists():
            continue
        sibs = read_file(sib_file).split(',')
        # if more than one sibling and cpu is not the first
        if len(sibs) > 1 and cpu.replace('cpu', '') in sibs[1:]:
            idx = cpu.replace('cpu', '')
            # offline this CPU
            online_file = cpu_dir / 'online'
            if online_file.exists():
                run(['sh', '-c', f'echo 0 > {online_file}'])
                siblings.append(idx)
    state['ht_offlined'] = siblings


def restore_hyperthreading(state):
    for idx in state.get('ht_offlined', []):
        online_file = Path(f'/sys/devices/system/cpu/cpu{idx}/online')
        if online_file.exists():
            run(['sh', '-c', f'echo 1 > {online_file}'])


def set_governor_performance(state):
    # set all to performance
    run(['cpupower', 'frequency-set', '-g', 'performance'])


def restore_governor(state):
    run(['cpupower', 'frequency-set', '-g', 'schedutil'])


def reserve_cores(state, cores):
    """
    Reserve cores for benchmark using cgroups v2 (systemd slices).
    cores: string like "0" or "1-3" or "0,2"
    """
    state['reserved_cores'] = cores

    # Create a custom slice for the benchmark
    slice_name = "bench.slice"
    state['slice'] = slice_name

    # Restrict CPUs for that slice
    run([
        "systemctl", "set-property", slice_name,
        f"AllowedCPUs={cores}"
    ])

def release_cores(state):
    """
    Release reserved cores by resetting slice properties.
    """
    slice_name = state.get('slice', "bench.slice")

    # Reset AllowedCPUs so tasks in this slice can run anywhere again
    run([
        "systemctl", "stop", slice_name
    ])


def load_state():
    if STATE_FILE.exists():
        return json.loads(STATE_FILE.read_text())
    return {}


def save_state(state):
    STATE_FILE.write_text(json.dumps(state))


def prepare(args):
    state = {}
    disable_aslr(state)
    disable_hyperthreading(state)
    set_governor_performance(state)
    reserve_cores(state, args.cores)
    save_state(state)
    print("System prepared for benchmarking.")


def restore(args):
    state = load_state()
    restore_aslr(state)
    restore_hyperthreading(state)
    restore_governor(state)
    release_cores(state)
    print("System settings restored.")


def main():
    parser = argparse.ArgumentParser(description="Benchmarker helper: prepare and restore system for low-noise benchmarks")
    sub = parser.add_subparsers(dest='cmd')

    p = sub.add_parser('prepare', help='Prepare system')
    p.add_argument('--cores', required=True, help='Comma or range list of CPU cores to reserve, e.g., "0-3,6"')

    r = sub.add_parser('restore', help='Restore system')

    args = parser.parse_args()
    if args.cmd == 'prepare':
        prepare(args)
    elif args.cmd == 'restore':
        restore(args)
    else:
        parser.print_help()

if __name__ == '__main__':
    if os.geteuid() != 0:
        print("This script must be run as root.")
        sys.exit(1)
    main()
