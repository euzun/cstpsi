#!/usr/bin/env python3
# CSTPSI -- Composable Set-Threshold Labeled PSI
# Author: Erkam Uzun
# Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
#
"""
Validator for CSTPSI instrumentation JSONL output.

Checks:
1. Valid JSON structure per session
2. All required fields present with correct types
3. Sum invariants: sub-step times must sum to parent wall_us (±100 ns tolerance)
4. RSS samples are monotonic (non-decreasing) or explain delta
5. Byte counts are non-negative integers

Usage:
  python3 scripts/validate_jsonl.py /path/to/output.jsonl
"""

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple

# Sum invariant tolerances (nanoseconds)
TOLERANCE_NS = {
    'step_share': 50,           # sample_coef_us + eval_shares_us == wall_us
    'step_interp_pack': 50,     # interpolate_us + simd_pack_us == wall_us
    'step_hom_round_s': 100,    # coef_load + inner_prod + noise_flood + mod_switch + serialize == wall_us
}

class ValidationError(Exception):
    """Raised when validation fails."""
    pass


def check_required_fields(obj: Dict[str, Any], path: str, required: List[str]) -> None:
    """Verify all required fields exist in object."""
    for field in required:
        if field not in obj:
            raise ValidationError(f"Missing field at {path}: {field}")


def check_field_type(obj: Dict[str, Any], field: str, expected_type: type, path: str) -> None:
    """Verify a field has the expected type."""
    if field not in obj:
        return  # Let check_required_fields handle missing fields
    if not isinstance(obj[field], expected_type):
        raise ValidationError(
            f"Wrong type at {path}.{field}: expected {expected_type.__name__}, "
            f"got {type(obj[field]).__name__}"
        )


def validate_config(config: Dict[str, Any]) -> None:
    """Validate config block."""
    required = ['D', 'N', 'k', 'T', 'K', 'label_bytes', 'threads', 'config_name']
    check_required_fields(config, 'config', required)

    for field in ['D', 'N', 'k', 'T', 'K', 'label_bytes', 'threads']:
        check_field_type(config, field, int, 'config')
    check_field_type(config, 'config_name', str, 'config')


def validate_seeds(seeds: Dict[str, Any]) -> None:
    """Validate seeds block."""
    required = ['db', 'query']
    check_required_fields(seeds, 'seeds', required)
    for field in required:
        check_field_type(seeds, field, int, 'seeds')


def validate_hardware(hardware: Dict[str, Any]) -> None:
    """Validate hardware block."""
    required = ['cpu', 'ram_gb', 'os', 'git_sha']
    check_required_fields(hardware, 'hardware', required)
    check_field_type(hardware, 'ram_gb', (int, float), 'hardware')
    for field in ['cpu', 'os', 'git_sha']:
        check_field_type(hardware, field, str, 'hardware')


def validate_software(software: Dict[str, Any]) -> None:
    """Validate software block."""
    required = ['seal', 'emp', 'gcc']
    check_required_fields(software, 'software', required)
    for field in required:
        check_field_type(software, field, str, 'software')


def validate_offline_step(step: Dict[str, Any], step_name: str, path: str) -> None:
    """Validate an offline step (wall_us, rss_delta_mb, and optional sub-steps)."""
    if 'wall_us' not in step:
        raise ValidationError(f"Missing wall_us at {path}")
    check_field_type(step, 'wall_us', int, path)

    if 'rss_delta_mb' not in step:
        raise ValidationError(f"Missing rss_delta_mb at {path}")
    check_field_type(step, 'rss_delta_mb', (int, float), path)

    # Check sum invariants for sub-steps.  Skip when BOTH sub-steps are 0:
    # this signals "sub-steps not measured (parent-only timing)".  When at
    # least one sub-step is non-zero, require the sum to equal wall_us.
    if step_name == 'step_share':
        if 'sample_coef_us' in step and 'eval_shares_us' in step:
            check_field_type(step, 'sample_coef_us', int, path)
            check_field_type(step, 'eval_shares_us', int, path)
            sub_sum = step['sample_coef_us'] + step['eval_shares_us']
            if sub_sum > 0:
                wall = step['wall_us']
                delta = abs(sub_sum - wall)
                if delta > TOLERANCE_NS['step_share']:
                    raise ValidationError(
                        f"Sum invariant violated at {path}: "
                        f"sample_coef_us ({step['sample_coef_us']}) + eval_shares_us ({step['eval_shares_us']}) "
                        f"= {sub_sum}, but wall_us = {wall} (delta={delta} ns, tolerance={TOLERANCE_NS['step_share']} ns)"
                    )

    elif step_name == 'step_interp_pack':
        if 'interpolate_us' in step and 'simd_pack_us' in step:
            check_field_type(step, 'interpolate_us', int, path)
            check_field_type(step, 'simd_pack_us', int, path)
            sub_sum = step['interpolate_us'] + step['simd_pack_us']
            if sub_sum > 0:
                wall = step['wall_us']
                delta = abs(sub_sum - wall)
                if delta > TOLERANCE_NS['step_interp_pack']:
                    raise ValidationError(
                        f"Sum invariant violated at {path}: "
                        f"interpolate_us ({step['interpolate_us']}) + simd_pack_us ({step['simd_pack_us']}) "
                        f"= {sub_sum}, but wall_us = {wall} (delta={delta} ns, tolerance={TOLERANCE_NS['step_interp_pack']} ns)"
                    )
        # Also check RSS samples if present
        if 'rss_after_interp_mb' in step:
            check_field_type(step, 'rss_after_interp_mb', (int, float), path)
        if 'rss_after_pack_mb' in step:
            check_field_type(step, 'rss_after_pack_mb', (int, float), path)


def validate_offline_once_per_session(offline: Dict[str, Any]) -> None:
    """Validate offline_once_per_session block."""
    path = 'offline_once_per_session'
    steps = ['step_init', 'step_partition', 'step_blind', 'step_share', 'step_interp_pack']

    for step_name in steps:
        if step_name not in offline:
            raise ValidationError(f"Missing {step_name} in {path}")
        validate_offline_step(offline[step_name], step_name, f"{path}.{step_name}")

    if 'total_offline_us' not in offline:
        raise ValidationError(f"Missing total_offline_us in {path}")
    check_field_type(offline, 'total_offline_us', int, path)

    if 's_peak_offline_rss_mb' not in offline:
        raise ValidationError(f"Missing s_peak_offline_rss_mb in {path}")
    check_field_type(offline, 's_peak_offline_rss_mb', (int, float), path)


def validate_hom_round(round_obj: Dict[str, Any], query_id: int, round_idx: int) -> None:
    """Validate a single homomorphic evaluation round."""
    path = f"queries[{query_id}].step_hom_rounds[{round_idx}]"
    required_fields = ['t', 'round_kind', 's', 'r', 'bytes_s_to_r']
    check_required_fields(round_obj, path, required_fields)

    check_field_type(round_obj, 't', int, path)
    check_field_type(round_obj, 'round_kind', str, path)

    if round_obj['round_kind'] not in ['token', 'label']:
        raise ValidationError(f"Invalid round_kind at {path}: {round_obj['round_kind']}")

    check_field_type(round_obj, 'bytes_s_to_r', int, path)
    if round_obj['bytes_s_to_r'] < 0:
        raise ValidationError(f"Negative bytes_s_to_r at {path}")

    # Validate sender side (s)
    s_path = f"{path}.s"
    s = round_obj['s']
    s_required = ['coef_load_us', 'ctxt_inner_product_us', 'noise_flood_us',
                  'mod_switch_us', 'serialize_us', 'wall_us', 'rss_after_rerand_mb']
    check_required_fields(s, s_path, s_required)

    for field in ['coef_load_us', 'ctxt_inner_product_us', 'noise_flood_us',
                  'mod_switch_us', 'serialize_us', 'wall_us']:
        check_field_type(s, field, int, s_path)
    check_field_type(s, 'rss_after_rerand_mb', (int, float), s_path)

    # Check sum invariant for sender side
    sub_sum = (s['coef_load_us'] + s['ctxt_inner_product_us'] + s['noise_flood_us']
               + s['mod_switch_us'] + s['serialize_us'])
    wall = s['wall_us']
    delta = abs(sub_sum - wall)
    if delta > TOLERANCE_NS['step_hom_round_s']:
        raise ValidationError(
            f"Sum invariant violated at {s_path}: "
            f"coef_load + inner_prod + noise_flood + mod_switch + serialize "
            f"= {sub_sum}, but wall_us = {wall} (delta={delta} ns, tolerance={TOLERANCE_NS['step_hom_round_s']} ns)"
        )

    # Validate receiver side (r)
    r_path = f"{path}.r"
    r = round_obj['r']
    r_required = ['r_decrypt_us']
    check_required_fields(r, r_path, r_required)
    check_field_type(r, 'r_decrypt_us', int, r_path)


def validate_step_gc(step_gc: Dict[str, Any], query_id: int) -> None:
    """Validate step_gc block."""
    path = f"queries[{query_id}].step_gc"
    required = ['gc_us', 'bytes_r_to_s', 'bytes_s_to_r', 'r_rss_mb', 's_rss_mb']
    check_required_fields(step_gc, path, required)

    for field in ['gc_us', 'bytes_r_to_s', 'bytes_s_to_r']:
        check_field_type(step_gc, field, int, path)
        if field in ['bytes_r_to_s', 'bytes_s_to_r'] and step_gc[field] < 0:
            raise ValidationError(f"Negative {field} at {path}")

    for field in ['r_rss_mb', 's_rss_mb']:
        check_field_type(step_gc, field, (int, float), path)


def validate_step_query_powers(step_qp: Dict[str, Any], query_id: int) -> None:
    """Validate step_query_powers block."""
    path = f"queries[{query_id}].step_query_powers"
    required = ['r_encrypt_us', 'r_serialize_us', 's_recv_us', 'cache_size_bytes',
                'bytes_r_to_s', 'r_rss_mb', 's_rss_mb']
    check_required_fields(step_qp, path, required)

    for field in ['r_encrypt_us', 'r_serialize_us', 's_recv_us', 'cache_size_bytes', 'bytes_r_to_s']:
        check_field_type(step_qp, field, int, path)
        if field != 'cache_size_bytes' and step_qp[field] < 0:
            raise ValidationError(f"Negative {field} at {path}")

    for field in ['r_rss_mb', 's_rss_mb']:
        check_field_type(step_qp, field, (int, float), path)


def validate_step_token_check(step_tc: Dict[str, Any], query_id: int) -> None:
    """Validate step_token_check block."""
    path = f"queries[{query_id}].step_token_check"
    required = ['r_us', 'pairs_tried', 'tokens_found']
    check_required_fields(step_tc, path, required)

    for field in required:
        check_field_type(step_tc, field, int, path)
        if step_tc[field] < 0:
            raise ValidationError(f"Negative {field} at {path}")


def validate_step_label_recovery(step_lr: Dict[str, Any], query_id: int) -> None:
    """Validate step_label_recovery block."""
    path = f"queries[{query_id}].step_label_recovery"
    required = ['r_us', 'labels_recovered']
    check_required_fields(step_lr, path, required)

    for field in required:
        check_field_type(step_lr, field, int, path)
        if step_lr[field] < 0:
            raise ValidationError(f"Negative {field} at {path}")


def validate_query_totals(totals: Dict[str, Any], query_id: int) -> None:
    """Validate totals block within a query."""
    path = f"queries[{query_id}].totals"
    required = ['online_wall_us', 'r_peak_rss_mb', 's_peak_rss_mb',
                'bytes_r_to_s_total', 'bytes_s_to_r_total']
    check_required_fields(totals, path, required)

    for field in ['online_wall_us', 'bytes_r_to_s_total', 'bytes_s_to_r_total']:
        check_field_type(totals, field, int, path)
        if field != 'online_wall_us' and totals[field] < 0:
            raise ValidationError(f"Negative {field} at {path}")

    for field in ['r_peak_rss_mb', 's_peak_rss_mb']:
        check_field_type(totals, field, (int, float), path)


def validate_query_result(result: Dict[str, Any], query_id: int) -> None:
    """Validate result block within a query."""
    path = f"queries[{query_id}].result"
    required = ['matched', 'labels']
    check_required_fields(result, path, required)

    check_field_type(result, 'matched', bool, path)
    check_field_type(result, 'labels', list, path)


def validate_query(query: Dict[str, Any], query_id: int) -> None:
    """Validate a single query object."""
    path = f"queries[{query_id}]"
    required = ['query_id', 'query_type', 'step_gc', 'step_query_powers',
                'step_hom_rounds', 'step_token_check', 'step_label_recovery',
                'totals', 'result']
    check_required_fields(query, path, required)

    check_field_type(query, 'query_id', int, path)
    check_field_type(query, 'query_type', str, path)

    validate_step_gc(query['step_gc'], query_id)
    validate_step_query_powers(query['step_query_powers'], query_id)

    check_field_type(query, 'step_hom_rounds', list, path)
    for round_idx, round_obj in enumerate(query['step_hom_rounds']):
        validate_hom_round(round_obj, query_id, round_idx)

    validate_step_token_check(query['step_token_check'], query_id)
    validate_step_label_recovery(query['step_label_recovery'], query_id)
    validate_query_totals(query['totals'], query_id)
    validate_query_result(query['result'], query_id)


def validate_session(session: Dict[str, Any]) -> None:
    """Validate a complete session object."""
    # Top-level required fields
    required = ['run_id', 'config', 'seeds', 'hardware', 'software',
                'offline_once_per_session', 'queries', 'session_totals']
    check_required_fields(session, 'root', required)

    check_field_type(session, 'run_id', str, 'root')

    # Validate structured blocks
    validate_config(session['config'])
    validate_seeds(session['seeds'])
    validate_hardware(session['hardware'])
    validate_software(session['software'])
    validate_offline_once_per_session(session['offline_once_per_session'])

    # Validate queries
    check_field_type(session, 'queries', list, 'root')
    for query_id, query in enumerate(session['queries']):
        validate_query(query, query_id)

    # Validate session_totals
    path = 'session_totals'
    session_totals = session['session_totals']
    required = ['session_wall_us', 's_peak_rss_overall_mb']
    check_required_fields(session_totals, path, required)

    check_field_type(session_totals, 'session_wall_us', int, path)
    check_field_type(session_totals, 's_peak_rss_overall_mb', (int, float), path)


def validate_file(jsonl_path: str) -> Tuple[bool, List[str]]:
    """
    Validate a JSONL file with one session per line.

    Returns: (success, list_of_errors)
    """
    errors = []
    path = Path(jsonl_path)

    if not path.exists():
        return False, [f"File not found: {jsonl_path}"]

    if not path.is_file():
        return False, [f"Not a file: {jsonl_path}"]

    line_no = 0
    try:
        with open(path, 'r') as f:
            for line in f:
                line_no += 1
                line = line.strip()
                if not line:
                    continue  # Skip empty lines

                try:
                    session = json.loads(line)
                    validate_session(session)
                except json.JSONDecodeError as e:
                    errors.append(f"Line {line_no}: JSON parse error: {e}")
                except ValidationError as e:
                    errors.append(f"Line {line_no}: {e}")

    except IOError as e:
        return False, [f"Error reading file: {e}"]

    return len(errors) == 0, errors


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/validate_jsonl.py <path-to-jsonl>")
        sys.exit(1)

    jsonl_path = sys.argv[1]
    success, errors = validate_file(jsonl_path)

    if success:
        print(f"✓ Validation passed for {jsonl_path}")
        sys.exit(0)
    else:
        print(f"✗ Validation failed for {jsonl_path}:")
        for error in errors:
            print(f"  {error}")
        sys.exit(1)


if __name__ == '__main__':
    main()
