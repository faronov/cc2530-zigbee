# SPDX-License-Identifier: BSD-3-Clause
"""Release semantics are mandatory; incidental replay counters are not pinned."""
import copy
import unittest

from xdata_acceptance import successful_join


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(complete=True, phase=5, terminal="serving", case=0, peak_sp=123,
                           board="lg_esl29_rev03", key_mode="default-tc", simulated=True,
                           hardware_observed=False, identities={"ihx": "image"})

    def test_complete_serving_join_is_required_without_pinning_incidental_counts(self):
        for steps, stops in ((1142, 559709), (1300, 600000)):
            successful_join(self.report | {"steps": steps, "peripheral_stops": stops}, {"ihx": "image"})

    def test_incomplete_fault_wrong_profile_stack_and_foreign_image_rejected(self):
        for key, value in (("complete", False), ("phase", 4), ("terminal", "fault"),
                           ("case", 2), ("case", False), ("peak_sp", 125), ("peak_sp", -1),
                           ("simulated", False), ("hardware_observed", True),
                           ("board", "generic"), ("key_mode", "install-code"),
                           ("identities", {"ihx": "different"})):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                successful_join(copy.deepcopy(self.report) | {key: value}, {"ihx": "image"})


if __name__ == "__main__":
    unittest.main()
