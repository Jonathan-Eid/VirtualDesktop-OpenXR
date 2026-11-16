import unittest
import numpy as np
from align_rigid_pair import pose_matrix, fit_pair, errors


class RigidPairTests(unittest.TestCase):
    def test_unknown_mount_and_room_transform(self):
        rng = np.random.default_rng(42)
        def random_pose():
            q = rng.normal(size=4)
            q /= np.linalg.norm(q)
            return pose_matrix([*rng.normal(size=3), *q])
        source = np.array([random_pose() for _ in range(40)])
        world, mount = random_pose(), random_pose()
        target = world @ source @ mount
        fitted_world, fitted_mount = fit_pair(source, target)
        np.testing.assert_allclose(fitted_world, world, atol=1e-10)
        np.testing.assert_allclose(fitted_mount, mount, atol=1e-10)
        position, angle = errors(source, target, fitted_world, fitted_mount)
        self.assertLess(max(position), 1e-10)
        self.assertLess(max(angle), 1e-4)

    def test_stationary_rejected(self):
        poses = np.repeat(np.eye(4)[None], 30, axis=0)
        with self.assertRaises(ValueError):
            fit_pair(poses, poses)


if __name__ == "__main__":
    unittest.main()
