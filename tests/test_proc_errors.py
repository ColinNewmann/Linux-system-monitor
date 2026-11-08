import os
from common import Server, request, run_test


def test(args, evidence):
    for path, failed, healthy in [('/proc/stat', 'GET_CPU', 'GET_MEM'),
                                 ('/proc/meminfo', 'GET_MEM', 'GET_UPTIME'),
                                 ('/proc/uptime', 'GET_UPTIME', 'GET_MEM')]:
        print(f'fault injection: fopen({path!r}) returns EACCES in test binary only')
        env = dict(os.environ, MONITOR_FAIL_PROC=path)
        with Server(args.out, evidence, binary='server-fault', env=env) as server:
            with server.connect() as sock:
                assert request(sock, (failed + '\n').encode()) == 'ERR SYSTEM_INFO\n'
                assert request(sock, b'GET_STATUS\n') == 'ERR SYSTEM_INFO\n'
                assert request(sock, (healthy + '\n').encode()).startswith('OK ')


if __name__ == '__main__':
    run_test('proc-errors', test)
