from common import ROOT, command, run_test


def test(args, evidence):
    command([ROOT / args.out / 'bin/io_probe'])


if __name__ == '__main__':
    run_test('io', test)
