"""Fit raw libsurvive positions to measured Quest STAGE positions (metres).

CSV header: sx,sy,sz,tx,ty,tz. Requires numpy. Never uses SteamVR.
"""
import argparse
import csv
from pathlib import Path

import numpy as np


def fit(source, target):
    if source.shape != target.shape or source.ndim != 2 or source.shape[1] != 3 or len(source) < 3:
        raise ValueError("Supply at least three matching 3D positions")
    if not np.isfinite(source).all() or not np.isfinite(target).all():
        raise ValueError("Positions must be finite")
    s = source - source.mean(axis=0)
    t = target - target.mean(axis=0)
    if np.linalg.matrix_rank(s, tol=1e-5) < 2 or np.linalg.matrix_rank(t, tol=1e-5) < 2:
        raise ValueError("Calibration points must not be collinear")
    u, _, vt = np.linalg.svd(s.T @ t)
    correction = np.diag([1, 1, np.linalg.det(vt.T @ u.T)])
    rotation = vt.T @ correction @ u.T
    translation = target.mean(axis=0) - rotation @ source.mean(axis=0)
    errors = np.linalg.norm(source @ rotation.T + translation - target, axis=1)
    return rotation, translation, errors


def quaternion(rotation):
    # Eigenvector method handles rotations near 180 degrees without a zero divisor.
    r = rotation
    k = np.array([
        [r[0, 0]-r[1, 1]-r[2, 2], r[0, 1]+r[1, 0], r[0, 2]+r[2, 0], r[2, 1]-r[1, 2]],
        [r[0, 1]+r[1, 0], r[1, 1]-r[0, 0]-r[2, 2], r[1, 2]+r[2, 1], r[0, 2]-r[2, 0]],
        [r[0, 2]+r[2, 0], r[1, 2]+r[2, 1], r[2, 2]-r[0, 0]-r[1, 1], r[1, 0]-r[0, 1]],
        [r[2, 1]-r[1, 2], r[0, 2]-r[2, 0], r[1, 0]-r[0, 1], np.trace(r)],
    ]) / 3
    _, vectors = np.linalg.eigh(k)
    q = vectors[:, -1]
    return q if q[3] >= 0 else -q


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-error", type=float, default=0.03, help="Maximum residual in metres")
    args = parser.parse_args()
    with args.csv.open(newline="") as file:
        rows = list(csv.DictReader(file))
    source = np.array([[float(row[key]) for key in ("sx", "sy", "sz")] for row in rows])
    target = np.array([[float(row[key]) for key in ("tx", "ty", "tz")] for row in rows])
    rotation, translation, errors = fit(source, target)
    print(f"Calibration samples: {len(rows)}; fit RMS: {np.sqrt(np.mean(errors**2)):.4f} m; max: {max(errors):.4f} m")
    print("Residuals measure agreement of these samples, not independently measured tracking accuracy.")
    if not np.isfinite(args.max_error) or args.max_error <= 0 or max(errors) > args.max_error:
        raise SystemExit("Calibration rejected; check correspondence, origin, units and base-station stability")
    args.output.write_text(" ".join(f"{value:.10g}" for value in (*translation, *quaternion(rotation))) + "\n")


if __name__ == "__main__":
    main()
