#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Transcribes selected MIT definitions and tables from the fetched Linux i915 tree into
src/drivers/gpu/i915/linux/*.inc with the original notices and a modification note.

#define lines are copied one by one; tables (context image offset arrays, MOCS entries)
are copied as verbatim blocks. Each value is kept; the header of every generated file
names the mechanical rewrites. Usage: gen-inc.py <tag> regs|ids|commands|lrc|mocs|all
"""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path.home() / 'zedBSD'
TAG = sys.argv[1] if len(sys.argv) > 1 else 'v6.19'
WHAT = sys.argv[2] if len(sys.argv) > 2 else 'all'
TREE = ROOT / 'plan/ws029/temp/linux' / TAG
OUT = ROOT / 'src/drivers/gpu/i915/linux'
PREFIX = 'drivers/gpu/drm/i915/'

# Bit helpers written once per .inc; every Linux helper below is rewritten to one of these.
HELPERS = '''#define I915_INC_BIT(n)			(1U << (n))
#define I915_INC_BIT64(n)		(1ULL << (n))
#define I915_INC_GENMASK(h, l)		(((0xffffffffU) >> (31U - (h))) & ((0xffffffffU) << (l)))
#define I915_INC_GENMASK64(h, l)	(((0xffffffffffffffffULL) >> (63U - (h))) & ((0xffffffffffffffffULL) << (l)))
#define I915_INC_PAGE_SIZE		4096U
'''
HELPER_NAMES = {'I915_INC_BIT', 'I915_INC_BIT64', 'I915_INC_GENMASK', 'I915_INC_GENMASK64', 'I915_INC_PAGE_SIZE'}

REWRITES = [
    (r'\b_MMIO\(', '('),
    (r'\bREG_BIT64\(', 'I915_INC_BIT64('),
    (r'\bBIT_ULL\(', 'I915_INC_BIT64('),
    (r'\bREG_BIT\(', 'I915_INC_BIT('),
    (r'\bBIT\(', 'I915_INC_BIT('),
    (r'\bREG_GENMASK64\(', 'I915_INC_GENMASK64('),
    (r'\bGENMASK_ULL\(', 'I915_INC_GENMASK64('),
    (r'\bREG_GENMASK\(', 'I915_INC_GENMASK('),
    (r'\bGENMASK\(', 'I915_INC_GENMASK('),
    (r'\bPAGE_SIZE\b', 'I915_INC_PAGE_SIZE'),
    (r'\((\d+) ?<< ?', r'(\1U << '),
    (r'MACRO__, \.\.\.', 'MACRO__'),
    (r', ## __VA_ARGS__', ''),
    (r'\bsizeof\(u32\)', '4U'),
    (r'\bsizeof\(u64\)', '8U'),
    (r'\s*\|\s*BUILD_BUG_ON_ZERO\((?:[^()]|\([^()]*\))*\)', ''),
]

REWRITE_NOTE = (' * Rewrites: _MMIO(x) -> (x); REG_BIT/BIT -> I915_INC_BIT; REG_BIT64/BIT_ULL -> I915_INC_BIT64;\n'
                ' * REG_GENMASK/GENMASK -> I915_INC_GENMASK; GENMASK_ULL -> I915_INC_GENMASK64; PAGE_SIZE ->\n'
                ' * I915_INC_PAGE_SIZE; integer shift bases gain a U suffix; variadic MACRO__ tables drop their\n'
                ' * __VA_ARGS__ pass-through; sizeof(u32)/sizeof(u64) -> 4U/8U; BUILD_BUG_ON_ZERO terms are removed.')

REGS = [
    'RENDER_CLASS', 'COPY_ENGINE_CLASS', 'VIDEO_DECODE_CLASS', 'VIDEO_ENHANCEMENT_CLASS', 'OTHER_CLASS',
    'GEN4_GTTMMADR_BAR', 'GEN4_GMADR_BAR', 'SNB_GMCH_CTRL', 'BDW_GMCH_GGMS_SHIFT', 'BDW_GMCH_GGMS_MASK',
    'FORCEWAKE_GT_GEN9', 'FORCEWAKE_ACK_GT_GEN9', 'FORCEWAKE_RENDER_GEN9', 'FORCEWAKE_ACK_RENDER_GEN9',
    'FORCEWAKE_MEDIA_VDBOX_GEN11', 'FORCEWAKE_ACK_MEDIA_VDBOX_GEN11', 'FORCEWAKE_MEDIA_VEBOX_GEN11',
    'FORCEWAKE_ACK_MEDIA_VEBOX_GEN11', 'FORCEWAKE_KERNEL', 'GFX_FLSH_CNTL_GEN6', 'GFX_FLSH_CNTL_EN',
    'GEN6_GDRST', 'GEN6_GRDOM_FULL', 'GEN6_GRDOM_RENDER', 'GEN11_GRDOM_RENDER', 'GEN11_GRDOM_BLT',
    'RING_RESET_CTL', 'RESET_CTL_REQUEST_RESET', 'RESET_CTL_READY_TO_RESET', 'RESET_CTL_CAT_ERROR',
    'RENDER_RING_BASE', 'BLT_RING_BASE', 'RING_TAIL', 'RING_HEAD', 'HEAD_ADDR', 'TAIL_ADDR', 'RING_START',
    'RING_CTL', 'RING_CTL_SIZE', 'RING_VALID', 'RING_HWS_PGA', 'RING_HWSTAM', 'RING_MI_MODE', 'STOP_RING',
    'MODE_IDLE', 'RING_IMR', 'RING_EIR', 'RING_EMR', 'RING_ESR', 'RING_ACTHD', 'RING_ACTHD_UDW', 'RING_BBADDR',
    'RING_BBADDR_UDW', 'RING_IPEIR', 'RING_IPEHR', 'RING_INSTDONE', 'RING_CONTEXT_CONTROL',
    'CTX_CTRL_INHIBIT_SYN_CTX_SWITCH', 'CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT', 'GEN8_RING_PDP_UDW',
    'GEN8_RING_PDP_LDW', 'RING_MODE_GEN7', 'GFX_RUN_LIST_ENABLE', 'GEN11_GFX_DISABLE_LEGACY_MODE',
    'GEN12_GFX_PREFETCH_DISABLE', 'RING_ELSP', 'RING_EXECLIST_STATUS_LO', 'RING_EXECLIST_STATUS_HI',
    'RING_CONTEXT_STATUS_PTR', 'RING_EXECLIST_SQ_CONTENTS', 'RING_EXECLIST_CONTROL', 'EL_CTRL_LOAD',
    'GEN8_EXECLISTS_STATUS_BUF', 'GEN11_EXECLISTS_STATUS_BUF2', 'BLIT_CCTL', 'BLIT_CCTL_DST_MOCS_MASK',
    'BLIT_CCTL_SRC_MOCS_MASK', 'RING_BB_PER_CTX_PTR', 'RING_INDIRECT_CTX', 'RING_INDIRECT_CTX_OFFSET',
    'GEN12_GLOBAL_MOCS', 'GEN9_LNCFCMOCS',
    'GEN11_GFX_MSTR_IRQ', 'GEN11_MASTER_IRQ', 'GEN11_DISPLAY_IRQ', 'GEN11_GU_MISC_IRQ', 'GEN11_GT_DW_IRQ',
    'GEN11_GT_INTR_DW', 'GEN11_INTR_IDENTITY_REG', 'GEN11_INTR_DATA_VALID', 'GEN11_INTR_ENGINE_CLASS',
    'GEN11_INTR_ENGINE_INSTANCE', 'GEN11_INTR_ENGINE_INTR', 'GEN11_IIR_REG_SELECTOR',
    'GEN11_RENDER_COPY_INTR_ENABLE', 'GEN11_VCS_VECS_INTR_ENABLE', 'GEN11_RCS0_RSVD_INTR_MASK',
    'GEN11_BCS_RSVD_INTR_MASK', 'GEN11_VCS0_VCS1_INTR_MASK', 'GEN11_VCS2_VCS3_INTR_MASK',
    'GEN11_VECS0_VECS1_INTR_MASK', 'GEN11_GPM_WGBOXPERF_INTR_ENABLE', 'GEN11_GPM_WGBOXPERF_INTR_MASK',
    'GEN11_GUC_SG_INTR_ENABLE', 'GEN11_GUC_SG_INTR_MASK', 'GEN11_CRYPTO_RSVD_INTR_ENABLE',
    'GEN11_CRYPTO_RSVD_INTR_MASK', 'GT_RENDER_USER_INTERRUPT', 'GT_CONTEXT_SWITCH_INTERRUPT',
    'GT_CS_MASTER_ERROR_INTERRUPT', 'GT_WAIT_SEMAPHORE_INTERRUPT', 'I915_ERROR_INSTRUCTION',
    'LRC_PPHWSP_PN', 'LRC_PPHWSP_SZ', 'LRC_STATE_PN', 'LRC_STATE_OFFSET', 'LRC_PPHWSP_SCRATCH',
    'LRC_PPHWSP_SCRATCH_ADDR', 'GEN11_LR_CONTEXT_RENDER_SIZE', 'GEN8_LR_CONTEXT_OTHER_SIZE',
    'CTX_CONTEXT_CONTROL', 'CTX_RING_HEAD', 'CTX_RING_TAIL', 'CTX_RING_START', 'CTX_RING_CTL', 'CTX_BB_STATE',
    'CTX_TIMESTAMP', 'CTX_PDP0_UDW', 'CTX_PDP0_LDW', 'CTX_R_PWR_CLK_STATE', 'GEN8_CTX_VALID',
    'GEN8_CTX_FORCE_RESTORE', 'GEN8_CTX_L3LLC_COHERENT', 'GEN8_CTX_PRIVILEGE', 'GEN8_CTX_ADDRESSING_MODE_SHIFT',
    'GEN8_CTX_ID_SHIFT', 'GEN11_SW_CTX_ID_SHIFT', 'GEN11_SW_CTX_ID_WIDTH', 'GEN11_ENGINE_CLASS_SHIFT',
    'GEN11_ENGINE_INSTANCE_SHIFT', 'GEN11_MAX_CONTEXT_HW_ID', 'GEN12_MAX_CONTEXT_HW_ID', 'GEN12_IDLE_CTX_ID',
    'CTX_DESC_FORCE_RESTORE', 'GEN11_CSB_ENTRIES', 'I915_HWS_CSB_BUF0_INDEX', 'ICL_HWS_CSB_WRITE_INDEX',
    'I915_GEM_HWS_PREEMPT', 'I915_GEM_HWS_SEQNO', 'I915_GEM_HWS_SEQNO_ADDR', 'I915_GEM_HWS_SCRATCH',
    'GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE', 'GEN12_CTX_SWITCH_DETAIL', 'GEN12_CSB_SW_CTX_ID_MASK',
    'EXECLIST_MAX_PORTS',
    'I915_GTT_PAGE_SIZE_4K', 'I915_PDES', 'I915_PDE_MASK', 'GEN8_3LVL_PDPES', 'GEN8_PAGE_PRESENT',
    'GEN8_PAGE_RW', 'GEN12_PPGTT_PTE_PAT0', 'GEN12_PPGTT_PTE_PAT1', 'GEN12_PPGTT_PTE_PAT2', 'GEN12_GGTT_PTE_LM',
    'GEN12_GGTT_PTE_ADDR_MASK', 'GEN8_PDE_PS_2M', 'GEN8_PDE_IPS_64K']

COMMANDS = [
    'INSTR_CLIENT_SHIFT', 'INSTR_MI_CLIENT', 'INSTR_BC_CLIENT', 'INSTR_RC_CLIENT', '__INSTR', 'MI_INSTR', 'MI_NOOP', 'MI_USER_INTERRUPT', 'MI_ARB_CHECK', 'MI_ARB_ON_OFF', 'MI_ARB_ENABLE', 'MI_ARB_DISABLE',
    'MI_BATCH_BUFFER_END', 'MI_SEMAPHORE_WAIT', 'MI_SEMAPHORE_WAIT_TOKEN', 'MI_SEMAPHORE_POLL',
    'MI_SEMAPHORE_SAD_EQ_SDD', 'MI_SEMAPHORE_SAD_GTE_SDD', 'MI_SEMAPHORE_GLOBAL_GTT', 'MI_STORE_DWORD_IMM_GEN4',
    'MI_USE_GGTT', 'MI_MEM_VIRTUAL', 'MI_LOAD_REGISTER_IMM', 'MI_LRI_FORCE_POSTED', 'MI_LRI_LRM_CS_MMIO',
    'MI_LRI_MMIO_REMAP_EN', 'MI_STORE_REGISTER_MEM_GEN8', 'MI_SRM_LRM_GLOBAL_GTT', 'MI_FLUSH_DW',
    'MI_FLUSH_DW_STORE_INDEX', 'MI_FLUSH_DW_OP_STOREDW', 'MI_FLUSH_DW_USE_GTT', 'MI_INVALIDATE_TLB',
    'MI_BATCH_BUFFER_START_GEN8', 'MI_BATCH_NON_SECURE_I965', 'XY_SRC_COPY_BLT_CMD', 'XY_COLOR_BLT_CMD',
    'BLT_WRITE_RGBA', 'BLT_WRITE_RGB', 'BLT_WRITE_A', 'BLT_DEPTH_32', 'BLT_ROP_SRC_COPY', 'BLT_ROP_COLOR_COPY',
    'XY_SRC_COPY_BLT_SRC_TILED',
    'XY_SRC_COPY_BLT_DST_TILED', 'GFX_OP_PIPE_CONTROL', 'PIPE_CONTROL_CS_STALL', 'PIPE_CONTROL_QW_WRITE',
    'PIPE_CONTROL_GLOBAL_GTT_IVB', 'PIPE_CONTROL_FLUSH_ENABLE', 'PIPE_CONTROL_TLB_INVALIDATE',
    'PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH', 'PIPE_CONTROL_DEPTH_CACHE_FLUSH', 'PIPE_CONTROL_DC_FLUSH_ENABLE',
    'PIPE_CONTROL_TILE_CACHE_FLUSH', 'PIPE_CONTROL_FLUSH_L3', 'PIPE_CONTROL_DEPTH_STALL',
    'PIPE_CONTROL_STORE_DATA_INDEX', 'PIPE_CONTROL0_HDC_PIPELINE_FLUSH', 'PIPE_CONTROL_COMMAND_CACHE_INVALIDATE',
    'PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE', 'PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE',
    'PIPE_CONTROL_VF_CACHE_INVALIDATE', 'PIPE_CONTROL_CONST_CACHE_INVALIDATE', 'PIPE_CONTROL_STATE_CACHE_INVALIDATE']

# name -> (output, define source files, defines, [(block source, start regex, end regex)], extra rewrites)
TARGETS = {
    'regs': ('i915-regs.inc', [
        'gt/intel_engine_types.h', 'intel_pci_config.h', 'include/drm/intel/i915_drm.h', 'gt/intel_gt_regs.h',
        'gt/intel_engine_regs.h', 'i915_reg.h', 'gt/intel_gtt.h', 'gt/intel_lrc_reg.h', 'gt/intel_lrc.h',
        'gt/intel_engine.h', 'gt/intel_execlists_submission.c', 'gt/gen8_ppgtt.c', 'gt/intel_engine_cs.c'],
        REGS, [], []),
    'ids': ('i915-ids.inc', ['include/drm/intel/pciids.h'],
            ['INTEL_ADLP_IDS', 'INTEL_ADLN_IDS', 'INTEL_RPLU_IDS', 'INTEL_RPLP_IDS'], [], []),
    'commands': ('i915-commands.inc', ['gt/intel_gpu_commands.h'], COMMANDS, [], []),
    'lrc': ('i915-lrc-offsets.inc', [], [], [
        ('gt/intel_lrc.c', r'^#define NOP\(x\)', r'^#define END 0'),
        ('gt/intel_lrc.c', r'^static const u8 gen12_xcs_offsets\[\] = \{', r'^\};'),
        ('gt/intel_lrc.c', r'^static const u8 gen12_rcs_offsets\[\] = \{', r'^\};')],
        [(r'\bNOP\(', 'I915_LRC_NOP('), (r'\bLRI\(', 'I915_LRC_LRI('), (r'\bPOSTED\b', 'I915_LRC_POSTED'),
         (r'\bREG16\(', 'I915_LRC_REG16('), (r'\bREG\(', 'I915_LRC_REG('), (r'\bEND\b', 'I915_LRC_END'),
         (r'\bstatic const u8\b', 'static const uint8_t')]),
    'mocs': ('i915-mocs.inc', [], [], [
        ('gt/intel_mocs.c', r'^/\* Defines for the tables \(XXX_MOCS_0', r'^#define L4_3_UC'),
        ('gt/intel_mocs.c', r'^#define MOCS_ENTRY\(', r'^\t\}'),
        ('gt/intel_mocs.c', r'^#define GEN11_MOCS_ENTRIES', r'^\t\t   L3_1_UC\)$'),
        ('gt/intel_mocs.c', r'^static const struct drm_i915_mocs_entry gen12_mocs_table\[\] = \{', r'^\};')],
        [(r'\bdrm_i915_mocs_entry\b', 'i915_mocs_entry')]),
}

# Identifiers permitted in a transcribed body besides other transcribed symbols and macro parameters.
ALLOWED = {'U', 'ULL'}
MIT_NOTICE_SOURCE = 'i915_reg.h'


def source_lines(path):
    return (TREE / path).read_text(errors='replace').split('\n')


def find_define(symbol, files):
    """Returns (path, line number, joined define text) for the first #define of symbol."""
    pattern = re.compile(r'^\s*#\s*define\s+%s\b' % re.escape(symbol))
    for path in files:
        lines = source_lines(path)
        for index, line in enumerate(lines):
            if pattern.match(line):
                text = line
                end = index
                while text.rstrip().endswith('\\'):
                    end += 1
                    text = text.rstrip()[:-1] + '\n' + lines[end]
                return path, index + 1, text
    raise SystemExit('%s: not defined in %s' % (symbol, files))


def find_block(path, start, end):
    """Returns (first line number, last line number, lines) of the first block delimited by the regexes."""
    lines = source_lines(path)
    begin = next(i for i, l in enumerate(lines) if re.match(start, l))
    stop = next(i for i in range(begin, len(lines)) if re.match(end, lines[i]))
    return begin + 1, stop + 1, lines[begin:stop + 1]


def rewrite(text, extra):
    for pattern, replacement in REWRITES + extra:
        text = re.sub(pattern, replacement, text)
    return text


def split_define(text):
    """Splits '#define NAME(params) body' into (name, params or None, body, trailing comment)."""
    comment = ''
    match = re.search(r'/\*.*\*/\s*$', text, re.S)
    if match and '\n' not in match.group(0):
        comment = match.group(0).strip()
        text = text[:match.start()].rstrip()
    match = re.match(r'^\s*#\s*define\s+(\w+)(\([^)]*\))?\s*(.*)$', text, re.S)
    return match.group(1), match.group(2), match.group(3).strip(), comment


def copyright_lines(path):
    out = []
    for line in source_lines(path)[:12]:
        stripped = line.strip(' */\t')
        if stripped.startswith('Copyright'):
            out.append(stripped)
    return out


def mit_notice():
    lines = source_lines(MIT_NOTICE_SOURCE)
    start = next(i for i, l in enumerate(lines) if 'Permission is hereby granted' in l)
    end = next(i for i, l in enumerate(lines) if 'IN THE SOFTWARE' in l or 'OTHER DEALINGS' in l)
    while 'OTHER DEALINGS' not in lines[end] and 'IN THE SOFTWARE' not in lines[end]:
        end += 1
    return [l.strip(' */\t') for l in lines[start:end + 1]]


def audited_digest(path):
    """Returns the SHA-256 the license audit recorded for path, or exits when it is missing."""
    audit = (ROOT / 'plan/ws029/i915-license-audit.md').read_text()
    for line in audit.split('\n'):
        cells = [c.strip() for c in line.split('|')]
        if len(cells) > 4 and cells[1] == path:
            if cells[3] != 'MIT':
                raise SystemExit('%s: audit verdict is %s, not MIT' % (path, cells[3]))
            return cells[2].strip('`')
    raise SystemExit('%s: not in plan/ws029/i915-license-audit.md' % path)


def source_path(path):
    return ('' if path.startswith('include/') else PREFIX) + path


def generate(name):
    output, files, symbols, blocks, extra = TARGETS[name]
    entries = []
    used = []
    for symbol in symbols:
        path, number, text = find_define(symbol, files)
        entries.append((symbol, path, number, rewrite(text, extra)))
        if path not in used:
            used.append(path)
    copied = []
    for path, start, end in blocks:
        first, last, lines = find_block(path, start, end)
        copied.append((path, first, last, [rewrite(l, extra) for l in lines]))
        if path not in used:
            used.append(path)
    for path in used:
        digest = hashlib.sha256((TREE / path).read_bytes()).hexdigest()
        if digest != audited_digest(path):
            raise SystemExit('%s: fetched file differs from the audited SHA-256' % path)
    defined = set(symbols) | HELPER_NAMES
    for symbol, path, number, text in entries:
        _, params, body, _ = split_define(text)
        parameters = set(re.findall(r'\w+', params or ''))
        for identifier in re.findall(r'(?<![0-9A-Za-z_])[A-Za-z_]\w*', body):
            if identifier in defined or identifier in parameters or identifier in ALLOWED:
                continue
            raise SystemExit('%s: body references %s which is not transcribed' % (symbol, identifier))
    lines = ['/* SPDX-License-Identifier: MIT */', '/*']
    notices = []
    for path in used:
        for line in copyright_lines(path):
            if line not in notices:
                notices.append(line)
    for line in notices:
        lines.append(' * ' + line)
    lines.append(' *')
    for line in mit_notice():
        lines.append((' * ' + line).rstrip())
    lines += [' *', ' * Modified for zedBSD: excerpted, mechanically rewritten and reformatted from the',
              ' * Linux kernel tag %s. Values are unchanged. Sources (SHA-256 of the fetched file):' % TAG]
    for path in used:
        digest = hashlib.sha256((TREE / path).read_bytes()).hexdigest()
        lines.append(' *   %s' % source_path(path))
        lines.append(' *     %s' % digest)
    lines += [REWRITE_NOTE]
    if extra:
        lines.append(' * File-specific renames: ' + '; '.join('%s -> %s' % (p.replace('\\b', '').replace('\\(', '('), r) for p, r in extra) + '.')
    lines += [' * Only the subset used by src/drivers/gpu/i915 remains.',
              ' * Generated by plan/ws029/tests/gen-inc.py; do not edit by hand.', ' */', '',
              '#ifndef DRIVERS_GPU_I915_LINUX_%s' % output.upper().replace('-', '_').replace('.', '_'),
              '#define DRIVERS_GPU_I915_LINUX_%s' % output.upper().replace('-', '_').replace('.', '_'), '']
    if name not in ('ids', 'mocs'):
        lines += ['/* Bit helpers replacing the Linux register helper macros. */', HELPERS.rstrip('\n'), '']
    for symbol, path, number, text in entries:
        _, params, body, comment = split_define(text)
        lines.append('/* %s:%d */' % (source_path(path), number))
        if '\n' in body:
            body = ' \\\n\t'.join(l.strip() for l in body.split('\n'))
        definition = '#define %s%s\t%s' % (symbol, params or '', body)
        if comment:
            definition += ' ' + comment
        lines.append(definition)
    for path, first, last, block in copied:
        lines += ['', '/* %s:%d-%d */' % (source_path(path), first, last)]
        lines += block
    lines += ['', '#endif', '']
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / output).write_text('\n'.join(lines))
    print('wrote', OUT / output, len(entries), 'definitions', len(copied), 'blocks')


if __name__ == '__main__':
    for target in (TARGETS if WHAT == 'all' else [WHAT]):
        generate(target)
