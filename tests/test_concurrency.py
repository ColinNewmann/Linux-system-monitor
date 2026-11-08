from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import threading
import time
from common import Server, child_pids, receive, request, run_test, wait_for


def test(args, evidence):
    with Server(args.out, evidence, timeout_ms=10000) as server:
        sockets = []
        try:
            sockets = [server.connect() for _ in range(10)]
            pids = wait_for(lambda: child_pids(server.process.pid) if len(child_pids(server.process.pid)) == 10 else None)
            print(f'10 simultaneous connections: parent={server.process.pid} child_pids={pids}')
            for pid in pids:
                status = Path(f'/proc/{pid}/status').read_text()
                assert f'PPid:\t{server.process.pid}\n' in status
                assert not re.search(r'^State:\s+Z', status, re.M)
            barrier = threading.Barrier(10)

            def worker(index):
                sock = sockets[index]
                barrier.wait(timeout=4)
                start = time.monotonic()
                sock.sendall(b'GET_CPU\n')
                response = receive(sock)
                end = time.monotonic()
                assert response.startswith('OK CPU '), response
                return {'client': index, 'start': start, 'end': end, 'response': response.strip()}

            with ThreadPoolExecutor(max_workers=10) as executor:
                rows = list(executor.map(worker, range(10)))
            overlap = min(row['end'] for row in rows) - max(row['start'] for row in rows)
            concurrent = max(row['end'] for row in rows) - min(row['start'] for row in rows)
            print(json.dumps(rows, indent=2))
            assert overlap > .1, f'10 client intervals did not overlap sufficiently: {overlap}'
            wait_for(lambda: server.logs().count('request_end') >= 10)
            logs = server.logs()
            starts, ends = {}, {}
            for line in logs.splitlines():
                pid = re.search(r'pid=(\d+)', line)
                event = re.search(r'request_(start|end) mono_ms=(\d+)', line)
                if pid and event and int(pid[1]) in pids:
                    (starts if event[1] == 'start' else ends)[int(pid[1])] = int(event[2])
            assert len(starts) == len(ends) == 10, 'DEBUG=1 required for server interval evidence'
            server_overlap_ms = min(ends.values()) - max(starts.values())
            assert server_overlap_ms > 100
        finally:
            for sock in sockets:
                sock.close()
        wait_for(lambda: not child_pids(server.process.pid))
        sequential_start = time.monotonic()
        with server.connect() as sock:
            for _ in range(10):
                assert request(sock, b'GET_CPU\n').startswith('OK CPU ')
        sequential = time.monotonic() - sequential_start
        print(f'concurrent_s={concurrent:.6f} sequential_s={sequential:.6f} '
              f'client_overlap_s={overlap:.6f} server_overlap_ms={server_overlap_ms}')
        assert concurrent < sequential * .7
        (evidence / 'timings.json').write_text(json.dumps({'clients': rows, 'pids': pids,
            'concurrent_s': concurrent, 'sequential_s': sequential,
            'server_starts_ms': starts, 'server_ends_ms': ends,
            'server_overlap_ms': server_overlap_ms}, indent=2) + '\n')
        wait_for(lambda: not child_pids(server.process.pid))
        print('all children reaped; parent still running:', server.process.poll() is None)


if __name__ == '__main__':
    run_test('concurrency', test)
