#!/usr/bin/env python3
"""
Visualization script for benchmarking results.
Generates bar charts and scaling analysis plots to visualize performance.
"""

import argparse
import os
import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

# Set theme and style for professional look
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


def sort_implementations(implementations):
    """
    Sort implementations with non-IO implementations first, followed by IO implementations.
    Within each group, implementations are sorted alphabetically.

    Args:
        implementations: List or Series of implementation names

    Returns:
        List of sorted implementation names
    """
    non_io = [impl for impl in implementations if "IO" not in impl]
    io = [impl for impl in implementations if "IO" in impl]

    return sorted(non_io) + sorted(io)

def plot_implementation_comparison(df, dataset, processes, output_dir):
    """
    Generate bar chart comparing different implementations for a specific dataset and process count.

    Args:
        df (DataFrame): The benchmark results
        dataset (str): The dataset to analyze
        processes (int): The process count to analyze
        output_dir (str): Directory to save the plot
    """
    # Filter data for the given dataset and processes
    filtered_df = df[(df['dataset'] == dataset) & (df['processes'] == processes)]

    if filtered_df.empty:
        print(f"No data found for dataset {dataset} with {processes} processes")
        return

    # Group by implementation and compute mean times
    impl_summary = filtered_df.groupby('implementation').agg({
        'read_time': 'mean',
        'main_time': 'mean',
        'total_time': 'mean'
    }).reset_index()

    # Sort implementations - non-IO first, then IO implementations
    impl_order = sort_implementations(impl_summary['implementation'])

    # Use the new order for sorting
    impl_summary['implementation'] = pd.Categorical(
        impl_summary['implementation'], categories=impl_order, ordered=True
    )
    impl_summary = impl_summary.sort_values('implementation')

    # Create figure
    fig, ax = plt.subplots(figsize=(max(10, len(impl_summary) * 2), 8))

    # Setup bar plot
    barWidth = 0.65
    br = np.arange(len(impl_summary))

    # Get dimensions from the first row
    try:
        first_row = filtered_df.iloc[0]
        dims = f"{first_row['nx']}x{first_row['ny']}x{first_row['nz']}, {first_row['timesteps']} timesteps"
    except:
        dims = os.path.basename(dataset)

    # Calculate max value for y-axis limit
    max_total = (impl_summary['read_time'] + impl_summary['main_time']).max()
    y_limit = max_total * 1.3  # Add 30% padding for labels

    # Plotting stacked bars
    read_bars = ax.bar(br, impl_summary['read_time'], width=barWidth,
                      color=COLORS[0], edgecolor='grey', label='Read Time')

    main_bars = ax.bar(br, impl_summary['main_time'], width=barWidth,
                      bottom=impl_summary['read_time'],
                      color=COLORS[1], edgecolor='grey', label='Main Time')

    # Find best implementation (lowest total time)
    best_idx = np.argmin(impl_summary['total_time'].values)
    best_impl = impl_summary.iloc[best_idx]['implementation']
    best_time = impl_summary.iloc[best_idx]['total_time']

    # Highlight best implementation
    total = impl_summary.iloc[best_idx]['read_time'] + impl_summary.iloc[best_idx]['main_time']
    ax.text(best_idx, total + max_total * 0.1, "★ BEST",
           ha='center', va='bottom', fontweight='bold', color='black',
           bbox=dict(facecolor='yellow', alpha=0.3, boxstyle='round,pad=0.3'))

    # Add data value labels
    for i, (r, m) in enumerate(zip(impl_summary['read_time'], impl_summary['main_time'])):
        total = r + m

        # Only show read time label if it's significant (at least 5% of total)
        if r > 0.05 * total:
            ax.text(i, r/2, format_time(r), ha='center', va='center',
                    fontweight='bold', color='white', fontsize=10)

        # Main time label
        if m > 0.05 * total:
            ax.text(i, r + m/2, format_time(m), ha='center', va='center',
                fontweight='bold', color='white', fontsize=10)

        # Total time - positioned slightly above the bar with background
        # Create a text box with background for the total time
        bbox_props = dict(
            boxstyle="round,pad=0.3",
            fc="lightyellow",
            ec="gray",
            lw=1,
            alpha=0.8
        )

        ax.text(
            i, total + 0.02 * max_total,
            format_time(total),
            ha='center',
            va='bottom',
            fontsize=10,
            fontweight='bold',
            color='black',
            bbox=bbox_props
        )

    # Styling
    ax.set_xlabel('Implementation', fontweight='bold', fontsize=12)
    ax.set_ylabel('Time (seconds)', fontweight='bold', fontsize=12)
    ax.set_title(f'Implementation Comparison\nDataset: {os.path.basename(dataset)}, {processes} Processes\n({dims})',
                 fontweight='bold', fontsize=14, pad=20)

    # Set y-axis limit with padding for labels
    ax.set_ylim(0, y_limit)

    # Set ticks and labels
    ax.set_xticks(br)
    ax.set_xticklabels(impl_summary['implementation'], fontsize=11)

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

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Save the figure
    output_file = os.path.join(output_dir, f"impl_comparison_{os.path.basename(dataset)}_{processes}p.png")
    plt.savefig(output_file, dpi=300)
    print(f"Saved: {output_file}")
    plt.close()

def plot_scaling_analysis(df, implementation, dataset, output_dir):
    """
    Generate scaling analysis plots for a specific implementation and dataset.

    Args:
        df (DataFrame): The benchmark results
        implementation (str): The implementation to analyze
        dataset (str): The dataset to analyze
        output_dir (str): Directory to save the plot
    """
    # Filter data for the given implementation and dataset
    filtered_df = df[(df['implementation'] == implementation) & (df['dataset'] == dataset)]

    if filtered_df.empty:
        print(f"No data found for implementation {implementation} with dataset {dataset}")
        return

    # Skip if we don't have multiple process counts
    process_counts = sorted(filtered_df['processes'].unique())
    if len(process_counts) <= 1:
        print(f"Insufficient process counts for scaling analysis of {implementation} with {dataset}")
        return

    # Extract problem dimensions for plot title
    try:
        first_row = filtered_df.iloc[0]
        dims = f"{first_row['nx']}x{first_row['ny']}x{first_row['nz']}, {first_row['timesteps']} timesteps"
    except:
        dims = os.path.basename(dataset)

    # Group by process count and compute mean times
    proc_summary = filtered_df.groupby('processes').agg({
        'read_time': 'mean',
        'main_time': 'mean',
        'total_time': 'mean'
    }).reset_index()

    # Sort by process count
    proc_summary = proc_summary.sort_values('processes')

    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))

    # Plot execution time vs. number of processes
    x = proc_summary['processes']
    y = proc_summary['total_time']

    # Execution time plot
    ax1.plot(x, y, marker='o', label=implementation, color=COLORS[0], linewidth=2, markersize=8)

    # Add data labels
    for j, (proc, time) in enumerate(zip(x, y)):
        ax1.text(proc, time + 0.05 * time, format_time(time),
               ha='center', va='bottom', fontsize=9)

    # Calculate speedup
    baseline_time = proc_summary['total_time'].iloc[0]  # Time with minimum processes
    y_speedup = baseline_time / proc_summary['total_time']

    # Speedup plot
    ax2.plot(x, y_speedup, marker='o', label="Actual Speedup", color=COLORS[0], linewidth=2, markersize=8)

    # Add ideal speedup line
    ideal_x = x.copy()
    ideal_y = ideal_x / ideal_x.iloc[0]
    ax2.plot(ideal_x, ideal_y, 'k--', label='Ideal Speedup')

    # Add data labels for speedup
    for j, (proc, speedup) in enumerate(zip(x, y_speedup)):
        ax2.text(proc, speedup + 0.1, f"{speedup:.2f}x",
                ha='center', va='bottom', fontsize=9)

    # Style plots
    ax1.set_xlabel('Number of Processes', fontweight='bold')
    ax1.set_ylabel('Execution Time (seconds)', fontweight='bold')
    ax1.set_title(f'Strong Scaling: Execution Time\n({dims})', fontweight='bold')

    # Use log scale if appropriate
    if len(process_counts) > 3:
        try:
            ax1.set_xscale('log', base=2)
            ax1.set_yscale('log', base=10)
        except:
            # Fallback to linear scale if log scale fails
            pass

    ax1.grid(True, which="both", ls="-", alpha=0.2)
    ax1.legend()

    ax2.set_xlabel('Number of Processes', fontweight='bold')
    ax2.set_ylabel('Speedup', fontweight='bold')
    ax2.set_title(f'Strong Scaling: Speedup\n({dims})', fontweight='bold')

    if len(process_counts) > 3:
        try:
            ax2.set_xscale('log', base=2)
        except:
            pass

    ax2.grid(True, which="both", ls="-", alpha=0.2)
    ax2.legend()

    # Add implementation name as suptitle
    plt.suptitle(f'Implementation: {implementation}', fontsize=16, fontweight='bold')
    plt.tight_layout()
    plt.subplots_adjust(top=0.88)  # Adjust for the suptitle

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Save the figure
    output_file = os.path.join(output_dir, f"scaling_{implementation}_{os.path.basename(dataset)}.png")
    plt.savefig(output_file, dpi=300)
    print(f"Saved: {output_file}")
    plt.close()

def plot_scaling_analysis_combined(df, dataset, output_dir):
    """
    Generate a combined scaling plot comparing all implementations for a specific dataset
    across different process counts.

    Args:
        df (DataFrame): The benchmark results
        dataset (str): The dataset to analyze
        output_dir (str): Directory to save the plot
    """
    # Filter data for the given dataset
    filtered_df = df[df['dataset'] == dataset]

    if filtered_df.empty:
        print(f"No data found for dataset {dataset}")
        return

    # Get all unique implementations and process counts
    implementations = sort_implementations(filtered_df['implementation'].unique())
    process_counts = sorted(filtered_df['processes'].unique())

    # Skip if we don't have multiple process counts
    if len(process_counts) <= 1:
        print(f"Insufficient process counts for scaling analysis with {dataset}")
        return

    # Extract problem dimensions for plot title
    try:
        first_row = filtered_df.iloc[0]
        dims = f"{first_row['nx']}x{first_row['ny']}x{first_row['nz']}, {first_row['timesteps']} timesteps"
    except:
        dims = os.path.basename(dataset)

    # Create figure with two subplots - execution time and speedup
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 10))

    # Color palette with enough colors for all implementations
    colors = plt.cm.viridis(np.linspace(0, 0.9, len(implementations)))
    markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p', '*', 'h', 'H', '+', 'x', 'd', '|']

    # Dictionary to store baseline times (for speedup calculation)
    baseline_times = {}

    # Dictionary to store handles for the legend
    handles = []

    # Plot each implementation
    for i, impl in enumerate(implementations):
        impl_df = filtered_df[filtered_df['implementation'] == impl]

        # Group by process count and compute mean times
        impl_summary = impl_df.groupby('processes').agg({
            'total_time': 'mean'
        }).reset_index()

        # Skip if this implementation doesn't have enough data points
        if len(impl_summary) <= 1:
            continue

        # Sort by process count
        impl_summary = impl_summary.sort_values('processes')

        # Define color, marker and style for this implementation
        color = colors[i]
        marker = markers[i % len(markers)]

        # Plot execution time
        line, = ax1.plot(impl_summary['processes'], impl_summary['total_time'],
                marker=marker, label=impl, color=color, linewidth=2, markersize=8)
        handles.append(line)

        # Store baseline time (time with minimum processes) for speedup calculation
        if not impl_summary.empty:
            baseline_times[impl] = impl_summary['total_time'].iloc[0]

            # Calculate and plot speedup
            speedup = baseline_times[impl] / impl_summary['total_time']
            ax2.plot(impl_summary['processes'], speedup,
                   marker=marker, label=impl, color=color, linewidth=2, markersize=8)

    # Add ideal speedup line in the second plot
    if process_counts:
        ideal_x = np.array(process_counts)
        ideal_y = ideal_x / ideal_x[0]
        ax2.plot(ideal_x, ideal_y, 'k--', label='Ideal Speedup', linewidth=2)

    # Style the execution time plot (ax1)
    ax1.set_xlabel('Number of Processes', fontweight='bold', fontsize=12)
    ax1.set_ylabel('Execution Time (seconds)', fontweight='bold', fontsize=12)
    ax1.set_title(f'Strong Scaling: Execution Time Comparison\n({dims})', fontweight='bold', fontsize=14)

    # Use log scale if appropriate
    if len(process_counts) > 3:
        try:
            ax1.set_xscale('log', base=2)
            ax1.set_yscale('log', base=10)
        except:
            # Fallback to linear scale if log scale fails
            pass

    ax1.grid(True, which="both", ls="-", alpha=0.2)

    # Style the speedup plot (ax2)
    ax2.set_xlabel('Number of Processes', fontweight='bold', fontsize=12)
    ax2.set_ylabel('Speedup', fontweight='bold', fontsize=12)
    ax2.set_title(f'Strong Scaling: Speedup Comparison\n({dims})', fontweight='bold', fontsize=14)

    if len(process_counts) > 3:
        try:
            ax2.set_xscale('log', base=2)
        except:
            pass

    ax2.grid(True, which="both", ls="-", alpha=0.2)

    # Create a shared legend for both plots
    # Place the legend outside the plot area
    lgd = fig.legend(handles, implementations,
                    loc='lower center',
                    bbox_to_anchor=(0.5, -0.12),
                    ncol=min(5, len(implementations)),
                    fontsize=10,
                    framealpha=0.9)

    # Add dataset name as suptitle
    plt.suptitle(f'Scaling Analysis for Dataset: {os.path.basename(dataset)}',
                fontsize=16, fontweight='bold')

    plt.tight_layout()
    plt.subplots_adjust(bottom=0.2, top=0.85)  # Adjust for the legend and suptitle

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Save the figure
    output_file = os.path.join(output_dir, f"scaling_combined_{os.path.basename(dataset)}.png")
    plt.savefig(output_file, dpi=300, bbox_extra_artists=(lgd,), bbox_inches='tight')
    print(f"Saved: {output_file}")
    plt.close()

def plot_dataset_comparison(df, implementation, processes, output_dir):
    """
    Generate bar chart comparing different datasets for a specific implementation and process count.

    Args:
        df (DataFrame): The benchmark results
        implementation (str): The implementation to analyze
        processes (int): The process count to analyze
        output_dir (str): Directory to save the plot
    """
    # Filter data for the given implementation and processes
    filtered_df = df[(df['implementation'] == implementation) & (df['processes'] == processes)]

    if filtered_df.empty:
        print(f"No data found for implementation {implementation} with {processes} processes")
        return

    # Group by dataset and compute mean times
    ds_summary = filtered_df.groupby('dataset').agg({
        'read_time': 'mean',
        'main_time': 'mean',
        'total_time': 'mean',
        'nx': 'first',
        'ny': 'first',
        'nz': 'first',
        'timesteps': 'first'
    }).reset_index()

    # Sort by total time for better visualization
    ds_summary = ds_summary.sort_values('total_time')

    # Create figure
    fig, ax = plt.subplots(figsize=(max(10, len(ds_summary) * 2), 8))

    # Setup bar plot
    barWidth = 0.65
    br = np.arange(len(ds_summary))

    # Calculate max value for y-axis limit
    max_total = (ds_summary['read_time'] + ds_summary['main_time']).max()
    y_limit = max_total * 1.3  # Add 30% padding for labels

    # Create dataset labels with dimensions
    dataset_labels = []
    for _, row in ds_summary.iterrows():
        base_name = os.path.basename(row['dataset'])
        dim_label = f"{base_name}\n({row['nx']}x{row['ny']}x{row['nz']}, {row['timesteps']} ts)"
        dataset_labels.append(dim_label)

    # Plotting stacked bars
    read_bars = ax.bar(br, ds_summary['read_time'], width=barWidth,
                      color=COLORS[0], edgecolor='grey', label='Read Time')

    main_bars = ax.bar(br, ds_summary['main_time'], width=barWidth,
                      bottom=ds_summary['read_time'],
                      color=COLORS[1], edgecolor='grey', label='Main Time')

    # Add data value labels
    for i, (r, m) in enumerate(zip(ds_summary['read_time'], ds_summary['main_time'])):
        total = r + m

        # Only show read time label if it's significant (at least 5% of total)
        if r > 0.05 * total:
            ax.text(i, r/2, format_time(r), ha='center', va='center',
                    fontweight='bold', color='white', fontsize=10)

        # Main time label
        if m > 0.05 * total:
            ax.text(i, r + m/2, format_time(m), ha='center', va='center',
                fontweight='bold', color='white', fontsize=10)

        # Total time - positioned slightly above the bar with background
        # Create a text box with background for the total time
        bbox_props = dict(
            boxstyle="round,pad=0.3",
            fc="lightyellow",
            ec="gray",
            lw=1,
            alpha=0.8
        )

        ax.text(
            i, total + 0.02 * max_total,
            format_time(total),
            ha='center',
            va='bottom',
            fontsize=10,
            fontweight='bold',
            color='black',
            bbox=bbox_props
        )

    # Styling
    ax.set_xlabel('Dataset', fontweight='bold', fontsize=12)
    ax.set_ylabel('Time (seconds)', fontweight='bold', fontsize=12)
    ax.set_title(f'Dataset Comparison\nImplementation: {implementation}, {processes} Processes',
                 fontweight='bold', fontsize=14, pad=20)

    # Set y-axis limit with padding for labels
    ax.set_ylim(0, y_limit)

    # Set ticks and labels
    ax.set_xticks(br)
    ax.set_xticklabels(dataset_labels, fontsize=10)

    # Add horizontal grid lines for better readability
    ax.yaxis.grid(True, linestyle='--', alpha=0.7)

    # Move legend to a better position
    ax.legend(loc='upper right', framealpha=0.9)

    # Add tight layout with more padding
    plt.tight_layout(pad=2.0)

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Save the figure
    output_file = os.path.join(output_dir, f"dataset_comparison_{implementation}_{processes}p.png")
    plt.savefig(output_file, dpi=300)
    print(f"Saved: {output_file}")
    plt.close()

def plot_dataset_comparison_combined(df, processes, output_dir):
    """
    Generate a combined bar chart that compares all implementations across different datasets
    for a specific process count. For each dataset, implementations are grouped together.

    This version uses:
    - Only total time (no read/main time separation)
    - Different colors for each implementation
    - Horizontal text labels

    Args:
        df (DataFrame): The benchmark results
        processes (int): The process count to analyze
        output_dir (str): Directory to save the plot
    """
    # Filter data for the given process count
    filtered_df = df[df['processes'] == processes]

    if filtered_df.empty:
        print(f"No data found for {processes} processes")
        return

    # Get unique datasets and implementations
    datasets = filtered_df['dataset'].unique()
    implementations = sort_implementations(filtered_df['implementation'].unique())

    if len(datasets) <= 1 or len(implementations) <= 1:
        print(f"Not enough datasets or implementations for combined comparison")
        return

    # Create a multi-index DataFrame
    comparison_data = []
    for dataset in datasets:
        for impl in implementations:
            subset = filtered_df[(filtered_df['dataset'] == dataset) &
                                 (filtered_df['implementation'] == impl)]
            if not subset.empty:
                comparison_data.append({
                    'dataset': dataset,
                    'implementation': impl,
                    'total_time': subset['total_time'].mean(),  # Use total_time directly
                    'nx': subset['nx'].iloc[0],
                    'ny': subset['ny'].iloc[0],
                    'nz': subset['nz'].iloc[0],
                    'timesteps': subset['timesteps'].iloc[0]
                })

    # Convert to DataFrame
    comp_df = pd.DataFrame(comparison_data)

    # Create figure
    fig_width = max(12, len(datasets) * 2 * len(implementations) / 4)
    fig, ax = plt.subplots(figsize=(fig_width, 10))

    # Prepare for plotting
    bar_width = 0.7 / len(implementations)  # Width of each bar, adjusted for number of implementations
    group_spacing = 0.3  # Spacing between dataset groups

    # Create color palette with enough distinct colors
    color_palette = sns.color_palette("muted", len(implementations))

    # Track best implementation for each dataset
    best_implementations = {}

    # Create dataset labels with dimensions
    dataset_labels = []
    for dataset in datasets:
        dataset_df = comp_df[comp_df['dataset'] == dataset].iloc[0]
        base_name = os.path.basename(dataset)
        dim_label = f"{base_name}\n({dataset_df['nx']}x{dataset_df['ny']}x{dataset_df['nz']}, {dataset_df['timesteps']} ts)"
        dataset_labels.append(dim_label)

        # Find best implementation for this dataset
        best_impl_row = comp_df[comp_df['dataset'] == dataset].loc[comp_df[comp_df['dataset'] == dataset]['total_time'].idxmin()]
        best_implementations[dataset] = {
            'implementation': best_impl_row['implementation'],
            'time': best_impl_row['total_time']
        }

    # Plot bars for each dataset and implementation
    dataset_positions = []  # Center positions for dataset labels
    legend_handles = []

    for i, dataset in enumerate(datasets):
        group_start = i * (len(implementations) * bar_width + group_spacing)
        dataset_positions.append(group_start + (len(implementations) * bar_width) / 2)

        for j, impl in enumerate(implementations):
            subset = comp_df[(comp_df['dataset'] == dataset) & (comp_df['implementation'] == impl)]
            if not subset.empty:
                x_pos = group_start + j * bar_width
                total_time = subset['total_time'].iloc[0]

                # Use color based on implementation
                color_idx = implementations.index(impl)
                bar = ax.bar(x_pos, total_time, width=bar_width,
                            color=color_palette[color_idx],
                            edgecolor='grey')

                # Add to legend only once
                if i == 0:
                    legend_handles.append((bar[0], impl))

                # Add data value labels
                if total_time > 0:
                    # Total time - positioned slightly above the bar with background
                    # Create a text box with background for the total time
                    bbox_props = dict(
                        boxstyle="round,pad=0.3",
                        fc="lightyellow",
                        ec="gray",
                        lw=1,
                        alpha=0.8
                    )

                    label_y = total_time + (max(comp_df['total_time']) * 0.02)
                    ax.text(
                        x_pos, label_y,
                        format_time(total_time),
                        ha='center',
                        va='bottom',
                        fontsize=10,
                        fontweight='bold',
                        color='black',
                        bbox=bbox_props
                    )


                # Mark if this is the best implementation for this dataset
                if impl == best_implementations[dataset]['implementation']:
                    label_y = total_time + (max(comp_df['total_time']) * 0.05)
                    ax.text(x_pos, label_y, "★",
                           ha='center', va='bottom', fontweight='bold', color='red',
                           fontsize=16)

    # Add dataset separators (vertical lines between dataset groups)
    for i in range(1, len(datasets)):
        pos = i * (len(implementations) * bar_width + group_spacing) - group_spacing/2
        ax.axvline(x=pos, color='gray', linestyle='--', alpha=0.3)

    # Style the plot
    ax.set_xticks(dataset_positions)
    ax.set_xticklabels(dataset_labels, fontsize=10)

    # Add a legend with implementation colors
    legend_items = [handle for handle, _ in legend_handles]
    legend_labels = [label for _, label in legend_handles]
    ax.legend(legend_items, legend_labels, loc='upper right', framealpha=0.9, ncol=min(3, len(implementations)))

    # Set y-axis with some padding
    ax.set_ylim(0, max(comp_df['total_time']) * 1.25)

    # Add grid lines and labels
    ax.grid(True, axis='y', linestyle='--', alpha=0.3)
    ax.set_ylabel('Total Time (seconds)', fontweight='bold', fontsize=12)
    ax.set_title(f'Implementation Comparison Across Datasets\n{processes} Processes',
                fontweight='bold', fontsize=14, pad=20)

    # Add tight layout
    plt.tight_layout()

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Save the figure
    output_file = os.path.join(output_dir, f"dataset_implementation_comparison_{processes}p.png")
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Saved: {output_file}")
    plt.close()

def generate_all_visualizations(df, output_dir):
    """
    Generate all possible visualization combinations.

    Args:
        df (DataFrame): The benchmark results
        output_dir (str): Directory to save the plots
    """
    # Create output directories
    impl_dir = os.path.join(output_dir, "implementation_comparisons")
    scaling_dir = os.path.join(output_dir, "scaling_individual")
    dataset_dir = os.path.join(output_dir, "dataset_individual")
    combined_dir = os.path.join(output_dir, "dataset_combined")

    os.makedirs(impl_dir, exist_ok=True)
    os.makedirs(scaling_dir, exist_ok=True)
    os.makedirs(dataset_dir, exist_ok=True)
    os.makedirs(combined_dir, exist_ok=True)

    # Get unique values
    datasets = df['dataset'].unique()
    implementations = df['implementation'].unique()
    process_counts = df['processes'].unique()

    # Generate all implementation comparisons
    print("\n=== Generating Implementation Comparisons ===")
    for dataset in datasets:
        for processes in process_counts:
            print(f"Dataset: {os.path.basename(dataset)}, Processes: {processes}")
            plot_implementation_comparison(df, dataset, processes, impl_dir)

    # Generate all scaling analysis plots
    print("\n=== Generating Scaling Analysis Plots ===")
    for implementation in implementations:
        for dataset in datasets:
            print(f"Implementation: {implementation}, Dataset: {os.path.basename(dataset)}")
            plot_scaling_analysis(df, implementation, dataset, scaling_dir)

    # Generate combined scaling analysis plots
    print("\n=== Generating Combined Scaling Analysis Plots ===")
    scaling_combined_dir = os.path.join(output_dir, "scaling_combined")
    os.makedirs(scaling_combined_dir, exist_ok=True)
    for dataset in datasets:
        print(f"Dataset: {os.path.basename(dataset)}")
        plot_scaling_analysis_combined(df, dataset, scaling_combined_dir)

    # Generate all dataset comparisons
    print("\n=== Generating Dataset Comparisons ===")
    for implementation in implementations:
        for processes in process_counts:
            print(f"Implementation: {implementation}, Processes: {processes}")
            plot_dataset_comparison(df, implementation, processes, dataset_dir)

    # Generate combined dataset-implementation comparisons
    print("\n=== Generating Combined Dataset-Implementation Comparisons ===")
    for processes in process_counts:
        print(f"Processes: {processes}")
        plot_dataset_comparison_combined(df, processes, combined_dir)

    print(f"\nAll visualizations saved to {output_dir}")

def main():
    parser = argparse.ArgumentParser(description='Generate visualizations for benchmark results')
    parser.add_argument('csv_file', help='CSV file containing benchmark results')
    parser.add_argument('output_dir', help='Output directory for visualizations')
    parser.add_argument('--implementation', '-i', help='Filter by implementation')
    parser.add_argument('--dataset', '-d', help='Filter by dataset')
    parser.add_argument('--processes', '-p', type=int, help='Filter by process count')

    args = parser.parse_args()

    # Check if the CSV file exists
    if not os.path.exists(args.csv_file):
        print(f"Error: CSV file {args.csv_file} not found")
        return 1

    # Read the CSV file
    df = pd.read_csv(args.csv_file)

    # Create output directory if it doesn't exist
    os.makedirs(args.output_dir, exist_ok=True)

    # Check if 'all' is specified
    if args.output_dir.lower() == 'all':
        output_dir = args.output_dir
        generate_all_visualizations(df, output_dir)
        return 0

    # Handle specific visualization requests
    if args.implementation and args.dataset and args.processes:
        # Plot implementation comparison
        plot_implementation_comparison(df, args.dataset, args.processes, args.output_dir)
    elif args.implementation and args.dataset:
        # Plot scaling analysis
        plot_scaling_analysis(df, args.implementation, args.dataset, args.output_dir)
    elif args.implementation and args.processes:
        # Plot dataset comparison
        plot_dataset_comparison(df, args.implementation, args.processes, args.output_dir)
    else:
        # Generate all visualizations
        generate_all_visualizations(df, args.output_dir)

    return 0

if __name__ == "__main__":
    sys.exit(main())
