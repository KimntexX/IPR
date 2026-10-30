"""Bounded eight-window subprocess batches; same RCD, no nested parallelism."""
import base64
import hashlib
import json
import subprocess
import numpy as np


def native_batch(tool,inputs,env):
    if not 1<=len(inputs)<=8:raise ValueError('Batch must have 1..8 inputs')
    requests=[];hashes=[]
    for array in inputs:
        if array.shape!=(256,256) or not np.isfinite(array).all():raise ValueError('Invalid CFA')
        raw=np.asarray(array,dtype='<f4').tobytes();hashes.append(hashlib.sha256(raw).hexdigest())
        requests.append(dict(cfa=base64.b64encode(raw).decode(),pattern=[[0,1],[1,2]]))
    process=subprocess.run([str(tool.resolve())],input=json.dumps(dict(batch=requests))+'\n',
        capture_output=True,text=True,encoding='utf-8',env=env,timeout=60)
    if process.returncode:raise RuntimeError(process.stderr)
    data=json.loads(process.stdout)
    if not isinstance(data,list) or len(data)!=len(inputs):raise ValueError('Truncated batch')
    result=[]
    for item,digest in zip(data,hashes):
        if item['shape']!=[256,256,3] or item['dtype']!='f32_le' or not 1<=item['workers']<=8:raise ValueError('Invalid batch contract')
        raw=base64.b64decode(item['after_median'],validate=True)
        if len(raw)!=256*256*3*4:raise ValueError('Truncated RGB')
        image=np.frombuffer(raw,dtype='<f4').reshape(256,256,3).astype(float)
        if not np.isfinite(image).all():raise ValueError('Nonfinite RGB')
        result.append((image,dict(input_sha256=digest,output_sha256=hashlib.sha256(raw).hexdigest(),workers=item['workers'])))
    return result
