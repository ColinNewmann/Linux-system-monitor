"""保留 clean build 與完整無特權測試的總紀錄。"""
import hashlib
import json
from common import ROOT, command, run_test


def test(args, evidence):
    sources = [ROOT / 'Makefile']
    for directory in ['client', 'server', 'shared', 'tests']:
        sources.extend(path for path in (ROOT / directory).rglob('*')
                       if path.suffix in {'.c', '.h', '.py', '.sh'})
    hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in sorted(sources)}
    (evidence / 'source-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    command(['gcc', '--version'])
    command(['make', '--version'])
    command(['make', 'clean'])
    build = command(['make', '-j2'])
    assert 'warning:' not in build.stderr
    results = command(['make', 'test-all'], timeout=120)
    assert 'warning:' not in results.stderr
    command(['gdb', '--nx', '--batch', '-ex', 'break main', '-ex', 'run', '-ex', 'bt',
             '-ex', 'continue', '--args', ROOT / 'build/protected-debug1/bin/client', '--help'])


if __name__ == '__main__':
    run_test('verification', test)
