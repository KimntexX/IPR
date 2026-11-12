"""Write a two-input ONNX identity fixture, not a trained denoising model."""
import argparse
from pathlib import Path
import onnx
from onnx import TensorProto, helper

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--filter', action='store_true', help='Use a local spatial filter consuming sigma')
args = parser.parse_args()
shape = [1, 4, 'H', 'W']
nodes = [helper.make_node('Identity', ['planes'], ['corrected'])]
if args.filter:
    nodes = [helper.make_node('AveragePool', ['planes'], ['mean'], kernel_shape=[3,3], pads=[1,1,1,1]),
             helper.make_node('Sub', ['mean', 'sigma'], ['corrected'])]
graph = helper.make_graph(
    nodes,
    'ipr_identity_fixture',
    [helper.make_tensor_value_info(name, TensorProto.FLOAT, shape) for name in ('planes', 'sigma')],
    [helper.make_tensor_value_info('corrected', TensorProto.FLOAT, shape)])
model = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 17)], ir_version=9)
onnx.checker.check_model(model)
args.output.parent.mkdir(parents=True, exist_ok=True)
onnx.save(model, args.output)
