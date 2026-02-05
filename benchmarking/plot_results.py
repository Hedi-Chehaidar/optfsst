import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns

def main(csv_path: str):
    df = pd.read_csv(csv_path)

    # Ensure expected columns exist
    #required = {"configuration", "Time", "CF"}
    required = {"configuration", "Time"}
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"CSV missing columns: {missing}")

    # Coerce numeric where possible (non-numeric CF rows become NaN)
    df["Time"] = pd.to_numeric(df["Time"], errors="coerce")
    #df["CF"] = pd.to_numeric(df["CF"], errors="coerce")
    #df_cf   = df[["configuration", "CF"]].copy()
    df_time = df[["configuration", "Time"]].copy()
    # Keep configuration order as it appears in the CSV
    config_order = list(dict.fromkeys(df["configuration"].astype(str).tolist()))

    '''# ---- Boxplot: CF ----
    plt.figure()
    ax = plt.gca()
    ax.axvline(
        x=3.5,
        color="black",
        linestyle="--",
        linewidth=1,
        alpha=0.8
    )
    sns.boxplot(
        data=df_cf,
        x="configuration",
        y="CF",
        order=config_order,
        ax=ax,
        showfliers=True,
        fill=False,
        flierprops=dict(
            marker=".",
            markersize=3,
            markerfacecolor="black",
            markeredgecolor="black"
        )

    )

    # Add mean markers + numeric labels
    means_cf = df_cf.groupby("configuration")["CF"].mean().reindex(config_order).values
    x_pos = np.arange(len(config_order))
    ax.scatter(x_pos, means_cf, marker="o", s=20, c="red", edgecolors="black", zorder=5)

    for i, mean_val in enumerate(means_cf):
        if np.isnan(mean_val):
            continue
        ax.text(
            i + 0.05,
            mean_val,
            f"{mean_val:.2f}",
            ha="left",
            va="top",
            fontsize=8,
            color="red"
        )
    ax.set_ylim(bottom=1)
    ax.set_xlabel("Configuration")
    ax.set_ylabel("Compression factor")
    #ax.set_yticks(list(range(1,14,2)))

    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    out_png1 = "./plots/best_config2.png"
    plt.savefig(out_png1, dpi=300)'''
    
    # ---- Boxplot: Time ----
    plt.figure()
    ax = plt.gca()
    ax.axvline(
        x=3.5,
        color="black",
        linestyle="--",
        linewidth=1,
        alpha=0.8
    )

    sns.boxplot(
        data=df_time,
        x="configuration",
        y="Time",
        order=config_order,
        ax=ax,
        showfliers=True,
        fill=False,
        flierprops=dict(
            marker=".",
            markersize=3,
            markerfacecolor="black",
            markeredgecolor="black"
        )
    )

    means_time = df_time.groupby("configuration")["Time"].mean().reindex(config_order).values
    x_pos = np.arange(len(config_order))
    ax.scatter(x_pos, means_time, marker="o", s=20, c="red", edgecolors="black", zorder=5)

    for i, mean_val in enumerate(means_time):
        if np.isnan(mean_val):
            continue
        ax.text(
            i + 0.2,
            mean_val,
            f"{mean_val:.1f}",
            ha="left",
            va="bottom",
            fontsize=8,
            color="red"
        )

    ax.set_xlabel("Configuration")
    ax.set_ylabel("Compression time [ms]")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    out_png2 = "./plots/decomp_time.png"
    plt.savefig(out_png2, dpi=300)

if __name__ == "__main__":
    main("./csv/decomp.csv")
