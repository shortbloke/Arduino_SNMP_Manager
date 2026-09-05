import unittest
from check_log import check


def good_log():
    lines = []
    for cycle in range(50):
        for stage in range(4):
            lines += [f'MEM cycle={cycle} stage={stage} free=20000 largest=19000 '
                      'minSampledFree=18000 minSampledBlock=17000']
            if stage >= 2:
                lines += [f'OID version={stage - 1} .1.3.6.1.2.1.1.1.0']
            lines += [f'RESULT cycle={cycle} stage={stage} status=Success']
    lines += ['DONE cycles=50 failures=0; inspect MEM trend and OID sets']
    return '\n'.join(lines)


class LogChecks(unittest.TestCase):
    def test_complete(self):
        result = check(good_log())
        self.assertEqual(result['cycles'], 50)
        self.assertEqual(result['minimum_sampled_free_heap'], 20000)

    def test_incomplete_or_failed(self):
        valid = good_log()
        for log in [valid.rsplit('\n', 1)[0], valid.replace('failures=0', 'failures=1'),
                    valid.replace('status=Success', 'status=Timeout', 1),
                    valid + '\nSoft WDT reset\nwdt reset',
                    valid + '\nRESULT cycle=0 stage=0 status=Success',
                    valid.replace('OID version=2 .1.3.6.1.2.1.1.1.0',
                                  'OID version=2 .1.3.6.1.2.1.1.2.0', 1),
                    '\n'.join(line for line in valid.splitlines() if not line.startswith('MEM cycle=1 stage=1 '))]:
            with self.subTest(log=log[-100:]):
                with self.assertRaises(ValueError):
                    check(log)


if __name__ == '__main__':
    unittest.main()
