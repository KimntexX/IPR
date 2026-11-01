"""Read-only documentation checks; no translation, repair or PDF rebuild."""
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r'\[[^\]\n]*\]\((<[^>]+>|[^)\s]+)(?:\s+"[^"]*")?\)')


def check(root=ROOT):
    errors, warnings = [], []
    paths = list((root / 'docs').rglob('*')) + list(root.glob('README*.md')) + [root / 'AGENTS.md']
    for path in paths:
        if not path.is_file() or path.suffix not in ('.md', '.tex', '.bib', '.json'):
            continue
        if any(part in ('build', '__pycache__') for part in path.relative_to(root).parts):
            continue
        try:
            text = path.read_text(encoding='utf-8-sig')
            if path.suffix == '.json':
                json.loads(text)
            if '\x00' in text:
                raise ValueError('NUL character in text document')
        except (UnicodeError, ValueError) as error:
            errors.append(f'{path.relative_to(root)}: {error}')
            continue
        if path.suffix == '.md':
            for link in LINK.findall(text):
                target = unquote(link.strip('<>').split('#', 1)[0])
                if not target or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', target):
                    continue
                if not (path.parent / target).exists():
                    warnings.append(f'{path.relative_to(root)} -> {target}')
    for warning in warnings:
        print('Missing local link (documentary debt):', warning)
    for error in errors:
        print(error, file=sys.stderr)
    print(f'Documentation: {len(errors)} errors, {len(warnings)} link warnings. '
          'No heading/external link check, translation or LaTeX compilation.')
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(check())
