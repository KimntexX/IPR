"""Check an extracted native package without modifying user preferences."""
import argparse
from pathlib import Path
from ci import assert_public, runtime_environment, smoke

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('--mode', choices=('system', 'standalone'), required=True)
args = parser.parse_args()
assert_public(args.directory)
print(smoke(args.directory.resolve(), args.mode, runtime_environment()))
