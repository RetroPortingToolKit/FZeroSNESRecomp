"""Record the packs explicitly supplied by a build, never user imports.

Ownership belongs to staging, outside the editable course manifests. The UI
labels these packs as included and the importer reserves their IDs.
"""
import argparse
import json
from pathlib import Path

BUNDLED_PACK_IDS = ('bower-league', 'cgp', 'max-league', 'astra-velocity')


def write_index(mods, packs):
    entries = []
    for pack in sorted(map(Path, packs)):
        descriptor = json.loads((pack / 'pack.json').read_text(encoding='utf-8'))
        entries.append({'id': descriptor['id'], 'title': descriptor['title']})
    if len({p['id'] for p in entries}) != len(entries):
        raise ValueError('Duplicate included pack IDs')
    mods = Path(mods)
    mods.mkdir(parents=True, exist_ok=True)
    (mods / '.bundled-packs.json').write_text(
        json.dumps({'format': 1, 'packs': entries}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--mods', type=Path, required=True)
    args = parser.parse_args()
    write_index(args.mods, [p.parent for p in args.source.glob('*/pack.json')])
