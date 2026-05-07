import sys

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

metric = ""


def add_mean_markers(ax, df, value_column, config_order, text_offset_x, text_va):
    means = df.groupby("configuration")[value_column].mean().reindex(config_order).values
    x_pos = np.arange(len(config_order))
    ax.scatter(x_pos, means, marker="o", s=20, c="red", edgecolors="black", zorder=5)

    for i, mean_val in enumerate(means):
        if np.isnan(mean_val):
            continue
        ax.text(
            i + text_offset_x,
            mean_val,
            f"{mean_val:.2f}",
            ha="left",
            va=text_va,
            fontsize=8,
            color="red",
        )


def base_boxplot(ax, df, value_column, config_order):
    sns.boxplot(
        data=df,
        x="configuration",
        y=value_column,
        order=config_order,
        ax=ax,
        showfliers=True,
        fill=False,
        flierprops=dict(
            marker=".",
            markersize=3,
            markerfacecolor="black",
            markeredgecolor="black",
        ),
    )


def plot_cf(df, config_order):
    df["CF"] = pd.to_numeric(df["CF"], errors="coerce")
    df_cf = df[["configuration", "CF"]].copy()

    plt.figure()
    ax = plt.gca()
    ax.axhline(y=1, color="red", linestyle="--", linewidth=1, alpha=0.8)
    base_boxplot(ax, df_cf, "CF", config_order)
    add_mean_markers(ax, df_cf, "CF", config_order, 0.05, "top")
    ax.set_ylim(bottom=0.5)
    ax.set_xlabel("Configuration")
    ax.set_ylabel(r"Compression factor improvement [$\times$]")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig("./plots/" + metric + ".png", dpi=300)


def plot_speed(df, config_order):
    df["Time"] = pd.to_numeric(df["Time"], errors="coerce")
    df_time = df[["configuration", "Time"]].copy()

    plt.figure()
    ax = plt.gca()
    base_boxplot(ax, df_time, "Time", config_order)
    add_mean_markers(ax, df_time, "Time", config_order, 0.08, "bottom")
    ax.set_xlabel("Configuration")
    if metric.startswith("compression"):
        ax.set_ylabel("Compression speed [MB/s]")
    else:
        ax.set_ylabel("Decompression speed [MB/s]")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig("./plots/" + metric + ".png", dpi=300)


def main(csv_path: str):
    df = pd.read_csv(csv_path)
    config_order = list(dict.fromkeys(df["configuration"].astype(str).tolist()))

    if "CF" in df.columns:
        plot_cf(df, config_order)
    elif "Time" in df.columns:
        plot_speed(df, config_order)
    else:
        raise ValueError("CSV must contain either a CF or Time column")


if __name__ == "__main__":
    metric = sys.argv[1]
    main("./csv/" + metric + ".csv")
