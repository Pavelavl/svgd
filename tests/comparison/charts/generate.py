#!/usr/bin/env python3
"""Generate benchmark charts from CSV results."""

import argparse
import os
import sys

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns


def load_data(csv_path: str) -> pd.DataFrame:
    """Load benchmark results from CSV."""
    return pd.read_csv(csv_path)


def plot_throughput_comparison(df: pd.DataFrame, output_dir: str):
    """Bar chart comparing RPS across systems and scenarios."""
    fig, ax = plt.subplots(figsize=(10, 6))

    systems = df['system'].unique()
    scenarios = df['scenario'].unique()

    x = range(len(scenarios))
    width = 0.25

    for i, system in enumerate(systems):
        system_data = df[df['system'] == system]
        rps_values = [system_data[system_data['scenario'] == s]['rps'].values[0]
                      if len(system_data[system_data['scenario'] == s]) > 0 else 0
                      for s in scenarios]
        ax.bar([xi + i * width for xi in x], rps_values, width, label=system)

    ax.set_xlabel('Scenario')
    ax.set_ylabel('Requests per Second')
    ax.set_title('Throughput Comparison')
    ax.set_xticks([xi + width for xi in x])
    ax.set_xticklabels(scenarios)
    ax.legend()
    ax.grid(axis='y', alpha=0.3)

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'throughput_comparison.png'), dpi=150)
    plt.close()
    print(f"  Saved: throughput_comparison.png")


def plot_latency_comparison(df: pd.DataFrame, output_dir: str):
    """Bar chart of latency comparison."""
    fig, ax = plt.subplots(figsize=(10, 6))

    sns.barplot(data=df, x='scenario', y='latency_p99_ms', hue='system',
                ax=ax, errorbar=None)
    ax.set_xlabel('Scenario')
    ax.set_ylabel('P99 Latency (ms)')
    ax.set_title('P99 Latency Comparison')
    ax.legend(title='System')
    ax.grid(axis='y', alpha=0.3)

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'latency_comparison.png'), dpi=150)
    plt.close()
    print(f"  Saved: latency_comparison.png")


def plot_memory_usage(df: pd.DataFrame, output_dir: str):
    """Line chart of memory usage."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for system in df['system'].unique():
        system_data = df[df['system'] == system].sort_values('concurrency')
        ax.plot(system_data['scenario'], system_data['mem_avg_mb'],
                marker='o', label=system, linewidth=2, markersize=8)

    ax.set_xlabel('Scenario')
    ax.set_ylabel('Memory (MB)')
    ax.set_title('Memory Usage Comparison')
    ax.legend()
    ax.grid(alpha=0.3)

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'memory_usage.png'), dpi=150)
    plt.close()
    print(f"  Saved: memory_usage.png")


def plot_efficiency(df: pd.DataFrame, output_dir: str):
    """Scatter plot: RPS vs Memory (efficiency)."""
    fig, ax = plt.subplots(figsize=(10, 8))

    colors = {'svgd': '#2ecc71', 'rrdtool': '#3498db', 'graphite': '#e74c3c'}

    for system in df['system'].unique():
        system_data = df[df['system'] == system]
        ax.scatter(system_data['mem_avg_mb'], system_data['rps'],
                   label=system, s=100, alpha=0.7, c=colors.get(system, 'gray'))

        for _, row in system_data.iterrows():
            ax.annotate(row['scenario'],
                        (row['mem_avg_mb'], row['rps']),
                        textcoords="offset points",
                        xytext=(5, 5),
                        fontsize=8)

    ax.set_xlabel('Memory Usage (MB)')
    ax.set_ylabel('Requests per Second')
    ax.set_title('Efficiency: Throughput vs Memory')
    ax.legend()
    ax.grid(alpha=0.3)

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'efficiency.png'), dpi=150)
    plt.close()
    print(f"  Saved: efficiency.png")


def plot_latency_heatmap(df: pd.DataFrame, output_dir: str):
    """Heatmap of P99 latency."""
    fig, ax = plt.subplots(figsize=(8, 6))

    pivot = df.pivot(index='system', columns='scenario', values='latency_p99_ms')

    sns.heatmap(pivot, annot=True, fmt='.1f', cmap='RdYlGn_r',
                ax=ax, cbar_kws={'label': 'P99 Latency (ms)'})

    ax.set_title('P99 Latency Heatmap (lower is better)')

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'latency_heatmap.png'), dpi=150)
    plt.close()
    print(f"  Saved: latency_heatmap.png")


def main():
    parser = argparse.ArgumentParser(description='Generate benchmark charts')
    parser.add_argument('csv', help='Path to benchmark results CSV')
    parser.add_argument('--output', '-o', default='./',
                        help='Output directory for charts')
    args = parser.parse_args()

    if not os.path.exists(args.csv):
        print(f"Error: CSV file not found: {args.csv}")
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    print(f"Loading data from: {args.csv}")
    df = load_data(args.csv)
    print(f"Found {len(df)} benchmark results")
    print()

    print("Generating charts...")
    plot_throughput_comparison(df, args.output)
    plot_latency_comparison(df, args.output)
    plot_memory_usage(df, args.output)
    plot_efficiency(df, args.output)
    plot_latency_heatmap(df, args.output)

    print()
    print(f"Done! Charts saved to: {args.output}")


if __name__ == '__main__':
    main()
