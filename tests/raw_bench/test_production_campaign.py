import unittest
from argparse import Namespace
from pathlib import Path
from production_campaign import full_equal,resolve_paths


class WholeImageTest(unittest.TestCase):
    def sample(self):return dict(shape=[2,2,4],dtype='f32',sha256='image')
    def test_identical(self):self.assertTrue(full_equal(self.sample(),self.sample()))
    def test_hash(self):
        b=self.sample();b['sha256']='other';self.assertFalse(full_equal(self.sample(),b))
    def test_shape(self):
        b=self.sample();b['shape']=[4,1,4];self.assertFalse(full_equal(self.sample(),b))
    def test_dtype(self):
        b=self.sample();b['dtype']='u16';self.assertFalse(full_equal(self.sample(),b))
    def test_relative_engine_path_resolved_before_cmake(self):
        args=resolve_paths(Namespace(engine=Path('build/reference'),revision='fixture',capture=True))
        self.assertTrue(args.engine.is_absolute())
        self.assertEqual(args.engine,Path('build/reference').resolve())
        self.assertEqual(args.revision,'fixture');self.assertTrue(args.capture)


if __name__=='__main__':unittest.main()
