import unittest
from inventory_cfa_corpus import choose_pilot


class CfaCorpusSelectionTests(unittest.TestCase):
    def setUp(self):
        self.records=[]
        for category,iso in (('burst42',4000),('confirmed_dark',1600),('confirmed_dark',51200)):
            for i in range(3):
                self.records.append(dict(category=category,split='development',metadata=dict(iso=iso),
                                         status='available_hash_match',label=f'{category}-{iso}-{i}'))

    def test_two_per_predeclared_category(self):
        chosen=choose_pilot(self.records)
        self.assertEqual(len(chosen),6)
        self.assertTrue(all(not r['label'].endswith('-2') for r in chosen))

    def test_sealed_and_validation_never_enter_pilot(self):
        for split in ('sealed','validation','historical_control_already_measured'):
            records=[dict(r,split=split) for r in self.records]
            self.assertEqual(choose_pilot(records),[])

    def test_missing_and_mismatched_sources_are_not_used(self):
        for status in ('missing','available_but_hash_mismatch'):
            self.assertEqual(choose_pilot([dict(r,status=status) for r in self.records]),[])

    def test_room_scenes_do_not_become_confirmed_blacks(self):
        records=[dict(r,category='illuminated_dark_room') for r in self.records]
        self.assertEqual(choose_pilot(records),[])

    def test_incomplete_selection_stays_incomplete(self):
        records=[r for r in self.records if r['metadata']['iso']!=51200]
        self.assertEqual(len(choose_pilot(records)),4)


if __name__=='__main__':
    unittest.main()
