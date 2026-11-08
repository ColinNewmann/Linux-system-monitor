from common import Server, run_test, wait_for


def test(args, evidence):
    for debug in [0, 1]:
        for level in [0, 1, 2]:
            print(f'=== compile DEBUG={debug}, server runtime={level} ===')
            with Server(f'build/protected-debug{debug}', evidence, level=level) as server:
                # Client 與 Server 的層級獨立，逐一驗證每種組合。
                for client_level in [0, 1, 2]:
                    result = server.client('--command', 'GET_MEM', '--log-level', str(client_level))
                    assert (' INFO ' in result.stderr) == (client_level >= 1)
                    assert (' DEBUG ' in result.stderr) == (debug == 1 and client_level == 2)
                    error = server.client('--command', 'BAD', '--log-level', str(client_level), check=False)
                    assert error.returncode == 1 and ' ERROR ' in error.stderr
                wait_for(lambda: 'invalid_command' in server.logs())
            logs = server.logs()
            print(logs)
            assert ' ERROR ' in logs
            assert (' INFO ' in logs) == (level >= 1)
            assert (' DEBUG ' in logs) == (debug == 1 and level == 2)


if __name__ == '__main__':
    run_test('logging', test)
