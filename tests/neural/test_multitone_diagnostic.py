import unittest

from diagnose_multitone_training import select_events,select_domains


class MultitoneDiagnosticTests(unittest.TestCase):
    def test_selection_is_fixed_by_content_and_serial_not_model_output(self):
        events, expected = [], []
        for family_index, family in enumerate(('weak_achromatic_wave', 'chromatic_wave')):
            for bin_index, frequency in enumerate((4, 8, 16, 24)):
                origin = 100*family_index+10*bin_index
                events.append(dict(family=family, serial=origin, frequency=frequency, amplitude=.03, score=0.))
                for offset in (3, 2, 1):
                    events.append(dict(family=family, serial=origin+offset, frequency=frequency,
                        amplitude=.05, score=-offset))
                expected.extend((origin+1, origin+2))
        selected = select_events(events)
        self.assertEqual([e['serial'] for e in selected], expected)
        for event in events:
            event['score'] *= -100
        self.assertEqual([e['serial'] for e in select_events(events)], expected)

    def test_missing_stratum_is_an_error_not_silent_replacement(self):
        with self.assertRaises(ValueError):
            select_events([])

    def test_disjoint_domains_are_both_probed_without_false_training_identity(self):
        previous=[]
        for family in ('weak_achromatic_wave','chromatic_wave'):
            for frequency in (4,8,16,24):
                for _ in range(2):
                    previous.append(dict(serial=len(previous),family=family,frequency=frequency,amplitude=.05,texture_serial=4000000+len(previous)))
        current=[dict(e,texture_serial=e['texture_serial']+4000000) for e in previous]
        selected=select_domains(previous,current)
        self.assertEqual(len(selected),32)
        self.assertEqual({r['source_domain'] for r in selected},{'previous','current'})
        self.assertTrue(all(r['event'] in previous for r in selected if r['source_domain']=='previous'))
        shared=select_domains(previous,list(previous))
        self.assertEqual(len(shared),16)
        self.assertTrue(all(r['source_domain']=='shared' for r in shared))
        partial=list(previous[:8])+current[8:]
        mixed=select_domains(previous,partial)
        self.assertEqual(len(mixed),24)
        self.assertEqual(sum(r['source_domain']=='shared' for r in mixed),8)
