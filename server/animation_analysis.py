"""Read-only evidence reducers; independent of MCP and Unreal transport."""


def skeleton_chain(snapshot, end_bone, start_bone=None, limit=128):
    bones = snapshot['bones']
    by_name = {b['name']: b for b in bones}
    by_index = {b['index']: b for b in bones}
    if end_bone not in by_name or (start_bone is not None and start_bone not in by_name):
        raise ValueError('Bone not found; names must match the inspected Skeleton')
    current = by_name[end_bone]
    chain, visited = [], set()
    while True:
        if current['index'] in visited:
            raise ValueError('Invalid cyclic skeleton hierarchy')
        visited.add(current['index'])
        chain.append(current)
        if current['name'] == start_bone or current['parent_index'] == -1:
            break
        if current['parent_index'] not in by_index:
            raise ValueError('Missing parent in skeleton hierarchy')
        current = by_index[current['parent_index']]
    if start_bone is not None and chain[-1]['name'] != start_bone:
        raise ValueError('start_bone is not an ancestor of end_bone')
    chain.reverse()
    return {'schema_version': 1, 'action': 'consume_result', 'asset_path': snapshot['asset'],
            'evidence_scope': 'skeleton_reference_pose', 'runtime_verified': False,
            'chain_length': len(chain), 'truncated': len(chain) > limit,
            'bones': chain[:limit], 'translation_unit': 'centimeters',
            'limitations': ['Not animation-frame, retarget-source or world-space pose.']}


def inspect_anim_blueprint(snapshot, offset=0, limit=50):
    if snapshot.get('class') != '/Script/Engine.AnimBlueprint':
        raise ValueError('Asset is not an Animation Blueprint')
    graphs = snapshot['graphs']
    issues, summaries = [], []
    node_count = 0
    for graph in graphs:
        nodes = graph['nodes']
        node_count += len(nodes)
        pins = {(n['id'], p['id']) for n in nodes for p in n['pins']}
        summaries.append({'graph_path': graph['path'], 'name': graph['name'],
                          'class': graph['class'], 'node_count': len(nodes)})
        for node in nodes:
            kind = node['class'].rsplit('.', 1)[-1]
            for pin in node['pins']:
                evidence = {'graph_path': graph['path'], 'node_id': node['id'], 'pin_id': pin['id']}
                if kind in ('AnimGraphNode_Root', 'AnimGraphNode_StateResult') and pin['direction'] == 'input' and pin['category'] == 'struct' and not pin['links']:
                    issues.append({'code': 'UNCONNECTED_POSE_RESULT', 'severity': 'warning',
                                   'message': 'Pose result input has no upstream link; inspect intended pose flow.', 'evidence': evidence})
                if kind == 'AnimStateEntryNode' and pin['direction'] == 'output' and not pin['links']:
                    issues.append({'code': 'UNCONNECTED_STATE_ENTRY', 'severity': 'warning',
                                   'message': 'State machine entry has no initial state link.', 'evidence': evidence})
                for link in pin['links']:
                    if (link['node'], link['pin']) not in pins:
                        issues.append({'code': 'UNRESOLVED_LINK', 'severity': 'warning',
                                       'message': 'Link target is absent from this graph snapshot; inspect exact graph before editing.', 'evidence': dict(evidence, target=link)})
    issues.sort(key=lambda i: (i['evidence']['graph_path'], i['evidence']['node_id'], i['evidence']['pin_id'], i['code']))
    end = min(offset + limit, len(issues))
    return {'schema_version': 1, 'action': 'consume_result', 'asset_path': snapshot['asset'],
            'revision': snapshot['revision'], 'evidence_scope': 'static_graph_structure',
            'runtime_verified': False, 'compile_verified': False,
            'summary': {'graphs': len(graphs), 'nodes': node_count, 'findings': len(issues)},
            'graphs': summaries[:100], 'graphs_truncated': len(summaries) > 100,
            'findings': issues[offset:end], 'next_offset': end if end < len(issues) else None,
            'next_tool': 'ue_inspect_blueprint',
            'limitations': ['No findings does not prove correctness.', 'No animation sampling, compilation or runtime evaluation was performed.',
                            'Findings are structural observations, not final root-cause diagnoses.']}
