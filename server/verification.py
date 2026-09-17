"""Bounded before/after postcondition verification for UE asset writes."""
import hashlib
import json


def _digest(value):
    encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return hashlib.sha256(encoded).hexdigest()


def _count_state(snapshot):
    counts = {}
    graphs = snapshot.get('graphs') if isinstance(snapshot, dict) else None
    if isinstance(graphs, list):
        nodes = [node for graph in graphs for node in graph.get('nodes', [])]
        pins = [pin for node in nodes for pin in node.get('pins', [])]
        links = [link for pin in pins for link in pin.get('links', [])]
        counts.update(graphs=len(graphs), nodes=len(nodes), pins=len(pins), links=len(links))
    for key in ('items', 'assets', 'notifies', 'curves', 'sections', 'slots', 'samples', 'axes', 'bodies', 'constraints'):
        value = snapshot.get(key) if isinstance(snapshot, dict) else None
        if isinstance(value, (list, dict)):
            counts[key] = len(value)
    return counts


def summarize(snapshot):
    """Return a bounded state summary suitable for an MCP response."""
    if not isinstance(snapshot, dict):
        return {'fingerprint': _digest(snapshot), 'type': type(snapshot).__name__}
    state = {key: value for key, value in snapshot.items() if key != 'revision'}
    return {
        'asset': snapshot.get('asset'),
        'revision': snapshot.get('revision'),
        'fingerprint': _digest(state),
        'counts': _count_state(snapshot),
    }


def _find_node(snapshot, graph_path, node_id):
    for graph in snapshot.get('graphs', []):
        if graph.get('path') != graph_path:
            continue
        for node in graph.get('nodes', []):
            if node.get('id') == node_id:
                return node
    return None


def _find_pin(snapshot, graph_path, node_id, pin_id):
    node = _find_node(snapshot, graph_path, node_id)
    if node is None:
        return None
    return next((pin for pin in node.get('pins', []) if pin.get('id') == pin_id), None)


def _link_exists(snapshot, graph_path, node_id, pin_id, target_node_id, target_pin_id):
    pin = _find_pin(snapshot, graph_path, node_id, pin_id)
    if pin is None:
        return False
    return any(link.get('node') == target_node_id and link.get('pin') == target_pin_id
               for link in pin.get('links', []))


def _operation_check(before, after, operation, args):
    graph_path = args.get('graph_path')
    node_id = args.get('node_id')
    pin_id = args.get('pin_id')
    if operation in ('connect_pins', 'disconnect_pins'):
        present = _link_exists(after, graph_path, node_id, pin_id,
                               args.get('target_node_id'), args.get('target_pin_id'))
        return {'name': 'link_present', 'expected': operation == 'connect_pins', 'actual': present}
    if operation == 'set_pin_default':
        pin = _find_pin(after, graph_path, node_id, pin_id)
        actual = None if pin is None else pin.get('default')
        return {'name': 'pin_default_matches', 'expected': args.get('value'), 'actual': actual,
                'passed': actual == args.get('value')}
    if operation == 'move_node':
        node = _find_node(after, graph_path, node_id)
        actual = None if node is None else [node.get('x'), node.get('y')]
        expected = [args.get('x'), args.get('y')]
        return {'name': 'node_position_matches', 'expected': expected, 'actual': actual,
                'passed': actual == expected}
    if operation == 'set_node_comment':
        node = _find_node(after, graph_path, node_id)
        actual = None if node is None else node.get('comment')
        return {'name': 'node_comment_matches', 'expected': args.get('comment'), 'actual': actual,
                'passed': actual == args.get('comment')}
    if operation in ('set_notify_scalar', 'set_notify_object'):
        try:
            config = json.loads(args.get('config_json', '{}'))
        except (TypeError, ValueError):
            config = {}
        property_name = config.get('property')
        expected = config.get('value') if operation == 'set_notify_scalar' else config.get('object_path', '')
        actual = next((item.get('value') for item in after.get('items', [])
                       if item.get('name') == property_name), None)
        if operation == 'set_notify_scalar' and isinstance(expected, str):
            expected_values = {expected}
            try:
                numeric = float(expected)
                expected_values.update((numeric, int(numeric) if numeric.is_integer() else numeric))
            except ValueError:
                pass
            passed = actual in expected_values or str(actual) == expected
        else:
            passed = actual == expected
        return {'name': 'notify_property_matches', 'expected': expected, 'actual': actual, 'passed': passed}
    if operation == 'replace_montage_sections':
        try:
            sections = json.loads(args.get('config_json', '{}')).get('sections', [])
        except (TypeError, ValueError):
            sections = []
        actual = after.get('items', [])
        def same_number(left, right):
            try:
                return abs(float(left) - float(right)) < 1e-4
            except (TypeError, ValueError):
                return left == right
        passed = len(actual) == len(sections) and all(
            item.get('SectionName') == expected.get('name') and
            same_number(item.get('StartTime'), expected.get('time')) and
            (item.get('NextSectionName') in ('None', '', None) if not expected.get('next')
             else item.get('NextSectionName') == expected.get('next'))
            for item, expected in zip(actual, sections)
        )
        return {'name': 'montage_sections_match', 'expected_count': len(sections),
                'actual_count': len(actual), 'passed': passed}
    if operation == 'replace_montage_slot':
        try:
            config = json.loads(args.get('config_json', '{}'))
            expected_slot = config.get('slot')
            segments = config.get('segments', [])
        except (TypeError, ValueError):
            expected_slot, segments = None, []
        actual_slot = next((item for item in after.get('items', [])
                            if item.get('SlotName') == expected_slot), None)
        actual_segments = (((actual_slot or {}).get('AnimTrack') or {}).get('AnimSegments') or {}).get('items', [])
        def same_number(left, right):
            try:
                return abs(float(left) - float(right)) < 1e-4
            except (TypeError, ValueError):
                return left == right
        passed = actual_slot is not None and len(actual_segments) == len(segments) and all(
            item.get('AnimReference') == expected.get('animation') and
            same_number(item.get('StartPos'), expected.get('start')) and
            same_number(item.get('AnimStartTime'), expected.get('in')) and
            same_number(item.get('AnimEndTime'), expected.get('out')) and
            same_number(item.get('AnimPlayRate'), expected.get('rate')) and
            same_number(item.get('LoopingCount'), expected.get('loops'))
            for item, expected in zip(actual_segments, segments)
        )
        return {'name': 'montage_slot_matches', 'slot': expected_slot,
                'expected_count': len(segments), 'actual_count': len(actual_segments),
                'passed': passed}
    if operation in ('delete_node', 'delete_state', 'delete_transition', 'delete_state_machine'):
        return {'name': 'node_removed', 'expected': False,
                'actual': _find_node(after, graph_path, node_id) is not None}
    if operation == 'bake_transform_curves':
        return {'name': 'raw_animation_revision_advanced', 'expected': True,
                'actual': before.get('revision') != after.get('revision')}
    return {'name': 'state_changed', 'expected': True,
            'actual': summarize(before)['fingerprint'] != summarize(after)['fingerprint']}


def verify_postcondition(before, after, operation, args, reported=None):
    before_summary = summarize(before) if before is not None else None
    after_summary = summarize(after)
    checks = []
    if before is not None:
        checks.append({
            'name': 'revision_changed',
            'expected': True,
            'actual': before_summary.get('revision') != after_summary.get('revision'),
            'informational': True,
        })
    else:
        checks.append({'name': 'after_state_exists', 'expected': True, 'actual': bool(after_summary.get('asset'))})
    operation_check = _operation_check(before or {}, after, operation, args)
    if 'passed' not in operation_check:
        operation_check['passed'] = operation_check.get('actual') == operation_check.get('expected')
    checks.append(operation_check)
    if isinstance(reported, dict) and reported.get('revision'):
        checks.append({
            'name': 'reported_revision_matches_after',
            'expected': reported['revision'],
            'actual': after_summary.get('revision'),
            'passed': reported['revision'] == after_summary.get('revision'),
            'informational': True,
        })
    verified = all(
        check.get('passed', check.get('actual') == check.get('expected'))
        for check in checks if not check.get('informational')
    )
    return {
        'verified': verified,
        'before': before_summary,
        'after': after_summary,
        'postconditions': checks,
        'error_code': None if verified else 'postcondition_not_met',
    }


def verified_write(bridge, action, inspect_action, asset_path, operation, args, call_args,
                   after_asset_path=None, inspection_args=None):
    """Execute a write and attach bounded before/after verification.

    Lightweight fakes can opt out by omitting ``supports_postconditions``. The
    production Bridge opts in so a successful dispatch is not mistaken for a
    verified editor mutation.
    """
    if not getattr(bridge, 'supports_postconditions', False):
        return bridge.call(action, **call_args)

    before = None
    if asset_path:
        before_args = dict(inspection_args or {})
        before_args['asset_path'] = asset_path
        try:
            before = bridge.call(inspect_action, **before_args)
        except RuntimeError:
            # Replacing morph deltas may create the named morph target, so a
            # missing pre-state is an expected condition rather than an
            # unobservable editor. The post-state still has to be readable.
            if operation != 'replace_morph_deltas' or inspect_action != 'inspect_ta_asset':
                raise
    result = bridge.call(action, **call_args)
    if not isinstance(result, dict) or result.get('error') or result.get('ok') is False:
        return result
    try:
        target = after_asset_path or asset_path
        if not target and isinstance(result, dict):
            target = result.get('asset')
        if not target:
            raise RuntimeError('The write response did not identify the created asset.')
        after_args = dict(inspection_args or {})
        after_args['asset_path'] = target
        used_fallback = False
        try:
            after = bridge.call(inspect_action, **after_args)
        except RuntimeError:
            # TA creation has no view argument. AnimSequence copies require
            # one, so use the dedicated read-only animation summary as a
            # bounded existence check when the generic view is unavailable.
            if inspect_action != 'inspect_ta_asset':
                raise
            after = bridge.call('inspect_animation_asset', asset_path=target)
            used_fallback = True
        verification = verify_postcondition(before, after, operation, args,
                                            None if used_fallback else result)
    except Exception as exc:
        verification = {
            'verified': False,
            'before': summarize(before) if before is not None else None,
            'after': None,
            'postconditions': [],
            'error_code': 'postcondition_unobservable',
            'error': str(exc),
        }
    enriched = dict(result)
    enriched['verified'] = verification['verified']
    enriched['verification'] = verification
    if not verification['verified']:
        enriched['ok'] = False
        enriched['error_code'] = verification['error_code']
        enriched['error'] = 'Editor write was dispatched but its postcondition was not verified.'
    return enriched
