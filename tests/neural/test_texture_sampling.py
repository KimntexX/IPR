from collections import Counter
import unittest
from unittest.mock import patch

import torch

from texture_sampling import joint_quotas,strata,texture_identity,sample_events
from audit_texture_coverage import FREQUENCY,AMPLITUDE,bin_index


class TextureSamplingTests(unittest.TestCase):
    def test_quotas_cover_joint_cells_without_changing_budget(self):
        for n in (12,31,34,39):
            quota=joint_quotas(n)
            self.assertEqual(len(quota),12)
            self.assertEqual(sum(quota.values()),n)
            self.assertTrue(all(v>=1 for v in quota.values()))
        for n in (11,True,-1):
            with self.assertRaises(ValueError):
                joint_quotas(n)

    def test_strata_are_deterministic_per_conditional_group_without_rng_mutation(self):
        events=[dict(serial=i,family='chromatic_wave',phase=0) for i in range(108)]
        before=torch.get_rng_state().clone()
        assigned=strata(events)
        self.assertEqual(assigned,strata(events))
        self.assertTrue(torch.equal(before,torch.get_rng_state()))
        for level in range(3):
            count=Counter(assigned[e['serial']] for e in events if e['serial']%3==level)
            self.assertEqual(len(count),12)
            self.assertEqual(sum(count.values()),36)

    def test_exposure_and_content_identity_are_distinct(self):
        self.assertEqual(texture_identity(dict(serial=7,texture_serial=4000000)),4000000)
        self.assertEqual(texture_identity(dict(serial=7)),7)
        with self.assertRaises(ValueError):
            texture_identity(dict(serial=7,texture_serial=True))

    def test_sampler_matches_all_declared_cells_with_bounded_distinct_id_stream(self):
        events=[dict(serial=3*i+2,family='chromatic_wave',phase=0) for i in range(12)]
        class Guard:
            calls=0
            def check(self):
                self.calls+=1
        def fake_texture(family,identity,side):
            f,a=divmod(identity%12,3)
            return torch.tensor([identity]),dict(frequency=(2.,6.,12.,20.)[f],amplitude=(.003,.01,.04)[a])
        guard=Guard()
        with patch('texture_sampling.texture',side_effect=fake_texture):
            rows,summary=sample_events(events,'stratified',{'8000000'},guard,lambda image:str(int(image[0])))
        self.assertEqual(len(rows),12)
        self.assertEqual(len({r['texture_serial'] for r in rows}),12)
        self.assertNotIn('8000000',{r['target_sha256'] for r in rows})
        self.assertEqual(len({(bin_index(r['frequency'],FREQUENCY),bin_index(r['amplitude'],AMPLITUDE)) for r in rows}),12)
        self.assertEqual(summary['accepted'],12)
        self.assertGreater(guard.calls,0)
