#!/usr/bin/env python3

"""
Benchmarking script for time series parallel processing implementations.
This script provides comprehensive benchmarking for different implementations
with various configurations, supporting strong and weak scaling analysis.
"""

import csv
import json
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime
import numpy as np
import pandas as pd
from pathlib import Path

# ===================== CONFIGURATION =====================
# Implementations to benchmark
IMPLEMENTATIONS = {
    "pankaj_code7": "../src/bin/pankaj_code7",
    "pankaj_code9": "../src/bin/pankaj_code9",
    "pankaj_code11": "../src/bin/pankaj_code11"
}

# Datasets
DATASETS = [
    "../data/data_64_64_64_3.txt",
    "../data/data_64_64_96_7.txt"
]

# Process counts to test
PROCESS_COUNTS = [8, 16]

# Number of iterations per configuration
ITERATIONS = 2

# Output directory
OUTPUT_DIR = f"results/benchmark_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

# Timeout in seconds
TIMEOUT = 600  # 10 minutes

# Specific process decompositions for each process count (optional)
# Format: {process_count: [(px, py, pz), ...]}
# If not specified, the script will determine optimal decomposition
PROCESS_DECOMPOSITIONS = {
    8: [(2, 2, 2)],
    16: [(4, 2, 2)],
    32: [(4, 4, 2)],
    64: [(4, 4, 4)]
}

# Generate visualizations after benchmarking
GENERATE_VISUALIZATIONS = False

# ===================== END CONFIGURATION =====================

class BenchmarkRunner:
    """Manages execution and data collection for benchmarking MPI programs."""

    def __init__(self):
        """Initialize with configured parameters."""
        self.implementations = IMPLEMENTATIONS
        self.datasets = DATASETS
        self.process_counts = PROCESS_COUNTS
        self.iterations = ITERATIONS
        self.timeout = TIMEOUT
        self.process_decompositions = PROCESS_DECOMPOSITIONS

        self.results = defaultdict(list)
        self.timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.results_dir = OUTPUT_DIR
        self.raw_dir = os.path.join(self.results_dir, "raw")

        # Create output directories
        os.makedirs(self.results_dir, exist_ok=True)
        os.makedirs(self.raw_dir, exist_ok=True)

        # Save configuration
        self.save_configuration()

        print("=" * 70)
        print(f"Benchmark Configuration:")
        print(f"  Implementations: {list(self.implementations.keys())}")
        print(f"  Datasets: {[os.path.basename(d) for d in self.datasets]}")
        print(f"  Process counts: {self.process_counts}")
        print(f"  Iterations: {self.iterations}")
        print(f"  Output directory: {self.results_dir}")
        print("=" * 70)

    def save_configuration(self):
        """Save benchmark configuration to JSON file."""
        config = {
            "timestamp": self.timestamp,
            "implementations": list(self.implementations.keys()),
            "datasets": self.datasets,
            "process_counts": self.process_counts,
            "iterations": self.iterations,
            "process_decompositions": self.process_decompositions,
            "timeout": self.timeout
        }

        with open(os.path.join(self.results_dir, "config.json"), 'w') as f:
            json.dump(config, f, indent=2)

    def parse_dimensions(self, dataset):
        """Extract dimensions from dataset filename."""
        match = re.search(r'data_(\d+)_(\d+)_(\d+)_(\d+)', dataset)
        if not match:
            print(f"Warning: Could not parse dimensions from {dataset}")
            return None

        return {
            "nx": int(match.group(1)),
            "ny": int(match.group(2)),
            "nz": int(match.group(3)),
            "timesteps": int(match.group(4))
        }

    def get_decomposition(self, processes, dims):
        """Determine process grid decomposition."""
        # Check if we have a specific decomposition for this process count
        if self.process_decompositions and processes in self.process_decompositions:
            for px, py, pz in self.process_decompositions[processes]:
                # Check if dimensions are divisible by the decomposition
                if (dims["nx"] % px == 0 and
                    dims["ny"] % py == 0 and
                    dims["nz"] % pz == 0 and
                    px * py * pz == processes):
                    return (px, py, pz)

            print(f"Warning: No valid decomposition found for {processes} processes in configuration")

        # Try to make a balanced 3D decomposition
        options = []
        for px in range(1, processes+1):
            if processes % px == 0:
                remaining = processes // px
                for py in range(1, remaining+1):
                    if remaining % py == 0:
                        pz = remaining // py
                        # Check if dimensions are divisible
                        if (dims["nx"] % px == 0 and
                            dims["ny"] % py == 0 and
                            dims["nz"] % pz == 0):
                            options.append((px, py, pz))

        if options:
            # Choose the most balanced option
            options.sort(key=lambda x: max(x)/min(x) if min(x) > 0 else float('inf'))
            return options[0]
        else:
            print(f"Warning: No valid decomposition found for {processes} processes")
            return None

    def extract_timing(self, output_file):
        """Extract timing information from output file."""
        try:
            with open(output_file, 'r') as f:
                lines = f.readlines()
                if len(lines) >= 3:
                    timing_line = lines[-1].strip()
                    read_time, main_time, total_time = map(float, timing_line.split(','))
                    return {
                        "read_time": read_time,
                        "main_time": main_time,
                        "total_time": total_time
                    }
        except Exception as e:
            print(f"Error extracting timing from {output_file}: {e}")
        return None

    def run_benchmark(self, executable, dataset, processes, decomposition, iteration):
        """Run a single benchmark instance."""
        # Parse dataset dimensions
        dims = self.parse_dimensions(dataset)
        if not dims:
            return None

        # Prepare output file
        impl_name = os.path.basename(executable)
        output_file = os.path.join(
            self.raw_dir,
            f"{impl_name}_{processes}p_{os.path.basename(dataset)}_{iteration}.txt"
        )

        # Prepare command
        px, py, pz = decomposition
        cmd = [
            "mpirun", "-np", str(processes),
            "--oversubscribe",
            executable,
            dataset,
            str(px), str(py), str(pz),
            str(dims["nx"]), str(dims["ny"]), str(dims["nz"]),
            str(dims["timesteps"]),
            output_file
        ]

        print(f"Running: {' '.join(cmd)}")

        try:
            # Run the command with timeout
            start_time = time.time()
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            try:
                stdout, stderr = process.communicate(timeout=self.timeout)
                elapsed = time.time() - start_time

                # Check if output file was created
                if os.path.exists(output_file):
                    timing = self.extract_timing(output_file)
                    if timing:
                        timing["wall_time"] = elapsed
                        return timing
                    else:
                        print(f"Error: Could not extract timing from output file")
                else:
                    print(f"Error: Output file not created")

                if stderr:
                    error_log = os.path.join(self.raw_dir, f"{impl_name}_{processes}p_error_{iteration}.log")
                    with open(error_log, 'wb') as f:
                        f.write(stderr)
                    print(f"Error log saved to {error_log}")

            except subprocess.TimeoutExpired:
                process.kill()
                print(f"Error: Command timed out after {self.timeout} seconds")

        except Exception as e:
            print(f"Error running command: {e}")

        return None

    def run_all_benchmarks(self):
        """Run all benchmarks according to configuration."""
        results_data = []

        for dataset in self.datasets:
            if not os.path.exists(dataset):
                print(f"Warning: Dataset {dataset} not found, skipping")
                continue

            for processes in self.process_counts:
                dims = self.parse_dimensions(dataset)
                if not dims:
                    continue

                decomposition = self.get_decomposition(processes, dims)
                if not decomposition:
                    continue

                for impl_name, executable in self.implementations.items():
                    print(f"\n{'='*70}")
                    print(f"Benchmarking {impl_name} with {processes} processes on {dataset}")
                    print(f"Decomposition: {decomposition[0]}x{decomposition[1]}x{decomposition[2]}")
                    print(f"{'='*70}")

                    iteration_results = []
                    for i in range(self.iterations):
                        print(f"Iteration {i+1}/{self.iterations}")

                        # Run the benchmark
                        timing = self.run_benchmark(
                            executable, dataset, processes, decomposition, i
                        )

                        if timing:
                            # Create a separate dictionary for the additional data
                            additional_data = {
                                "implementation": impl_name,
                                "dataset": dataset,
                                "processes": processes,
                                "px": decomposition[0],
                                "py": decomposition[1],
                                "pz": decomposition[2],
                                "iteration": i,
                                "nx": dims["nx"],
                                "ny": dims["ny"],
                                "nz": dims["nz"],
                                "timesteps": dims["timesteps"],
                                "problem_size": dims["nx"] * dims["ny"] * dims["nz"] * dims["timesteps"]
                            }

                            # Update the timing dictionary
                            timing.update(additional_data)

                            results_data.append(timing)
                            iteration_results.append(timing)

                        # Short delay between iterations
                        time.sleep(1)

                    # Compute statistics for this configuration
                    if iteration_results:
                        read_times = [r["read_time"] for r in iteration_results]
                        main_times = [r["main_time"] for r in iteration_results]
                        total_times = [r["total_time"] for r in iteration_results]

                        print("\nResults:")
                        print(f"Read Time:  {np.mean(read_times):.4f}s (±{np.std(read_times):.4f})")
                        print(f"Main Time:  {np.mean(main_times):.4f}s (±{np.std(main_times):.4f})")
                        print(f"Total Time: {np.mean(total_times):.4f}s (±{np.std(total_times):.4f})")
                    else:
                        print("No valid results collected")

        # Save complete results to CSV
        if results_data:
            results_df = pd.DataFrame(results_data)
            csv_path = os.path.join(self.results_dir, "benchmark_results.csv")
            results_df.to_csv(csv_path, index=False)
            print(f"\nResults saved to {csv_path}")

            # Also save a summary with statistics
            summary = results_df.groupby(
                ['implementation', 'dataset', 'processes', 'px', 'py', 'pz']
            ).agg({
                'read_time': ['mean', 'std', 'min', 'max'],
                'main_time': ['mean', 'std', 'min', 'max'],
                'total_time': ['mean', 'std', 'min', 'max']
            }).reset_index()

            summary_path = os.path.join(self.results_dir, "benchmark_summary.csv")
            summary.to_csv(summary_path)
            print(f"Summary saved to {summary_path}")

            return results_df
        else:
            print("No results collected")
            return None

def main():
    """Main entry point for the benchmarking script."""

    # Run benchmarks
    runner = BenchmarkRunner()
    results = runner.run_all_benchmarks()

    # Generate visualizations if requested
    if GENERATE_VISUALIZATIONS and results is not None:
        try:
            print("\nGenerating visualizations...")
            from visualize import generate_visualizations
            generate_visualizations(runner.results_dir, results)
        except ImportError:
            print("Warning: Could not import visualization module")
        except Exception as e:
            print(f"Error generating visualizations: {e}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
