#!/usr/bin/env python3
"""Run against a private Net-SNMP process on loopback; never a production agent."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
agent = shutil.which('snmpd')
probe = shutil.which('snmpget')
if not agent or not probe:
    raise SystemExit('Install Net-SNMP snmpd and snmpget first')
with tempfile.TemporaryDirectory(prefix='snmp-interop-') as directory:
    work = Path(directory)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    config = work / 'snmpd.conf'
    config.write_text('rwcommunity interop 127.0.0.1\n')
    env = dict(os.environ, SNMP_PERSISTENT_DIR=str(work), SNMPCONFPATH=str(work), MIBS='')
    binary = work / 'interop'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-g',
                    '-I' + str(root / 'tests/native/stubs'), '-I' + str(root / 'src'),
                    str(root / 'tests/interop/main.cpp'), str(root / 'tests/native/stubs/Arduino.cpp'),
                    *map(str, sorted((root / 'src').glob('*.cpp'))), '-o', str(binary)], check=True)
    with (work / 'agent.log').open('w+') as log:
        daemon = subprocess.Popen([agent, '-f', '-Lo', '-r', '-C', '-c', str(config),
                                   '-p', str(work / 'agent.pid'), f'udp:127.0.0.1:{port}'],
                                  env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            for _ in range(30):
                if daemon.poll() is not None:
                    raise RuntimeError('Private snmpd exited before readiness')
                result = subprocess.run([probe, '-v2c', '-c', 'interop', '-t', '0.2', '-r', '0',
                                         f'127.0.0.1:{port}', '.1.3.6.1.2.1.1.3.0'],
                                        env=env, capture_output=True, timeout=3)
                if result.returncode == 0:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError('Private snmpd did not become ready')
            subprocess.run([str(binary), str(port)], env=env, check=True, timeout=60)
        except Exception:
            log.flush()
            log.seek(0)
            print(log.read()[-5000:])
            raise
        finally:
            daemon.terminate()
            try:
                daemon.wait(timeout=5)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.wait()
