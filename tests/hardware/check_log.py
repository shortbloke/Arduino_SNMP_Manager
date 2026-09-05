#!/usr/bin/env python3
"""Validate a complete hardware run; do not infer success from a partial serial log."""
import argparse
from collections import defaultdict
from pathlib import Path
import re


def check(text):
    results = {}
    oids = defaultdict(list)
    samples = []
    cycle = None
    done = None
    for line in text.splitlines():
        if re.search(r'\bFAIL\b|wdt reset|Exception \(', line):
            raise ValueError('Failure/reset in serial log: ' + line)
        memory = re.fullmatch(r'MEM cycle=(\d+) stage=(\d+) free=(\d+) largest=(\d+) '
                              r'minSampledFree=(\d+) minSampledBlock=(\d+)', line)
        if memory:
            cycle, stage, free, block, _, _ = map(int, memory.groups())
            if not free or not block:
                raise ValueError('Zero available heap/block')
            samples.append((cycle, stage, free, block))
        result = re.fullmatch(r'RESULT cycle=(\d+) stage=(\d+) status=(.*)', line)
        if result:
            key = (int(result[1]), int(result[2]))
            if key in results or result[3] != 'Success':
                raise ValueError('Duplicate or unsuccessful stage: ' + line)
            results[key] = result[3]
        oid = re.fullmatch(r'OID version=([12]) (\.[0-9.]+)', line)
        if oid:
            if cycle is None:
                raise ValueError('OID before cycle context')
            oids[(cycle, int(oid[1]))].append(oid[2])
        ending = re.fullmatch(r'DONE cycles=(\d+) failures=(\d+); inspect MEM trend and OID sets', line)
        if ending:
            if done is not None or int(ending[2]):
                raise ValueError('Duplicate completion or reported failures')
            done = int(ending[1])
    if done != 50 or len(results) != done * 4:
        raise ValueError('Missing complete 50-cycle run')
    for number in range(done):
        if any((number, stage) not in results for stage in range(4)):
            raise ValueError('Missing stage')
        first, second = oids[(number, 1)], oids[(number, 2)]
        if not first or first != second:
            raise ValueError('GETNEXT/GETBULK OID sets differ')
        numeric = [tuple(map(int, oid.lstrip('.').split('.'))) for oid in first]
        if any(a >= b for a, b in zip(numeric, numeric[1:])):
            raise ValueError('Non-increasing OIDs')
        if any(not any(c == number and s == stage for c, s, _, _ in samples)
               for stage in range(4)):
            raise ValueError('Missing per-stage memory measurements')
    return {'cycles': done, 'stages': len(results), 'minimum_sampled_free_heap': min(s[2] for s in samples),
            'minimum_sampled_largest_block': min(s[3] for s in samples),
            'first_cycle_end_heap': next(s[2] for s in reversed(samples) if s[0] == 0 and s[1] == 3),
            'last_cycle_end_heap': next(s[2] for s in reversed(samples) if s[0] == done - 1 and s[1] == 3)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        print(check(args.log.read_text(errors='replace')))
    except ValueError as error:
        parser.exit(1, str(error) + '\n')
