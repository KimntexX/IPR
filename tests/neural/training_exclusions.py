"""Keep initializer ancestry excluded even when the active stage is a prefix."""


def training_truth_hashes(data):
    events = data.get('texture_augmentation', {}).get('events', [])
    ancestors = data.get('lineage_exclusions', {}).get('target_sha256', [])
    if not isinstance(events, list) or not isinstance(ancestors, list):
        raise ValueError('Declared training/ancestor manifests must be lists')
    hashes = [event['target_sha256'] for event in events]+ancestors
    if any(not isinstance(h, str) or not h for h in hashes):
        raise ValueError('Nonempty training truth identities required')
    return set(hashes)
