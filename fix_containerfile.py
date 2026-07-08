#!/usr/bin/env python3
import os, sys
for f in ['Containerfile', 'Containerfile.dev']:
    path = os.path.join(os.path.dirname(__file__), f)
    with open(path, 'rb') as fh:
        raw = fh.read()
    old = b'ubuntu@sha256:4aaf0b273f92a76e458efc72cef4893c2c54ae2f1451d07112f1ce79f3ac0487'
    new = b'docker.io/library/ubuntu:26.04'
    if old in raw:
        raw = raw.replace(old, new)
        with open(path, 'wb') as fh:
            fh.write(raw)
        print(f'{f}: fixed')
    else:
        print(f'{f}: pattern not found')
        # check what we have
        idx = raw.find(b'ubuntu@')
        if idx >= 0:
            print(f'  found ubuntu@ at {idx}: {raw[idx:idx+80]}')
