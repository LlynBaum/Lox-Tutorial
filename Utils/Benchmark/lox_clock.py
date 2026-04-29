import subprocess
import statistics
import re
import os
from datetime import datetime

# Configuration
CLOX_PATH = "../../CLox/build/cmake-build-release-build/CLox"  # Path to your compiled clox executable
LOX_FILE = "scripts/ClassInit.lox"
WARM_UP = 3
TRIALS = 10
RESULTS_DIR = "results"


def run_benchmark():
    # Ensure the results directory exists
    if not os.path.exists(RESULTS_DIR):
        os.makedirs(RESULTS_DIR)

    for i in range (WARM_UP):
        try:
            subprocess.run([CLOX_PATH, LOX_FILE], capture_output=True)
            print(f"  WarmUp {i + 1}")
        except subprocess.CalledProcessError as e:
            print(f"  WarmUp {i + 1}: clox crashed with error: {e.stderr}")

    times = []
    print(f"Running {TRIALS} trials of {LOX_FILE}...")

    for i in range(TRIALS):
        try:
            result = subprocess.run(
                [CLOX_PATH, LOX_FILE],
                capture_output=True,
                text=True,
                check=True
            )

            output = result.stdout.strip().split('\n')
            if not output:
                continue

            last_line = output[-1]
            match = re.search(r"(\d+\.?\d*)", last_line)

            if match:
                execution_time = float(match.group(1))
                times.append(execution_time)
                print(f"  Trial {i + 1}: {execution_time:.4f}s")
            else:
                print(f"  Trial {i + 1}: Failed to parse output: '{last_line}'")

        except subprocess.CalledProcessError as e:
            print(f"  Trial {i + 1}: clox crashed with error: {e.stderr}")

    if times:
        avg_time = statistics.mean(times)
        min_time = min(times)
        max_time = max(times)
        stdev = statistics.stdev(times) if len(times) > 1 else 0.0

        # Prepare results string
        report = (
                f"Benchmark Results for {LOX_FILE}\n"
                f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
                f"{'=' * 40}\n"
                f"Trials:  {TRIALS}\n"
                f"Average: {avg_time:.4f}s\n"
                f"Best:    {min_time:.4f}s\n"
                f"Worst:   {max_time:.4f}s\n"
                f"Stdev:   {stdev:.4f}s\n"
                f"{'=' * 40}\n"
                f"Raw Data (seconds):\n" + ", ".join(f"{t:.4f}" for t in times)
        )

        # Print to console
        print("\n" + report)

        # Write to file
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        base_name = os.path.splitext(os.path.basename(LOX_FILE))[0]
        file_name = f"{base_name}_{timestamp}.txt"
        file_path = os.path.join(RESULTS_DIR, file_name)

        with open(file_path, "w") as f:
            f.write(report)

        print(f"\nResults saved to: {file_path}")


if __name__ == "__main__":
    run_benchmark()