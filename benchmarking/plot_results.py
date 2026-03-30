import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns

def main(csv_path: str):
    df = pd.read_csv(csv_path)

    # Coerce numeric where possible (non-numeric CF rows become NaN)
    df["Time"] = pd.to_numeric(df["Time"], errors="coerce")
    df_time = df[["configuration", "Time"]].copy()
    # Keep configuration order as it appears in the CSV
    config_order = list(dict.fromkeys(df["configuration"].astype(str).tolist()))

    
    
    # ---- Boxplot: Time ----
    plt.figure()
    ax = plt.gca()
    #ax.set_yscale("log")
   
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
    #print( (means_time - 1) * 100)
    for i, mean_val in enumerate(means_time):
        if np.isnan(mean_val):
            continue
        ax.text(
            i + 0.08,
            mean_val,
            f"{mean_val:.2f}",
            ha="left",
            va="bottom",
            fontsize=8,
            color="red"
        )

    ax.set_xlabel("Configuration")
    ax.set_ylabel("Deompression speed [MB/s]")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    out_png2 = "./plots/decompression_speed.png"
    plt.savefig(out_png2, dpi=300)

if __name__ == "__main__":
    main("./csv/decompression_speed.csv")
