"""Estimate room alignment and an unknown rigid tracker/controller mounting offset.

Consumes quest_probe CSV. Requires numpy. Model: target = world * source * mount.
"""
import argparse
import csv
from pathlib import Path

import numpy as np
from align_points import quaternion


def pose_matrix(values):
    values = np.asarray(values, dtype=float)
    if values.shape != (7,) or not np.isfinite(values).all():
        raise ValueError("Pose must contain seven finite values")
    x, y, z, w = values[3:]
    norm = np.linalg.norm(values[3:])
    if abs(norm - 1) > 0.01:
        raise ValueError("Pose quaternion must be normalized")
    x, y, z, w = np.array([x, y, z, w]) / norm
    matrix = np.eye(4)
    matrix[:3, :3] = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                      [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                      [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
    matrix[:3, 3] = values[:3]
    return matrix


def project_rotation(matrix):
    u, _, vt = np.linalg.svd(matrix)
    return u @ np.diag([1, 1, np.linalg.det(u @ vt)]) @ vt


def fit_pair(source, target):
    if len(source) != len(target) or len(source) < 20:
        raise ValueError("At least 20 paired poses are required")
    if not np.isfinite(source).all() or not np.isfinite(target).all():
        raise ValueError("Poses must be finite")
    # Cap the fit cost; distant pairs give more rotation excitation than only
    # adjacent frames. All samples are still used for residual reporting.
    indices = np.unique(np.linspace(0, len(source)-1, min(120, len(source)), dtype=int))
    source, target = source[indices], target[indices]
    relative = []
    for step in (1, max(2, len(indices)//3)):
        for i in range(step, len(indices)):
            a = np.linalg.inv(source[i-step]) @ source[i]
            b = np.linalg.inv(target[i-step]) @ target[i]
            relative.append((a, b))
    equations = np.vstack([np.kron(np.eye(3), a[:3, :3]) - np.kron(b[:3, :3].T, np.eye(3)) for a, b in relative])
    _, singular, vt = np.linalg.svd(equations, full_matrices=False)
    if singular[-2] < 0.05 or singular[-1] > 0.5 * singular[-2]:
        raise ValueError("Insufficient independent rotations or inconsistent rigid mounting")
    rotation = vt[-1].reshape((3, 3), order="F")
    if np.linalg.det(rotation) < 0:
        rotation = -rotation
    rotation = project_rotation(rotation)
    lhs = np.vstack([a[:3, :3]-np.eye(3) for a, _ in relative])
    rhs = np.concatenate([rotation @ b[:3, 3]-a[:3, 3] for a, b in relative])
    translation, _, rank, singular = np.linalg.lstsq(lhs, rhs, rcond=None)
    if rank < 3 or singular[-1] < 0.1:
        raise ValueError("Rotate around multiple axes to determine the mounting offset")
    mount = np.eye(4)
    mount[:3, :3], mount[:3, 3] = rotation, translation
    worlds = np.array([t @ np.linalg.inv(mount) @ np.linalg.inv(s) for s, t in zip(source, target)])
    world = np.eye(4)
    world[:3, :3] = project_rotation(worlds[:, :3, :3].mean(axis=0))
    world[:3, 3] = worlds[:, :3, 3].mean(axis=0)
    return world, mount


def errors(source, target, world, mount):
    prediction = world @ source @ mount
    position = np.linalg.norm(prediction[:, :3, 3] - target[:, :3, 3], axis=1)
    rotation = np.transpose(prediction[:, :3, :3], (0, 2, 1)) @ target[:, :3, :3]
    angle = np.arccos(np.clip((np.trace(rotation, axis1=1, axis2=2)-1)/2, -1, 1))
    return position, np.degrees(angle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.csv.open(newline="") as file:
        rows = list(csv.DictReader(file))
    if len(rows) < 30:
        raise SystemExit("No usable calibration: at least 30 paired samples are required")
    source = np.array([pose_matrix([float(row[k]) for k in ("sx", "sy", "sz", "sqx", "sqy", "sqz", "sqw")]) for row in rows])
    target = np.array([pose_matrix([float(row[k]) for k in ("tx", "ty", "tz", "tqx", "tqy", "tqz", "tqw")]) for row in rows])
    holdout = np.arange(len(rows)) % 5 == 0
    world, mount = fit_pair(source[~holdout], target[~holdout])
    position, angle = errors(source[holdout], target[holdout], world, mount)
    print(f"Reserved-sample position RMS: {np.sqrt(np.mean(position**2)):.4f} m; max: {max(position):.4f} m")
    print(f"Reserved-sample orientation RMS: {np.sqrt(np.mean(angle**2)):.2f} degrees; max: {max(angle):.2f} degrees")
    print(f"Estimated rigid mounting offset length: {np.linalg.norm(mount[:3, 3]):.4f} m")
    print("Sample residuals are not independent physical-accuracy measurements. Validate a separate movement sequence.")
    if max(position) > 0.06 or np.sqrt(np.mean(position**2)) > 0.03 or max(angle) > 10:
        raise SystemExit("Calibration rejected: inconsistent poses; check rigid mounting, timing, tracking and room calibration")
    args.output.write_text(" ".join(f"{v:.10g}" for v in (*world[:3, 3], *quaternion(world[:3, :3]))) + "\n")


if __name__ == "__main__":
    main()
