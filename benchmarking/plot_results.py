import sys

import matplotlib
import numpy as np
import pandas as pd
import seaborn as sns

metric = ""

LEGACY_IMPROVEMENT_CONFIGS = {
    "improvement": [
        "FSST + dp-train",
        "+ triples",
        "+ prune",
        "FSST + dp-encode",
        "+ dp-train",
        "+ triples",
        "+ prune = OptFSST",
    ],
    "improvement12": [
        "FSST12 + dp-train",
        "+ triples",
        "+ prune",
        "FSST12 + dp-encode",
        "+ dp-train",
        "+ triples",
        "+ prune = OptFSST12",
    ],
}

CANONICAL_IMPROVEMENT_CONFIGS = {
    "improvement": [
        "FSST + dp-train",
        "+ triples (dp-train)",
        "+ prune",
        "FSST + dp-encode",
        "+ dp-train",
        "+ triples (dp-encode)",
        "+ prune = OptFSST",
    ],
    "improvement12": [
        "FSST12 + dp-train",
        "+ triples (dp-train)",
        "+ prune",
        "FSST12 + dp-encode",
        "+ dp-train",
        "+ triples (dp-encode)",
        "+ prune = OptFSST12",
    ],
}

DISPLAY_LABELS = {
    "+ triples (dp-train)": "+ triples",
    "+ triples (dp-encode)": "+ triples",
}

PLOT_LABELS = {
    "FSST + dp-train": "FSST\n+ dp-train",
    "+ triples (dp-train)": "+ triples",
    "+ prune": "+ prune",
    "FSST + dp-encode": "FSST\n+ dp-encode",
    "+ dp-train": "+ dp-train",
    "+ triples (dp-encode)": "+ triples",
    "+ prune = OptFSST": "+ prune\n= OptFSST",
    "FSST12 + dp-train": "FSST12\n+ dp-train",
    "FSST12 + dp-encode": "FSST12\n+ dp-encode",
    "+ prune = OptFSST12": "+ prune\n= OptFSST12",
}

FONT_SIZES = {
    "title": 14,
    "axis_label": 14,
    "tick": 11,
    "legend": 11,
    "base": 12,
}

# important: set backend before importing pyplot
matplotlib.use("pgf")

matplotlib.rcParams.update(
    {
        "pgf.texsystem": "pdflatex",
        "pgf.rcfonts": False,
        "font.family": "serif",
        "pgf.preamble": "\n".join(
            [
                r"\usepackage{amsmath}",
                r"\usepackage{bm}",
                r"\usepackage{graphicx}",
                r"\usepackage{mathpazo}",
            ]
        ),
        "axes.labelsize": FONT_SIZES["axis_label"],
        "axes.titlesize": FONT_SIZES["title"],
        "axes.titlepad": 6.0,
        "axes.labelpad": 4.0,
        "axes.linewidth": 0.75,
        "axes.grid": True,
        "axes.grid.axis": "y",
        "axes.grid.which": "both",
        "grid.color": "#b8b8b8",
        "grid.linewidth": 0.7,
        "grid.alpha": 1.0,
        "axes.edgecolor": "black",
        "axes.labelcolor": "black",
        "axes.xmargin": 0.0125,
        "xtick.labelsize": FONT_SIZES["tick"],
        "ytick.labelsize": FONT_SIZES["tick"] - 3,
        "xtick.direction": "in",
        "ytick.direction": "in",
        "xtick.major.size": 0,
        "ytick.major.size": 2.5,
        "xtick.major.width": 1.0,
        "ytick.major.width": 1.0,
        "ytick.minor.visible": True,
        "ytick.minor.size": 1.5,
        "ytick.minor.width": 0.75,
        "lines.linewidth": 3.0,
        "lines.markersize": 7.5,
        "lines.markeredgewidth": 0.8,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.spines.left": True,
        "axes.spines.bottom": False,
        "legend.fontsize": FONT_SIZES["legend"],
        "legend.frameon": False,
        "legend.handlelength": 2.0,
        "legend.handletextpad": 0.5,
        "font.size": FONT_SIZES["base"],
        "text.color": "black",
        "axes.titleweight": "normal",
        "axes.labelweight": "normal",
        "mathtext.fontset": "cm",
    }
)

import matplotlib.pyplot as plt


def metric_title(metric_name):
    if metric_name.startswith("table_construction"):
        return "Table construction speed"
    if metric_name.startswith("compression"):
        return "Compression speed"
    if metric_name.startswith("decompression"):
        return "Decompression speed"
    if metric_name.startswith("cf_block"):
        return "Compression factor"
    return metric_name.replace("_", " ").title()


def is_absolute_cf_metric():
    return metric.startswith("cf_block")


def normalize_improvement_configurations(df):
    if metric not in CANONICAL_IMPROVEMENT_CONFIGS or "file" not in df.columns:
        return df

    df = df.copy()
    canonical_sequence = CANONICAL_IMPROVEMENT_CONFIGS[metric]
    legacy_sequence = LEGACY_IMPROVEMENT_CONFIGS[metric]
    first_file = df["file"].iloc[0]
    first_sequence = df.loc[df["file"] == first_file, "configuration"].astype(str).tolist()

    if first_sequence == canonical_sequence:
        return df

    if first_sequence != legacy_sequence:
        return df

    group_sizes = df.groupby("file").size()
    if not (group_sizes == len(canonical_sequence)).all():
        return df

    sequence_map = dict(enumerate(canonical_sequence))
    df["configuration"] = df.groupby("file").cumcount().map(sequence_map)
    return df


def display_label(configuration):
    return PLOT_LABELS.get(DISPLAY_LABELS.get(configuration, configuration), PLOT_LABELS.get(configuration, configuration))


def apply_configuration_axis(ax, config_order):
    x_positions = np.arange(len(config_order))
    ax.set_xticks(x_positions)
    ax.set_xticklabels([display_label(label) for label in config_order], rotation=0, ha="center")

    for separator_label in ("FSST + dp-encode", "FSST12 + dp-encode"):
        if separator_label in config_order:
            ax.axvline(
                x=config_order.index(separator_label) - 0.5,
                color="black",
                linewidth=1.0,
                alpha=0.8,
                zorder=1,
            )


def maybe_show():
    if matplotlib.get_backend().lower() != "pgf":
        plt.show()


MEDIAN_LINE_HALF_WIDTH = 0.18


def add_mean_markers(ax, df, value_column, config_order, text_offset_x, text_va):
    means = df.groupby("configuration")[value_column].mean().reindex(config_order).values
    x_pos = np.arange(len(config_order))
    ax.scatter(x_pos, means, marker="o", s=55, c="#e74c3c", edgecolors="black", zorder=5)


def add_median_lines(ax, df, value_column, config_order):
    medians = df.groupby("configuration")[value_column].median().reindex(config_order).values
    x_pos = np.arange(len(config_order))
    for x, y in zip(x_pos, medians):
        ax.hlines(
            y,
            x - MEDIAN_LINE_HALF_WIDTH,
            x + MEDIAN_LINE_HALF_WIDTH,
            color="black",
            linewidth=1.4,
            zorder=4,
        )


def base_violinplot(ax, df, value_column, config_order):
    sns.violinplot(
        data=df,
        x="configuration",
        y=value_column,
        order=config_order,
        ax=ax,
        inner=None,
        cut=0,
        linewidth=1.2,
        color="#9ec1e6",
    )


def figure_width(config_order):
    if metric.startswith("decompression"):
        return 10.5
    width_per_config = 10.5 / 7
    return max(6.0, width_per_config * len(config_order))


def plot_cf(df, config_order):
    df["CF"] = pd.to_numeric(df["CF"], errors="coerce")
    df_cf = df[["configuration", "CF"]].copy()

    fig, ax = plt.subplots(figsize=(figure_width(config_order), 4.8))
    absolute = is_absolute_cf_metric()
    if not absolute:
        ax.axhline(y=1, color="red", linestyle="--", linewidth=1, alpha=0.8)
    base_violinplot(ax, df_cf, "CF", config_order)
    add_median_lines(ax, df_cf, "CF", config_order)
    add_mean_markers(ax, df_cf, "CF", config_order, 0.0, "top")
    if absolute:
        ax.set_yscale("log")
        ax.set_ylim(bottom=1.0)
        ax.set_ylabel("Compression factor")
        ax.set_title(metric_title(metric))
    else:
        ax.set_ylim(bottom=0.5)
        ax.set_ylabel(r"Compression factor improvement [$\times$]")
    ax.set_xlabel("Configuration")
    apply_configuration_axis(ax, config_order)
    output_path = "./plots/" + metric + ".pdf"
    fig.tight_layout(pad=0.6)
    fig.savefig(output_path, format="pdf", bbox_inches="tight", dpi=300)
    maybe_show()


def plot_speed(df, config_order):
    df["Time"] = pd.to_numeric(df["Time"], errors="coerce")
    df_time = df[["configuration", "Time"]].copy()

    fig, ax = plt.subplots(figsize=(figure_width(config_order), 4.8))
    base_violinplot(ax, df_time, "Time", config_order)
    add_median_lines(ax, df_time, "Time", config_order)
    add_mean_markers(ax, df_time, "Time", config_order, 0.0, "bottom")
    ax.set_xlabel("Configuration")
    ax.set_title(metric_title(metric))
    if metric.startswith("table_construction"):
        ax.set_ylabel("Table construction speed [MB/s]")
    elif metric.startswith("compression"):
        ax.set_ylabel("Compression speed [MB/s]")
    else:
        ax.set_ylabel("Decompression speed [MB/s]")
    apply_configuration_axis(ax, config_order)
    output_path = "./plots/" + metric + ".pdf"
    fig.tight_layout(pad=0.6)
    fig.savefig(output_path, format="pdf", bbox_inches="tight", dpi=300)
    maybe_show()


def plot_summary_table(df, config_order, value_column):
    stats = df.groupby("configuration")[value_column].agg(["min", "mean", "median", "max"]).reindex(config_order)
    columns = ["min", "mean", "median", "max"]
    cell_text = [[f"{stats.loc[cfg, col]:.2f}" for col in columns] for cfg in config_order]

    fig_height = 0.45 * (len(config_order) + 1) + 0.4
    fig, ax = plt.subplots(figsize=(6.0, fig_height))
    ax.axis("off")

    table = ax.table(
        cellText=cell_text,
        rowLabels=config_order,
        colLabels=columns,
        cellLoc="center",
        rowLoc="center",
        loc="center",
        edges="horizontal",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(FONT_SIZES["base"])
    table.scale(1.0, 1.4)

    output_path = "./plots/" + metric + ".pdf"
    fig.tight_layout(pad=0.3)
    fig.savefig(output_path, format="pdf", bbox_inches="tight", dpi=300)
    maybe_show()


def main(csv_path: str):
    df = normalize_improvement_configurations(pd.read_csv(csv_path))
    config_order = list(dict.fromkeys(df["configuration"].astype(str).tolist()))

    if metric.endswith("_table"):
        value_column = "CF" if "CF" in df.columns else "Time"
        plot_summary_table(df, config_order, value_column)
        return

    if "CF" in df.columns:
        plot_cf(df, config_order)
    elif "Time" in df.columns:
        plot_speed(df, config_order)
    else:
        raise ValueError("CSV must contain either a CF or Time column")


if __name__ == "__main__":
    metric = sys.argv[1]
    csv_metric = metric[: -len("_table")] if metric.endswith("_table") else metric
    main("./csv/" + csv_metric + ".csv")
