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
        "axes.grid": False,
        "axes.axisbelow": True,
        "grid.color": "#b8b8b8",
        "grid.linewidth": 0.7,
        "grid.alpha": 1.0,
        "axes.edgecolor": "black",
        "axes.labelcolor": "black",
        "axes.xmargin": 0.0125,
        "xtick.labelsize": FONT_SIZES["tick"],
        "ytick.labelsize": FONT_SIZES["tick"],
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
from matplotlib.lines import Line2D

GRID_MAJOR_COLOR = "#a8a8a8"
GRID_MINOR_COLOR = "#dcdcdc"
SEPARATOR_COLOR = "#4a90c8"
SHADING_COLOR = "#f3cf9e"
VIOLIN_ZORDER = 2
MEDIAN_ZORDER = 4
MEAN_ZORDER = 5
ARROW_COLOR = "#c0392b"

SPEED_SEPARATOR_LABELS = ("FSST + dp-encode", "FSST12 + dp-encode", "FSST12")
DP_ENCODE_SEPARATORS = ("FSST + dp-encode", "FSST12 + dp-encode")


def configure_grid(ax):
    ax.minorticks_on()
    ax.tick_params(axis="x", which="minor", bottom=False, top=False)
    ax.grid(
        axis="y",
        which="major",
        color=GRID_MAJOR_COLOR,
        linewidth=0.7,
        alpha=1.0,
        zorder=0,
    )
    ax.grid(
        axis="y",
        which="minor",
        color=GRID_MINOR_COLOR,
        linewidth=0.5,
        alpha=1.0,
        zorder=0,
    )
    ax.set_axisbelow(True)


def push_violins_below_grid(ax):
    for collection in ax.collections:
        collection.set_zorder(VIOLIN_ZORDER)


def should_show_marker_legend():
    if metric == "improvement12":
        return False
    if metric.startswith("decompression"):
        return False
    if metric.startswith("table_construction"):
        return False
    return True


def add_marker_legend(ax):
    handles = [
        Line2D([0], [0], color="black", linewidth=1.4, label="Median"),
        Line2D(
            [0], [0],
            marker="o",
            color="#e74c3c",
            markeredgecolor="black",
            linestyle="None",
            markersize=7,
            label="Mean",
        ),
    ]
    ax.legend(handles=handles, loc="best", frameon=False, fontsize=FONT_SIZES["legend"])


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


def separator_labels_for_metric():
    if metric.endswith("_block_compressors"):
        return ()
    if metric.startswith("compression") or metric.startswith("decompression") or metric.startswith("table_construction"):
        return SPEED_SEPARATOR_LABELS
    return DP_ENCODE_SEPARATORS


def apply_configuration_axis(ax, config_order):
    x_positions = np.arange(len(config_order))
    ax.set_xticks(x_positions)
    ax.set_xticklabels([display_label(label) for label in config_order], rotation=0, ha="center")

    for separator_label in separator_labels_for_metric():
        if separator_label in config_order:
            ax.axvline(
                x=config_order.index(separator_label) - 0.5,
                color=SEPARATOR_COLOR,
                linewidth=1.2,
                alpha=0.95,
                zorder=1,
            )


def maybe_show():
    if matplotlib.get_backend().lower() != "pgf":
        plt.show()


def center_xlabel_to_tight_bbox(fig, ax):
    for _ in range(2):
        fig.canvas.draw()
        renderer = fig.canvas.get_renderer()
        tb = fig.get_tightbbox(renderer)
        fig_w_in = fig.get_size_inches()[0]
        target_fig_x = (tb.x0 + tb.x1) / 2.0 / fig_w_in
        ax_pos = ax.get_position()
        target_ax_x = (target_fig_x - ax_pos.x0) / ax_pos.width
        ax.xaxis.label.set_x(target_ax_x)


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


def add_dp_encoding_annotations(ax, df, value_column, config_order):
    sep_idx = None
    for label in DP_ENCODE_SEPARATORS:
        if label in config_order:
            sep_idx = config_order.index(label)
            break
    if sep_idx is None:
        return

    left = -0.5
    right = len(config_order) - 0.5
    sep_x = sep_idx - 0.5

    ax.axvspan(sep_x, right, color=SHADING_COLOR, alpha=0.22, zorder=0)

    trans = ax.get_xaxis_transform()
    left_center = (left + sep_x) / 2
    right_center = (sep_x + right) / 2
    ax.text(
        left_center, 1.02, "w/o text DP-encoding",
        transform=trans, ha="center", va="bottom",
        fontsize=FONT_SIZES["legend"], clip_on=False,
    )
    ax.text(
        right_center, 1.02, "w/ text DP-encoding",
        transform=trans, ha="center", va="bottom",
        fontsize=FONT_SIZES["legend"], clip_on=False,
    )

    rightmost_label = config_order[-1]
    rightmost_values = df.loc[df["configuration"] == rightmost_label, value_column]
    if rightmost_values.empty:
        return
    rightmost_mean = float(rightmost_values.mean())
    rightmost_x = len(config_order) - 1
    arrow_x = rightmost_x + 0.42

    ax.plot(
        [rightmost_x, arrow_x], [rightmost_mean, rightmost_mean],
        color="black", linewidth=0.7, linestyle=(0, (3, 2)),
        zorder=5,
    )
    ax.annotate(
        "",
        xy=(arrow_x, rightmost_mean),
        xytext=(arrow_x, 1.0),
        arrowprops=dict(arrowstyle="<->", color="black", lw=1.0,
                        shrinkA=1.5, shrinkB=1.5),
        zorder=6,
    )
    ax.text(
        arrow_x + 0.05,
        (1.0 + rightmost_mean) / 2,
        rf"{rightmost_mean:.2f}$\times$",
        ha="left", va="center",
        color=ARROW_COLOR,
        fontsize=FONT_SIZES["legend"] - 1,
        zorder=6,
    )

    current_xlim = ax.get_xlim()
    ax.set_xlim(current_xlim[0], max(current_xlim[1], arrow_x + 0.7))


def figure_width(config_order):
    if metric.startswith("decompression"):
        return 10.5
    width_per_config = 10.5 / 7
    return max(6.0, width_per_config * len(config_order))


def plot_cf(df, config_order):
    df["CF"] = pd.to_numeric(df["CF"], errors="coerce")
    df_cf = df[["configuration", "CF"]].copy()

    fig_height = 5.2 if not is_absolute_cf_metric() else 4.8
    fig, ax = plt.subplots(figsize=(figure_width(config_order), fig_height))
    absolute = is_absolute_cf_metric()
    if not absolute:
        ax.axhline(y=1, color="red", linestyle="--", linewidth=1, alpha=0.8, zorder=3)
    base_violinplot(ax, df_cf, "CF", config_order)
    push_violins_below_grid(ax)
    add_median_lines(ax, df_cf, "CF", config_order)
    add_mean_markers(ax, df_cf, "CF", config_order, 0.0, "top")
    if absolute:
        ax.set_yscale("log")
        ax.set_ylim(bottom=1.0)
        ax.set_ylabel("Compression factor")
    else:
        ax.set_ylim(bottom=0.5)
        ax.set_ylabel(r"Compression factor improvement [$\times$]")
    ax.set_xlabel("Configuration", labelpad=14)
    apply_configuration_axis(ax, config_order)
    configure_grid(ax)
    if not absolute:
        add_dp_encoding_annotations(ax, df_cf, "CF", config_order)
    if should_show_marker_legend():
        add_marker_legend(ax)
    output_path = "./plots/" + metric + ".pdf"
    fig.tight_layout(pad=0.6)
    center_xlabel_to_tight_bbox(fig, ax)
    fig.savefig(output_path, format="pdf", bbox_inches="tight", dpi=300)
    maybe_show()


def hide_zero_gridline(ax):
    for line in ax.get_ygridlines():
        ydata = line.get_ydata()
        if len(ydata) and abs(float(ydata[0])) < 1e-12:
            line.set_visible(False)


def plot_speed(df, config_order):
    df["Time"] = pd.to_numeric(df["Time"], errors="coerce")
    is_decompression = metric.startswith("decompression")
    if is_decompression:
        df["Time"] = df["Time"] / 1000.0
    df_time = df[["configuration", "Time"]].copy()

    fig, ax = plt.subplots(figsize=(figure_width(config_order), 4.8))
    base_violinplot(ax, df_time, "Time", config_order)
    push_violins_below_grid(ax)
    add_median_lines(ax, df_time, "Time", config_order)
    add_mean_markers(ax, df_time, "Time", config_order, 0.0, "bottom")
    ax.set_xlabel("Configuration", labelpad=14)
    if metric.startswith("table_construction"):
        ax.set_ylabel("Table construction speed [MB/s]")
    elif is_decompression:
        ax.set_ylabel("Decompression speed [GB/s]")
    else:
        ax.set_ylabel("Compression speed [MB/s]")

    log_scale = metric == "compression_speed_block_compressors"
    if log_scale:
        ax.set_yscale("log")
    else:
        ax.set_ylim(bottom=0)

    apply_configuration_axis(ax, config_order)
    configure_grid(ax)
    if not log_scale:
        fig.canvas.draw()
        hide_zero_gridline(ax)
    if should_show_marker_legend():
        add_marker_legend(ax)
    output_path = "./plots/" + metric + ".pdf"
    fig.tight_layout(pad=0.6)
    center_xlabel_to_tight_bbox(fig, ax)
    fig.savefig(output_path, format="pdf", bbox_inches="tight", dpi=300)
    maybe_show()


def compute_category_groups(config_order):
    groups = []
    current_cat = None
    start_idx = None
    for i, cfg in enumerate(config_order):
        if cfg in ("FSST", "OptFSST"):
            cat = "FSST"
        elif cfg in ("FSST12", "OptFSST12"):
            cat = "FSST12"
        else:
            cat = "Block-based"
        if cat != current_cat:
            if current_cat is not None:
                groups.append((current_cat, start_idx, i - 1))
            current_cat = cat
            start_idx = i
    if current_cat is not None:
        groups.append((current_cat, start_idx, len(config_order) - 1))
    return groups


def plot_summary_table(df, config_order, value_column):
    stats = df.groupby("configuration")[value_column].agg(["min", "mean", "median", "max"]).reindex(config_order)
    columns = ["min", "mean", "median", "max"]
    is_cf = value_column == "CF"

    def fmt(v):
        if is_cf:
            return rf"{v:.2f}$\times$"
        return f"{v:.2f}"

    groups = compute_category_groups(config_order)

    cat_w = 0.55
    label_w = 1.45
    data_w = 1.0

    col_xs = [0.0, cat_w, cat_w + label_w]
    for _ in range(len(columns)):
        col_xs.append(col_xs[-1] + data_w)
    total_width = col_xs[-1]

    header_h = 0.75
    row_h = 0.75
    total_height = header_h + row_h * len(config_order)

    fig_height = 0.42 * (len(config_order) + 1) + 0.4
    fig_width = total_width * 1.0
    fig, ax = plt.subplots(figsize=(fig_width, fig_height))
    ax.set_xlim(0, total_width)
    ax.set_ylim(0, total_height)
    ax.invert_yaxis()
    ax.axis("off")

    for j, col_name in enumerate(columns):
        x_center = col_xs[2 + j] + data_w / 2
        ax.text(x_center, header_h / 2, col_name, ha="center", va="center",
                fontsize=FONT_SIZES["base"])

    for i, label in enumerate(config_order):
        y = header_h + i * row_h + row_h / 2
        ax.text(col_xs[1] + label_w / 2, y, label, ha="center", va="center",
                fontsize=FONT_SIZES["base"])

    for i, cfg in enumerate(config_order):
        y = header_h + i * row_h + row_h / 2
        for j, col in enumerate(columns):
            x_center = col_xs[2 + j] + data_w / 2
            ax.text(x_center, y, fmt(stats.loc[cfg, col]), ha="center", va="center",
                    fontsize=FONT_SIZES["base"])

    for cat_name, start, end in groups:
        y_top = header_h + start * row_h
        y_bot = header_h + (end + 1) * row_h
        y_center = (y_top + y_bot) / 2
        x_center = col_xs[0] + cat_w / 2
        ax.text(x_center, y_center, cat_name, ha="center", va="center",
                rotation=90, fontsize=FONT_SIZES["base"])

    line_color = "black"
    thin_w = 0.6
    thick_w = 1.6

    ax.plot([col_xs[0], total_width], [0, 0], color=line_color, linewidth=thick_w)
    ax.plot([col_xs[0], total_width], [total_height, total_height], color=line_color, linewidth=thick_w)
    ax.plot([col_xs[0], total_width], [header_h, header_h], color=line_color, linewidth=thin_w)

    group_boundary_rows = {start for _, start, _ in groups[1:]}
    for i in range(1, len(config_order)):
        if i in group_boundary_rows:
            continue
        y = header_h + i * row_h
        ax.plot([col_xs[1], total_width], [y, y], color=line_color,
                linewidth=thin_w * 0.55, alpha=0.5)

    for _, start, _ in groups[1:]:
        y = header_h + start * row_h
        ax.plot([col_xs[0], total_width], [y, y], color=line_color, linewidth=thick_w)

    ax.plot([col_xs[1], col_xs[1]], [0, total_height], color=line_color, linewidth=thin_w)

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
