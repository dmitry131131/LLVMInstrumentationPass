#!/usr/bin/env python3
"""Гистограммы распределения паттернов по результату pattern_stats.py.

Читает stat.txt (вывод pattern_stats.py) и строит 5 горизонтальных
гистограмм — по одной на каждый размер паттерна (1..5 инструкций).
Каждая гистограмма сохраняется в отдельный файл: <prefix>_1.png ... <prefix>_5.png.
"""

import argparse
import re
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SECTION_RE = re.compile(r"^=== Паттерны из (\d+) инструкций ===$")
TOTAL_RE = re.compile(r"Всего окон: ([\d,]+); уникальных паттернов: ([\d,]+)")
ROW_RE = re.compile(r"^\s*(\d+)\s+([\d,]+)\s+([\d.]+)%\s+(\S.*?)\s*$")


def parse_stat(path):
    """Возвращает {n: {'total': int, 'unique': int, 'rows': [(count, share, pattern)]}}."""
    stats = {}
    current_n = None

    with open(path, encoding="utf-8") as f:
        for line in f:
            m = SECTION_RE.match(line)
            if m:
                current_n = int(m.group(1))
                stats[current_n] = {"total": 0, "unique": 0, "rows": []}
                continue
            if current_n is None:
                continue
            m = TOTAL_RE.search(line)
            if m:
                stats[current_n]["total"] = int(m.group(1).replace(",", ""))
                stats[current_n]["unique"] = int(m.group(2).replace(",", ""))
                continue
            m = ROW_RE.match(line)
            if m:
                count = int(m.group(2).replace(",", ""))
                share = float(m.group(3))
                pattern = m.group(4)
                stats[current_n]["rows"].append((count, share, pattern))

    return stats


def plot_histogram(stats, n, output, log_scale=False):
    """Рисует гистограмму top-20 паттернов размера n и сохраняет в отдельный PNG."""
    data = stats[n]
    rows = data["rows"]
    # Сверху вниз — от самого частого паттерна.
    labels = [p for _, _, p in reversed(rows)]
    counts = [c for c, _, _ in reversed(rows)]
    shares = [s for _, s, _ in reversed(rows)]

    fig, ax = plt.subplots(figsize=(14, 0.45 * len(rows) + 2), constrained_layout=True)
    bars = ax.barh(labels, counts, color="steelblue", edgecolor="none")
    if log_scale:
        ax.set_xscale("log")

    # Подписи значений на концах баров.
    for bar, count, share in zip(bars, counts, shares):
        ax.text(
            bar.get_width(),
            bar.get_y() + bar.get_height() / 2,
            f" {count:,} ({share:.2f}%)".replace(",", " "),
            va="center",
            ha="left",
            fontsize=8,
        )

    ax.set_title(
        f"Паттерны из {n} инструкций — топ-{len(rows)} "
        f"из {data['unique']:,} уникальных "
        f"(всего окон: {data['total']:,})".replace(",", " ")
    )
    ax.set_xlabel("Число вхождений")
    ax.set_ylabel("Паттерн")
    ax.grid(axis="x", linestyle=":", alpha=0.5)
    # Место под подписи значений.
    ax.set_xlim(0, max(counts) * 1.25)

    fig.savefig(output, dpi=120)
    plt.close(fig)
    print(f"Сохранено: {output}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="Строит гистограммы (5 шт.) по результату pattern_stats.py, каждая в отдельном файле."
    )
    parser.add_argument("--file", default="stat.txt", help="файл со статистикой (по умолчанию stat.txt)")
    parser.add_argument(
        "--output-prefix",
        default="patterns_histogram",
        help="префикс имён файлов: <prefix>_1.png ... <prefix>_5.png",
    )
    parser.add_argument("--log-scale", action="store_true", help="логарифмическая шкала по X")
    args = parser.parse_args()

    stats = parse_stat(args.file)
    if not stats:
        sys.exit(f"В файле {args.file} не найдено секций с паттернами — проверьте формат.")
    for n in sorted(stats):
        print(f"Секция N={n}: строк в таблице: {len(stats[n]['rows'])}", file=sys.stderr)
        plot_histogram(stats, n, f"{args.output_prefix}_{n}.png", log_scale=args.log_scale)


if __name__ == "__main__":
    main()
