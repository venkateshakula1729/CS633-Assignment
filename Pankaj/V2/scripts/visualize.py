#!/usr/bin/env python3
"""
Visualization script for time series parallel processing benchmark results.
Generates various plots to analyze performance characteristics.
"""

import argparse
import os
import sys
import json
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import shutil
from pathlib import Path
from matplotlib.ticker import ScalarFormatter

# Set theme and style for consistent, professional look
plt.style.use('ggplot')
sns.set_theme(style="whitegrid")
plt.rcParams['figure.figsize'] = (12, 8)
plt.rcParams['font.size'] = 12
COLORS = sns.color_palette("colorblind", 10)

def format_time(seconds):
    """Format time value with appropriate units."""
    if seconds < 0.001:
        return f"{seconds*1000000:.2f} μs"
    elif seconds < 1:
        return f"{seconds*1000:.2f} ms"
    else:
        return f"{seconds:.4f} s"

def plot_implementation_comparison(results_dir, df):
    """Generate polished bar chart comparing different implementations."""
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Get unique process counts for separate plots
    process_counts = sorted(df['processes'].unique())

    for processes in process_counts:
        process_df = df[df['processes'] == processes]

        # Group results for this process count
        impl_summary = process_df.groupby('implementation').agg({
            'read_time': ['mean', 'std'],
            'main_time': ['mean', 'std'],
            'total_time': ['mean', 'std']
        })

        # Reshape for easier plotting
        plot_data = pd.DataFrame({
            'Implementation': impl_summary.index,
            'Read Time': impl_summary[('read_time', 'mean')],
            'Main Time': impl_summary[('main_time', 'mean')],
            'Total Time': impl_summary[('total_time', 'mean')]
        })

        # Create figure with appropriate size
        fig, ax = plt.subplots(figsize=(max(10, len(plot_data) * 2.5), 8))

        # Set width of bars
        barWidth = 0.65
        br = np.arange(len(plot_data))

        # Calculate max value for y-axis limit
        max_total = (plot_data['Read Time'] + plot_data['Main Time']).max()
        y_limit = max_total * 1.3  # Add 30% padding for labels

        # Plotting bars - store the bar objects for later reference
        read_bars = ax.bar(br, plot_data['Read Time'], width=barWidth,
                          color=COLORS[0], edgecolor='grey', label='Read Time')

        main_bars = ax.bar(br, plot_data['Main Time'], width=barWidth,
                          bottom=plot_data['Read Time'],
                          color=COLORS[1], edgecolor='grey', label='Main Time')

        # Find best implementation (lowest total time)
        best_idx = np.argmin(plot_data['Total Time'].values)
        best_impl = plot_data.iloc[best_idx]['Implementation']
        best_time = plot_data.iloc[best_idx]['Total Time']

        # Highlight best implementation with bounds check
        if 0 <= best_idx < len(read_bars):

            # Optional: Add a star or marker above the best implementation
            total = plot_data.iloc[best_idx]['Read Time'] + plot_data.iloc[best_idx]['Main Time']
            ax.text(best_idx, total + max_total * 0.1, "★ BEST",
                   ha='center', va='bottom', fontweight='bold', color='black',
                   bbox=dict(facecolor='yellow', alpha=0.3, boxstyle='round,pad=0.3'))

        # Add data value labels with better positioning
        for i, (r, m) in enumerate(zip(plot_data['Read Time'], plot_data['Main Time'])):
            total = r + m

            # Only show read time label if it's significant
            if r > 0.1 * total:
                ax.text(i, r/2, format_time(r), ha='center', va='center',
                       fontweight='bold', color='white', fontsize=10)

            # Main time label
            ax.text(i, r + m/2, format_time(m), ha='center', va='center',
                   fontweight='bold', color='white', fontsize=10)

            # Total time label
            ax.text(i, total + 0.02 * max_total, format_time(total),
                   ha='center', va='bottom', fontsize=10)

            # # Calculate improvement over baseline (assuming baseline is first)
            # if i > 0 and len(plot_data) > 0:
            #     baseline_total = plot_data.iloc[0]['Total Time']
            #     improvement = (baseline_total - total) / baseline_total * 100

            #     # Position improvement text based on its value to avoid overlap
            #     if improvement > 0:
            #         color = 'green'
            #         text = f"{improvement:.1f}% faster"
            #     else:
            #         color = 'red'
            #         text = f"{-improvement:.1f}% slower"

            #     # Position text higher for larger values to prevent overlap
            #     y_pos = total + (0.06 + abs(improvement)/400) * max_total
            #     ax.text(i, y_pos, text, ha='center', va='bottom',
            #            fontsize=9, color=color,
            #            bbox=dict(facecolor='white', alpha=0.6, pad=0.1, boxstyle='round'))

        # Styling with better spacing
        ax.set_xlabel('Implementation', fontweight='bold', fontsize=12)
        ax.set_ylabel('Time (seconds)', fontweight='bold', fontsize=12)
        ax.set_title(f'Performance Comparison with {processes} Processes',
                     fontweight='bold', fontsize=14, pad=20)

        # Set y-axis limit with padding for labels
        ax.set_ylim(0, y_limit)

        # Set ticks and labels
        ax.set_xticks(br)
        ax.set_xticklabels(plot_data['Implementation'], fontsize=11)

        # Add horizontal grid lines for better readability
        ax.yaxis.grid(True, linestyle='--', alpha=0.7)

        # Move legend to a better position
        ax.legend(loc='upper right', framealpha=0.9)

        # Add a text box with best implementation in upper left
        props = dict(boxstyle='round,pad=0.5', facecolor='wheat', alpha=0.8)
        ax.text(0.02, 0.98, f"Best Implementation:\n{best_impl}\n({format_time(best_time)})",
               transform=ax.transAxes, fontsize=11,
               verticalalignment='top', bbox=props)

        # Add tight layout with more padding
        plt.tight_layout(pad=2.0)
        plt.savefig(os.path.join(fig_dir, f"impl_comparison_{processes}p.png"), dpi=300)
        plt.close()

def plot_scaling_analysis(results_dir, df):
    """Generate scaling analysis plots."""
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Extract unique implementations and datasets
    implementations = sorted(df['implementation'].unique())
    datasets = sorted(df['dataset'].unique())

    # Strong scaling: fixed problem size, varying process count
    for dataset in datasets:
        dataset_df = df[df['dataset'] == dataset]

        # Skip if we don't have data for this dataset
        if dataset_df.empty:
            continue

        # Extract problem dimensions for plot title
        try:
            first_row = dataset_df.iloc[0]
            dims = f"{first_row['nx']}x{first_row['ny']}x{first_row['nz']}, {first_row['timesteps']} timesteps"
        except (IndexError, KeyError):
            dims = os.path.basename(dataset)

        # Skip if we don't have multiple process counts
        if len(dataset_df['processes'].unique()) <= 1:
            continue

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))

        # Plot execution time vs. number of processes
        for i, impl in enumerate(implementations):
            impl_df = dataset_df[dataset_df['implementation'] == impl]

            # Skip if no data for this implementation
            if impl_df.empty:
                continue

            # Sort by number of processes
            impl_df = impl_df.sort_values('processes')

            # Group by process count and compute mean and std
            proc_summary = impl_df.groupby('processes').agg({
                'total_time': ['mean', 'std']
            }).reset_index()

            if proc_summary.empty:
                continue

            x = proc_summary['processes']
            y = proc_summary[('total_time', 'mean')]

            # Execution time plot - no error bars
            ax1.plot(x, y, marker='o', label=impl, color=COLORS[i % len(COLORS)])

            # Add data labels
            for j, (proc, time) in enumerate(zip(x, y)):
                ax1.text(proc, time + 0.05 * time, format_time(time),
                       ha='center', va='bottom', fontsize=8)

        # Calculate speedup relative to baseline implementation with lowest process count
        baseline_times = {}

        for i, impl in enumerate(implementations):
            impl_df = dataset_df[dataset_df['implementation'] == impl]

            # Skip if no data for this implementation
            if impl_df.empty:
                continue

            # Sort by number of processes
            impl_df = impl_df.sort_values('processes')

            # Group by process count and compute mean
            proc_summary = impl_df.groupby('processes').agg({
                'total_time': ['mean']
            }).reset_index()

            if proc_summary.empty:
                continue

            # Find baseline time (time with minimum processes for this implementation)
            min_procs = proc_summary['processes'].min()

            # Store the baseline time for each implementation
            if min_procs not in baseline_times:
                baseline_times[min_procs] = {}
            baseline_times[min_procs][impl] = proc_summary[proc_summary['processes'] == min_procs][('total_time', 'mean')].iloc[0]

            x = proc_summary['processes']

            # Get the baseline time for this implementation
            baseline_time = baseline_times[min_procs][impl]
            y_speedup = baseline_time / proc_summary[('total_time', 'mean')]

            # Speedup plot
            ax2.plot(x, y_speedup, marker='o', label=impl, color=COLORS[i % len(COLORS)])

            # Add ideal speedup line (only once, using the process counts from the first implementation)
            if i == 0:
                ideal_x = x.copy()
                ideal_y = ideal_x / ideal_x.iloc[0]
                ax2.plot(ideal_x, ideal_y, 'k--', label='Ideal Speedup')

            # Add data labels
            for j, (proc, speedup) in enumerate(zip(x, y_speedup)):
                ax2.text(proc, speedup + 0.1, f"{speedup:.2f}x",
                        ha='center', va='bottom', fontsize=8)

        # Style plots
        ax1.set_xlabel('Number of Processes', fontweight='bold')
        ax1.set_ylabel('Execution Time (seconds)', fontweight='bold')
        ax1.set_title(f'Strong Scaling: Execution Time\n({dims})', fontweight='bold')

        # Only set log scale if we have multiple process counts
        process_counts = sorted(dataset_df['processes'].unique())
        if len(process_counts) > 1:
            try:
                ax1.set_xscale('log', base=2)
                ax1.set_yscale('log', base=10)
            except Exception:
                # Fallback to linear scale if log scale fails
                pass

        ax1.grid(True, which="both", ls="-", alpha=0.2)
        ax1.legend()

        ax2.set_xlabel('Number of Processes', fontweight='bold')
        ax2.set_ylabel('Speedup', fontweight='bold')
        ax2.set_title(f'Strong Scaling: Speedup\n({dims})', fontweight='bold')

        if len(process_counts) > 1:
            try:
                ax2.set_xscale('log', base=2)
            except Exception:
                # Fallback to linear scale if log scale fails
                pass

        ax2.grid(True, which="both", ls="-", alpha=0.2)
        ax2.legend()

        plt.tight_layout()
        plt.savefig(os.path.join(fig_dir, f"scaling_{os.path.basename(dataset)}.png"), dpi=300)
        plt.close()

def plot_communication_analysis(results_dir, df):
    """Generate plots analyzing communication patterns."""
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Extract unique implementations and process counts
    implementations = sorted(df['implementation'].unique())
    process_counts = sorted(df['processes'].unique())

    # Create a plot for each implementation showing how decomposition affects performance
    for impl in implementations:
        impl_df = df[df['implementation'] == impl]

        # Skip if we don't have data for this implementation
        if impl_df.empty:
            continue

        # For each process count, analyze different decompositions
        for procs in process_counts:
            proc_df = impl_df[impl_df['processes'] == procs]

            # Skip if we don't have data for this process count
            if proc_df.empty:
                continue

            # Group by decomposition
            decomp_summary = proc_df.groupby(['px', 'py', 'pz']).agg({
                'read_time': ['mean'],
                'main_time': ['mean'],
                'total_time': ['mean', 'std']
            }).reset_index()

            # Skip if we don't have multiple decompositions
            if len(decomp_summary) <= 1:
                continue

            # Sort by total time
            decomp_summary = decomp_summary.sort_values(('total_time', 'mean'))

            # Create decomposition labels
            decomp_labels = [f"{px}x{py}x{pz}" for px, py, pz in
                            zip(decomp_summary['px'], decomp_summary['py'], decomp_summary['pz'])]

            # Plot decomposition comparison
            fig, ax = plt.subplots(figsize=(10, 6))

            # Set width of bars
            barWidth = 0.6
            br = np.arange(len(decomp_summary))

            # Plotting bars
            read_bars = ax.bar(br, decomp_summary[('read_time', 'mean')], width=barWidth,
                              color=COLORS[0], edgecolor='grey', label='Read Time')

            main_bars = ax.bar(br, decomp_summary[('main_time', 'mean')], width=barWidth,
                              bottom=decomp_summary[('read_time', 'mean')],
                              color=COLORS[1], edgecolor='grey', label='Main Time')

            # Error bars removed

            # Add data value labels
            for i, (r, m) in enumerate(zip(decomp_summary[('read_time', 'mean')],
                                         decomp_summary[('main_time', 'mean')])):
                total = r + m

                # Total time label
                ax.text(i, total + 0.02, format_time(total), ha='center', va='bottom')

                # Calculate improvement over worst decomposition
                worst_total = decomp_summary[('total_time', 'mean')].max()
                improvement = (worst_total - total) / worst_total * 100
                if improvement > 1:  # Only show if improvement is significant
                    ax.text(i, total + 0.1, f"{improvement:.1f}% better",
                           ha='center', va='bottom', fontsize=10, color='green')

            # Styling
            ax.set_xlabel('Process Grid Decomposition (PX×PY×PZ)', fontweight='bold')
            ax.set_ylabel('Time (seconds)', fontweight='bold')
            ax.set_title(f'Impact of Domain Decomposition\n{impl} with {procs} Processes', fontweight='bold')
            ax.set_xticks(br)
            ax.set_xticklabels(decomp_labels)
            ax.legend(loc='upper right')

            plt.tight_layout()
            plt.savefig(os.path.join(fig_dir, f"decomp_{impl}_{procs}p.png"), dpi=300)
            plt.close()

def plot_heatmap_analysis(results_dir, df):
    """Generate heatmaps for parameter sweep analysis."""
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Extract unique datasets
    datasets = sorted(df['dataset'].unique())

    for dataset in datasets:
        dataset_df = df[df['dataset'] == dataset]

        # Skip if we don't have data for this dataset
        if dataset_df.empty:
            continue

        # Create a matrix of implementations vs. process counts for total time
        implementations = sorted(dataset_df['implementation'].unique())
        process_counts = sorted(dataset_df['processes'].unique())

        # Check if we have enough data for a meaningful heatmap
        if len(implementations) <= 1 or len(process_counts) <= 1:
            continue

        # Prepare data for heatmap
        heatmap_data = np.zeros((len(implementations), len(process_counts)))

        for i, impl in enumerate(implementations):
            for j, procs in enumerate(process_counts):
                filtered = dataset_df[(dataset_df['implementation'] == impl) &
                                     (dataset_df['processes'] == procs)]
                if not filtered.empty:
                    heatmap_data[i, j] = filtered['total_time'].mean()
                else:
                    heatmap_data[i, j] = np.nan

        # Create heatmap
        fig, ax = plt.subplots(figsize=(12, 8))
        im = ax.imshow(heatmap_data, cmap='viridis_r')

        # Add colorbar
        cbar = ax.figure.colorbar(im, ax=ax)
        cbar.ax.set_ylabel('Execution Time (seconds)', rotation=-90, va="bottom", fontweight='bold')

        # Label axes
        ax.set_xticks(np.arange(len(process_counts)))
        ax.set_yticks(np.arange(len(implementations)))
        ax.set_xticklabels(process_counts)
        ax.set_yticklabels(implementations)

        ax.set_xlabel('Number of Processes', fontweight='bold')
        ax.set_ylabel('Implementation', fontweight='bold')
        ax.set_title(f'Performance Heatmap: {os.path.basename(dataset)}', fontweight='bold')

        # Rotate x-labels for better readability
        plt.setp(ax.get_xticklabels(), rotation=45, ha="right", rotation_mode="anchor")

        # Annotate cells with exact times
        for i in range(len(implementations)):
            for j in range(len(process_counts)):
                if not np.isnan(heatmap_data[i, j]):
                    text = ax.text(j, i, format_time(heatmap_data[i, j]),
                                  ha="center", va="center", color="w", fontsize=10)

        plt.tight_layout()
        plt.savefig(os.path.join(fig_dir, f"heatmap_{os.path.basename(dataset)}.png"), dpi=300)
        plt.close()

def plot_parallel_efficiency(results_dir, df):
    """Generate parallel efficiency plots."""
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Extract unique implementations and datasets
    implementations = sorted(df['implementation'].unique())
    datasets = sorted(df['dataset'].unique())

    # For each dataset, plot parallel efficiency
    for dataset in datasets:
        dataset_df = df[df['dataset'] == dataset]

        # Skip if we don't have data for this dataset
        if dataset_df.empty:
            continue

        # Skip if we don't have multiple process counts
        if len(dataset_df['processes'].unique()) <= 1:
            continue

        # Extract problem dimensions for plot title
        try:
            first_row = dataset_df.iloc[0]
            dims = f"{first_row['nx']}x{first_row['ny']}x{first_row['nz']}, {first_row['timesteps']} timesteps"
        except (IndexError, KeyError):
            dims = os.path.basename(dataset)

        fig, ax = plt.subplots(figsize=(10, 6))

        for i, impl in enumerate(implementations):
            impl_df = dataset_df[dataset_df['implementation'] == impl]

            # Skip if no data for this implementation
            if impl_df.empty:
                continue

            # Sort by process count
            impl_df = impl_df.sort_values('processes')

            # Group by process count
            proc_summary = impl_df.groupby('processes').agg({
                'total_time': ['mean']
            }).reset_index()

            if proc_summary.empty:
                continue

            # Calculate serial time (time with minimum processes)
            min_procs = proc_summary['processes'].min()
            serial_time = proc_summary[proc_summary['processes'] == min_procs][('total_time', 'mean')].iloc[0]

            # Calculate parallel efficiency: (serial_time) / (p * parallel_time)
            proc_summary['efficiency'] = serial_time / (proc_summary['processes'] * proc_summary[('total_time', 'mean')])

            # Plot efficiency
            ax.plot(proc_summary['processes'], proc_summary['efficiency'],
                   marker='o', label=impl, color=COLORS[i % len(COLORS)])

            # Add data labels
            for p, eff in zip(proc_summary['processes'], proc_summary['efficiency']):
                ax.text(p, eff + 0.02, f"{eff:.2f}", ha='center', va='bottom', fontsize=8)

        # Draw horizontal line at 100% efficiency
        ax.axhline(y=1.0, color='r', linestyle='--', alpha=0.3)

        # Styling
        ax.set_xlabel('Number of Processes', fontweight='bold')
        ax.set_ylabel('Parallel Efficiency', fontweight='bold')
        ax.set_title(f'Parallel Efficiency Analysis\n({dims})', fontweight='bold')

        # Only set log scale if we have multiple process counts
        process_counts = sorted(dataset_df['processes'].unique())
        if len(process_counts) > 1:
            try:
                ax.set_xscale('log', base=2)
            except Exception:
                # Fallback to linear scale if log scale fails
                pass

        ax.grid(True, which="both", ls="-", alpha=0.2)
        ax.legend()

        plt.tight_layout()
        plt.savefig(os.path.join(fig_dir, f"efficiency_{os.path.basename(dataset)}.png"), dpi=300)
        plt.close()

def plot_breakdown_analysis(results_dir, df):
    """Generate detailed time breakdown analysis."""
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Extract unique implementations
    implementations = sorted(df['implementation'].unique())

    # Compute the percentage of time spent in each phase for different process counts
    for impl in implementations:
        impl_df = df[df['implementation'] == impl]

        # Skip if we don't have data for this implementation
        if impl_df.empty:
            continue

        # Skip if we don't have multiple process counts
        if len(impl_df['processes'].unique()) <= 1:
            continue

        # Group by process count
        proc_summary = impl_df.groupby('processes').agg({
            'read_time': ['mean'],
            'main_time': ['mean'],
            'total_time': ['mean']
        }).reset_index()

        # Skip if empty summary
        if proc_summary.empty:
            continue

        # Calculate percentages
        proc_summary['read_pct'] = proc_summary[('read_time', 'mean')] / proc_summary[('total_time', 'mean')] * 100
        proc_summary['main_pct'] = proc_summary[('main_time', 'mean')] / proc_summary[('total_time', 'mean')] * 100

        # Sort by process count
        proc_summary = proc_summary.sort_values('processes')

        # Create plot
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 7))

        # Plot absolute times
        ax1.bar(proc_summary['processes'], proc_summary[('read_time', 'mean')],
               label='Read Time', color=COLORS[0])
        ax1.bar(proc_summary['processes'], proc_summary[('main_time', 'mean')],
               bottom=proc_summary[('read_time', 'mean')],
               label='Main Time', color=COLORS[1])

        # Add data labels
        for i, (p, r, m) in enumerate(zip(proc_summary['processes'],
                                        proc_summary[('read_time', 'mean')],
                                        proc_summary[('main_time', 'mean')])):
            # Read time label if significant
            if r > 0.05 * (r + m):
                ax1.text(p, r/2, format_time(r), ha='center', va='center',
                        color='white', fontweight='bold')

            # Main time label
            ax1.text(p, r + m/2, format_time(m), ha='center', va='center',
                    color='white', fontweight='bold')

            # Total time
            ax1.text(p, r + m + 0.02, format_time(r + m), ha='center', va='bottom')

        # Styling for absolute times
        ax1.set_xlabel('Number of Processes', fontweight='bold')
        ax1.set_ylabel('Time (seconds)', fontweight='bold')
        ax1.set_title('Absolute Time Breakdown', fontweight='bold')
        ax1.legend(loc='upper right')

        # Plot percentage breakdown
        ax2.bar(proc_summary['processes'], proc_summary['read_pct'],
               label='Read Time %', color=COLORS[0])
        ax2.bar(proc_summary['processes'], proc_summary['main_pct'],
               bottom=proc_summary['read_pct'],
               label='Main Time %', color=COLORS[1])

        # Add percentage labels
        for i, (p, r_pct, m_pct) in enumerate(zip(proc_summary['processes'],
                                                proc_summary['read_pct'],
                                                proc_summary['main_pct'])):
            # Read percentage if significant
            if r_pct > 5:
                ax2.text(p, r_pct/2, f"{r_pct:.1f}%", ha='center', va='center',
                        color='white', fontweight='bold')

            # Main percentage
            ax2.text(p, r_pct + m_pct/2, f"{m_pct:.1f}%", ha='center', va='center',
                    color='white', fontweight='bold')

        # Styling for percentage breakdown
        ax2.set_xlabel('Number of Processes', fontweight='bold')
        ax2.set_ylabel('Percentage of Total Time', fontweight='bold')
        ax2.set_title('Percentage Time Breakdown', fontweight='bold')
        ax2.legend(loc='upper right')

        # Overall plot styling
        fig.suptitle(f'Time Breakdown Analysis: {impl}', fontsize=16, fontweight='bold')
        plt.tight_layout()
        plt.subplots_adjust(top=0.9)  # Adjust for the suptitle

        plt.savefig(os.path.join(fig_dir, f"breakdown_{impl}.png"), dpi=300)
        plt.close()

def generate_visualizations(results_dir, df=None):
    """Generate all visualizations for the benchmark results."""
    if df is None:
        # Try to load results from CSV
        csv_path = os.path.join(results_dir, "benchmark_results.csv")
        if not os.path.exists(csv_path):
            print(f"Error: Results file not found at {csv_path}")
            return False

        df = pd.read_csv(csv_path)

    # Create figures directory
    fig_dir = os.path.join(results_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    # Generate all plots
    print("Generating implementation comparison plots...")
    plot_implementation_comparison(results_dir, df)

    print("Generating scaling analysis plots...")
    plot_scaling_analysis(results_dir, df)

    print("Generating communication analysis plots...")
    plot_communication_analysis(results_dir, df)

    print("Generating heatmap analysis plots...")
    plot_heatmap_analysis(results_dir, df)

    print("Generating efficiency analysis plots...")
    plot_parallel_efficiency(results_dir, df)

    print("Generating time breakdown analysis plots...")
    plot_breakdown_analysis(results_dir, df)

    print(f"All visualizations saved to {fig_dir}")
    return True

def main():
    """Main entry point for visualization script."""
    parser = argparse.ArgumentParser(description='Generate visualizations for benchmark results')

    parser.add_argument('results_dir', help='Directory containing benchmark results')
    parser.add_argument('--output', '-o', help='Output directory for visualizations (default: same as results_dir)')

    args = parser.parse_args()

    results_dir = args.results_dir
    output_dir = args.output if args.output else results_dir

    if not os.path.exists(results_dir):
        print(f"Error: Results directory {results_dir} not found")
        return 1

    # If output directory is different, create it
    if output_dir != results_dir:
        os.makedirs(output_dir, exist_ok=True)

        # Copy CSV file to output directory
        csv_path = os.path.join(results_dir, "benchmark_results.csv")
        if os.path.exists(csv_path):
            shutil.copy2(csv_path, os.path.join(output_dir, "benchmark_results.csv"))

    # Generate visualizations
    success = generate_visualizations(output_dir)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
