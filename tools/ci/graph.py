# Resolves product prerequisites, affected consumers, and diagnostic paths without scheduling jobs.
# SPDX-License-Identifier: Apache-2.0

from collections import defaultdict, deque
from dataclasses import dataclass
from fnmatch import fnmatchcase
from functools import cache
import hashlib
import os
import subprocess


@dataclass(frozen=True)
class Product:
    inputs: tuple = ()
    requires: tuple = ()
    excludes: tuple = ()
    covers: tuple = ()  # Test policy: changes select this check, not a build prerequisite.
    after: tuple = ()   # Scheduling prerequisite; a change alone does not invalidate this product.


class Graph:
    def __init__(self):
        self.nodes = {}
        self.enrolled = set()

    def add(self, key, *, inputs=(), requires=(), excludes=(), covers=(), after=(), ci=False):
        if key in self.nodes:
            raise ValueError(f'duplicate product: {key}')
        self.nodes[key] = Product(tuple(inputs), tuple(requires), tuple(excludes), tuple(covers), tuple(after))
        if ci:
            self.enrolled.add(key)
        return key

    def finish(self):
        self.users = defaultdict(set)
        self.patterns = defaultdict(set)
        for key, node in self.nodes.items():
            for dep in (*node.requires, *node.after):
                if dep not in self.nodes:
                    raise ValueError(f'{key} requires missing product {dep}')
            for dep in node.requires:
                self.users[dep].add(key)
            for pat in (*node.inputs, *node.covers):
                self.patterns[pat].add(key)
        active = set()
        @cache
        def visit(key):
            if key in active:
                raise ValueError(f'cycle at {key}')
            active.add(key)
            for dep in (*self.nodes[key].requires, *self.nodes[key].after):
                visit(dep)
            active.remove(key)
        for key in self.nodes:
            visit(key)
        return self

    def affected(self, path):
        """Return predecessor links for one shortest explanation per product."""
        hits = {k for pat, keys in self.patterns.items() if fnmatchcase(path, pat)
                for k in keys if not any(fnmatchcase(path, e) for e in self.nodes[k].excludes)}
        parents = dict.fromkeys(sorted(hits))
        queue = deque(parents)
        while queue:
            key = queue.popleft()
            for user in sorted(self.users[key] - parents.keys()):
                parents[user] = key
                queue.append(user)
        return parents

    def prerequisites(self, roots):
        result = set(roots)
        queue = list(result)
        while queue:
            node = self.nodes[queue.pop()]
            for key in (*node.requires, *node.after):
                if key not in result:
                    result.add(key)
                    queue.append(key)
        return result

    def fingerprint(self, root, key):
        """Hash tracked semantic inputs; exclude test policy, order-only edges and generated files."""
        keys = {key}
        queue = [key]
        while queue:
            needed = set(self.nodes[queue.pop()].requires) - keys
            keys.update(needed)
            queue.extend(needed)
        nodes = [self.nodes[k] for k in keys if self.nodes[k].inputs]
        digest = hashlib.sha256()
        index = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--stage', '-z'])
        for entry in index.split(b'\0'):
            if not entry:
                continue
            metadata, raw = entry.split(b'\t', 1)
            path = os.fsdecode(raw)
            if not any(any(fnmatchcase(path, pat) for pat in n.inputs) and
                       not any(fnmatchcase(path, pat) for pat in n.excludes) for n in nodes):
                continue
            mode, revision, stage = metadata.split()
            if stage != b'0':
                raise ValueError('cannot fingerprint unmerged input: ' + path)
            file = root / path
            if mode == b'160000':
                content = revision
            elif file.is_symlink():
                content = os.fsencode(os.readlink(file))
            elif file.exists():
                content = file.read_bytes()
            else:
                mode, content = b'missing', b''
            digest.update(raw + b'\0' + mode + b'\0' + hashlib.sha256(content).digest())
        return digest.hexdigest()
