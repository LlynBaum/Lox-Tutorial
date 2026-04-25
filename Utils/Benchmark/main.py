import subprocess
import time
import statistics
import os

# Configuration
INTERPRETER_PATH = "../../CLox/build/cmake-build-release-build/CLox"
BENCHMARK_DIR = "./scripts"  # Folder containing your .lox scripts
ITERATIONS = 5
WARMUP_RUNS = 2



def run_benchmark(script_path):
    # Warm-up to let the OS cache the binary and files
    for _ in range(WARMUP_RUNS):
        subprocess.run([INTERPRETER_PATH, script_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    times = []
    for _ in range(ITERATIONS):
        start = time.perf_counter()
        subprocess.run([INTERPRETER_PATH, script_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        end = time.perf_counter()
        times.append(end - start)

    return statistics.mean(times), statistics.stdev(times)


def main():
    if not os.path.exists(BENCHMARK_DIR):
        print(f"Error: {BENCHMARK_DIR} directory not found.")
        return

    scripts = [f for f in os.listdir(BENCHMARK_DIR) if f.endswith(".lox")]

    print(f"{'Script':<25} | {'Avg Time':<10} | {'Stdev':<10}")
    print("-" * 50)

    for script in sorted(scripts):
        path = os.path.join(BENCHMARK_DIR, script)
        avg, sd = run_benchmark(path)
        print(f"{script:<25} | {avg:>8.4f}s | {sd:>8.4f}s")


if __name__ == "__main__":
    main()