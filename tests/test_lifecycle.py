from pathlib import Path
import time
from common import Server, child_pids, request, run_test, wait_for


def test(args, evidence):
    with Server(args.out, evidence) as server:
        parent = server.process.pid
        before = len(list(Path(f'/proc/{parent}/fd').iterdir()))
        for _ in range(30):
            with server.connect() as sock:
                assert request(sock, b'GET_MEM\n').startswith('OK MEM ')
        wait_for(lambda: not child_pids(parent))
        after = len(list(Path(f'/proc/{parent}/fd').iterdir()))
        print(f'parent={parent} fd_before={before} fd_after={after} children={child_pids(parent)}')
        assert after == before
        held = [server.connect() for _ in range(10)]
        try:
            pids = wait_for(lambda: child_pids(parent) if len(child_pids(parent)) == 10 else None)
            print('shutdown with held children:', pids)
            # Child 只保留 stdin/out/err、accepted socket；不保留 listener／self-pipe。
            for pid in pids:
                fds = {p.name: str(p.readlink()) for p in Path(f'/proc/{pid}/fd').iterdir()}
                print(f'child={pid} descriptors={fds}')
                assert len(fds) == 4
            server.process.terminate()
            assert server.process.wait(timeout=3) == 0
            assert all(not Path(f'/proc/{pid}').exists() for pid in pids)
        finally:
            for sock in held:
                sock.close()
        port = server.port
    with Server(args.out, evidence, port=port) as restarted:
        restarted.client('--command', 'GET_STATUS')
        print(f'restarted successfully on same port={port}')


if __name__ == '__main__':
    run_test('lifecycle', test)
