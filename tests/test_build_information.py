import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--engine', type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='turbohap-build-info-') as temporary:
    root = Path(temporary)
    version = subprocess.check_output([str(args.binary.resolve()), '--version'], cwd=root, text=True)
    info = json.loads(subprocess.check_output([str(args.binary.resolve()), '--build-info'], cwd=root))
    if not version.startswith('TurboHap ') or info['version'] not in version:
        raise RuntimeError('Version interfaces disagree')
    if not re.fullmatch('[0-9a-f]{64}', info['source_sha256']):
        raise RuntimeError('Missing exact source identity')
    if not info['compiler'] or not info['openmp_libraries'] or not info['compile_flags']:
        raise RuntimeError('Missing build settings')
    if info['executable_sha256'] != hashlib.sha256(args.engine.read_bytes()).hexdigest():
        raise RuntimeError('Metadata does not identify the built inference executable')
    if list(root.iterdir()):
        raise RuntimeError('Metadata query created files')
print('PASS version and build metadata without inference inputs')
