#!/usr/bin/env python3
"""Unified chart generator for all benchmark results.

Reads all CSV files from results directory and machine profiles,
generates comparison charts with machine context.
"""

import argparse
import glob
import json
import os
import sys
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns


def load_machine_profiles(machines_dir: str) -> dict:
    """Load all machine profiles from machines/ directory."""
    profiles = {}
    for path in glob.glob(os.path.join(machines_dir, "*.json")):
        with open(path) as f:
            profile = json.load(f)
            profiles[profile["machine_id"]] = profile
    return profiles


def load_all_results(results_dir: str) -> dict:
    """Load all CSV files from results directory."""
    results = {}
    for csv_file in glob.glob(os.path.join(results_dir, "*.csv")):
        name = Path(csv_file).stem
        try:
            results[name] = pd.read_csv(csv_file)
        except Exception as e:
            print(f"Warning: failed to load {csv_file}: {e}")
    return results


def get_hostname(machine_id: str, profiles: dict) -> str:
    """Get hostname for machine_id, fallback to short ID."""
    if machine_id in profiles:
        return profiles[machine_id].get("hostname", machine_id[:8])
    return machine_id[:8]


def plot_throughput_comparison(results: dict, profiles: dict, output_dir: str):
    """Bar chart comparing throughput across machines and scenarios."""
    if "comparison" not in results:
        print("  Skipping throughput_comparison: no comparison data")
        return

    df = results["comparison"]
    if "machine_id" not in df.columns:
        print("  Skipping throughput_comparison: no machine_id column")
        return

    fig, ax = plt.subplots(figsize=(12, 6))

    machines = df["machine_id"].unique()
    scenario_col = "scenario" if "scenario" in df.columns else None
    scenarios = df[scenario_col].unique() if scenario_col else ["default"]

    x = range(len(scenarios))
    width = 0.8 / max(len(machines), 1)

    for i, machine_id in enumerate(machines):
        machine_data = df[df["machine_id"] == machine_id]
        hostname = get_hostname(machine_id, profiles)

        rps_values = []
        for s in scenarios:
            if scenario_col:
                row = machine_data[machine_data[scenario_col] == s]
            else:
                row = machine_data
            if len(row) > 0 and "rps" in row.columns:
                rps_values.append(row["rps"].iloc[0])
            else:
                rps_values.append(0)

        ax.bar([xi + i * width for xi in x], rps_values, width, label=hostname)

    ax.set_xlabel("Scenario")
    ax.set_ylabel("Requests per Second")
    ax.set_title("Throughput Comparison by Machine")
    ax.set_xticks([xi + width * (len(machines) - 1) / 2 for xi in x])
    ax.set_xticklabels(scenarios)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    output_path = os.path.join(output_dir, "throughput_comparison.png")
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: throughput_comparison.png")


def plot_latency_heatmap(results: dict, profiles: dict, output_dir: str):
    """Heatmap of P99 latency across machines and scenarios."""
    if "comparison" not in results:
        print("  Skipping latency_heatmap: no comparison data")
        return

    df = results["comparison"]
    if "machine_id" not in df.columns or "latency_p99_ms" not in df.columns:
        print("  Skipping latency_heatmap: missing required columns")
        return

    # Replace machine_id with hostname for display
    df_display = df.copy()
    df_display["machine"] = df_display["machine_id"].apply(
        lambda x: get_hostname(x, profiles)
    )

    scenario_col = "scenario" if "scenario" in df_display.columns else "test_name"
    if scenario_col not in df_display.columns:
        print("  Skipping latency_heatmap: no scenario column")
        return

    # Handle duplicates by taking mean for each machine/scenario combo
    pivot = df_display.pivot_table(index="machine", columns=scenario_col, values="latency_p99_ms", aggfunc="mean")

    fig, ax = plt.subplots(figsize=(10, 6))
    sns.heatmap(pivot, annot=True, fmt=".1f", cmap="RdYlGn_r",
                ax=ax, cbar_kws={"label": "P99 Latency (ms)"})
    ax.set_title("P99 Latency Heatmap (lower is better)")

    plt.tight_layout()
    output_path = os.path.join(output_dir, "latency_heatmap.png")
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: latency_heatmap.png")


def plot_efficiency_scatter(results: dict, profiles: dict, output_dir: str):
    """Scatter plot: RPS vs Memory usage."""
    if "comparison" not in results:
        print("  Skipping efficiency_scatter: no comparison data")
        return

    df = results["comparison"]
    required = ["machine_id", "rps", "mem_avg_mb"]
    if not all(c in df.columns for c in required):
        print("  Skipping efficiency_scatter: missing required columns")
        return

    fig, ax = plt.subplots(figsize=(10, 8))

    for machine_id in df["machine_id"].unique():
        machine_data = df[df["machine_id"] == machine_id]
        hostname = get_hostname(machine_id, profiles)
        ax.scatter(machine_data["mem_avg_mb"], machine_data["rps"],
                   label=hostname, s=100, alpha=0.7)

        for _, row in machine_data.iterrows():
            if "scenario" in row:
                ax.annotate(row["scenario"],
                            (row["mem_avg_mb"], row["rps"]),
                            textcoords="offset points", xytext=(5, 5), fontsize=8)

    ax.set_xlabel("Memory Usage (MB)")
    ax.set_ylabel("Requests per Second")
    ax.set_title("Efficiency: Throughput vs Memory")
    ax.legend()
    ax.grid(alpha=0.3)

    plt.tight_layout()
    output_path = os.path.join(output_dir, "efficiency.png")
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: efficiency.png")


def plot_memory_usage(results: dict, profiles: dict, output_dir: str):
    """Line chart of memory usage across scenarios."""
    if "comparison" not in results:
        print("  Skipping memory_usage: no comparison data")
        return

    df = results["comparison"]
    if "machine_id" not in df.columns or "mem_avg_mb" not in df.columns:
        print("  Skipping memory_usage: missing required columns")
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    scenario_col = "scenario" if "scenario" in df.columns else None
    if not scenario_col:
        print("  Skipping memory_usage: no scenario column")
        return

    for machine_id in df["machine_id"].unique():
        machine_data = df[df["machine_id"] == machine_id]
        hostname = get_hostname(machine_id, profiles)

        ax.plot(machine_data[scenario_col], machine_data["mem_avg_mb"],
                marker="o", label=hostname, linewidth=2, markersize=8)

    ax.set_xlabel("Scenario")
    ax.set_ylabel("Memory (MB)")
    ax.set_title("Memory Usage Comparison")
    ax.legend()
    ax.grid(alpha=0.3)

    plt.tight_layout()
    output_path = os.path.join(output_dir, "memory_usage.png")
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: memory_usage.png")


def plot_load_test_summary(results: dict, profiles: dict, output_dir: str):
    """Bar chart for load test results if available."""
    if "load" not in results:
        print("  Skipping load_test_summary: no load data")
        return

    df = results["load"]
    if "machine_id" not in df.columns:
        print("  Skipping load_test_summary: no machine_id column")
        return

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Throughput by test
    ax1 = axes[0]
    for machine_id in df["machine_id"].unique():
        machine_data = df[df["machine_id"] == machine_id]
        hostname = get_hostname(machine_id, profiles)
        if "throughput_rps" in machine_data.columns:
            test_names = machine_data["test_name"] if "test_name" in machine_data.columns else range(len(machine_data))
            ax1.bar([f"{hostname}-{t}" for t in test_names],
                    machine_data["throughput_rps"], label=hostname, alpha=0.7)
    ax1.set_xlabel("Test")
    ax1.set_ylabel("Throughput (RPS)")
    ax1.set_title("Load Test Throughput")
    ax1.tick_params(axis='x', rotation=45)
    ax1.legend()
    ax1.grid(axis="y", alpha=0.3)

    # Latency distribution
    ax2 = axes[1]
    for machine_id in df["machine_id"].unique():
        machine_data = df[df["machine_id"] == machine_id]
        hostname = get_hostname(machine_id, profiles)
        if "latency_avg_ms" in machine_data.columns:
            test_names = machine_data["test_name"] if "test_name" in machine_data.columns else range(len(machine_data))
            ax2.bar([f"{hostname}-{t}" for t in test_names],
                    machine_data["latency_avg_ms"], label=hostname, alpha=0.7)
    ax2.set_xlabel("Test")
    ax2.set_ylabel("Avg Latency (ms)")
    ax2.set_title("Load Test Latency")
    ax2.tick_params(axis='x', rotation=45)
    ax2.legend()
    ax2.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    output_path = os.path.join(output_dir, "load_test_summary.png")
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: load_test_summary.png")


def main():
    parser = argparse.ArgumentParser(description="Generate benchmark charts")
    parser.add_argument("--results-dir", "-r", default=None,
                        help="Results directory (default: parent of this script)")
    parser.add_argument("--output", "-o", default=None,
                        help="Output directory for charts")
    args = parser.parse_args()

    # Determine paths
    script_dir = Path(__file__).parent
    results_dir = Path(args.results_dir) if args.results_dir else script_dir.parent
    output_dir = Path(args.output) if args.output else script_dir / "output"
    machines_dir = results_dir / "machines"

    os.makedirs(output_dir, exist_ok=True)

    print(f"Results directory: {results_dir}")
    print(f"Machines directory: {machines_dir}")
    print(f"Output directory: {output_dir}")
    print()

    # Load data
    print("Loading data...")
    profiles = load_machine_profiles(str(machines_dir))
    print(f"  Found {len(profiles)} machine profiles")

    results = load_all_results(str(results_dir))
    print(f"  Found {len(results)} result files: {list(results.keys())}")
    print()

    if not results:
        print("No results found. Run tests first.")
        print(f"Expected CSV files in: {results_dir}")
        sys.exit(1)

    # Generate charts
    print("Generating charts...")
    plot_throughput_comparison(results, profiles, str(output_dir))
    plot_latency_heatmap(results, profiles, str(output_dir))
    plot_efficiency_scatter(results, profiles, str(output_dir))
    plot_memory_usage(results, profiles, str(output_dir))
    plot_load_test_summary(results, profiles, str(output_dir))

    print()
    print(f"Done! Charts saved to: {output_dir}")


if __name__ == "__main__":
    main()
