#!/usr/bin/env python3
"""Rebuild derived CSV/SVG documents from immutable analysis evidence (stdlib only)."""
import argparse
import csv
import hashlib
import html
import json
import math
import re
from decimal import Decimal
from xml.etree import ElementTree
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / 'docs/validation/raw'
OUT = ROOT / 'docs/validation/metrics'
HASH_FORMAT = 'sha256-text-lf-v1'


def digest(path):
    # Text evidence: Git autocrlf must not invalidate cross-platform provenance.
    # Keep all other bytes exact; this is not the digest of images or profiles.
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def family(name):
    for key, target in [('color-mixer', 'melangeur'), ('bruit', 'bruit'),
                        ('temperature', 'balance-blancs'), ('nuance', 'balance-blancs'),
                        ('blancs-hautes', 'tonalite'), ('contraste', 'tonalite'),
                        ('ombre', 'tonalite'), ('lumiere', 'tonalite'),
                        ('texture', 'detail'), ('clarte', 'detail'), ('voile', 'detail'),
                        ('vibrance', 'couleur-globale'), ('saturation', 'couleur-globale')]:
        if key in name:
            return target
    return 'base-raw-profils'


def numeric_rows(node, pointer='', context=''):
    if isinstance(node, dict):
        context = str(node.get('scene', node.get('path', context)))
        for key, value in node.items():
            escaped = str(key).replace('~', '~0').replace('/', '~1')
            yield from numeric_rows(value, pointer + '/' + escaped, context)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            yield from numeric_rows(value, pointer + '/' + str(index), context)
    elif isinstance(node, (int, float)) and not isinstance(node, bool) and math.isfinite(node):
        yield pointer, node, context


def write_csv(path, rows, fields):
    if 'source_sha256' in fields or 'sha256' in fields:
        fields = list(fields) + ['source_hash_format']
        rows = [dict(row, source_hash_format=HASH_FORMAT) for row in rows]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def validate(output):
    hashes, documents, checked = {}, {}, 0
    def actual_hash(source):
        if source not in hashes:
            hashes[source] = digest(ROOT / source)
        return hashes[source]
    for path in sorted(output.glob('*.csv')):
        with path.open(encoding='utf-8', newline='') as stream:
            for row in csv.DictReader(stream):
                source = row.get('source')
                expected_hash = row.get('source_sha256', row.get('sha256'))
                if source and expected_hash and row.get('source_hash_format') != HASH_FORMAT:
                    raise ValueError(f'Regenerate derived provenance: {path.name}')
                if source and expected_hash and actual_hash(source) != expected_hash:
                    raise ValueError(f'Stale evidence: {path.name} / {source}')
                pointer = row.get('locator', '')
                if pointer.startswith('/') and source and 'context' in row:
                    if source not in documents:
                        documents[source] = json.loads((ROOT / source).read_text(encoding='utf-8-sig'))
                    value = documents[source]
                    for part in pointer[1:].split('/'):
                        key = part.replace('~1', '/').replace('~0', '~')
                        value = value[int(key)] if isinstance(value, list) else value[key]
                    if Decimal(str(value)) != Decimal(row['value']):
                        raise ValueError(f'Numeric mismatch: {source}#{pointer}')
                    checked += 1
    curves = list((output / 'curves').glob('*.svg'))
    for path in curves:
        if ElementTree.parse(path).getroot().tag != '{http://www.w3.org/2000/svg}svg':
            raise ValueError(f'Invalid SVG: {path}')
    print(f'Validated: {len(hashes)} source hashes, {checked} JSON values, {len(curves)} SVG documents')


def chart(path, title, labels, series, ylabel, note):
    """Small vector chart. Missing points are gaps, never zero/interpolated."""
    if not labels or not series:
        return
    escape = html.escape
    values = [v for _, points in series for v in points if v is not None]
    if not values:
        return
    low, high = min(0, min(values)), max(0, max(values))
    if high == low:
        high = low + 1
    high += (high - low) * .08
    width, height, left, top, plotw, ploth = 1100, 650, 95, 80, 740, 390
    numeric_x = all(isinstance(value, (int, float)) for value in labels)
    if numeric_x and max(labels) != min(labels):
        x = lambda index: left + (labels[index] - min(labels)) * plotw / (max(labels) - min(labels))
    else:
        x = lambda index: left + index * plotw / max(1, len(labels) - 1)
    y = lambda value: top + ploth - (value - low) * ploth / (high - low)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="white"/>',
             f'<text x="30" y="32" font-size="20">{escape(title)}</text>',
             f'<text x="30" y="58" font-size="13">{escape(ylabel)}</text>']
    for tick in range(6):
        value = low + (high - low) * tick / 5
        yy = y(value)
        parts += [f'<path d="M{left},{yy}h{plotw}" stroke="#ddd"/>',
                  f'<text x="{left-8}" y="{yy+4}" text-anchor="end" font-size="12">{value:.3g}</text>']
    for index, label in enumerate(labels):
        parts.append(f'<text transform="translate({x(index)},{top+ploth+20}) rotate(25)" font-size="11">{escape(str(label))}</text>')
    colors = ['#1565c0', '#d84315', '#2e7d32', '#7b1fa2', '#00838f', '#795548']
    for number, (name, points) in enumerate(series):
        color = colors[number % len(colors)]
        segment = []
        for index, value in enumerate(points + [None]):
            if value is None:
                if len(segment) > 1:
                    parts.append(f'<polyline points="{" ".join(segment)}" fill="none" stroke="{color}" stroke-width="2"/>')
                segment = []
            else:
                segment.append(f'{x(index):.2f},{y(value):.2f}')
                parts.append(f'<circle cx="{x(index)}" cy="{y(value)}" r="4" fill="{color}"/>')
        parts.append(f'<text x="855" y="{100+number*22}" fill="{color}" font-size="11">{escape(name)}</text>')
    parts.append(f'<text x="30" y="620" font-size="12">{escape(note)}</text></svg>')
    path.write_text('\n'.join(parts), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--check', action='store_true', help='Verify existing provenance, JSON values and SVG syntax without rewriting')
    args = parser.parse_args()
    output = args.output
    if args.check:
        validate(output)
        return
    (output / 'curves').mkdir(parents=True, exist_ok=True)
    tables, inventory, tonal, noise, responses, mixer = defaultdict(list), [], [], [], [], []
    for source in sorted(RAW.glob('*')):
        if source.suffix not in ('.json', '.txt'):
            continue
        relative, sha = source.relative_to(ROOT).as_posix(), digest(source)
        category = family(source.name)
        inventory.append({'source': relative, 'sha256': sha, 'family': category, 'bytes': source.stat().st_size})
        if source.suffix == '.json':
            data = json.loads(source.read_text(encoding='utf-8-sig'))
            for pointer, value, context in numeric_rows(data):
                tables[category].append({'source': relative, 'source_sha256': sha,
                                         'locator': pointer, 'value': value,
                                         'context': context, 'unit': 'see source schema'})
            if source.name.startswith('mesure-color-mixer-intermediaires-'):
                metrics = {'hue': 'core_hue_delta', 'saturation': 'core_saturation_delta', 'luminance': 'core_Y_ratio'}
                for band, record in data['results'].items():
                    for index, variant in enumerate(record['variants']):
                        component = variant['component']
                        metric = metrics[component]
                        if variant.get('metadata_valid', True) and metric in variant:
                            mixer.append({'source': relative, 'source_sha256': sha,
                                          'locator': f'/results/{band}/variants/{index}/{metric}',
                                          'scene': Path(data['reference']['path']).stem,
                                          'core_pixels': record.get('core_pixels', 0),
                                          'band': band, 'engine': variant['engine'], 'component': component,
                                          'value': variant['value'], 'metric': metric, 'measurement': variant[metric]})
            if source.name == 'mesure-bruit-v2-etendu-2026-10-05-v1.json':
                def find_noise(node, path=()):
                    if isinstance(node, dict):
                        if 'summary' in node and 'value' in node and node.get('protocol_valid'):
                            for metric in ('flat_sigma_Y_ratio', 'flat_sigma_chroma_ratio', 'detail_edge_gradient_ratio'):
                                if metric in node['summary']:
                                    noise.append({'source': relative, 'locator': '/'.join(path),
                                                  'value': node['value'], 'metric': metric,
                                                  'measurement': node['summary'][metric]})
                        for key, item in node.items():
                            find_noise(item, path + (str(key),))
                    elif isinstance(node, list):
                        for index, item in enumerate(node):
                            find_noise(item, path + (str(index),))
                find_noise(data)
        else:
            feature, engine, corpus = '', '', ''
            for line_number, line in enumerate(source.read_text(encoding='utf-8-sig').splitlines(), 1):
                if re.fullmatch(r'[A-Z][A-Z _]+', line.strip()):
                    feature = line.strip()
                if line.startswith('SET '):
                    feature = line[4:].strip()
                matched = re.match(r'\s+(LR|IPR): (.*)', line)
                if matched:
                    engine, corpus = matched.groups()
                matched_response = re.match(r'\s*(bare|suffix_2): (.*)', line)
                if matched_response and feature and engine:
                    state, content = matched_response.groups()
                    revision = re.search(r'-v(\d+)-', source.name)
                    for metric, number in re.findall(r'(\w+)=([-+]?\d+(?:\.\d+)?)', content):
                        if metric == 'RGB':
                            continue # RGB triplets remain in the complete literal CSV.
                        responses.append({'source': relative, 'source_sha256': sha, 'line': line_number,
                                          'feature': feature, 'engine': engine, 'corpus': corpus,
                                          'state': state, 'revision': int(revision[1]) if revision else 1,
                                          'metric': metric, 'measurement': float(number)})
                matched = re.match(r'\s*(low|mid|high)\s*: ratio -100=([-\d.]+), \+100=([-\d.]+)', line)
                if feature and matched:
                    region, minus, plus = matched.groups()
                    revision = re.search(r'-v(\d+)-', source.name)
                    for state, value in [(-100, minus), (100, plus)]:
                        tonal.append({'source': relative, 'source_sha256': sha, 'feature': feature,
                                      'engine': engine, 'corpus': corpus, 'region': region,
                                      'state': state, 'revision': int(revision[1]) if revision else 1,
                                      'ratio': float(value)})
                # Preserve every textual numerical line with its exact context.
                for index, token in enumerate(re.findall(r'(?<![\w])[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?', line)):
                    tables[category].append({'source': relative, 'source_sha256': sha,
                                             'locator': f'line:{line_number}/token:{index}',
                                             'value': token, 'context': line,
                                             'unit': 'literal token; see line context'})
    fields = ['source', 'source_sha256', 'locator', 'value', 'context', 'unit']
    for category, rows in tables.items():
        write_csv(output / f'{category}.csv', rows, fields)
    write_csv(output / 'sources.csv', inventory, ['source', 'sha256', 'family', 'bytes'])
    # Some historical campaigns only published tables, not machine-readable raw
    # results. Preserve their cells separately; do not pretend these are raw samples.
    reported = []
    for source in sorted((ROOT / 'docs/validation/analyses').glob('*.md')):
        section, header, sha = '', [], digest(source)
        for line_number, line in enumerate(source.read_text(encoding='utf-8-sig').splitlines(), 1):
            if line.startswith('#'):
                section = line.lstrip('# ').strip()
            if not line.startswith('|'):
                header = []
                continue
            cells = [cell.strip() for cell in line.strip('|').split('|')]
            if all(re.fullmatch(r'[:\- ]+', cell) for cell in cells):
                continue
            if not header:
                header = cells
                continue
            for column, cell in enumerate(cells[1:], 1):
                for index, number in enumerate(re.findall(r'(?<![\w])[-+−]?\d+(?:[,.]\d+)?', cell)):
                    reported.append({'source': source.relative_to(ROOT).as_posix(), 'source_sha256': sha,
                                     'line': line_number, 'section': section, 'row': cells[0],
                                     'column': header[column] if column < len(header) else str(column),
                                     'token': index, 'value': number.replace('−', '-').replace(',', '.'),
                                     'cell': cell, 'evidence_level': 'published report table, not raw sample'})
    write_csv(output / 'rapports-tableaux.csv', reported,
              ['source', 'source_sha256', 'line', 'section', 'row', 'column', 'token', 'value', 'cell', 'evidence_level'])
    write_csv(output / 'tonalite-revisions.csv', tonal, ['source', 'source_sha256', 'feature', 'engine', 'corpus', 'region', 'state', 'revision', 'ratio'])
    write_csv(output / 'bruit-progressivite.csv', noise, ['source', 'locator', 'value', 'metric', 'measurement'])
    write_csv(output / 'reglages-reponses.csv', responses, ['source', 'source_sha256', 'line', 'feature', 'engine', 'corpus', 'state', 'revision', 'metric', 'measurement'])
    write_csv(output / 'melangeur-progressivite.csv', mixer, ['source', 'source_sha256', 'locator', 'scene', 'core_pixels', 'band', 'engine', 'component', 'value', 'metric', 'measurement'])
    for band in sorted({row['band'] for row in mixer}):
        for component in ('hue', 'saturation', 'luminance'):
            rows = [r for r in mixer if r['band'] == band and r['component'] == component]
            # Select a single scene with the most core pixels, never pool subjects.
            representative = max(rows, key=lambda r: (r['core_pixels'], r['source']))['source']
            rows = [r for r in rows if r['source'] == representative]
            values = sorted({r['value'] for r in rows})
            series = [(engine, [next((r['measurement'] for r in rows if r['engine'] == engine and r['value'] == value), None) for value in values]) for engine in ('LR', 'IPR')]
            chart(output / 'curves' / f'melangeur-{band}-{component}.svg', f'Mélangeur {band} / {component} — {rows[0]["scene"]}', values,
                  series, rows[0]['metric'] if rows else component,
                  'Progressivité aux valeurs -50/-25/+25/+50 ; neutres propres aux moteurs. Pas de temps CPU ni de parité colorimétrique.')
    for feature in sorted({row['feature'] for row in responses}):
        rows = [r for r in responses if r['feature'] == feature]
        # Show the latest revision as a response, not an unproven chronological gain.
        revision = max(r['revision'] for r in rows)
        rows = [r for r in rows if r['revision'] == revision]
        preferred = ['delta_log_R_B', 'delta_log_RB_over_G', 'delta_chroma',
                     'detail_ratio', 'local_contrast_ratio', 'Y']
        metric = next((key for key in preferred if any(r['metric'] == key for r in rows)), 'Y')
        series = [(engine, [next((r['measurement'] for r in rows if r['engine'] == engine and r['state'] == state and r['metric'] == metric), None)
                            for state in ('bare', 'suffix_2')]) for engine in ('LR', 'IPR')]
        chart(output / 'curves' / f'reponse-{feature.lower()}.svg', f'{feature} — réponse publiée v{revision}', ['bare', 'suffix_2'],
              series, metric + ' (unités et convention : source)',
              'Deux états selon la convention de campagne ; ne pas déduire leurs unités du suffixe. Lacunes laissées vides. Voir reglages-reponses.csv.')
    for feature in sorted({row['feature'] for row in tonal}):
        # Same explicit scene list/count only. Separate corpus groups, never average them.
        corpus_groups = sorted({row['corpus'] for row in tonal if row['feature'] == feature and row['engine'] == 'IPR'})
        for group, corpus in enumerate(corpus_groups, 1):
            rows = [row for row in tonal if row['feature'] == feature and row['engine'] == 'IPR' and row['corpus'] == corpus]
            sources = sorted({row['source'] for row in rows}, key=lambda s: (next(r['revision'] for r in rows if r['source'] == s), s))
            series = [(f'{region} / {state:+}', [next((r['ratio'] for r in rows if r['source'] == s and r['region'] == region and r['state'] == state), None) for s in sources])
                      for region in ('low', 'mid', 'high') for state in (-100, 100)]
            chart(output / 'curves' / f'tonalite-{feature}-{group}.svg', f'{feature} — réponse IPR par révision',
                  [f'v{next(r["revision"] for r in rows if r["source"] == s)} [{i+1}]' for i, s in enumerate(sources)],
                  series, 'Ratio de luminance au neutre ; sans unité',
                  'Révisions, pas temps CPU. Sources et corpus exacts : tonalite-revisions.csv. Un ratio moindre n’est pas toujours meilleur.')
    # Extract benchmark values from the published table, rather than copy constants.
    timing_source = ROOT / 'docs/reports/archive/technical/rapport-optimisation-performance-2026-10-01.md'
    timings = []
    for line_number, line in enumerate(timing_source.read_text(encoding='utf-8-sig').splitlines(), 1):
        if line.startswith('|'):
            cells = [cell.replace('**', '').strip() for cell in line.strip('|').split('|')]
            if len(cells) == 3 and all(re.fullmatch(r'\d+,\d+ s', cell) for cell in cells[1:]):
                for revision, cell in zip(('avant', 'apres_parallelisation'), cells[1:]):
                    timings.append({'source': timing_source.relative_to(ROOT).as_posix(), 'source_sha256': digest(timing_source),
                                    'line': line_number, 'stage': cells[0], 'revision': revision,
                                    'seconds': float(cell[:-2].replace(',', '.')), 'platform': 'Windows / local historical 2026-10-01'})
    write_csv(output / 'temps-ouverture.csv', timings, ['source', 'source_sha256', 'line', 'stage', 'revision', 'seconds', 'platform'])
    chart(output / 'curves/temps-ouverture.svg', 'Ouverture RAW — benchmark historique Windows', ['Avant', 'Après parallélisation'],
          [(stage, [next(r['seconds'] for r in timings if r['stage'] == stage and r['revision'] == rev) for rev in ('avant', 'apres_parallelisation')])
           for stage in dict.fromkeys(r['stage'] for r in timings)], 'Durée (secondes)',
          'Une campagne locale, sans répétitions/variance publiées ; ne décrit pas le build actuel. Détails : temps-ouverture.csv.')
    benchmark_fields = ['stage', 'datetime_utc', 'machine', 'os', 'build_revision', 'libraw_version',
                        'source_sha256', 'profile_sha256', 'settings', 'resolution', 'cache_mode',
                        'repeat', 'duration_ms', 'peak_rss_mib']
    stages = ['opening_total', 'profile_lookup', 'raw_decode', 'lcp_dcp', 'proxy',
              'render', 'thumbnail', 'decoded_cache_lookup', 'export', 'batch_export_future']
    write_csv(output / 'benchmark-template.csv', [{field: stage if field == 'stage' else '' for field in benchmark_fields} for stage in stages], benchmark_fields)
    # One selected scene, components and engines stay separate; no pooled quality score.
    groups = defaultdict(list)
    for row in noise:
        if '6V1A9950' in row['locator'] and row['metric'] in ('flat_sigma_Y_ratio', 'flat_sigma_chroma_ratio'):
            identity = re.sub(r'/variants/\d+$', '', row['locator']) + '/' + row['metric']
            groups[identity].append(row)
    for metric in ('flat_sigma_Y_ratio', 'flat_sigma_chroma_ratio'):
        selected = [(key, rows) for key, rows in sorted(groups.items()) if key.endswith(metric)]
        values = sorted({r['value'] for _, rows in selected for r in rows})
        series = [(key.replace('scenes/', '').replace('/'+metric, ''),
                   [next((r['measurement'] for r in rows if r['value'] == v), None) for v in values]) for key, rows in selected]
        chart(output / 'curves' / f'bruit-{metric}.svg', 'Bruit v2 — 6V1A9950, progression du curseur', values,
              series, 'Ratio au neutre propre à chaque moteur ; sans unité',
              'Une baisse du résidu Haar/MAD ne prouve pas la conservation des détails ; ce graphe ne classe pas la qualité LR/IPR.')
    latest = RAW / 'comparaison-exports-profils-20261008.json'
    if latest.exists():
        pairs = json.loads(latest.read_text(encoding='utf-8'))['pairs']
        scenes = sorted({Path(p['ipr']['path']).name.split('-')[0] for p in pairs})
        chart(output / 'curves/base-profils.svg', 'Base colorimétrique — derniers exports, par scène', scenes,
              [(profile, [next((p['global']['delta_e00_mean'] for p in pairs if Path(p['ipr']['path']).name.startswith(scene+'-') and profile in p['ipr']['path']), None) for scene in scenes])
               for profile in ('CameraNeutral', 'AdobeStandard')], 'Delta E00 moyen à Lightroom (D65)',
              'Comparaison entre scènes, pas évolution chronologique ni fidélité physique. Source : comparaison-exports-profils-20261008.json.')
    print(f'{len(inventory)} sources ; {sum(map(len, tables.values()))} valeurs ; {len(list((output / "curves").glob("*.svg")))} graphiques')


if __name__ == '__main__':
    main()
