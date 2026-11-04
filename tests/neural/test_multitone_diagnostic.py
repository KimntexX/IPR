import unittest

from diagnose_multitone_training import select_events


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
