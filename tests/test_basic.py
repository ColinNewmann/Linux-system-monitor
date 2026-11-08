import signal
import socket
import subprocess
import time
from common import ROOT, Server, command, fields, receive, request, run_test, wait_for


def test(args, evidence):
    command(['ldd', ROOT / args.out / 'bin/client'])
    command(['ldd', ROOT / args.out / 'bin/server'])
    with Server(args.out, evidence) as server:
        for name in ['GET_STATUS', 'GET_CPU', 'GET_MEM', 'GET_UPTIME']:
            result = server.client('--command', name, '--log-level', '2')
            assert result.stdout.startswith('OK ')
            values = fields(result.stdout)
            if name == 'GET_CPU':
                assert 0 <= float(values['usage_pct']) <= 100
                assert int(values['sample_ms']) >= 190
            if name == 'GET_MEM':
                proc = dict((line.split(':')[0], int(line.split()[1]))
                            for line in open('/proc/meminfo') if line.startswith(('MemTotal:', 'MemAvailable:')))
                total, available, used = (int(values[key]) for key in ['total_kb', 'available_kb', 'used_kb'])
                assert total == proc['MemTotal'] and 0 <= available <= total
                assert used == total - available
                assert abs(float(values['used_pct']) - 100 * used / total) < .02
                print(f'/proc comparison total={total}; available_snapshot={proc["MemAvailable"]}; response={available}')
                assert abs(available - proc['MemAvailable']) < max(total * .1, 1024)
            if name == 'GET_UPTIME':
                proc_uptime = float(open('/proc/uptime').read().split()[0])
                assert abs(float(values['seconds']) - proc_uptime) < 2
        with server.connect() as sock:
            sock.sendall(b'GET_')
            time.sleep(.05)
            assert request(sock, b'MEM\n').startswith('OK MEM ')
            sock.sendall(b'GET_UPTIME\nGET_MEM\n')
            responses = [receive(sock), receive(sock)]
            print('pipelined:', responses)
            assert responses[0].startswith('OK UPTIME ') and responses[1].startswith('OK MEM ')
            for invalid in [b'GET_MEM junk\n', b'\n', b'get_mem\n']:
                assert request(sock, invalid) == 'ERR INVALID_REQUEST\n'
        for invalid in [b'GET_MEM\x00\n', b'GET_MEM\r\n', b'GET_MEM\x01\n', b'A' * 128 + b'\n']:
            with server.connect() as sock:
                assert request(sock, invalid).startswith('ERR ')
        # 邊界：127 字元加換行合法入框，128 字元加換行超限。
        with server.connect() as sock:
            assert request(sock, b'A' * 127 + b'\n') == 'ERR INVALID_REQUEST\n'
        with server.connect() as sock:
            sock.sendall(b'GET_MEM')
            sock.shutdown(socket.SHUT_WR)
            assert receive(sock) == 'ERR INVALID_REQUEST\n'
        server.client(input='1\n2\n3\n4\n6\n')
        result = server.client('--monitor', '--count', '3', '--interval-ms', '300')
        assert result.stdout.count('OK STATUS') == 3
        path = evidence / 'ctrl-c-client.stdout'
        with path.open('w') as output:
            argv = [str(ROOT / args.out / 'bin/client'), '--port', str(server.port), '--monitor', '--interval-ms', '300']
            print('$', ' '.join(argv), '> ', path.name)
            process = subprocess.Popen(argv, stdout=output, stderr=output)
            try:
                wait_for(lambda: 'OK STATUS' in path.read_text())
                process.send_signal(signal.SIGINT)
                assert process.wait(timeout=2) == 0
            finally:
                if process.poll() is None:
                    process.kill(); process.wait()
        print(path.read_text())
        invalid = server.client('--command', 'BAD', check=False)
        assert invalid.returncode == 1 and 'ERR INVALID_REQUEST' in invalid.stdout
    for program in ['client', 'server']:
        for option in [['--port', '0'], ['--log-level', '3'], ['--timeout-ms', 'abc']]:
            assert command([ROOT / args.out / 'bin' / program, *option], check=False).returncode == 2


if __name__ == '__main__':
    run_test('basic', test)
