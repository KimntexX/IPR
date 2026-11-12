"""Explicit execution amendments, distinct from immutable scientific checkpoint identity."""
import ast
import copy
import json
from pathlib import Path
from campaign_artifacts import sha256


def numerical_update_ast(source):
    tree=ast.parse(source);function=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='update')
    class StripMonitoring(ast.NodeTransformer):
        def visit_Expr(self,node):
            if isinstance(node.value,ast.Call) and isinstance(node.value.func,ast.Name) and node.value.func.id in ('phase','check'):return None
            return self.generic_visit(node)
    filtered=StripMonitoring().visit(copy.deepcopy(function))
    return ast.dump(filtered,include_attributes=False)


def load(path,scientific_path,root):
    if path is None:return None
    path=Path(path)
    if not path.resolve().is_relative_to((root/'build-patch_neural_restoration').resolve()):raise ValueError('Owned execution contract required')
    execution=json.loads(path.read_text(encoding='utf-8'))
    if sha256(scientific_path) not in execution['scientific_plan_sha256']:raise ValueError('Execution amendment not authorized for this scientific plan')
    p=execution['resources']
    if not (0<p['soft_mib']<p['hard_mib']<=6144 and 0<p['system_fraction']<=.92):raise ValueError('Unsafe execution budgets')
    for name,digest in execution['runtime_tools'].items():
        if sha256(root/'tests/neural'/name)!=digest:raise ValueError('Execution dependency changed: '+name)
    return execution


def validate_sources(plan,execution,root):
    for name,expected in plan['tools'].items():
        actual=sha256(root/'tests/neural'/name)
        if actual==expected:continue
        amendment=None if execution is None else execution['tool_amendments'].get(name)
        if not amendment or amendment['from_sha256']!=expected or amendment['to_sha256']!=actual:
            raise ValueError('Sealed scientific source changed: '+name)
