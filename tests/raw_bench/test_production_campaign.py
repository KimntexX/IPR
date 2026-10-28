import unittest
from production_campaign import full_equal


class WholeImageTest(unittest.TestCase):
    def sample(self):return dict(shape=[2,2,4],dtype='f32',sha256='image')
    def test_identical(self):self.assertTrue(full_equal(self.sample(),self.sample()))
    def test_hash(self):
        b=self.sample();b['sha256']='other';self.assertFalse(full_equal(self.sample(),b))
    def test_shape(self):
        b=self.sample();b['shape']=[4,1,4];self.assertFalse(full_equal(self.sample(),b))
    def test_dtype(self):
        b=self.sample();b['dtype']='u16';self.assertFalse(full_equal(self.sample(),b))


if __name__=='__main__':unittest.main()
