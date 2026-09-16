"""Load domain skill manifests and project their metadata onto MCP tools.

The bridge still owns the executable schemas for compatibility.  Domain
manifests own classification and execution policy, which lets us migrate
schemas and entry points incrementally without changing client tool names.
"""
import json
from pathlib import Path


SKILLS_ROOT = Path(__file__).resolve().parent


def _load_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def load_catalog():
    catalog = {}
    for manifest_path in sorted(SKILLS_ROOT.glob('*/tools.json')):
        manifest = _load_json(manifest_path)
        skill = manifest['skill']
        defaults = {
            'skill': skill,
            'execution': manifest.get('execution', 'sync'),
            'affinity': manifest.get('affinity', 'editor'),
            'timeout_seconds': manifest.get('timeout_seconds', 45),
            'read_only': False,
            'destructive': False,
            'idempotent': False,
            'open_world': False,
            'verification': 'none',
            'entrypoint': 'bridge.invoke',
        }
        read_only_tools = set(manifest.get('read_only_tools', []))
        destructive_tools = set(manifest.get('destructive_tools', []))
        idempotent_tools = set(manifest.get('idempotent_tools', []))
        verification = manifest.get('verification', {})
        timeouts = manifest.get('timeouts', {})
        next_tools = manifest.get('next_tools', {})
        for name in manifest['tools']:
            item = dict(defaults)
            item['read_only'] = name in read_only_tools
            item['destructive'] = name in destructive_tools
            item['idempotent'] = name in idempotent_tools
            item['verification'] = verification.get(name, item['verification'])
            item['timeout_seconds'] = timeouts.get(name, item['timeout_seconds'])
            if name in next_tools:
                item['next_tools'] = list(next_tools[name])
            catalog[name] = item
    return catalog


def apply_tool_metadata(tools):
    """Attach standard MCP annotations and UE-specific execution metadata."""
    catalog = load_catalog()
    names = {tool['name'] for tool in tools}
    missing = names - set(catalog)
    unknown = set(catalog) - names
    if missing or unknown:
        details = []
        if missing:
            details.append('missing skill entries: ' + ', '.join(sorted(missing)))
        if unknown:
            details.append('skill entries without tools: ' + ', '.join(sorted(unknown)))
        raise RuntimeError('; '.join(details))

    for tool in tools:
        item = catalog[tool['name']]
        existing = dict(tool.get('annotations', {}))
        existing.update({
            'readOnlyHint': item['read_only'],
            'destructiveHint': item['destructive'],
            'idempotentHint': item['idempotent'],
            'openWorldHint': item['open_world'],
        })
        tool['annotations'] = existing
        tool['metadata'] = {
            'skill': item['skill'],
            'execution': item['execution'],
            'affinity': item['affinity'],
            'timeoutSeconds': item['timeout_seconds'],
            'verification': item['verification'],
            'entrypoint': item['entrypoint'],
        }
        if 'next_tools' in item:
            tool['metadata']['nextTools'] = item['next_tools']
    return tools

