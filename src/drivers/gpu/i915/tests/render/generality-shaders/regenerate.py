#!/usr/bin/env python3
"""Compile the generality test's shaders and generate what the kernel test and the host fixtures embed: the
SPIR-V, the uniform and push-constant blocks, the vertex data and the pixels every step expects."""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: python3 src/drivers/gpu/i915/tests/render/generality-shaders/regenerate.py
#
# Writes <name>.spv next to each GLSL source and tests/fixtures/generality-shaders-gen.inc (included by
# tests/render/generality.c, the scenario "vke2", and by the host fixtures i915-vk-lower-test.c and
# i915-vk-compile-test.c).  The file holds arrays and macros only.
#
# The target is 64 x 64 pixels of R8G8B8A8_UNORM, and every fragment shader writes the four bytes of one
# 32-bit word per pixel (red the low byte): an integer result, or the bits of a float.  The expected words
# are computed here: the integers modulo 2^32 with the SPIR-V definitions (OpSDiv rounds toward zero,
# OpSMod takes the divisor's sign), the floats as IEEE-754 single precision operations in the order the
# executor's compiler lowers them (every value is exact or a correctly rounded single), with the inputs
# chosen so that no result depends on the precision of the hardware's reciprocal.
import hashlib
import math
import pathlib
import re
import struct
import subprocess

# Source, stage, C array.
SHADERS = (
    ('quad.vert', 'vertex', 'i915_vke2_quad_vert'),
    ('matrix.vert', 'vertex', 'i915_vke2_matrix_vert'),
    ('matrix.frag', 'fragment', 'i915_vke2_matrix_frag'),
    ('int.frag', 'fragment', 'i915_vke2_int_frag'),
    ('float.frag', 'fragment', 'i915_vke2_float_frag'),
    ('loop.frag', 'fragment', 'i915_vke2_loop_frag'),
    ('vary16.vert', 'vertex', 'i915_vke2_vary16_vert'),
    ('vary16.frag', 'fragment', 'i915_vke2_vary16_frag'),
    ('subset.frag', 'fragment', 'i915_vke2_subset_frag'),
    ('vin16.vert', 'vertex', 'i915_vke2_vin16_vert'),
    ('vin16.frag', 'fragment', 'i915_vke2_vin16_frag'),
    ('spill.frag', 'fragment', 'i915_vke2_spill_frag'),
    ('vio16.vert', 'vertex', 'i915_vke2_vio16_vert'),
)

SIZE = 64


def run(arguments):
    """Runs one tool and returns what it printed."""
    return subprocess.run(arguments, check=True, timeout=60, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).stdout.strip()


def f32(x):
    """Rounds a double to the nearest float (ties to even)."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bits(x):
    """The bits of a float."""
    return struct.unpack('<I', struct.pack('<f', x))[0]


def u32(v):
    return v & 0xFFFFFFFF


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def floor_f(x):
    """RNDD: a float rounded down; a zero keeps its sign."""
    if x == 0.0:
        return x
    return float(math.floor(x))


def trunc_f(x):
    """RNDZ: a float rounded toward zero; a zero keeps its sign."""
    if x == 0.0:
        return x
    value = float(math.trunc(x))
    return value if value != 0.0 else math.copysign(0.0, x)


def round_even_f(x):
    """RNDE: a float rounded to the nearest integer, ties to even."""
    value = float(round(x))
    return value if value != 0.0 else math.copysign(0.0, x)


# ------------------------------------------------------------------ matrices

# A matrix is a list of columns; M[c][r] is column c, row r.
MAT_M = [[1.0, 0.5, -1.0, 2.0], [0.0, 1.5, 1.0, -0.5], [2.0, -1.0, 0.25, 1.0], [-0.5, 0.0, 1.0, 1.0]]
MAT_R = [[0.5, 1.0, 0.0, -1.0], [1.0, -0.5, 2.0, 0.0], [0.0, 1.0, 1.0, 0.5], [1.5, 0.0, -1.0, 1.0]]
MAT_M3 = [[1.0, -2.0, 0.5], [0.25, 1.0, 3.0], [-1.0, 0.5, 2.0]]
EXTRA = [0.5, -1.25, 2.0, 7.0]
PUSH_P = [[1.0, 0.0, 2.0, -1.0], [0.5, 1.0, 0.0, 1.0], [-2.0, 0.25, 1.0, 0.0], [0.0, 1.0, -1.0, 3.0]]
PUSH_Q = [[2.0, -1.0, 0.0, 1.0], [1.0, 0.5, -2.0, 0.0], [0.0, 3.0, 1.0, -1.0], [-1.5, 0.0, 0.5, 2.0]]

# The matrix step's placement: turn is the transpose of a quarter turn (x, y) -> (-y, x), scale doubles x and y.
QUARTER = [[0.0, 1.0, 0.0, 0.0], [-1.0, 0.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]
TURN = [[QUARTER[r][c] for r in range(4)] for c in range(4)]
SCALE = [[2.0, 0.0, 0.0, 0.0], [0.0, 2.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]


def column_major_words(matrix, stride_floats=4):
    """A matrix as std140 / std430 column-major memory: column c at c * stride."""
    words = []
    for column in matrix:
        words.extend(column + [0.0] * (stride_floats - len(column)))
    return words


def row_major_words(matrix):
    """A 4 x 4 matrix as row-major memory: row r at r * 16 bytes."""
    return [matrix[c][r] for r in range(4) for c in range(4)]


def mat_mul(left, right):
    """left * right as the compiler lowers it: each sum starts at the first product and adds the others in order."""
    rows = len(left[0])
    inner = len(left)
    result = []
    for column in right:
        out = []
        for r in range(rows):
            total = None
            for k in range(inner):
                product = f32(left[k][r] * column[k])
                total = product if total is None else f32(total + product)
            out.append(total)
        result.append(out)
    return result


def mat_vec(matrix, vector):
    return mat_mul(matrix, [list(vector)])[0]


def vec_mat(vector, matrix):
    out = []
    for column in matrix:
        total = None
        for k in range(len(vector)):
            product = f32(vector[k] * column[k])
            total = product if total is None else f32(total + product)
        out.append(total)
    return out


def transpose(matrix):
    return [[matrix[c][r] for c in range(len(matrix))] for r in range(len(matrix[0]))]


def dot(a, b):
    total = None
    for k in range(len(a)):
        product = f32(a[k] * b[k])
        total = product if total is None else f32(total + product)
    return total


def matrix_pixel(x, y):
    v = [f32(float(x & 7) - 3.0), f32(float(y & 3) * 0.5), float((x >> 4) & 3), f32(1.0 + float(y >> 3))]
    l = mat_mul(MAT_M, MAT_R)
    l[2] = [f32(l[2][k] + v[k]) for k in range(4)]
    if (y & 8) == 8:
        l = transpose(l)
    a = mat_vec(l, v)
    b = vec_mat(v, PUSH_P)
    c = mat_vec([[f32(value * 0.5) for value in column] for column in PUSH_Q], v)
    o = [[f32(v[r] * a[col]) for r in range(3)] for col in range(4)]
    d = mat_vec(o, [1.0, -2.0, 0.5, 3.0])
    e = mat_vec(MAT_M3, [v[2], v[1], v[0]])
    e = [f32(e[k] + EXTRA[k]) for k in range(3)]
    g = mat_vec(transpose(MAT_M3), v[:3])
    n2 = [[v[0], v[1]], [a[2], a[3]]]
    w2 = mat_vec(n2, [2.0, -1.0])
    if (y & 4) == 0:
        groups = [a, b, c, d + [e[0]]]
    else:
        groups = [[e[1], e[2], g[0], g[1]], [g[2], w2[0], w2[1], l[0][0]],
                  [l[1][1], l[3][2], dot(a, b), o[3][0]],
                  [o[3][1], o[3][2], mat_vec(PUSH_P, v)[0], mat_vec(MAT_R, v)[3]]]
    k = x & 15
    return bits(groups[k >> 2][k & 3])


# ------------------------------------------------------------------ integers

def int_operands(x, y):
    variant = y >> 4
    h = u32(x * 2654435761 + y * 2246822519)
    h ^= h >> 15
    h = u32(h * 668265263)
    h ^= h >> 13
    a = s32(h)
    if variant == 1:
        a = a >> 20
    elif variant == 2:
        a = (a & 255) - 128
    elif variant == 3:
        a = s32(a | 0x80000000)
    k = x & 31
    b = 1 << k if k < 16 else (k - 16) * 37 + 3
    if (x >> 5) & 1:
        b = -b
    if variant == 0 and (x & 3) == 3:
        b = s32(u32(h * 2654435761) | 1)
    if (x & 7) == 5 and a != 0:
        b = a
    return a, b


def sdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def int_pixel(x, y):
    a, b = int_operands(x, y)
    assert b != 0
    assert not (a == -2 ** 31 and b == -1)
    assert a != -2 ** 31 and b != -2 ** 31
    op = y & 15
    ua, ub = u32(a), u32(b)
    if op == 0:
        r = a + b
    elif op == 1:
        r = a - b
    elif op == 2:
        r = a * b
    elif op == 3:
        r = sdiv(a, b)
    elif op == 4:
        r = a % b
    elif op == 5:
        r = ua // ub
    elif op == 6:
        r = ua % ub
    elif op == 7:
        r = s32(a << (x & 31)) ^ (a >> (y & 31)) ^ s32(ua >> ((x + y) & 31))
    elif op == 8:
        r = ((a & b) ^ (a | ~b)) + (a ^ b)
    elif op == 9:
        sign = (a > 0) - (a < 0)
        r = s32(-a) ^ abs(b) ^ (sign * 7) ^ s32(min(a, b) * 3) ^ max(a, b) ^ min(max(a, -1000), 1000 + x)
    elif op == 10:
        low, high = min(ub, 1000), max(ub, 1000)
        r = min(ua, ub) ^ (max(ua, ub) >> 1) ^ min(max(ua, low), high)
    elif op == 11:
        r = (int(a == b) | int(a != b) << 1 | int(a < b) << 2 | int(a <= b) << 3 | int(a > b) << 4 |
             int(a >= b) << 5 | int(ua < ub) << 6 | int(ua <= ub) << 7 | int(ua > ub) << 8 | int(ua >= ub) << 9)
    elif op == 12:
        r = bits(f32(float(ua)))
    elif op == 13:
        r = bits(f32(float(a)))
    elif op == 14:
        f = f32(f32(float(a >> 7)) * f32(0.37))
        r = int(f) * 3 + (int(abs(f)) >> 1)
    else:
        r = (ua >> 3) - (a >> 3) + s32(-a) * (b | 1)
    return u32(r)


# ------------------------------------------------------------------ floats

def float_pixel(x, y):
    op = y & 7
    fx = f32(f32(f32(float(x) * f32(0.53)) + f32(0.11)) - 11.0)
    fy = f32(f32(float(y >> 3) * f32(0.53)) + f32(0.73))
    h = f32(float(x - 32) * 0.5)
    if op in (0, 1):
        divisor = fy if op == 0 else -fy
        quotient = fx / divisor
        fraction = quotient - math.floor(quotient)
        assert 0.005 < fraction < 0.995, (x, y, quotient)
        r = f32(fx - f32(divisor * float(math.floor(quotient))))
    elif op == 2:
        r = round_even_f(h)
    elif op == 3:
        r = f32(round_even_f(h) + trunc_f(f32(h * 0.75)))
    elif op == 4:
        ceiling = -floor_f(-f32(h * 0.75))
        sign = 1.0 if h > 0.0 else (-1.0 if h < 0.0 else h)
        r = f32(ceiling + f32(sign * 4.0))
    elif op == 5:
        step = 0.0 if h < 1.0 else 1.0
        r = f32(step + floor_f(f32(h * f32(0.3))))
    elif op == 6:
        span = 1.0 / 32.0
        t = f32(f32(h + 16.0) * span)
        t = min(max(t, 0.0), 1.0)
        term = f32(2.0 * t)
        term = f32(3.0 - term)
        term = f32(t * term)
        r = f32(t * term)
    else:
        r = f32(f32(fx - float(math.floor(fx))) + abs(fx))
    return bits(r)


# ------------------------------------------------------------------ loops

def loop_pixel(x, y):
    n = x % 17
    acc = 1
    for i in range(n):
        if i == (y & 3):
            continue
        if acc > 3000 + y * 16:
            break
        if (i & 1) == 0:
            acc = s32(acc * 3 + i)
        else:
            acc = s32(acc + (x ^ i))
    s = 0
    for j in range(y % 7):
        k = 0
        while k <= x % 5:
            if ((j + k) & 1) == 0:
                s += j * k + 1
            else:
                s -= k
            k += 1
            if s > 20 + (y >> 3):
                break
    t = x + y + 1
    c = 0
    while True:
        t = 3 * t + 1 if (t & 1) == 1 else t // 2
        c += 1
        if not (t != 1 and c < 40):
            break
    f = 0.0
    m = 0
    while True:
        if m >= (y & 7):
            break
        f = f32(f + f32(float(m) * 0.5))
        m += 1
    return u32(u32(acc) ^ (u32(s & 255) << 16) ^ (c << 24) ^ (int(f32(f * 2.0)) << 8))


# ------------------------------------------------------------------ varyings and attributes

# The seed vary16.vert scales its varyings by, and the data attributes 2 .. 15 of vin16.vert.
SEED = (3.0, -2.0, 5.0, 1.0)
VIN_DATA = [(float(k), float(2 * k - 7), float(1 - k), 3.0) for k in range(16)]


def varying(k):
    return [SEED[c] * (k + 1) + (k, -k, k * 0.5, 16 - k)[c] for c in range(4)]


def twice_weighted(locations):
    """Twice the weighted sum vary16.frag / subset.frag add to the pixel term (exact halves)."""
    total = 0.0
    for k in locations:
        total += sum(varying(k)[c] * (4 * k + c + 1) for c in range(4))
    assert total * 2 == int(total * 2)
    return int(total * 2)


def twice_vin16():
    sums = [sum(VIN_DATA[k][c] * k for k in range(2, 16)) for c in range(4)]
    total = sums[0] * 1 + sums[1] * 3 + sums[2] * 5 + sums[3] * 7
    return int(total * 2)


def vio16_varying(k):
    """Varying k (1 .. 15) of vio16.vert: data attribute k + 1 weighted by k + 1 plus the next one (2 after 15)."""
    if k == 15:
        return [VIN_DATA[2][c] * 16.0 + VIN_DATA[15][c] for c in range(4)]
    following = k + 2 if k + 2 <= 15 else 2
    return [VIN_DATA[k + 1][c] * (k + 1) + VIN_DATA[following][c] for c in range(4)]


def twice_vio16():
    """Twice the weighted sum vary16.frag adds to the pixel term over vio16.vert's varyings (whole numbers)."""
    total = 0.0
    for k in range(1, 16):
        total += sum(vio16_varying(k)[c] * (4 * k + c + 1) for c in range(4))
    assert total == int(total) and abs(total) < (1 << 23)
    return int(total * 2)


# ------------------------------------------------------------------ spilling

def spill_pairs(directory):
    """The products spill.frag sums, in its order, read from its source."""
    source = (directory / 'spill.frag').read_text()
    pairs = [(int(a), int(b)) for a, b in re.findall(r's \+= t(\d+) \* t(\d+);', source)]
    assert len(pairs) == 96
    return pairs


def spill_pixel(x, y, pairs):
    """spill.frag: 96 values of the pixel, the per-pixel loop over a and b, then the sum, in the lowering's order."""
    fx, fy = float(x), float(y)
    t = [f32(f32(fx * float(k + 1)) + fy) for k in range(96)]
    a = t[7]
    b = t[50]
    n = int(f32(fx + fy)) % 5
    for _ in range(n):
        a = f32(f32(a * 0.5) + t[3])
        b = f32(f32(b + f32(t[90] * 0.25)) - a)
    s = 0.0
    for first, second in pairs:
        s = f32(s + f32(t[first] * t[second]))
    s = f32(s + f32(a + b))
    return bits(s)


def c_words(lines, symbol, words, kind='uint32_t', per_line=6, fmt='0x{:08x}U'):
    lines.append(f'static const {kind} {symbol}[{len(words)}] = {{')
    for offset in range(0, len(words), per_line):
        lines.append('\t' + ', '.join(fmt.format(word) for word in words[offset:offset + per_line]) + ',')
    lines.append('};')


def image(function):
    return [function(x, y) for y in range(SIZE) for x in range(SIZE)]


def main():
    directory = pathlib.Path(__file__).resolve().parent
    fixtures = directory.parent.parent / 'fixtures'
    version = run(['glslc', '--version']).splitlines()[0]
    lines = [
        '/*',
        ' * zedBSD',
        ' * Copyright (C) 2026 Awe Morris',
        ' *',
        ' * SPDX-License-Identifier: Zlib',
        ' */',
        '',
        '/*',
        ' * GENERATED FILE - the SPIR-V, the inputs and the expected pixels of the generality test.',
        ' * Do not edit by hand.',
        ' *',
        ' * Generator : src/drivers/gpu/i915/tests/render/generality-shaders/regenerate.py',
        f' * Compiler  : {version} (--target-env=vulkan1.1 --target-spv=spv1.0 -O0)',
        ' *',
        ' * A pixel is RGBA8 with red in the low byte; a float is its IEEE-754 bits.',
        ' */',
    ]
    for source, stage, symbol in SHADERS:
        path = directory / source
        output = directory / (source + '.spv')
        run(['glslc', '--target-env=vulkan1.1', '--target-spv=spv1.0', f'-fshader-stage={stage}',
             '-O0', str(path), '-o', str(output)])
        run(['spirv-val', '--target-env', 'vulkan1.1', str(output)])
        binary = output.read_bytes()
        words = struct.unpack(f'<{len(binary) // 4}I', binary)
        lines.extend(['', f'/* {source} (sha256 {hashlib.sha256(path.read_bytes()).hexdigest()}), {len(words)} words. */'])
        c_words(lines, symbol, words)

    lines.extend(['', '/* The matrix step\'s uniform block (binding 0, std140): m, r (row-major), m3 (MatrixStride 16), extra. */'])
    matrices = column_major_words(MAT_M) + row_major_words(MAT_R) + column_major_words(MAT_M3) + EXTRA
    assert len(matrices) == 48
    c_words(lines, 'i915_vke2_matrices', [bits(v) for v in matrices], per_line=4)
    lines.extend(['', '/* The matrix step\'s placement block (binding 1, std140): turn (row-major), scale. */'])
    placement = row_major_words(TURN) + column_major_words(SCALE)
    c_words(lines, 'i915_vke2_placement', [bits(v) for v in placement], per_line=4)
    lines.extend(['', '/* The matrix step\'s push constants (std430): p (row-major), q. */'])
    push = row_major_words(PUSH_P) + column_major_words(PUSH_Q)
    c_words(lines, 'i915_vke2_push', [bits(v) for v in push], per_line=4)

    # The matrix step's quad: where each corner of the target must land, sent through the inverse chain.
    lines.extend(['', '/*',
                  ' * The matrix step\'s four vertices (x, y of the position before the chain), top left, top right,',
                  ' * bottom right, bottom left: the chain transpose(turn) * scale takes each to its corner of the target.',
                  ' */'])
    chain = mat_mul(transpose(TURN), SCALE)
    before = []
    for fx, fy in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)):
        # The chain is a quarter turn and a doubling: (px, py) -> (-2 py, 2 px), so px = fy / 2, py = -fx / 2.
        px, py = fy / 2.0, -fx / 2.0
        assert mat_vec(chain, [px, py, 0.0, 1.0])[:2] == [fx, fy]
        before.extend([bits(px), bits(py)])
    c_words(lines, 'i915_vke2_matrix_corners', before, per_line=2)

    lines.extend(['', '/* The seed of vary16.vert (location 2), and the data attributes 2 .. 15 of vin16.vert. */'])
    c_words(lines, 'i915_vke2_seed', [bits(v) for v in SEED], per_line=4)
    c_words(lines, 'i915_vke2_vin_data', [bits(v) for k in range(2, 16) for v in VIN_DATA[k]], per_line=4)

    lines.extend(['', '/*',
                  ' * The varying and attribute steps write floor(x) * 1000 + floor(y) * 100000 plus a constant: twice',
                  ' * the constant of each (every sum is a whole number of halves).',
                  ' */',
                  f'#define I915_VKE2_VARY16_TWICE {twice_weighted(range(1, 16))}U',
                  f'#define I915_VKE2_SUBSET_TWICE {twice_weighted((11, 1, 15, 6))}U',
                  f'#define I915_VKE2_VIN16_TWICE {twice_vin16()}U',
                  f'#define I915_VKE2_VIO16_TWICE {twice_vio16()}U'])

    lines.extend(['', '/* Every pixel of the matrix step: the bits of the float result it picks. */'])
    c_words(lines, 'i915_vke2_matrix_expected', image(matrix_pixel), per_line=8)
    lines.extend(['', '/* Every pixel of the integer step: its 32-bit result. */'])
    c_words(lines, 'i915_vke2_int_expected', image(int_pixel), per_line=8)
    lines.extend(['', '/* Every pixel of the float step: the bits of its result. */'])
    c_words(lines, 'i915_vke2_float_expected', image(float_pixel), per_line=8)
    lines.extend(['', '/* Every pixel of the loop step: its 32-bit result. */'])
    c_words(lines, 'i915_vke2_loop_expected', image(loop_pixel), per_line=8)
    lines.extend(['', '/* Every pixel of the spill step: the bits of its sum. */'])
    pairs = spill_pairs(directory)
    c_words(lines, 'i915_vke2_spill_expected', image(lambda x, y: spill_pixel(x, y, pairs)), per_line=8)
    lines.append('')
    (fixtures / 'generality-shaders-gen.inc').write_text('\n'.join(lines))


if __name__ == '__main__':
    main()
