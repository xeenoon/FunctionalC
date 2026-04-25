from __future__ import annotations

import argparse
from pathlib import Path

from edu_complex_data import REQUESTED_SIZES, generate_dataset


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("size", type=int, nargs="+", help="Record counts to generate")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "data",
        help="Directory for generated binary datasets",
    )
    args = parser.parse_args()

    for size in args.size:
        out_path = args.out_dir / f"edu_complex_{size}.bin"
        print(f"generating {out_path} ({size} records)", flush=True)
        generate_dataset(out_path, size)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
