import unittest
import numpy as np
from align_points import fit, quaternion


class AlignmentTests(unittest.TestCase):
    def test_known_transform_and_quaternion(self):
        source = np.array([[0., 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]])
        rotation = np.array([[0., 0, 1], [0, 1, 0], [-1, 0, 0]])
        translation = np.array([2., 3, 4])
        r, t, errors = fit(source, source @ rotation.T + translation)
        np.testing.assert_allclose(r, rotation, atol=1e-12)
        np.testing.assert_allclose(t, translation, atol=1e-12)
        self.assertLess(max(errors), 1e-12)
        np.testing.assert_allclose(quaternion(r), [0, np.sqrt(0.5), 0, np.sqrt(0.5)], atol=1e-12)

    def test_half_turn(self):
        np.testing.assert_allclose(abs(quaternion(np.diag([1., -1, -1]))), [1, 0, 0, 0], atol=1e-12)

    def test_degenerate_and_nonfinite(self):
        for points in (np.zeros((4, 3)), np.full((4, 3), np.nan)):
            with self.assertRaises(ValueError):
                fit(points, points)


if __name__ == "__main__":
    unittest.main()
