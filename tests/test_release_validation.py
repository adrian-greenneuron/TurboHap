"""Check benchmark evidence, generated claims and package boundaries."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import subprocess

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from release_summary import validate, claims
from check_release import boundary, check_generated


class ReleaseValidation(unittest.TestCase):
    def data(self):return json.loads((ROOT/'docs/turbohap_benchmarks_111126.json').read_text())
    def test_qualified_package(self):
        d=self.data();validate(d,final=d['status']=='release');self.assertEqual(claims(d)['TurboHap_raw_file_checks'],360)
    def test_pending_blocks_release(self):
        d=self.data();d['status']='release'
        for item in d['datasets']:
            if item['id']=='n250000_m80000':item['gnu_status']='pending'
        removed={r['id'] for r in d['observations'] if r['dataset']=='n250000_m80000' and r['program']=='GNU FindHap'}
        d['observations']=[r for r in d['observations'] if r['id'] not in removed]
        d['validation']=[c for c in d['validation'] if c['id'] not in removed]
        with self.assertRaises(ValueError):validate(d,final=True)
    def test_bad_evidence(self):
        def duplicate(d):d['observations'].append(d['observations'][0])
        def missing(d):d['observations'].pop()
        def zero(d):d['observations'][0]['wall_seconds']=0
        def nan(d):d['observations'][0]['wall_seconds']=float('nan')
        def setting(d):d['observations'][0]['maxhap']=500000
        def failed(d):d['validation'][0]['files'][0]['passed']=False
        def exception(d):d['validation'][0]['files'][0].update(raw_identical=False,record_order_exception=True)
        for mutate in (duplicate,missing,zero,nan,setting,failed,exception):
            with self.subTest(mutation=mutate.__name__):
                d=self.data();mutate(d)
                with self.assertRaises(ValueError):validate(d)
    @unittest.skipUnless(importlib.util.find_spec("matplotlib") and importlib.util.find_spec("numpy"), "Reporting dependencies not installed")
    def test_stale_headline(self):
        import shutil
        with tempfile.TemporaryDirectory() as tmp:
            from render_benchmarks import GENERATED
            target=Path(tmp)
            for n in set(GENERATED) | {'docs/turbohap_benchmarks_111126.json','tools/render_benchmarks.py'}:
                (target/n).parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(ROOT/n,target/n)
            check_generated(target)
            p=target/'README.md'
            old=p.read_text(); expected=f"{claims(self.data())['max_speedup']:.1f}× faster"
            new=old.replace(expected,'999.9× faster',1)
            self.assertNotEqual(old,new)
            p.write_text(new)
            with self.assertRaisesRegex(ValueError,'Stale generated presentation: README.md'):check_generated(target)
    def test_even_allowlisted_experiment_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'experimental').mkdir();(root/'experimental/README.md').write_text('experiment')
            policy=dict(schema_version=1,release_version='1.5.0',files={'experimental/README.md':'user-documentation','PUBLIC_PACKAGE_FILES.json':'release-tooling'})
            (root/'PUBLIC_PACKAGE_FILES.json').write_text(json.dumps(policy))
            with self.assertRaisesRegex(ValueError,'Unsupported public path|Unpromoted'):boundary(root)
    def test_disabled_build_hook_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'CMakeLists.txt').write_text('option(FINDHAP_EXPERIMENTAL_SAFETY "disabled" OFF)\n')
            policy=dict(schema_version=1,release_version='1.5.0',files={'CMakeLists.txt':'build','PUBLIC_PACKAGE_FILES.json':'release-tooling'})
            (root/'PUBLIC_PACKAGE_FILES.json').write_text(json.dumps(policy))
            with self.assertRaisesRegex(ValueError,'Unpromoted build hook'):boundary(root)
    def test_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'README.md').symlink_to('/etc/hosts')
            with self.assertRaisesRegex(ValueError,'Symlink'):boundary(root)


if __name__=='__main__':
    if '--child' in sys.argv:
        sys.argv.remove('--child');unittest.main()
    else:
        for flags in ([],['-O']):subprocess.run([sys.executable,*flags,__file__,'--child'],check=True)
