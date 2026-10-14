#!/usr/bin/env python3
"""Статистика повторяющихся паттернов в трассе исполнения (LLVM-IR).

Трасса — файл, по одной строке на инструкцию-зависимость вида ``A <- B``.
Паттерном длины N считается окно из N подряд идущих строк трассы.

Для каждого N = 1..max-pattern выводится таблица самых частых паттернов.
"""

import argparse
import sys
from array import array


def encode_tokens(path):
    """Читает трассу и кодирует каждую строку целым id.

    Возвращает (ids, id_to_line), где ids — массив 'B' (id на токен),
    id_to_line — список исходных строк по их id.
    """
    line_to_id = {}
    id_to_line = []
    ids = array("B")

    with open(path, "rb") as f:
        append = ids.append
        for raw in f:
            token = raw.rstrip(b"\n")
            tid = line_to_id.get(token)
            if tid is None:
                tid = len(id_to_line)
                line_to_id[token] = tid
                id_to_line.append(token)
            append(tid)

    return ids, id_to_line


def count_windows_python(ids, n):
    """Подсчёт окон длины n на чистом Python (fallback без numpy)."""
    from collections import Counter

    counts = Counter()
    window = tuple(ids[: n - 1]) if len(ids) >= n - 1 else ()
    update = counts.update
    for i in range(len(ids) - n + 1):
        window = window[1:] + (ids[i + n - 1],)
        update((window,))
    return counts


def count_windows_numpy(ids, n, base, chunk_size=2_000_000):
    """Подсчёт окон длины n через numpy.

    Каждое окно кодируется в int64 в позиционной системе с основанием `base`.
    """
    import numpy as np

    total = len(ids) - n + 1
    if total <= 0:
        return np.empty(0, dtype=np.int64), np.empty(0, dtype=np.int64)

    # Коэффициенты позиций: старший байт окна умножается на base^(n-1).
    coeffs = np.array([base ** (n - 1 - j) for j in range(n)], dtype=np.int64)

    all_keys = np.empty(total, dtype=np.int64)
    pos = 0
    flat = np.frombuffer(ids, dtype=np.uint8)
    for start in range(0, total, chunk_size):
        stop = min(start + chunk_size, total)
        # Строки окон: (stop-start, n) через strided view.
        keys = np.zeros(stop - start, dtype=np.int64)
        for j in range(n):
            keys += flat[start + j : stop + j].astype(np.int64) * coeffs[j]
        all_keys[pos : pos + (stop - start)] = keys
        pos += stop - start

    unique, counts = np.unique(all_keys, return_counts=True)
    return unique, counts


def decode_key(key, n, base, id_to_line):
    """Разворачивает закодированное окно обратно в список исходных строк."""
    parts = []
    for _ in range(n):
        key, rem = divmod(key, base)
        parts.append(id_to_line[rem])
    parts.reverse()
    return parts


def main():
    parser = argparse.ArgumentParser(
        description="Частотная статистика паттернов из 1..N подряд идущих строк трассы."
    )
    parser.add_argument("--file", default="./build/log.txt", help="путь к файлу трассы")
    parser.add_argument("--top", type=int, default=20, help="сколько паттернов показать (по умолчанию 20)")
    parser.add_argument("--max-pattern", type=int, default=5, help="максимальная длина паттерна (по умолчанию 5)")
    args = parser.parse_args()

    print(f"Читаю {args.file} ...", file=sys.stderr)
    ids, id_to_line = encode_tokens(args.file)
    total_lines = len(ids)
    base = max(len(id_to_line), 2)
    print(
        f"Строк: {total_lines:,}, уникальных типов: {len(id_to_line)}",
        file=sys.stderr,
    )

    try:
        import numpy  # noqa: F401

        use_numpy = True
    except ImportError:
        use_numpy = False
        print("numpy не найден — использую медленный fallback на чистый Python", file=sys.stderr)

    for n in range(1, args.max_pattern + 1):
        total_windows = total_lines - n + 1
        if total_windows <= 0:
            print(f"\n=== Паттерны из {n} инструкций: окон нет ===")
            continue

        if use_numpy:
            unique, counts = count_windows_numpy(ids, n, base)
            pairs = sorted(zip(counts.tolist(), unique.tolist()), reverse=True)
        else:
            pairs = count_windows_python(ids, n).most_common()

        print(f"\n=== Паттерны из {n} инструкций ===")
        print(f"Всего окон: {total_windows:,}; уникальных паттернов: {len(pairs):,}")
        print(f"{'#':>3}  {'вхождений':>12}  {'доля':>7}  паттерн")
        for rank, (count, key) in enumerate(pairs[: args.top], start=1):
            parts = decode_key(key, n, base, id_to_line) if use_numpy else [
                id_to_line[x] for x in key
            ]
            pattern = " | ".join(p.decode() for p in parts)
            share = 100.0 * count / total_windows
            print(f"{rank:>3}  {count:>12,}  {share:>6.2f}%  {pattern}")


if __name__ == "__main__":
    main()
