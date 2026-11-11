import unittest
from resume_campaign import resumed_steps


class ResumedStepsTests(unittest.TestCase):
    def test_global_steps_after_the_stop_become_child_steps_with_the_end(self):
        self.assertEqual(resumed_steps([37500, 75000, 112500, 150000], 74965, 150000), [35, 37535, 75035])
        self.assertEqual(resumed_steps([0, 65625, 75000], 74965, 80000), [35, 5035])
        self.assertEqual(resumed_steps([10], 10, 20), [10])


if __name__ == '__main__':
    unittest.main()
