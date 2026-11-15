import json
from pathlib import Path
import tempfile
import unittest
import numpy as np
from capture import compare_captures,load_capture,sha256


class CaptureTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def capture(self,name,values,mode='parity'):
        a=np.asarray(values);a=a.reshape(2,2,1);binary=self.root/(name+'.bin');binary.write_bytes(a.tobytes())
        dtype='u16' if a.dtype.kind=='u' else 'f32'
        path=self.root/(name+'.json')
        path.write_text(json.dumps(dict(schema_version=1,source=dict(sha256='same'),mode=mode,pixels=dict(path=binary.name,sha256=sha256(binary)),
            stages=[dict(stage='unpacked_cfa',shape=list(a.shape),dtype=dtype,offset_bytes=0,stride_bytes=a.shape[1]*a.itemsize,
                origin_yx=[0,0],layout='cfa',orientation='sensor',unit='DN',context=dict(cfa=[[0,1],[3,2]]))])),encoding='utf-8')
        return path

    def test_exact(self):
        a=self.capture('a',np.arange(4,dtype='<u2'));b=self.capture('b',np.arange(4,dtype='<u2'))
        self.assertEqual(compare_captures(a,b,('unpacked_cfa',))['verdict'],'pass')

    def test_missing_stage_fails(self):
        a=self.capture('a',np.arange(4,dtype='<u2'))
        self.assertEqual(compare_captures(a,a)['verdict'],'fail')

    def test_integer_mismatch(self):
        a=self.capture('a',np.arange(4,dtype='<u2'));b=self.capture('b',np.arange(4,dtype='<u2')+1)
        self.assertEqual(compare_captures(a,b,('unpacked_cfa',))['stages'][0]['unequal_values'],4)
        with self.assertRaises(ValueError):compare_captures(a,b,('unpacked_cfa',),{'unpacked_cfa':dict(atol=1,rtol=0)})

    def test_nonfinite(self):
        a=self.capture('a',np.array([0,1,2,np.nan],dtype='<f4'))
        self.assertEqual(compare_captures(a,a,('unpacked_cfa',))['verdict'],'fail')

    def test_hash(self):
        a=self.capture('a',np.arange(4,dtype='<u2'));(self.root/'a.bin').write_bytes(b'bad')
        with self.assertRaises(ValueError):load_capture(a)

    def test_signed_not_parity(self):
        a=self.capture('a',np.arange(4,dtype='<f4'),mode='signed')
        with self.assertRaises(ValueError):compare_captures(a,a,('unpacked_cfa',))
        self.assertEqual(compare_captures(a,a,('unpacked_cfa',),purpose='measurement')['verdict'],'measured_not_parity')

    def test_geometry(self):
        a=self.capture('a',np.arange(4,dtype='<u2'));b=self.capture('b',np.arange(4,dtype='<u2'))
        data=json.loads(b.read_text());data['stages'][0]['orientation']='display';b.write_text(json.dumps(data))
        self.assertEqual(compare_captures(a,b,('unpacked_cfa',))['verdict'],'fail')

    def test_explicit_scope_records_uncompared_stages(self):
        a=self.capture('a',np.arange(4,dtype='<u2'));b=self.capture('b',np.arange(4,dtype='<u2'))
        data=json.loads(b.read_text());stage=dict(data['stages'][0]);stage['stage']='candidate_only';stage['offset_bytes']=8
        binary=self.root/'b.bin';binary.write_bytes(binary.read_bytes()*2);data['pixels']['sha256']=sha256(binary)
        data['stages'].append(stage);b.write_text(json.dumps(data))
        result=compare_captures(a,b,('unpacked_cfa',))
        self.assertEqual(result['verdict'],'pass');self.assertEqual(result['uncompared_candidate_stages'],['candidate_only'])


if __name__=='__main__':unittest.main()
