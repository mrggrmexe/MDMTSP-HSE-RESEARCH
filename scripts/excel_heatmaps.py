#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path

import pandas as pd
from openpyxl import load_workbook
from openpyxl.formatting.rule import ColorScaleRule
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.utils import get_column_letter


HEADER_FILL = PatternFill(fill_type="solid", fgColor="1F4E78")
HEADER_FONT = Font(color="FFFFFF", bold=True)
THIN_BORDER = Border(
    left=Side(style="thin", color="D9D9D9"),
    right=Side(style="thin", color="D9D9D9"),
    top=Side(style="thin", color="D9D9D9"),
    bottom=Side(style="thin", color="D9D9D9"),
)


def pick_first_existing_column(df: pd.DataFrame, candidates: list[str]) -> str | None:
    for name in candidates:
        if name in df.columns:
            return name
    return None


def as_numeric(series: pd.Series) -> pd.Series:
    return pd.to_numeric(series, errors="coerce")


def remove_sheet_if_exists(wb, title: str) -> None:
    if title in wb.sheetnames:
        ws = wb[title]
        wb.remove(ws)


def write_matrix_sheet(
    wb,
    title: str,
    matrix: pd.DataFrame,
    legend_text: str,
    number_format: str,
) -> None:
    remove_sheet_if_exists(wb, title)
    ws = wb.create_sheet(title)

    ws["A1"] = legend_text
    ws["A1"].font = Font(bold=True)

    ws.cell(row=2, column=1, value="algorithm_id")
    for j, col_name in enumerate(matrix.columns, start=2):
        ws.cell(row=2, column=j, value=str(col_name))

    for i, (row_name, row_values) in enumerate(matrix.iterrows(), start=3):
        ws.cell(row=i, column=1, value=str(row_name))
        for j, value in enumerate(row_values.tolist(), start=2):
            if pd.isna(value):
                ws.cell(row=i, column=j, value=None)
            else:
                ws.cell(row=i, column=j, value=float(value))

    max_row = ws.max_row
    max_col = ws.max_column

    for cell in ws[2]:
        cell.fill = HEADER_FILL
        cell.font = HEADER_FONT
        cell.alignment = Alignment(horizontal="center", vertical="center", textRotation=90)
        cell.border = THIN_BORDER

    for row_idx in range(3, max_row + 1):
        c = ws.cell(row=row_idx, column=1)
        c.font = Font(bold=True)
        c.alignment = Alignment(horizontal="left", vertical="center")
        c.border = THIN_BORDER

    for row in ws.iter_rows(min_row=3, max_row=max_row, min_col=2, max_col=max_col):
        for cell in row:
            cell.number_format = number_format
            cell.alignment = Alignment(horizontal="center", vertical="center")
            cell.border = THIN_BORDER

    ws.freeze_panes = "B3"
    ws.auto_filter.ref = f"A2:{get_column_letter(max_col)}{max_row}"

    ws.column_dimensions["A"].width = 28
    for col_idx in range(2, max_col + 1):
        ws.column_dimensions[get_column_letter(col_idx)].width = 5.5
    ws.row_dimensions[2].height = 140

    if max_row >= 3 and max_col >= 2:
        data_range = f"B3:{get_column_letter(max_col)}{max_row}"
        ws.conditional_formatting.add(
            data_range,
            ColorScaleRule(
                start_type="percentile",
                start_value=10,
                start_color="63BE7B",
                mid_type="percentile",
                mid_value=50,
                mid_color="FFEB84",
                end_type="percentile",
                end_value=90,
                end_color="F8696B",
            ),
        )


def build_gap_matrix(df: pd.DataFrame) -> pd.DataFrame:
    algo_col = pick_first_existing_column(df, ["algorithm_id", "algorithm"])
    instance_col = pick_first_existing_column(df, ["instance_name", "instance"])
    value_col = pick_first_existing_column(
        df,
        [
            "median_gap_to_best_observed",
            "mean_gap_to_best_observed",
            "min_gap_to_best_observed",
        ],
    )
    if algo_col is None or instance_col is None or value_col is None:
        raise ValueError("algorithm_instance_summary.csv does not contain required gap heatmap columns")

    work = df[[algo_col, instance_col, value_col]].copy()
    work[value_col] = as_numeric(work[value_col])
    work = work.dropna(subset=[value_col])

    return (
        work.pivot_table(
            index=algo_col,
            columns=instance_col,
            values=value_col,
            aggfunc="median",
        )
        .sort_index()
        .sort_index(axis=1)
    )


def build_time_matrix(df: pd.DataFrame) -> tuple[pd.DataFrame, str]:
    algo_col = pick_first_existing_column(df, ["algorithm_id", "algorithm"])
    instance_col = pick_first_existing_column(df, ["instance_name", "instance"])
    value_col = pick_first_existing_column(
        df,
        [
            "median_time_ratio_to_fastest",
            "mean_time_ratio_to_fastest",
            "median_wall_time_ms",
            "mean_wall_time_ms",
        ],
    )
    if algo_col is None or instance_col is None or value_col is None:
        raise ValueError("algorithm_instance_summary.csv does not contain required time heatmap columns")

    work = df[[algo_col, instance_col, value_col]].copy()
    work[value_col] = as_numeric(work[value_col])
    work = work.dropna(subset=[value_col])

    matrix = (
        work.pivot_table(
            index=algo_col,
            columns=instance_col,
            values=value_col,
            aggfunc="median",
        )
        .sort_index()
        .sort_index(axis=1)
    )
    return matrix, value_col


def add_heatmap_sheets_to_workbook(
    workbook_path: Path,
    algorithm_instance_summary_csv: Path,
) -> None:
    if not workbook_path.exists():
        raise FileNotFoundError(f"workbook not found: {workbook_path}")
    if not algorithm_instance_summary_csv.exists():
        raise FileNotFoundError(f"CSV not found: {algorithm_instance_summary_csv}")

    df = pd.read_csv(algorithm_instance_summary_csv)
    if df.empty:
        raise ValueError(f"CSV is empty: {algorithm_instance_summary_csv}")

    gap_matrix = build_gap_matrix(df)
    time_matrix, time_metric_name = build_time_matrix(df)

    wb = load_workbook(workbook_path)

    write_matrix_sheet(
        wb=wb,
        title="Gap_Heatmap",
        matrix=gap_matrix,
        legend_text="Median gap to best observed, % (lower is better).",
        number_format="0.000",
    )
    write_matrix_sheet(
        wb=wb,
        title="Time_Heatmap",
        matrix=time_matrix,
        legend_text=f"{time_metric_name} (lower is better).",
        number_format="0.000",
    )

    wb.save(workbook_path)
