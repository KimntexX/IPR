"""Render a read-only, dependency-free vertical Git network as local HTML/SVG."""
import argparse
from datetime import datetime, timezone
from html import escape
from pathlib import Path
import subprocess
import webbrowser


COLOURS = ['#67b7ff', '#78dba9', '#ffcb75', '#d9a1ff', '#ff8f9a', '#70d9e4', '#c6d475']


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], encoding='utf-8')


def read_history(root):
    refs, labels, tags = {}, {}, set()
    rows = git(root, 'for-each-ref', '--format=%(refname)%09%(objectname)%09%(*objectname)%09%(symref)',
               'refs/heads', 'refs/remotes/origin', 'refs/tags')
    for row in rows.splitlines():
        name, sha, peeled, symbolic = row.split('\t')
        if symbolic:
            continue
        sha = peeled or sha
        if name.startswith('refs/tags/'):
            tags.add(sha)
            label = name.removeprefix('refs/tags/')
        else:
            label = name.removeprefix('refs/heads/').removeprefix('refs/remotes/')
            refs[label] = sha
        labels.setdefault(sha, []).append(label)
    if not refs:
        raise ValueError('No local or origin branches found in this repository.')
    commits = {}
    # Explicit ref namespaces exclude stash and other worktrees' transient HEADs.
    history = git(root, 'log', '--branches', '--remotes=origin', '--tags', '--topo-order', '--reverse',
                  '--format=%H%x09%P%x09%cs%x09%s')
    for row in history.splitlines():
        sha, parents, date, subject = row.split('\t', 3)
        commits[sha] = {'parents': parents.split(), 'date': date, 'subject': subject}
    # Tags pointing at trees or blobs have no graph node.
    labels = {sha: names for sha, names in labels.items() if sha in commits}
    return commits, refs, labels, tags


def layout(commits, refs, labels, tags, full=False):
    def priority(name):
        base = name.removeprefix('origin/')
        return (0 if base == 'main' else 1 if base == 'dev' else 2, base)

    names = sorted({name.removeprefix('origin/') for name in refs}, key=priority)
    owners = {}
    # Reserve integration first-parent lines before attributing feature histories.
    for name in names:
        for ref in (name, 'origin/' + name):
            sha = refs.get(ref)
            while sha in commits and sha not in owners:
                owners[sha] = name
                parents = commits[sha]['parents']
                sha = parents[0] if parents else None
    # Histories reachable only through a deleted branch still have a visible lane.
    if any(sha not in owners for sha in commits):
        names.append('other history')
    for sha in commits:
        owners.setdefault(sha, 'other history')
    children = {}
    for node in commits.values():
        for parent in node['parents']:
            children[parent] = children.get(parent, 0) + 1
    selected = {sha for sha, node in commits.items()
                if full or sha in labels or sha in tags or len(node['parents']) != 1
                or children.get(sha, 0) > 1}
    # Propagate nearest retained ancestors in topological order. Each edge follows
    # real Git parent links; omitted linear commits never create fake merges.
    nearest, edges = {}, []
    for sha, node in commits.items():
        ancestors = set()
        for parent in node['parents']:
            ancestors.update(nearest.get(parent, set()))
        if sha in selected:
            edges.extend((parent, sha) for parent in sorted(ancestors))
            nearest[sha] = {sha}
        else:
            nearest[sha] = ancestors
    nodes = [sha for sha in commits if sha in selected]
    return names, owners, nodes, edges


def render(root, full=False):
    commits, refs, labels, tags = read_history(root)
    names, owners, nodes, edges = layout(commits, refs, labels, tags, full)
    lane_width, row_height = 260, 82
    width, height = max(820, len(names) * lane_width + 40), len(nodes) * row_height + 100
    positions = {sha: (45 + names.index(owners[sha]) * lane_width, 100 + row * row_height)
                 for row, sha in enumerate(nodes)}
    colours = {name: COLOURS[i % len(COLOURS)] for i, name in enumerate(names)}
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" role="img" aria-label="Vertical Git branch network">',
           '<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#e5edf5} '
           '.node:hover circle{stroke:white;stroke-width:4}.node:hover text{fill:white}</style>',
           '<rect width="100%" height="100%" fill="#151b23"/>']
    for i, name in enumerate(names):
        x = 45 + i * lane_width
        svg += [f'<path d="M{x} 66 V{height - 20}" stroke="{colours[name]}" opacity=".12"/>',
                f'<text x="{x - 15}" y="32" font-size="14" font-weight="bold">{escape(name)}</text>']
    for parent, child in edges:
        x1, y1 = positions[parent]
        x2, y2 = positions[child]
        mid = (y1 + y2) / 2
        svg.append(f'<path d="M{x1} {y1} C{x1} {mid},{x2} {mid},{x2} {y2}" '
                   f'fill="none" stroke="{colours[owners[child]]}" stroke-width="2" opacity=".6"/>')
    for sha in nodes:
        node = commits[sha]
        x, y = positions[sha]
        badges = ', '.join(labels.get(sha, []))
        title = f"{sha}\n{node['date']} — {node['subject']}\n{badges}"
        kind = 'MERGE' if len(node['parents']) > 1 else 'COMMIT'
        lines = [badges, f"{sha[:7]} · {node['date']} · {kind}", node['subject']]
        svg += [f'<g class="node"><title>{escape(title)}</title>',
                f'<rect x="{x + 11}" y="{y - 23}" width="232" height="65" rx="4" fill="#151b23" opacity=".94"/>',
                f'<circle cx="{x}" cy="{y}" r="{7 if sha in tags else 5}" '
                f'fill="{colours[owners[sha]]}" stroke="#151b23" stroke-width="2"/>']
        for index, line in enumerate(lines):
            shown = line if len(line) <= 33 else line[:32] + '…'
            svg.append(f'<text x="{x + 16}" y="{y - 10 + index * 18}" '
                       f'font-size="{12 if index == 0 else 11}">{escape(shown)}</text>')
        svg.append('</g>')
    svg.append('</svg>')
    image = '\n'.join(svg)
    mode = 'Tous les commits' if full else 'Versions, fusions, bifurcations et extrémités de branches'
    generated = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')
    html = f'''<!doctype html>
<html lang="fr"><meta charset="utf-8"><title>IPR — réseau Git vertical</title>
<style>
body{{margin:0;background:#151b23;color:#e5edf5;font:14px Segoe UI,Arial,sans-serif}}
header{{position:sticky;top:0;z-index:1;background:#222c38;padding:16px 24px;border-bottom:1px solid #465364}}
h1{{font-size:21px;margin:0 0 8px}}p{{margin:6px 0}}label{{display:inline-flex;align-items:center;gap:12px}}
main{{overflow:auto}}svg{{display:block}}small{{color:#bac7d5}}
</style><header><h1>IPR — réseau Git vertical</h1>
<p>{escape(mode)} · {len(nodes)} nœuds affichés sur {len(commits)} commits · {len(names)} branches/pistes.</p>
<p>Ancien en haut → récent en bas. Survolez un nœud pour lire son message complet et ses références.</p>
<label>Zoom <input id="zoom" type="range" min="30" max="160" value="85"><output id="value">85 %</output></label>
<p><small>Instantané local · {generated}. Les références origin sont celles du dernier fetch, sans accès réseau automatique.
Les fichiers non commités ne sont pas représentés. Les couleurs identifient les pistes, pas l’état des CI.
Les lignes suivent la parenté Git ; en vue synthétique, elles peuvent regrouper des commits intermédiaires.</small></p>
</header><main>{image}</main><script>
const zoom = document.getElementById('zoom'), svg = document.querySelector('svg');
function resize() {{const scale = Number(zoom.value) / 100;
svg.style.width = ({width} * scale) + 'px'; svg.style.height = ({height} * scale) + 'px';
document.getElementById('value').textContent = zoom.value + ' %';}}
zoom.addEventListener('input', resize); resize();
</script></html>'''
    return html, image


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path, help='Output directory (default: <repo>/build-git-network)')
    parser.add_argument('--full', action='store_true', help='Show every commit instead of milestones only')
    parser.add_argument('--open', action='store_true', help='Open the local HTML in the default browser')
    args = parser.parse_args(argv)
    try:
        html, svg = render(args.repo, args.full)
    except (subprocess.CalledProcessError, ValueError) as error:
        parser.exit(1, f'Cannot read Git history: {error}\n')
    output = args.output or args.repo / 'build-git-network'
    output.mkdir(parents=True, exist_ok=True)
    page = output / 'network.html'
    page.write_text(html, encoding='utf-8')
    (output / 'network.svg').write_text(svg, encoding='utf-8')
    print(page.resolve())
    if args.open:
        webbrowser.open(page.resolve().as_uri())


if __name__ == '__main__':
    main()
