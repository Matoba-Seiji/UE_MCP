"""Bounded, session-local Blueprint snapshots and deterministic structural diffs."""
from collections import OrderedDict
import copy
import json
import uuid


def structural_diff(before, after):
    changes = []

    def compare(left, right, path):
        if type(left) is not type(right):
            changes.append({'path': path, 'change': 'replace', 'before': left, 'after': right})
        elif isinstance(left, dict):
            for key in sorted(left.keys() | right.keys()):
                child = path + '/' + key.replace('~', '~0').replace('/', '~1')
                if key not in left:
                    changes.append({'path': child, 'change': 'add', 'after': right[key]})
                elif key not in right:
                    changes.append({'path': child, 'change': 'remove', 'before': left[key]})
                else:
                    compare(left[key], right[key], child)
        elif left != right:
            changes.append({'path': path, 'change': 'replace', 'before': left, 'after': right})

    def normalize(snapshot):
        result = {k: v for k, v in snapshot.items() if k not in ('revision', 'graphs')}
        result['graphs'] = {}
        for graph in snapshot.get('graphs', []):
            item = {k: v for k, v in graph.items() if k != 'nodes'}
            item['nodes'] = {}
            for node in graph.get('nodes', []):
                data = {k: v for k, v in node.items() if k != 'pins'}
                data['pins'] = {}
                for pin in node.get('pins', []):
                    value = dict(pin)
                    value['links'] = sorted(pin.get('links', []), key=lambda x: json.dumps(x, sort_keys=True))
                    data['pins'][pin['id']] = value
                item['nodes'][node['id']] = data
            result['graphs'][graph['path']] = item
        return result

    if before['asset'] != after['asset']:
        raise ValueError('Snapshots must refer to the same asset')
    compare(normalize(before), normalize(after), '')
    return changes


class SnapshotStore:
    def __init__(self, max_count=16, max_bytes=32 * 1024 * 1024):
        self.items = OrderedDict()
        self.max_count = max_count
        self.max_bytes = max_bytes
        self.bytes = 0

    def add(self, snapshot):
        size = len(json.dumps(snapshot, ensure_ascii=False).encode('utf-8'))
        if size > self.max_bytes:
            raise ValueError('Snapshot exceeds the session snapshot memory limit')
        while self.items and (len(self.items) >= self.max_count or self.bytes + size > self.max_bytes):
            _, (_, old_size) = self.items.popitem(last=False)
            self.bytes -= old_size
        ident = uuid.uuid4().hex
        self.items[ident] = (copy.deepcopy(snapshot), size)
        self.bytes += size
        return ident

    def get(self, ident):
        if ident not in self.items:
            raise ValueError('Snapshot missing or evicted; capture again in this MCP session')
        return copy.deepcopy(self.items[ident][0])
