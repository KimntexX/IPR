"""Six-scene WB campaign: XMP associations, native group metrics, response pairing.

No production coefficients fitted; no cross-engine pixel MAE or image deletion.
"""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

from analysis import campaign, native
from analysis import metrics
from investigate_tonal_base import profile_summary


def xmp(path):
    with Image.open(path) as image:
        packet = image.tag_v2.get(700, b'')
        if isinstance(packet, tuple):
            packet = bytes(packet)
        settings = {}
        for element in ET.fromstring(packet).iter():
            settings.update({key.split('}')[1]: value for key, value in element.attrib.items()
                             if key.startswith('{' + native.CRS + '}')})
            if element.tag.startswith('{' + native.CRS + '}') and element.text:
                settings[element.tag.split('}')[1]] = element.text.strip()
        return settings


def neutral_paths(root):
    """Locate actual shared neutrals, using LR RawFileName, not export numbering.

    Keep every IPR candidate: an export filename alone proves neither the
    selected DCP nor which executable produced it.
    """
    result = defaultdict(lambda: {'LR': [], 'IPR': []})
    for path in sorted(root.iterdir()):
        if not path.is_file() or path.suffix.lower() not in ('.tif', '.tiff'):
            continue
        if '-IPR' in path.stem:
            result[path.stem.split('-IPR', 1)[0]]['IPR'].append(path)
        else:
            settings = xmp(path)
            source = settings.get('RawFileName')
            if not source:
                raise ValueError('Shared LR neutral without embedded RawFileName: ' + str(path))
            result[Path(source.replace('\\', '/')).stem]['LR'].append(path)
    return dict(result)


def neutral_audit(base, step=8):
    """Read-only audit: shared As Shot neutrals and current LR WB variants.

    Reports own-image statistics only, never pixel MAE across engines. The
    absolute LR As Shot temperature/tint are metadata, not manual-error flags.
    """
    if step < 1:
        raise ValueError('Positive sampling step required')
    scenes = neutral_paths(base / 'neutre')
    inspected, rows = [], []
    for scene, engines in sorted(scenes.items()):
        row = {'scene': scene, 'shared_neutrals': [], 'variants': [], 'issues': [],
               'ipr_selected_reference': None, 'selection_provenance': None}
        preferred = [p for p in engines['LR'] if p.stem == scene + '-LR-CameraNeutral-AsShot']
        selected_lr = preferred[0] if len(preferred) == 1 else (
            engines['LR'][0] if len(engines['LR']) == 1 else None)
        row['lr_selected_reference'] = str(selected_lr.resolve()) if selected_lr else None
        if selected_lr is None:
            row['issues'].append('missing_or_ambiguous_shared_LR_neutral')
        if not engines['IPR']:
            row['issues'].append('missing_shared_IPR_neutral')
        neutral_settings = None
        for engine, paths in engines.items():
            for path in paths:
                pixels, metadata, before = native.inspect(path)
                inspected.append((path, before, metadata['sha256']))
                encoded_rgb = native.encoded(pixels, step=step)
                rgb = metrics.linearize(encoded_rgb)
                y = rgb @ np.array([.2126, .7152, .0722])
                item = {'engine': engine, 'metadata': metadata,
                        'sampling_step': step, 'native_samples': int(y.size),
                        'mean_linear_RGB': rgb.mean(axis=(0, 1)).tolist(),
                        'mean_linear_Y': float(y.mean()),
                        'clipped_percent': float(100 * (encoded_rgb.max(axis=-1) >= 1 - 1 / 65535).mean())}
                row['shared_neutrals'].append(item)
                item['selected_reference'] = (path == selected_lr if engine == 'LR' else
                                              path.stem == scene + '-IPR-CameraNeutral-AsShot')
                if engine == 'LR' and path == selected_lr:
                    neutral_settings = metadata['xmp_settings']
                    row['lr_as_shot'] = {key: neutral_settings.get(key) for key in
                                        ('WhiteBalance', 'Temperature', 'Tint', 'CameraProfile',
                                         'LensProfileEnable', 'AutoLateralCA')}
                    if neutral_settings.get('WhiteBalance') != 'As Shot':
                        row['issues'].append('shared_LR_neutral_not_As_Shot')
                if engine == 'IPR':
                    item['settings_provenance'] = 'unknown: filename is not a settings manifest'
                    if path.stem == scene + '-IPR-CameraNeutral-AsShot':
                        row['ipr_selected_reference'] = str(path.resolve())
                        row['selection_provenance'] = 'explicit_filename_candidate_not_verified_settings'
        if row['ipr_selected_reference'] is None:
            row['issues'].append('no_explicit_CameraNeutral_AsShot_IPR_candidate')
        for feature, folder, field in [('temperature', 'temperature', 'Temperature'),
                                       ('tint', 'nuance', 'Tint')]:
            by_value = defaultdict(list)
            directory = base / 'couleur' / folder
            if not directory.exists():
                row['issues'].append('missing_LR_series:' + feature)
                continue
            for path in sorted(directory.iterdir()):
                if path.suffix.lower() not in ('.tif', '.tiff') or '-IPR-' in path.stem:
                    continue
                before = native.signature(path)
                settings = xmp(path)
                if Path(settings.get('RawFileName', '').replace('\\', '/')).stem != scene:
                    continue
                value = float(settings[field])
                digest = native.sha256(path)
                if native.signature(path) != before:
                    raise ValueError('Variant changed during XMP/hash inspection: ' + str(path))
                inspected.append((path, before, digest))
                by_value[value].append(str(path.resolve()))
                differences = []
                if neutral_settings is not None:
                    for control in ('CameraProfile', 'LensProfileEnable', 'AutoLateralCA'):
                        if settings.get(control) != neutral_settings.get(control):
                            differences.append(control)
                row['variants'].append({'file': str(path.resolve()), 'sha256': digest,
                                        'feature': feature, 'value': value, 'xmp': settings,
                                        'shared_neutral_mismatches': differences,
                                        'status': 'diagnostic' if differences or neutral_settings is None else
                                                  'metadata_consistent_pixels_not_compared'})
            expected = (2000, 3500, 5200, 6500, 9000, 50000) if feature == 'temperature' else (
                -150, -100, -50, -25, 0, 25, 50, 100, 150)
            row.setdefault('coverage', {})[feature] = {
                'present_values': sorted(by_value), 'missing_values': [v for v in expected if v not in by_value],
                'duplicates': [{'value': v, 'files': paths} for v, paths in sorted(by_value.items()) if len(paths) > 1]}
        ipr_hashes = [item['metadata']['sha256'] for item in row['shared_neutrals'] if item['engine'] == 'IPR']
        row['IPR_candidates_byte_identical'] = len(ipr_hashes) > 1 and len(set(ipr_hashes)) == 1
        rows.append(row)
        print('Shared-neutral audit:', scene, flush=True)
    for path, before, digest in inspected:
        native.verify_unchanged(path, before, digest)
    return {'scope': 'shared As Shot neutrals in base/neutre and LR WB metadata',
            'reference_policy': 'shared neutrals are not replaced by manual 5200/0 supports',
            'method': {'space': 'linear sRGB D65', 'global_step': step,
                       'clipping_encoded_threshold': 1 - 1 / 65535,
                       'geometry': 'native pixels; no resampling or inter-engine registration'},
            'units': {'mean_linear_RGB': 'relative linear channel value 0..1',
                      'mean_linear_Y': 'relative linear luminance; Rec.709 weights',
                      'clipped_percent': 'percent of native sampled pixels',
                      'temperature': 'kelvin', 'tint': 'software-specific units'},
            'lr_As_Shot_values_are_not_input_errors': True, 'scenes': rows,
            'limits': ['IPR neutral filenames do not prove settings or executable provenance',
                       'No cross-engine pixel alignment, MAE or colour-truth acceptance',
                       'An optical mismatch remains diagnostic, never silently corrected',
                       'Variant metadata consistency alone is not functional validation'],
            'cleanup_eligible': False}


def memory_records(paths):
    """Reuse immutable evidence, not a removed generator CLI or its manifest.

    Files and references remain checked against their original pixel hashes
    by campaign.analyse. Reuse is explicit and never labelled a fresh render.
    """
    result = []
    for path in paths:
        before = native.sha256(path)
        document = native.read_json(path)
        for original in document['records']:
            if original['engine'] != 'IPR' or original['feature'] not in ('temperature', 'tint'):
                continue
            record = {key: original[key] for key in ('scene', 'engine', 'feature', 'value',
                       'file', 'reference', 'settings', 'reference_settings')}
            record['file'] = str(native.local_path(record['file']))
            record['reference'] = str(native.local_path(record['reference']))
            record['expected_sha256'] = original['metadata']['sha256']
            record['reference_sha256'] = original['reference_metadata']['sha256']
            record['provenance'] = {'kind': 'immutable_measurement_memory', 'memory': str(path.resolve()),
                                    'memory_sha256': before, 'fresh_IPR_render': False,
                                    'original_provenance': original.get('provenance'),
                                    'original_campaign_id': document['campaign_id'],
                                    'original_production_sources_sha256': document.get('comparison', {}).get(
                                        'production_sources_sha256', {})}
            result.append(record)
        if native.sha256(path) != before:
            raise ValueError('Canonical memory changed during reading: ' + str(path))
    return result


def records(lr_root, memories, selected=('temperature', 'tint')):
    result, inventory = [], []
    historical = memory_records(memories)
    for feature, folder, field in [('temperature', 'temperature', 'Temperature'), ('tint', 'nuance', 'Tint')]:
        if feature not in selected:
            continue
        ipr = [r for r in historical if r['feature'] == feature]
        for r in ipr:
            r['reference_role'] = 'support'
        result.extend(ipr)
        by_scene = defaultdict(list)
        for path in sorted((lr_root / folder).glob('*.tif')):
            settings = xmp(path)
            scene = Path(settings['RawFileName']).stem
            by_scene[scene].append((path, settings))
        for scene, entries in by_scene.items():
            anchors = [(p, s) for p, s in entries if float(s[field]) == (5200 if feature == 'temperature' else 0)]
            if not anchors:
                # A missing manual zero is not fabricated from an As Shot +5.
                # Keep this LR series in the inventory, without publishing a
                # response against a semantically different baseline.
                inventory.extend({'file': str(p), 'scene': scene, 'feature': feature,
                                  'value': float(s[field]), 'xmp': s,
                                  'excluded_reason': 'missing_manual_support'} for p, s in entries)
                continue
            anchor = anchors[0][0]
            for path, settings in entries:
                inventory.append({'file': str(path), 'scene': scene, 'feature': feature,
                                  'value': float(settings[field]), 'xmp': settings})
                if settings.get('CameraProfile') != 'Camera Neutral':
                    raise ValueError('Unexpected LR profile: ' + str(path))
                zero_controls = ['Exposure2012', 'Contrast2012', 'Highlights2012', 'Shadows2012',
                                 'Whites2012', 'Blacks2012', 'Texture', 'Clarity2012', 'Dehaze',
                                 'Vibrance', 'Saturation', 'Sharpness', 'LuminanceSmoothing',
                                 'ColorNoiseReduction', 'GrainAmount', 'AutoLateralCA', 'LensProfileEnable']
                zero_controls += [key for key in settings if key.startswith(('HueAdjustment',
                                  'SaturationAdjustment', 'LuminanceAdjustment'))]
                issues = ['secondary_LR_control_nonzero_or_unknown:' + key
                          for key in zero_controls if float(settings.get(key, 'nan')) != 0]
                result.append({'scene': scene, 'engine': 'LR', 'feature': feature,
                               'value': float(settings[field]), 'file': str(path.resolve()),
                               'reference': str(anchor.resolve()), 'reference_role': 'support',
                               'preflight_issues': issues,
                               'provenance': {'kind': 'embedded_LR_XMP', 'control_field': field}})
    return result, inventory


def pairings(measurements):
    series = defaultdict(list)
    for r in measurements:
        if r['status'] != 'excluded':
            series[r['scene'], r['feature'], r['value'], r['engine']].append(r)
    paired = []
    for scene, feature, value in sorted({k[:3] for k in series}):
        a, b = series[scene, feature, value, 'IPR'], series[scene, feature, value, 'LR']
        if not a or not b:
            continue
        field = 'delta_log_R_B' if feature == 'temperature' else 'delta_log_magenta_green'
        va, vb = a[0]['metrics'][field], b[0]['metrics'][field]
        channels = {}
        for engine, record in [('IPR', a[0]), ('LR', b[0])]:
            m = record['metrics']
            channels[engine] = {
                'channel_ratio_of_means_RGB': [v / n if n > 1e-12 else None for v, n in
                    zip(m['channel_mean_RGB_variant'], m['channel_mean_RGB_neutral'])],
                'Y_ratio': m['Y_ratio'], 'clipped_percent': m['clipped_percent_variant'],
                'native_dimensions': record['metadata']['dimensions']}
        paired.append({'scene': scene, 'feature': feature, 'value': value,
                       'metric': field, 'IPR': va, 'LR': vb,
                       'IPR_minus_LR': va - vb if va is not None and vb is not None else None,
                       'IPR_axis_samples': a[0]['metrics']['axis_samples'],
                       'LR_axis_samples': b[0]['metrics']['axis_samples'],
                       'IPR_status': a[0]['status'], 'LR_status': b[0]['status'],
                       'duplicate_IPR_count': len(a),
                       'duplicate_LR_count': len(b),
                       'photometry': channels,
                       'cross_engine_pixel_alignment': False,
                       'masks_independent_by_engine_and_state': True})
    aggregates = []
    for feature, value in sorted({(p['feature'], p['value']) for p in paired}):
        matches = [p for p in paired if p['feature'] == feature and p['value'] == value]
        valid = [p for p in matches if p['IPR_minus_LR'] is not None and
                 p['IPR_status'] == p['LR_status'] == 'measured' and
                 p['duplicate_IPR_count'] == p['duplicate_LR_count'] == 1]
        aggregates.append({'feature': feature, 'value': value, 'scenes': len(valid),
                           **{field + '_median': float(np.median([p[field] for p in valid])) if valid else None
                              for field in ('IPR', 'LR', 'IPR_minus_LR')}})
    return paired, aggregates


def main():
    execution_source_hash = native.sha256(__file__)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lr-root', type=Path, default=Path('tests/image/base/couleur'))
    parser.add_argument('--ipr-memory', type=Path, action='append',
                        help='Immutable prior measurement JSON whose IPR pixels are explicitly reused')
    parser.add_argument('--audit-neutrals', action='store_true',
                        help='Audit actual base/neutre and current LR WB series without generating images')
    parser.add_argument('--base', type=Path, default=Path('tests/image/base'))
    parser.add_argument('--output-dir', type=Path, default=Path('docs/validation/raw'))
    parser.add_argument('--dcp', type=Path, help='Read-only DCP inventory; not a rendering override')
    parser.add_argument('--feature', choices=('temperature', 'tint'), action='append',
                        help='Optional subcampaign; default analyses both controls')
    args = parser.parse_args()
    if args.audit_neutrals:
        if args.ipr_memory or args.feature or args.dcp:
            parser.error('--audit-neutrals does not accept IPR memory, feature or DCP overrides')
        now = datetime.now(timezone.utc)
        document = {'schema_version': 1, 'data_version': 2, 'measured_at_utc': now.isoformat(),
                    'analysis_kind': 'shared_WB_neutral_audit', 'group': 'couleur',
                    'script_sha256': execution_source_hash,
                    'audit': neutral_audit(args.base.resolve())}
        if native.sha256(__file__) != execution_source_hash:
            raise ValueError('Analyser source changed during neutral audit; no memory published')
        args.output_dir.mkdir(parents=True, exist_ok=True)
        output = args.output_dir / ('audit-neutres-wb-v002-' + now.strftime('%Y%m%dT%H%M%S%fZ') + '.json')
        with output.open('x', encoding='utf8') as stream:
            json.dump(document, stream, indent=2, ensure_ascii=False, allow_nan=False)
        print(output, flush=True)
        return
    if not args.ipr_memory:
        parser.error('--ipr-memory is required for explicit reuse of historical IPR pixels')
    selected = args.feature or ['temperature', 'tint']
    rs, inventory = records(args.lr_root, args.ipr_memory, selected)
    print('Analysing', len(rs), 'states; own-engine 5200 K anchors; no pixel registration', flush=True)
    document = campaign.analyse('couleur', rs, step=8, data_version=2)
    for record in document['records']:
        record['issues'].extend(record.get('preflight_issues', []))
        if record['issues'] and record['status'] != 'excluded':
            record['status'], record['protocol_usable'] = 'diagnostic', False
    document['coverage'] = {status: sum(r['status'] == status for r in document['records'])
                            for status in ('measured', 'diagnostic', 'excluded')}
    document['progressions'] = campaign.progressions('couleur', document['records'])
    document['comparison'] = {}
    document['comparison']['pairs'], document['comparison']['aggregates'] = pairings(document['records'])
    document['comparison']['lr_inventory'] = inventory
    document['comparison']['limits'] = ['Only Camera Neutral sampled: no cross-profile causal inference',
        'Absolute IPR and LR tint zero have different As Shot anchors',
        'Response axes use independent unclipped masks; no inter-engine pixel MAE',
        'No absolute colorimetric ground truth or human visual acceptance']
    document['comparison']['script_sha256'] = execution_source_hash
    document['comparison']['reused_IPR_memories'] = [
        {'file': str(path.resolve()), 'sha256': native.sha256(path)} for path in args.ipr_memory]
    for r in rs:
        if r['engine'] == 'IPR' and native.sha256(r['provenance']['memory']) != r['provenance']['memory_sha256']:
            raise ValueError('Canonical IPR memory changed during analysis')
    document['comparison']['fresh_IPR_render'] = False
    document['comparison']['shared_neutral_directory'] = str((args.lr_root.parent / 'neutre').resolve())
    document['comparison']['reference_scope'] = (
        'Manual WB response supports; not a replacement for shared As Shot neutrals in base/neutre')
    document['comparison']['production_sources_sha256'] = {
        str(path): native.sha256(path) for path in map(Path, [
            'src/core/ColorPipeline.cpp', 'src/editor/imageeditor.cpp',
            'src/profiles/CameraProfileEngine.cpp', 'src/raw/RawDecoder.cpp'])}
    current_sources = document['comparison']['production_sources_sha256']
    document['comparison']['production_unchanged_since_IPR_render'] = all(
        r['provenance']['original_production_sources_sha256'] == current_sources
        for r in rs if r['engine'] == 'IPR')
    if args.dcp:
        document['comparison']['dcp_inventory'] = profile_summary(args.dcp)
        document['comparison']['dcp_inventory']['identity_limitation'] = (
            'Inventory of explicitly supplied local DCP; manifest stores label only, not opened path/hash')
    keys = {(r['scene'], r['feature'], r['value'], r['engine']) for r in rs}
    document['comparison']['unpaired_states'] = [
        {'scene': s, 'feature': f, 'value': v, 'present_engine': e}
        for s, f, v, e in sorted(keys)
        if (s, f, v, 'LR' if e == 'IPR' else 'IPR') not in keys]
    timestamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    if native.sha256(__file__) != execution_source_hash:
        raise ValueError('Analyser source changed during comparison; no memory published')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / ('comparaison-temperature-nuance-v002-' + timestamp + '.json')
    with output.open('x', encoding='utf8') as stream:
        json.dump(document, stream, indent=2, ensure_ascii=False, allow_nan=False)
    print(output, flush=True)
    print(json.dumps(document['comparison']['aggregates'], indent=2), flush=True)


if __name__ == '__main__':
    main()
