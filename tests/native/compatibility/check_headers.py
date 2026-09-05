"""Check standalone headers and the link-time configuration contract."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
compiler = shlex.split(os.environ.get('CXX', 'c++'))
flags = ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
         '-fno-rtti', '-I' + str(root / 'src'), '-I' + str(root / 'tests/native/stubs')]


def run(*args):
    subprocess.run(compiler + flags + list(args), check=True)


with tempfile.TemporaryDirectory(prefix='snmp-headers-') as directory:
    work = Path(directory)
    client = work / 'client.cpp'
    for header in sorted((root / 'src').glob('*.h')):
        client.write_text('#include "' + header.name + '"\n')
        run('-fsyntax-only', str(client))

    client.write_text('#include "SNMPConfig.h"\nint main() {}\n')
    config = str(root / 'src/SNMPConfig.cpp')
    run('-c', config, '-o', str(work / 'default.o'))
    for macro, value in [('SNMP_PACKET_LENGTH', '768'), ('SNMP_OCTETSTRING_MAX_LENGTH', '512'),
                         ('MAX_OID_LENGTH', '192'), ('SNMP_MAX_PENDING_REQUESTS', '2'),
                         ('DEBUG', '1'), ('DEBUG_BER', '1'), ('SUPPRESS_ERROR_FAILED_PARSE', '1')]:
        define = '-D' + macro + '=' + value
        run(define, '-c', str(client), '-o', str(work / 'client.o'))
        result = subprocess.run(compiler + [str(work / 'client.o'), str(work / 'default.o'),
                                           '-o', str(work / 'mismatch')], capture_output=True, text=True)
        if result.returncode == 0 or 'BuildConfiguration' not in result.stderr:
            raise RuntimeError('Configuration mismatch was not rejected: ' + macro + '\n' + result.stderr)
        run(define, str(client), config, '-o', str(work / 'matching'))
        subprocess.run([str(work / 'matching')], check=True)

    (work / 'settings.h').write_text('#define SNMP_PACKET_LENGTH 768\n#define DEBUG_BER\n')
    run('-I' + str(work), '-DSNMP_CONFIG_HEADER="settings.h"', str(client), config,
        '-o', str(work / 'shared-header'))
    subprocess.run([str(work / 'shared-header')], check=True)
print('Standalone headers and matching/mismatched configurations passed')
