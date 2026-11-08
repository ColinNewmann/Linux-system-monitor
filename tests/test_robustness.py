import signal
import socket
import struct
import time
from common import ROOT, Server, child_pids, command, receive, request, run_test, wait_for


def test(args, evidence):
    for variant, out in [('baseline', args.baseline), ('protected', args.out)]:
        print(f'=== {variant}: Test A invalid and oversized ===')
        with Server(out, evidence, timeout_ms=400) as server:
            for payload in [b'GET_MEM garbage\n', b'GET_MEM' + b'A' * 200 + b'\n']:
                with server.connect() as sock:
                    result = request(sock, payload)
                    assert result.startswith('OK MEM ' if variant == 'baseline' else 'ERR ')
            print(f'=== {variant}: Test B silent client ===')
            with server.connect() as sock:
                wait_for(lambda: len(child_pids(server.process.pid)) == 1)
                start = time.monotonic()
                time.sleep(.8)
                pids = child_pids(server.process.pid)
                print(f'observed_s={time.monotonic() - start:.3f} remaining_children={pids}')
                if variant == 'baseline':
                    assert len(pids) == 1
                else:
                    assert not pids
                    assert receive(sock) == 'ERR TIMEOUT\n'
                    assert sock.recv(1) == b''
            wait_for(lambda: not child_pids(server.process.pid))
            if variant == 'protected':
                print('=== protected: slow partial request cannot reset deadline ===')
                with server.connect() as sock:
                    sock.sendall(b'G'); time.sleep(.25); sock.sendall(b'E'); time.sleep(.3)
                    assert receive(sock) == 'ERR TIMEOUT\n'
                wait_for(lambda: not child_pids(server.process.pid))
            print(f'=== {variant}: Test C deterministic shared send path ===')
            probe = command([ROOT / out / 'bin/disconnect_probe'], check=False)
            assert probe.returncode == (-signal.SIGPIPE if variant == 'baseline' else 0)
            print(f'=== {variant}: Test C TCP reset integration ===')
            start_count = server.logs().count('request_start')
            with server.connect() as sock:
                sock.sendall(b'GET_CPU\n')
                wait_for(lambda: server.logs().count('request_start') > start_count)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            wait_for(lambda: not child_pids(server.process.pid))
            print('TCP reset server log:\n' + server.logs())
            assert server.process.poll() is None
            server.client('--command', 'GET_MEM')


if __name__ == '__main__':
    run_test('robustness', test, lambda parser: parser.add_argument('--baseline', default='build/baseline-debug1'))
