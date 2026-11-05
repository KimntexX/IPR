"""Compares two builds of iprRenderServer state by state, on the images of a base.

For refactorings that must not change a single pixel: both servers render every state,
and a state agrees only if its size and its bytes are the same. Writes one JSON proof.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analysis.render import RenderServer  # noqa: E402

RENDER_TIMEOUT_SECONDS = 600


def digest(rendered):
    pixels, metadata = rendered
    return dict(shape=list(pixels.shape), dtype=str(pixels.dtype),
                sha256=hashlib.sha256(pixels.tobytes()).hexdigest(),
                metadata_sha256=hashlib.sha256(json.dumps(metadata, sort_keys=True, default=str).encode()).hexdigest())


def compare_state(state, reference_render, candidate_render):
    """One state's verdict: renders that fail on both sides agree only if neither produced pixels."""
    entry = dict(scene=state["scene"], feature=state["feature"], value=state["value"])
    results = []
    for render in (reference_render, candidate_render):
        try:
            results.append(digest(render(state["source"], state["feature"], state["value"])))
        except ValueError as error:
            results.append(dict(error=str(error)))
    entry["reference"], entry["candidate"] = results
    entry["identical"] = results[0] == results[1] and "error" not in results[0]
    return entry


def summarize(entries):
    return dict(states=len(entries), identical=sum(e["identical"] for e in entries),
                different=[e for e in entries if not e["identical"]])


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, help="iprRenderServer before the change")
    parser.add_argument("--candidate", required=True, help="iprRenderServer after the change")
    parser.add_argument("--base", required=True, help="Folder of source images, read only")
    parser.add_argument("--group", default="all")
    parser.add_argument("--stride", type=int, default=1, help="Keep one state in this many, the list being long")
    parser.add_argument("--output", required=True, help="JSON proof; refused if it exists")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    output = Path(arguments.output)
    if output.exists():
        raise SystemExit(f"{output} already exists")
    if arguments.stride < 1:
        raise SystemExit("--stride must be at least 1")
    started = time.monotonic()
    entries = []
    with RenderServer(arguments.reference, timeout=RENDER_TIMEOUT_SECONDS) as reference, \
            RenderServer(arguments.candidate, timeout=RENDER_TIMEOUT_SECONDS) as candidate:
        listed = reference.states(arguments.base, arguments.group)
        if listed != candidate.states(arguments.base, arguments.group):
            raise SystemExit("The two servers do not list the same states")
        states = listed[::arguments.stride]
        for index, state in enumerate(states, 1):
            entries.append(compare_state(state, reference.render, candidate.render))
            verdict = "same" if entries[-1]["identical"] else "DIFFERENT"
            print(f"{index}/{len(states)} {state['scene']} {state['feature']} {state['value']}: {verdict}", flush=True)
        proof = dict(created_utc=datetime.now(timezone.utc).isoformat(), group=arguments.group,
                     listed_states=len(listed), stride=arguments.stride,
                     reference_sha256=reference.sha256, candidate_sha256=candidate.sha256,
                     scenes=sorted({state["scene"] for state in states}),
                     seconds=round(time.monotonic() - started, 1), summary=summarize(entries), entries=entries)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(proof, stream, indent=1)
    print(f"{proof['summary']['identical']}/{proof['summary']['states']} identical")
    return 0 if proof["summary"]["identical"] == proof["summary"]["states"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
