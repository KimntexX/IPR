"""Describe existing contour regressions without inference, fitting or selection."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time

CONTRAST_EDGES = (0., .15, .35, .65, math.inf)
CONTRAST_NAMES = ('[0,.15)', '[.15,.35)', '[.35,.65)', '[.65,infinity)')
PHASES = ('RGGB', 'GRBG', 'GBRG', 'BGGR')
MSE_FLOOR = 1e-12


def digest(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def save(path, value):
    with path.open('x', encoding='utf-8') as handle:
        json.dump(value, handle, indent=2, allow_nan=False)


def reject_nonfinite(value):
    raise ValueError('Nonfinite JSON: '+value)


def descriptors(recipe):
    low, high = recipe['low_rgb'], recipe['high_rgb']
    angle = recipe['normal_degrees']
    if (len(low) != 3 or len(high) != 3 or not all(math.isfinite(v) for v in low+high)
            or not math.isfinite(angle) or not 0 <= angle < 180):
        raise ValueError('Finite RGB colours and normal in [0,180) required')
    contrast = max(abs(a-b) for a, b in zip(low, high))
    group = next(i for i in range(4) if CONTRAST_EDGES[i] <= contrast < CONTRAST_EDGES[i+1])
    return dict(contrast_rgb_linf=contrast, contrast_bin=CONTRAST_NAMES[group],
        normal_bin=int(angle//45),
        colour_kind='achromatic' if max(low)-min(low) <= 1e-12 and max(high)-min(high) <= 1e-12 else 'chromatic')


def summarize(rows):
    if not rows:
        return None
    totals = {key: sum(r[key] for r in rows) for key in
        ('pixels', 'baseline_sse', 'output_sse', 'reference_sse', 'baseline_opponent_sse', 'output_opponent_sse')}
    count = totals['pixels']
    if count <= 0 or any(not math.isfinite(v) or v < 0 for v in totals.values()):
        raise ValueError('Finite nonnegative errors and positive pixel support required')
    base = totals['baseline_sse']/(3*count)
    opponent = totals['baseline_opponent_sse']/(2*count)
    informative = [r for r in rows if r['baseline_sse']/(3*r['pixels']) > MSE_FLOOR]
    ratios = [r['output_sse']/r['baseline_sse'] for r in informative]
    delta = lambda r: (r['output_sse']-r['baseline_sse'])/(3*r['pixels'])
    return dict(cases=len(rows), distinct_contents=len({r['seed'] for r in rows}), **totals,
        baseline_mse=base, output_mse=totals['output_sse']/(3*count),
        absolute_mse_change=(totals['output_sse']-totals['baseline_sse'])/(3*count),
        mse_ratio=totals['output_sse']/totals['baseline_sse'] if base > MSE_FLOOR else None,
        opponent_mse_ratio=totals['output_opponent_sse']/totals['baseline_opponent_sse'] if opponent > MSE_FLOOR else None,
        median_informative_case_ratio=statistics.median(ratios) if ratios else None,
        maximum_informative_case_ratio=max(ratios) if ratios else None,
        improved=sum(delta(r) < -MSE_FLOOR for r in rows), worsened=sum(delta(r) > MSE_FLOOR for r in rows),
        tied=sum(abs(delta(r)) <= MSE_FLOOR for r in rows),
        informative_cases=len(informative),
        candidate_over_reference_mse=totals['output_sse']/totals['reference_sse']
            if totals['reference_sse']/(3*count) > MSE_FLOOR else None)


def flatten(proof):
    recipes = {r['seed']: r for r in proof['freeze']['reserved_edges']}
    records = proof['groups']['reserved']
    if len(recipes) != 32 or len(records) != 128 or len({(r['seed'], r['phase_name']) for r in records}) != 128:
        raise ValueError('Expected32 unique reserved contents and128 unique content/phase pairs')
    rows = []
    for record in records:
        recipe = recipes[record['seed']]
        phase = PHASES.index(record['phase_name'])
        if (record['target_sha256'] != recipe['target_sha256']
                or record['packed_sha256'] != recipe['packed_sha256_by_phase'][phase]
                or any(record[k] != recipe[k] for k in ('low_rgb', 'high_rgb', 'normal_degrees', 'offset'))):
            raise ValueError('Changed recipe or observation identity')
        score, reference = record['metrics'], record['reference_metrics']
        keys = ('baseline_sse', 'output_sse', 'baseline_opponent_sse', 'output_opponent_sse')
        if (score['pixels'] <= 0 or any(not math.isfinite(score[k]) or score[k] < 0 for k in keys)
                or score['pixels'] != reference['pixels'] or score['baseline_sse'] != reference['baseline_sse']):
            raise ValueError('Invalid or unmatched paired error support')
        rows.append(dict(seed=record['seed'], phase_name=record['phase_name'], normal_degrees=recipe['normal_degrees'],
            offset=recipe['offset'], low_rgb=recipe['low_rgb'], high_rgb=recipe['high_rgb'],
            target_sha256=record['target_sha256'], packed_sha256=record['packed_sha256'],
            output_sha256=record['output_sha256'], **descriptors(recipe), pixels=score['pixels'],
            **{key: score[key] for key in keys}, reference_sse=reference['output_sse']))
    if any({r['phase_name'] for r in rows if r['seed'] == seed} != set(PHASES) for seed in recipes):
        raise ValueError('Incomplete phase support')
    return rows


def analyze(rows):
    contrast = {b: summarize([r for r in rows if r['contrast_bin'] == b]) for b in CONTRAST_NAMES}
    normal = {str(b): summarize([r for r in rows if r['normal_bin'] == b]) for b in range(4)}
    phase = {p: summarize([r for r in rows if r['phase_name'] == p]) for p in PHASES}
    colour = {c: summarize([r for r in rows if r['colour_kind'] == c]) for c in ('achromatic', 'chromatic')}
    for partition in (contrast, normal, phase, colour):
        if sum(v['pixels'] for v in partition.values() if v) != sum(r['pixels'] for r in rows):
            raise ValueError('Grouping lost or duplicated pixel mass')
    contents = []
    for seed in sorted({r['seed'] for r in rows}):
        group = [r for r in rows if r['seed'] == seed]
        contents.append(dict(seed=seed, **descriptors(group[0]), score=summarize(group)))
    return dict(all=summarize(rows), by_contrast=contrast, by_normal=normal, by_phase=phase, by_colour_kind=colour,
        by_normal_phase={str(b): {p: summarize([r for r in rows if r['normal_bin'] == b and r['phase_name'] == p])
            for p in PHASES} for b in range(4)},
        by_contrast_phase={b: {p: summarize([r for r in rows if r['contrast_bin'] == b and r['phase_name'] == p])
            for p in PHASES} for b in CONTRAST_NAMES}, contents=contents)


def run(source, output):
    root = Path(__file__).resolve().parents[2]
    source, output = source.resolve(), output.resolve()
    if (not source.is_relative_to((root/'docs/validation/raw').resolve())
            or not output.is_relative_to((root/'build-patch_neural_restoration').resolve())):
        raise ValueError('Use canonical JSON source and dedicated neural build output')
    source_hash = digest(source)
    proof = json.loads(source.read_text(encoding='utf-8'), parse_constant=reject_nonfinite)
    if proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Expected completed contour-diversity experiment')
    now = datetime.now(timezone.utc)
    campaign = 'neural-contour-regressions-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(source_campaign=proof['campaign_id'], source_sha256=source_hash,
        source_weights_sha256=proof['weights_sha256'], config=proof['freeze']['config'],
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        code_sha256=digest(Path(__file__)), contrast_definition='max(abs(high_rgb-low_rgb)), balanced camera RGB units, not perceptual contrast.',
        contrast_bins=list(CONTRAST_NAMES), normal_bins_degrees=[0,45,90,135,180],
        relative_error_mse_floor=MSE_FLOOR, absolute_mse_tie_tolerance=MSE_FLOOR,
        policy='Post-hoc descriptive grouping of already consulted reserved controls; no fit, inference or threshold selection.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, status='incomplete', limits=[
            '32 contents, four phase measurements each; no independence between128 pairs.',
            'Small unequal contrast/colour bins; orientation, contrast and colour are confounded.',
            'Grouping rules fixed before this diagnostic, but data already consulted: descriptive, not confirmatory.',
            'RGB/opponent quadratic errors in camera domain, not visible/perceptual error bounds.',
            'No change to qualification gates, weights, training recipes or production.'])
    started = time.monotonic()
    try:
        rows = flatten(proof)
        result['records'] = rows
        result['analysis'] = analyze(rows)
        expected = proof['summaries']['reserved']['summary']
        measured = result['analysis']['all']
        if measured['pixels'] != expected['pixels'] or abs(measured['mse_ratio']-expected['mse_ratio']) > 1e-12:
            raise ValueError('Prior summary not reproduced')
        if digest(source) != source_hash or digest(Path(__file__)) != freeze['code_sha256']:
            raise ValueError('Source or analysis changed')
        result['status'] = 'completed_descriptive_contour_regression_audit_no_inference'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    brief = lambda r: {k: r[k] for k in ('cases', 'distinct_contents', 'mse_ratio',
        'median_informative_case_ratio', 'improved', 'worsened')} if r else None
    print(json.dumps(dict(all=brief(result['analysis']['all']), **{n: {k: brief(v) for k, v in result['analysis'][n].items()}
        for n in ('by_contrast', 'by_normal', 'by_phase', 'by_colour_kind')}), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.source, args.output)
