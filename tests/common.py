"""Linux 整合測試共用工具；所有結果來自本次執行。"""
import argparse
import contextlib
import datetime
import json
import os
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]


class Tee:
    def __init__(self, *files):
        self.files = files

    def write(self, text):
        for file in self.files:
            file.write(text)
            file.flush()

    def flush(self):
        for file in self.files:
            file.flush()


def command(args, *, check=True, timeout=20, **kwargs):
    args = [str(arg) for arg in args]
    print('$', shlex.join(args), flush=True)
    result = subprocess.run(args, text=True, capture_output=True, timeout=timeout, **kwargs)
    if result.stdout:
        print(result.stdout, end='' if result.stdout.endswith('\n') else '\n')
    if result.stderr:
        print(result.stderr, end='' if result.stderr.endswith('\n') else '\n')
    print(f'exit={result.returncode}')
    if check and result.returncode:
        raise AssertionError(f'command failed: {args}, exit={result.returncode}')
    return result


def wait_for(predicate, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.02)
    raise AssertionError('condition not reached before deadline')


def child_pids(pid):
    try:
        return [int(x) for x in Path(f'/proc/{pid}/task/{pid}/children').read_text().split()]
    except FileNotFoundError:
        return []


def receive(sock):
    data = bytearray()
    while not data.endswith(b'\n'):
        part = sock.recv(1)
        if not part:
            break
        data.extend(part)
        if len(data) > 4096:
            raise AssertionError('response exceeded test limit')
    return data.decode('ascii')


def request(sock, payload):
    sock.sendall(payload)
    result = receive(sock)
    print(f'request={payload[:150]!r} bytes={len(payload)} response={result.rstrip()!r}')
    return result


def fields(response):
    return dict(item.split('=', 1) for item in response.split()[2:])


class Server:
    def __init__(self, out, evidence, *, level=2, timeout_ms=3000, port=None,
                 binary='server', env=None):
        self.out = ROOT / out
        self.evidence = evidence
        self.level = level
        self.timeout_ms = timeout_ms
        self.binary = binary
        self.env = env
        if port is None:
            with socket.socket() as temporary:
                temporary.bind(('127.0.0.1', 0))
                port = temporary.getsockname()[1]
        self.port = port
        self.process = None

    def __enter__(self):
        self.log_path = self.evidence / f'server-{self.port}-{time.monotonic_ns()}.log'
        self.log_file = self.log_path.open('w')
        args = [str(self.out / 'bin' / self.binary), '--port', str(self.port),
                '--log-level', str(self.level), '--timeout-ms', str(self.timeout_ms)]
        print('$', shlex.join(args), '2>', self.log_path.relative_to(ROOT))
        self.process = subprocess.Popen(args, stdout=self.log_file, stderr=self.log_file, env=self.env)
        try:
            deadline = time.monotonic() + 4
            while True:
                if self.process.poll() is not None:
                    raise AssertionError(f'server exited: {self.log_path.read_text()}')
                try:
                    with self.connect():
                        break
                except ConnectionRefusedError:
                    if time.monotonic() >= deadline:
                        raise AssertionError('server startup timed out')
                    time.sleep(.02)
            # 等待 readiness probe 建立的子程序完成回收。
            time.sleep(.05)
            wait_for(lambda: not child_pids(self.process.pid))
            print(f'server_parent={self.process.pid} port={self.port}')
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def connect(self):
        return socket.create_connection(('127.0.0.1', self.port), timeout=3)

    def client(self, *args, **kwargs):
        return command([self.out / 'bin/client', '--port', self.port, *args], **kwargs)

    def logs(self):
        return self.log_path.read_text()

    def __exit__(self, *exc):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
            try:
                self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                raise AssertionError('server shutdown exceeded deadline')
            finally:
                self.log_file.close()
            print(f'server_exit={self.process.returncode} log={self.log_path.relative_to(ROOT)}')
            if self.process.returncode != 0 and exc[0] is None:
                raise AssertionError('server did not exit cleanly')


def run_test(name, function, extra_arguments=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', default='build/protected-debug1')
    if extra_arguments:
        extra_arguments(parser)
    args = parser.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    evidence = ROOT / 'docs/evidence' / f'{stamp}-{name}'
    evidence.mkdir(parents=True)
    code = 0
    with (evidence / 'transcript.txt').open('w') as transcript:
        tee = Tee(sys.stdout, transcript)
        with contextlib.redirect_stdout(tee), contextlib.redirect_stderr(tee):
            print(f'test={name} utc={stamp} cwd={ROOT}')
            print('$', shlex.join(['python3', *sys.argv]))
            print('uname=', os.uname())
            try:
                function(args, evidence)
                status = 'PASS'
            except PermissionError as error:
                code, status = 77, 'BLOCKED'
                print(f'BLOCKED: {error}; execution environment denied operation')
            except Exception:
                code, status = 1, 'FAIL'
                traceback.print_exc()
            print(f'{status}: {name}')
    (evidence / 'result.json').write_text(json.dumps({'test': name, 'status': status,
                                                    'exit_code': code, 'utc': stamp}, indent=2) + '\n')
    print('evidence:', evidence.relative_to(ROOT))
    raise SystemExit(code)
