#!/usr/bin/env python3

import argparse
import os
import sys

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
import numpy as np

matplotlib.rcParams.update({
    "figure.figsize": (14, 7),
    "font.size": 12,
    "axes.titlesize": 14,
    "axes.labelsize": 12,
})


def load_data(csv_path: str) -> pd.DataFrame:
    """Загрузка и первичная обработка результатов."""
    df = pd.read_csv(csv_path)

    df = df[df["status"] == "ok"].copy()

    if df.empty:
        print("Нет успешных запусков в CSV!")
        sys.exit(1)

    df["time_sec"] = pd.to_numeric(df["time_sec"], errors="coerce")
    df = df.dropna(subset=["time_sec"])

    return df


def compute_stats(df: pd.DataFrame) -> pd.DataFrame:
    """Среднее, медиана, std по каждой паре (solver, graph)."""
    stats = df.groupby(["solver", "graph"])["time_sec"].agg(
        mean_time="mean",
        median_time="median",
        std_time="std",
        min_time="min",
        max_time="max",
        runs="count",
    ).reset_index()

    stats["std_time"] = stats["std_time"].fillna(0)
    return stats


def plot_time_comparison(stats: pd.DataFrame, output_dir: str):
    """Барчарт: время работы на каждом графе, решатели рядом."""
    solvers = stats["solver"].unique()
    graphs = sorted(stats["graph"].unique())
    n_solvers = len(solvers)
    n_graphs = len(graphs)

    # Цвета для решателей
    colors = plt.cm.Set2(np.linspace(0, 1, max(n_solvers, 3)))

    fig, ax = plt.subplots(figsize=(max(12, n_graphs * 2), 7))

    bar_width = 0.8 / n_solvers
    x = np.arange(n_graphs)

    for i, solver in enumerate(solvers):
        solver_stats = stats[stats["solver"] == solver]
        times = []
        errors = []
        for graph in graphs:
            row = solver_stats[solver_stats["graph"] == graph]
            if not row.empty:
                times.append(row["mean_time"].values[0])
                errors.append(row["std_time"].values[0])
            else:
                times.append(0)
                errors.append(0)

        offset = (i - n_solvers / 2 + 0.5) * bar_width
        bars = ax.bar(
            x + offset, times, bar_width,
            yerr=errors, capsize=3,
            label=solver, color=colors[i], edgecolor="gray", linewidth=0.5,
        )

        # Подписи значений на барах
        for bar, t in zip(bars, times):
            if t > 0:
                ax.text(
                    bar.get_x() + bar.get_width() / 2, bar.get_height(),
                    f"{t:.2f}",
                    ha="center", va="bottom", fontsize=8,
                )

    ax.set_xlabel("Граф")
    ax.set_ylabel("Время (сек)")
    ax.set_title("Сравнение времени работы решателей")
    ax.set_xticks(x)
    ax.set_xticklabels(graphs, rotation=45, ha="right")
    ax.legend(title="Решатель")
    ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    path = os.path.join(output_dir, "time_comparison.png")
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"  → {path}")


def plot_speedup(stats: pd.DataFrame, output_dir: str, baseline: str = None):
    """Барчарт: ускорение относительно базового решателя."""
    solvers = stats["solver"].unique()

    if baseline is None:
        # Берём первый решатель как базовый
        baseline = solvers[0]

    if baseline not in solvers:
        print(f"Базовый решатель '{baseline}' не найден в данных, пропускаю speedup")
        return

    graphs = sorted(stats["graph"].unique())
    baseline_stats = stats[stats["solver"] == baseline].set_index("graph")

    other_solvers = [s for s in solvers if s != baseline]
    if not other_solvers:
        print("Только один решатель, speedup не построить")
        return

    n_others = len(other_solvers)
    colors = plt.cm.Set2(np.linspace(0, 1, max(n_others, 3)))

    fig, ax = plt.subplots(figsize=(max(12, len(graphs) * 2), 7))

    bar_width = 0.8 / n_others
    x = np.arange(len(graphs))

    for i, solver in enumerate(other_solvers):
        solver_stats = stats[stats["solver"] == solver].set_index("graph")
        speedups = []
        for graph in graphs:
            if graph in baseline_stats.index and graph in solver_stats.index:
                base_t = baseline_stats.loc[graph, "mean_time"]
                other_t = solver_stats.loc[graph, "mean_time"]
                if other_t > 0:
                    speedups.append(base_t / other_t)
                else:
                    speedups.append(0)
            else:
                speedups.append(0)

        offset = (i - n_others / 2 + 0.5) * bar_width
        bars = ax.bar(
            x + offset, speedups, bar_width,
            label=f"{solver} vs {baseline}",
            color=colors[i], edgecolor="gray", linewidth=0.5,
        )

        for bar, s in zip(bars, speedups):
            if s > 0:
                ax.text(
                    bar.get_x() + bar.get_width() / 2, bar.get_height(),
                    f"{s:.1f}×",
                    ha="center", va="bottom", fontsize=9,
                )

    ax.axhline(y=1, color="red", linestyle="--", alpha=0.5, label=f"baseline ({baseline})")
    ax.set_xlabel("Граф")
    ax.set_ylabel(f"Ускорение (baseline / solver)")
    ax.set_title(f"Ускорение относительно {baseline}")
    ax.set_xticks(x)
    ax.set_xticklabels(graphs, rotation=45, ha="right")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    path = os.path.join(output_dir, "speedup.png")
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"  → {path}")


def print_summary_table(stats: pd.DataFrame):
    """Печатает сводную таблицу в консоль."""
    pivot = stats.pivot_table(
        index="graph", columns="solver", values="mean_time", aggfunc="first"
    )
    print("\n" + "=" * 60)
    print(" Среднее время (сек)")
    print("=" * 60)
    print(pivot.to_string(float_format="{:.3f}".format))
    print()


def main():
    parser = argparse.ArgumentParser(description="Графики сравнения решателей CFPQ")
    parser.add_argument("csv", help="Путь к results.csv")
    parser.add_argument("--output", "-o", default="plots", help="Директория для графиков")
    parser.add_argument("--baseline", "-b", default=None,
                        help="Имя базового решателя для speedup (по умолчанию — первый)")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    print("Загрузка данных...")
    df = load_data(args.csv)
    print(f"  {len(df)} успешных замеров, {df['solver'].nunique()} решателей, "
          f"{df['graph'].nunique()} графов")

    stats = compute_stats(df)
    print_summary_table(stats)

    print("Построение графиков...")
    plot_time_comparison(stats, args.output)
    plot_speedup(stats, args.output, baseline=args.baseline)

    print("\nГотово!")


if __name__ == "__main__":
    main()