#!/usr/bin/env python3
"""
Generates valid SPIR-V 1.0 shader modules into Minecraft.Client/Windows64/Render/vk_shaders_spv.h
for LegacyCraft's Vulkan backend and GDraw Vulkan implementation.
No external glslc / Vulkan SDK required.
"""
import os
import struct

SPV_MAGIC = 0x07230203
SPV_VERSION_1_0 = 0x00010000

# Opcodes
OP_EXT_INST_IMPORT = 11
OP_EXT_INST = 12
OP_MEMORY_MODEL = 14
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_CAPABILITY = 17
OP_TYPE_VOID = 19
OP_TYPE_BOOL = 20
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_MATRIX = 24
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLER = 26
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_ARRAY = 28
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_TYPE_FUNCTION = 33
OP_CONSTANT_TRUE = 41
OP_CONSTANT_FALSE = 42
OP_CONSTANT = 43
OP_CONSTANT_COMPOSITE = 44
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_VARIABLE = 59
OP_LOAD = 61
OP_STORE = 62
OP_ACCESS_CHAIN = 65
OP_DECORATE = 71
OP_MEMBER_DECORATE = 72
OP_VECTOR_SHUFFLE = 79
OP_COMPOSITE_CONSTRUCT = 80
OP_COMPOSITE_EXTRACT = 81
OP_IMAGE_SAMPLE_IMPLICIT_LOD = 87
OP_CONVERT_STOF = 111
OP_CONVERT_UTOF = 112
OP_BITCAST = 124
OP_FNEGATE = 127
OP_IADD = 128
OP_FADD = 129
OP_ISUB = 130
OP_FSUB = 131
OP_FMUL = 133
OP_FDIV = 136
OP_VECTOR_TIMES_SCALAR = 142
OP_MATRIX_TIMES_VECTOR = 145
OP_DOT = 148
OP_LOGICAL_OR = 166
OP_LOGICAL_AND = 167
OP_LOGICAL_NOT = 168
OP_SELECT = 169
OP_IEQUAL = 170
OP_INOT_EQUAL = 171
OP_SGREATER_EQUAL = 179
OP_SLESS_THAN = 177
OP_FORD_LESS_THAN = 184
OP_FORD_GREATER_THAN = 186
OP_FORD_LESS_EQUAL = 188
OP_FORD_GREATER_EQUAL = 190
OP_SHIFT_RIGHT_LOGICAL = 194
OP_BITWISE_XOR = 198
OP_BITWISE_AND = 199
OP_DPDX = 207
OP_DPDY = 208
OP_SELECTION_MERGE = 247
OP_LABEL = 248
OP_BRANCH = 249
OP_BRANCH_CONDITIONAL = 250
OP_KILL = 252
OP_RETURN = 253

# GLSL.std.450 extinst opcodes
GLSL_FABS = 4
GLSL_SIN = 13
GLSL_COS = 14
GLSL_POW = 26
GLSL_EXP = 27
GLSL_SQRT = 31
GLSL_FMIN = 37
GLSL_FMAX = 40
GLSL_FCLAMP = 43
GLSL_FMIX = 46
GLSL_STEP = 48
GLSL_LENGTH = 66
GLSL_CROSS = 68
GLSL_NORMALIZE = 69

# Storage classes
SC_UNIFORM_CONSTANT = 0
SC_INPUT = 1
SC_UNIFORM = 2
SC_OUTPUT = 3
SC_FUNCTION = 7

# Decorations
DEC_BLOCK = 2
DEC_COL_MAJOR = 5
DEC_ARRAY_STRIDE = 6
DEC_MATRIX_STRIDE = 7
DEC_BUILTIN = 11
DEC_LOCATION = 30
DEC_BINDING = 33
DEC_DESCRIPTOR_SET = 34
DEC_OFFSET = 35

BUILTIN_POSITION = 0
EXEC_VERTEX = 0
EXEC_FRAGMENT = 4
EXEC_MODE_ORIGIN_UPPER_LEFT = 7


def encode_str(s: str):
    b = s.encode("utf-8") + b"\x00"
    while len(b) % 4 != 0:
        b += b"\x00"
    return list(struct.unpack("<%dI" % (len(b) // 4), b))


def f32_bits(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(v)))[0]


def i32_bits(v: int) -> int:
    return struct.unpack("<I", struct.pack("<i", int(v)))[0]


class SpvModule:
    def __init__(self, exec_model: int):
        self.exec_model = exec_model
        self.next_id = 1
        self.cap_words = []
        self.ext_words = []
        self.mem_words = []
        self.entry_words = []
        self.exec_mode_words = []
        self.dec_words = []
        self.type_words = []
        self.glob_var_words = []
        self.func_words = []
        self.local_var_words = []
        self.interface_ids = []
        self.consts = {}
        self.ptr_types = {}

        self.emit_cap(1)  # Capability Shader
        self.glsl_ext = self.fresh()
        self.ext_words.extend(self.inst(OP_EXT_INST_IMPORT, self.glsl_ext, *encode_str("GLSL.std.450")))
        self.mem_words.extend(self.inst(OP_MEMORY_MODEL, 0, 1))  # Logical, GLSL450

        # Core scalar/vector/matrix types
        self.t_void = self.decl_type(OP_TYPE_VOID)
        self.t_bool = self.decl_type(OP_TYPE_BOOL)
        self.t_i32 = self.decl_type(OP_TYPE_INT, 32, 1)
        self.t_u32 = self.decl_type(OP_TYPE_INT, 32, 0)
        self.t_f32 = self.decl_type(OP_TYPE_FLOAT, 32)
        self.t_vec2 = self.decl_type(OP_TYPE_VECTOR, self.t_f32, 2)
        self.t_vec3 = self.decl_type(OP_TYPE_VECTOR, self.t_f32, 3)
        self.t_vec4 = self.decl_type(OP_TYPE_VECTOR, self.t_f32, 4)
        self.t_ivec4 = self.decl_type(OP_TYPE_VECTOR, self.t_i32, 4)
        self.t_uvec4 = self.decl_type(OP_TYPE_VECTOR, self.t_u32, 4)
        self.t_bvec3 = self.decl_type(OP_TYPE_VECTOR, self.t_bool, 3)
        self.t_mat4 = self.decl_type(OP_TYPE_MATRIX, self.t_vec4, 4)
        self.t_fn_void = self.decl_type(OP_TYPE_FUNCTION, self.t_void)

        # Sampled 2D image type
        self.t_img2d = self.decl_type(OP_TYPE_IMAGE, self.t_f32, 1, 0, 0, 0, 1, 0)
        self.t_sampled_img2d = self.decl_type(OP_TYPE_SAMPLED_IMAGE, self.t_img2d)

    def fresh(self) -> int:
        rid = self.next_id
        self.next_id += 1
        return rid

    def inst(self, opcode: int, *args):
        wc = 1 + len(args)
        return [(wc << 16) | opcode] + list(args)

    def emit_cap(self, cap: int):
        self.cap_words.extend(self.inst(OP_CAPABILITY, cap))

    def decl_type(self, opcode: int, *args) -> int:
        tid = self.fresh()
        self.type_words.extend(self.inst(opcode, tid, *args))
        return tid

    def ptr_type(self, sc: int, base_t: int) -> int:
        key = (sc, base_t)
        if key not in self.ptr_types:
            self.ptr_types[key] = self.decl_type(OP_TYPE_POINTER, sc, base_t)
        return self.ptr_types[key]

    def const_f32(self, v: float) -> int:
        bits = f32_bits(v)
        key = ("f32", bits)
        if key not in self.consts:
            cid = self.fresh()
            self.type_words.extend(self.inst(OP_CONSTANT, self.t_f32, cid, bits))
            self.consts[key] = cid
        return self.consts[key]

    def const_i32(self, v: int) -> int:
        bits = i32_bits(v)
        key = ("i32", bits)
        if key not in self.consts:
            cid = self.fresh()
            self.type_words.extend(self.inst(OP_CONSTANT, self.t_i32, cid, bits))
            self.consts[key] = cid
        return self.consts[key]

    def const_u32(self, v: int) -> int:
        bits = int(v) & 0xFFFFFFFF
        key = ("u32", bits)
        if key not in self.consts:
            cid = self.fresh()
            self.type_words.extend(self.inst(OP_CONSTANT, self.t_u32, cid, bits))
            self.consts[key] = cid
        return self.consts[key]

    def const_bool(self, v: bool) -> int:
        key = ("bool", bool(v))
        if key not in self.consts:
            cid = self.fresh()
            self.type_words.extend(self.inst(OP_CONSTANT_TRUE if v else OP_CONSTANT_FALSE, self.t_bool, cid))
            self.consts[key] = cid
        return self.consts[key]

    def const_vec(self, vec_t: int, elem_ids) -> int:
        key = ("vec", vec_t, tuple(elem_ids))
        if key not in self.consts:
            cid = self.fresh()
            self.type_words.extend(self.inst(OP_CONSTANT_COMPOSITE, vec_t, cid, *elem_ids))
            self.consts[key] = cid
        return self.consts[key]

    def decorate(self, target: int, dec: int, *args):
        self.dec_words.extend(self.inst(OP_DECORATE, target, dec, *args))

    def member_decorate(self, struct_t: int, member_idx: int, dec: int, *args):
        self.dec_words.extend(self.inst(OP_MEMBER_DECORATE, struct_t, member_idx, dec, *args))

    def add_io_var(self, sc: int, val_t: int, location: int = None, builtin: int = None) -> int:
        vid = self.fresh()
        pt = self.ptr_type(sc, val_t)
        self.glob_var_words.extend(self.inst(OP_VARIABLE, pt, vid, sc))
        if location is not None:
            self.decorate(vid, DEC_LOCATION, location)
        if builtin is not None:
            self.decorate(vid, DEC_BUILTIN, builtin)
        self.interface_ids.append(vid)
        return vid

    def add_uniform_struct(self, members, set_idx: int, binding_idx: int) -> tuple:
        """members = list of (type_id, byte_offset, extra_decs). extra_decs: ('mat', 16) or None"""
        st = self.decl_type(OP_TYPE_STRUCT, *[m[0] for m in members])
        self.decorate(st, DEC_BLOCK)
        for idx, (mt, off, extra) in enumerate(members):
            self.member_decorate(st, idx, DEC_OFFSET, off)
            if extra == "mat":
                self.member_decorate(st, idx, DEC_COL_MAJOR)
                self.member_decorate(st, idx, DEC_MATRIX_STRIDE, 16)
        vid = self.fresh()
        pt = self.ptr_type(SC_UNIFORM, st)
        self.glob_var_words.extend(self.inst(OP_VARIABLE, pt, vid, SC_UNIFORM))
        self.decorate(vid, DEC_DESCRIPTOR_SET, set_idx)
        self.decorate(vid, DEC_BINDING, binding_idx)
        return vid, st

    def add_sampler2d(self, set_idx: int, binding_idx: int) -> int:
        vid = self.fresh()
        pt = self.ptr_type(SC_UNIFORM_CONSTANT, self.t_sampled_img2d)
        self.glob_var_words.extend(self.inst(OP_VARIABLE, pt, vid, SC_UNIFORM_CONSTANT))
        self.decorate(vid, DEC_DESCRIPTOR_SET, set_idx)
        self.decorate(vid, DEC_BINDING, binding_idx)
        return vid

    def make_array(self, elem_t: int, length: int, stride: int) -> int:
        len_id = self.const_u32(length)
        at = self.decl_type(OP_TYPE_ARRAY, elem_t, len_id)
        self.decorate(at, DEC_ARRAY_STRIDE, stride)
        return at

    # Function code emission
    def begin_main(self):
        self.main_id = self.fresh()
        self.entry_words.extend(self.inst(OP_ENTRY_POINT, self.exec_model, self.main_id, *encode_str("main"), *self.interface_ids))
        if self.exec_model == EXEC_FRAGMENT:
            self.exec_mode_words.extend(self.inst(OP_EXECUTION_MODE, self.main_id, EXEC_MODE_ORIGIN_UPPER_LEFT))
        self.func_head = self.inst(OP_FUNCTION, self.t_void, self.main_id, 0, self.t_fn_void)
        self.func_head += self.inst(OP_LABEL, self.fresh())

    def end_main(self):
        self.emit(OP_RETURN)
        self.func_words.extend(self.func_head)
        self.func_words.extend(self.local_var_words)
        self.func_words.extend(self.body_words)
        self.func_words.extend(self.inst(OP_FUNCTION_END))

    body_words = None

    def init_body(self):
        self.body_words = []

    def emit(self, opcode: int, *args):
        self.body_words.extend(self.inst(opcode, *args))

    def op(self, opcode: int, res_t: int, *args) -> int:
        rid = self.fresh()
        self.emit(opcode, res_t, rid, *args)
        return rid

    def glsl(self, ext_op: int, res_t: int, *args) -> int:
        rid = self.fresh()
        self.emit(OP_EXT_INST, res_t, rid, self.glsl_ext, ext_op, *args)
        return rid

    def load(self, res_t: int, ptr: int) -> int:
        return self.op(OP_LOAD, res_t, ptr)

    def store(self, ptr: int, val: int):
        self.emit(OP_STORE, ptr, val)

    def local_var(self, val_t: int) -> int:
        vid = self.fresh()
        pt = self.ptr_type(SC_FUNCTION, val_t)
        self.local_var_words.extend(self.inst(OP_VARIABLE, pt, vid, SC_FUNCTION))
        return vid

    def access(self, res_val_t: int, sc: int, base_ptr: int, *indices) -> int:
        pt = self.ptr_type(sc, res_val_t)
        return self.op(OP_ACCESS_CHAIN, pt, base_ptr, *indices)

    def ubo_load(self, ubo_ptr: int, res_t: int, *member_indices) -> int:
        idx_ids = [self.const_i32(i) for i in member_indices]
        p = self.access(res_t, SC_UNIFORM, ubo_ptr, *idx_ids)
        return self.load(res_t, p)

    def extract(self, res_t: int, comp: int, idx: int) -> int:
        return self.op(OP_COMPOSITE_EXTRACT, res_t, comp, idx)

    def construct(self, res_t: int, *elems) -> int:
        return self.op(OP_COMPOSITE_CONSTRUCT, res_t, *elems)

    def shuffle(self, res_t: int, v1: int, v2: int, *comps) -> int:
        return self.op(OP_VECTOR_SHUFFLE, res_t, v1, v2, *comps)

    def emit_if(self, cond_id: int, then_fn, else_fn=None):
        l_then = self.fresh()
        l_else = self.fresh() if else_fn else None
        l_merge = self.fresh()
        self.emit(OP_SELECTION_MERGE, l_merge, 0)
        if else_fn:
            self.emit(OP_BRANCH_CONDITIONAL, cond_id, l_then, l_else)
        else:
            self.emit(OP_BRANCH_CONDITIONAL, cond_id, l_then, l_merge)
        self.emit(OP_LABEL, l_then)
        terminated_then = bool(then_fn())
        if not terminated_then:
            self.emit(OP_BRANCH, l_merge)
        if else_fn:
            self.emit(OP_LABEL, l_else)
            terminated_else = bool(else_fn())
            if not terminated_else:
                self.emit(OP_BRANCH, l_merge)
        self.emit(OP_LABEL, l_merge)

    def unpack_s8(self, u32_val: int, shift_bits: int) -> int:
        shifted = self.op(OP_SHIFT_RIGHT_LOGICAL, self.t_u32, u32_val, self.const_u32(shift_bits)) if shift_bits else u32_val
        masked = self.op(OP_BITWISE_AND, self.t_u32, shifted, self.const_u32(0xFF))
        ival = self.op(OP_BITCAST, self.t_i32, masked)
        ge128 = self.op(OP_SGREATER_EQUAL, self.t_bool, ival, self.const_i32(128))
        sub256 = self.op(OP_ISUB, self.t_i32, ival, self.const_i32(256))
        signed_i = self.op(OP_SELECT, self.t_i32, ge128, sub256, ival)
        return self.op(OP_CONVERT_STOF, self.t_f32, signed_i)

    def unpack_s16(self, u32_val: int, shift_bits: int) -> int:
        shifted = self.op(OP_SHIFT_RIGHT_LOGICAL, self.t_u32, u32_val, self.const_u32(shift_bits)) if shift_bits else u32_val
        masked = self.op(OP_BITWISE_AND, self.t_u32, shifted, self.const_u32(0xFFFF))
        ival = self.op(OP_BITCAST, self.t_i32, masked)
        ge32768 = self.op(OP_SGREATER_EQUAL, self.t_bool, ival, self.const_i32(32768))
        sub65536 = self.op(OP_ISUB, self.t_i32, ival, self.const_i32(65536))
        signed_i = self.op(OP_SELECT, self.t_i32, ge32768, sub65536, ival)
        return self.op(OP_CONVERT_STOF, self.t_f32, signed_i)

    def emit_mat4_mul_vec4_cols(self, mat_cols, v4: int) -> int:
        x = self.extract(self.t_f32, v4, 0)
        y = self.extract(self.t_f32, v4, 1)
        z = self.extract(self.t_f32, v4, 2)
        w = self.extract(self.t_f32, v4, 3)
        t0 = self.op(OP_VECTOR_TIMES_SCALAR, self.t_vec4, mat_cols[0], x)
        t1 = self.op(OP_VECTOR_TIMES_SCALAR, self.t_vec4, mat_cols[1], y)
        t2 = self.op(OP_VECTOR_TIMES_SCALAR, self.t_vec4, mat_cols[2], z)
        t3 = self.op(OP_VECTOR_TIMES_SCALAR, self.t_vec4, mat_cols[3], w)
        s01 = self.op(OP_FADD, self.t_vec4, t0, t1)
        s23 = self.op(OP_FADD, self.t_vec4, t2, t3)
        return self.op(OP_FADD, self.t_vec4, s01, s23)

    def build(self) -> list:
        header = [SPV_MAGIC, SPV_VERSION_1_0, 0, self.next_id, 0]
        return (
            header
            + self.cap_words
            + self.ext_words
            + self.mem_words
            + self.entry_words
            + self.exec_mode_words
            + self.dec_words
            + self.type_words
            + self.glob_var_words
            + self.func_words
        )


def add_world_vs_ubo(m: SpvModule) -> int:
    arr4 = m.make_array(m.t_vec4, 4, 16)
    arr2 = m.make_array(m.t_vec4, 2, 16)
    members = [
        (m.t_mat4, 0, "mat"),      # 0: u_WVP
        (m.t_mat4, 64, "mat"),     # 1: u_MV
        (m.t_mat4, 128, "mat"),    # 2: u_TexMatrix
        (arr4,     192, None),     # 3: u_TexGen[4]
        (arr2,     256, None),     # 4: u_LightDir[2]
        (arr2,     288, None),     # 5: u_LightColor[2]
        (m.t_vec4, 320, None),     # 6: u_LightAmbient
        (m.t_vec2, 336, None),     # 7: u_GlobalLightUV
        (m.t_i32,  344, None),     # 8: u_LightingEnable
        (m.t_i32,  348, None),     # 9: u_UseTexGen
    ]
    vid, _ = m.add_uniform_struct(members, 0, 0)
    return vid


def gen_mc_std_vert():
    m = SpvModule(EXEC_VERTEX)
    m.init_body()
    ubo = add_world_vs_ubo(m)

    in_pos   = m.add_io_var(SC_INPUT, m.t_vec3, location=0)
    in_uv    = m.add_io_var(SC_INPUT, m.t_vec2, location=1)
    in_col   = m.add_io_var(SC_INPUT, m.t_u32,  location=2)
    in_norm  = m.add_io_var(SC_INPUT, m.t_u32,  location=3)
    in_extra = m.add_io_var(SC_INPUT, m.t_u32,  location=4)

    out_gl_pos  = m.add_io_var(SC_OUTPUT, m.t_vec4, builtin=BUILTIN_POSITION)
    out_uv      = m.add_io_var(SC_OUTPUT, m.t_vec4, location=0)
    out_lightuv = m.add_io_var(SC_OUTPUT, m.t_vec2, location=1)
    out_fogdist = m.add_io_var(SC_OUTPUT, m.t_f32,  location=2)
    out_col     = m.add_io_var(SC_OUTPUT, m.t_vec4, location=3)
    out_viewpos = m.add_io_var(SC_OUTPUT, m.t_vec3, location=4)

    v_col_var = m.local_var(m.t_vec4)
    v_uv_var = m.local_var(m.t_vec4)
    v_luv_var = m.local_var(m.t_vec2)

    m.begin_main()

    pos3 = m.load(m.t_vec3, in_pos)
    one = m.const_f32(1.0)
    zero = m.const_f32(0.0)
    pos4 = m.construct(m.t_vec4, m.extract(m.t_f32, pos3, 0), m.extract(m.t_f32, pos3, 1), m.extract(m.t_f32, pos3, 2), one)

    wvp = m.ubo_load(ubo, m.t_mat4, 0)
    clip = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, wvp, pos4)
    cx = m.extract(m.t_f32, clip, 0)
    cy = m.extract(m.t_f32, clip, 1)
    cz = m.extract(m.t_f32, clip, 2)
    cw = m.extract(m.t_f32, clip, 3)
    half = m.const_f32(0.5)
    nz = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, cz, half), m.op(OP_FMUL, m.t_f32, cw, half))
    clip_vk = m.construct(m.t_vec4, cx, cy, nz, cw)
    m.store(out_gl_pos, clip_vk)

    mv = m.ubo_load(ubo, m.t_mat4, 1)
    view_pos = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, mv, pos4)
    view_xyz = m.shuffle(m.t_vec3, view_pos, view_pos, 0, 1, 2)
    m.store(out_viewpos, view_xyz)
    fog_d = m.glsl(GLSL_LENGTH, m.t_f32, view_xyz)
    m.store(out_fogdist, fog_d)

    # Color unpack
    col_u = m.load(m.t_u32, in_col)
    inv255 = m.const_f32(1.0 / 255.0)
    def col_ch(sh):
        s = m.op(OP_SHIFT_RIGHT_LOGICAL, m.t_u32, col_u, m.const_u32(sh)) if sh else col_u
        a = m.op(OP_BITWISE_AND, m.t_u32, s, m.const_u32(0xFF))
        f = m.op(OP_CONVERT_UTOF, m.t_f32, a)
        return m.op(OP_FMUL, m.t_f32, f, inv255)
    base_col = m.construct(m.t_vec4, col_ch(24), col_ch(16), col_ch(8), col_ch(0))
    m.store(v_col_var, base_col)

    light_en = m.ubo_load(ubo, m.t_i32, 8)
    is_lit = m.op(OP_INOT_EQUAL, m.t_bool, light_en, m.const_i32(0))
    def do_lighting():
        norm_u = m.load(m.t_u32, in_norm)
        inv127 = m.const_f32(1.0 / 127.0)
        nx = m.op(OP_FMUL, m.t_f32, m.unpack_s8(norm_u, 0), inv127)
        ny = m.op(OP_FMUL, m.t_f32, m.unpack_s8(norm_u, 8), inv127)
        nz_n = m.op(OP_FMUL, m.t_f32, m.unpack_s8(norm_u, 16), inv127)
        n4 = m.construct(m.t_vec4, nx, ny, nz_n, zero)
        vn4 = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, mv, n4)
        vn3 = m.glsl(GLSL_NORMALIZE, m.t_vec3, m.shuffle(m.t_vec3, vn4, vn4, 0, 1, 2))
        amb4 = m.ubo_load(ubo, m.t_vec4, 6)
        accum = m.shuffle(m.t_vec3, amb4, amb4, 0, 1, 2)
        for li in (0, 1):
            lc4 = m.ubo_load(ubo, m.t_vec4, 5, li)
            la = m.extract(m.t_f32, lc4, 3)
            en_l = m.op(OP_FORD_GREATER_THAN, m.t_bool, la, half)
            ld4 = m.ubo_load(ubo, m.t_vec4, 4, li)
            ld3 = m.glsl(GLSL_NORMALIZE, m.t_vec3, m.shuffle(m.t_vec3, ld4, ld4, 0, 1, 2))
            ndotl = m.glsl(GLSL_FMAX, m.t_f32, m.op(OP_DOT, m.t_f32, vn3, ld3), zero)
            contrib = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, m.shuffle(m.t_vec3, lc4, lc4, 0, 1, 2), ndotl)
            next_acc = m.op(OP_FADD, m.t_vec3, accum, contrib)
            acc_var = m.local_var(m.t_vec3)
            m.store(acc_var, accum)
            def add_contrib(nxt=next_acc, var=acc_var):
                m.store(var, nxt)
                return False
            m.emit_if(en_l, add_contrib)
            accum = m.load(m.t_vec3, acc_var)
        ones3 = m.const_vec(m.t_vec3, [one, one, one])
        clamped = m.glsl(GLSL_FMIN, m.t_vec3, accum, ones3)
        cur_c = m.load(m.t_vec4, v_col_var)
        cur_rgb = m.shuffle(m.t_vec3, cur_c, cur_c, 0, 1, 2)
        new_rgb = m.op(OP_FMUL, m.t_vec3, cur_rgb, clamped)
        new_c = m.construct(m.t_vec4, m.extract(m.t_f32, new_rgb, 0), m.extract(m.t_f32, new_rgb, 1), m.extract(m.t_f32, new_rgb, 2), m.extract(m.t_f32, cur_c, 3))
        m.store(v_col_var, new_c)
        return False
    m.emit_if(is_lit, do_lighting)
    m.store(out_col, m.load(m.t_vec4, v_col_var))

    # UV / TexGen
    tex_mat = m.ubo_load(ubo, m.t_mat4, 2)
    texgen_en = m.ubo_load(ubo, m.t_i32, 9)
    use_tg = m.op(OP_INOT_EQUAL, m.t_bool, texgen_en, m.const_i32(0))
    def do_texgen():
        g0 = m.op(OP_DOT, m.t_f32, pos4, m.ubo_load(ubo, m.t_vec4, 3, 0))
        g1 = m.op(OP_DOT, m.t_f32, pos4, m.ubo_load(ubo, m.t_vec4, 3, 1))
        g2 = m.op(OP_DOT, m.t_f32, pos4, m.ubo_load(ubo, m.t_vec4, 3, 2))
        g3 = m.op(OP_DOT, m.t_f32, view_pos, m.ubo_load(ubo, m.t_vec4, 3, 3))
        g = m.construct(m.t_vec4, g0, g1, g2, g3)
        m.store(v_uv_var, m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, tex_mat, g))
        return False
    def do_std_uv():
        uv_raw = m.load(m.t_vec2, in_uv)
        ux = m.extract(m.t_f32, uv_raw, 0)
        uy = m.extract(m.t_f32, uv_raw, 1)
        gt1 = m.op(OP_FORD_GREATER_THAN, m.t_bool, ux, one)
        le2 = m.op(OP_FORD_LESS_EQUAL, m.t_bool, ux, m.const_f32(2.0))
        wrap_cond = m.op(OP_LOGICAL_AND, m.t_bool, gt1, le2)
        ux_wrapped = m.op(OP_SELECT, m.t_f32, wrap_cond, m.op(OP_FSUB, m.t_f32, ux, one), ux)
        uv4 = m.construct(m.t_vec4, ux_wrapped, uy, zero, one)
        tuv = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, tex_mat, uv4)
        res_uv = m.construct(m.t_vec4, m.extract(m.t_f32, tuv, 0), m.extract(m.t_f32, tuv, 1), zero, one)
        m.store(v_uv_var, res_uv)
        return False
    m.emit_if(use_tg, do_texgen, do_std_uv)
    m.store(out_uv, m.load(m.t_vec4, v_uv_var))

    # Lightmap UV
    extra_u = m.load(m.t_u32, in_extra)
    is_global_l = m.op(OP_IEQUAL, m.t_bool, extra_u, m.const_u32(0xFE00FE00))
    def do_global_luv():
        gluv = m.ubo_load(ubo, m.t_vec2, 7)
        off = m.const_f32(0.5 / 16.0)
        offs = m.const_vec(m.t_vec2, [off, off])
        m.store(v_luv_var, m.op(OP_FADD, m.t_vec2, gluv, offs))
        return False
    def do_vert_luv():
        lx = m.unpack_s16(extra_u, 0)
        ly = m.unpack_s16(extra_u, 16)
        eight = m.const_f32(8.0)
        inv256 = m.const_f32(1.0 / 256.0)
        fx = m.op(OP_FMUL, m.t_f32, m.op(OP_FADD, m.t_f32, lx, eight), inv256)
        fy = m.op(OP_FMUL, m.t_f32, m.op(OP_FADD, m.t_f32, ly, eight), inv256)
        m.store(v_luv_var, m.construct(m.t_vec2, fx, fy))
        return False
    m.emit_if(is_global_l, do_global_luv, do_vert_luv)
    m.store(out_lightuv, m.load(m.t_vec2, v_luv_var))

    m.end_main()
    return m.build()


def gen_mc_comp_vert():
    m = SpvModule(EXEC_VERTEX)
    m.init_body()
    ubo = add_world_vs_ubo(m)

    in_a = m.add_io_var(SC_INPUT, m.t_ivec4, location=0)
    in_b = m.add_io_var(SC_INPUT, m.t_ivec4, location=1)

    out_gl_pos  = m.add_io_var(SC_OUTPUT, m.t_vec4, builtin=BUILTIN_POSITION)
    out_uv      = m.add_io_var(SC_OUTPUT, m.t_vec4, location=0)
    out_lightuv = m.add_io_var(SC_OUTPUT, m.t_vec2, location=1)
    out_fogdist = m.add_io_var(SC_OUTPUT, m.t_f32,  location=2)
    out_col     = m.add_io_var(SC_OUTPUT, m.t_vec4, location=3)
    out_viewpos = m.add_io_var(SC_OUTPUT, m.t_vec3, location=4)

    m.begin_main()
    a = m.load(m.t_ivec4, in_a)
    b = m.load(m.t_ivec4, in_b)
    one = m.const_f32(1.0)
    zero = m.const_f32(0.0)
    half = m.const_f32(0.5)

    inv1024 = m.const_f32(1.0 / 1024.0)
    px = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, a, 0)), inv1024)
    py = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, a, 1)), inv1024)
    pz = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, a, 2)), inv1024)
    pos4 = m.construct(m.t_vec4, px, py, pz, one)

    wvp = m.ubo_load(ubo, m.t_mat4, 0)
    clip = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, wvp, pos4)
    nz = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, clip, 2), half), m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, clip, 3), half))
    m.store(out_gl_pos, m.construct(m.t_vec4, m.extract(m.t_f32, clip, 0), m.extract(m.t_f32, clip, 1), nz, m.extract(m.t_f32, clip, 3)))

    mv = m.ubo_load(ubo, m.t_mat4, 1)
    vp = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, mv, pos4)
    vp_xyz = m.shuffle(m.t_vec3, vp, vp, 0, 1, 2)
    m.store(out_viewpos, vp_xyz)
    m.store(out_fogdist, m.glsl(GLSL_LENGTH, m.t_f32, vp_xyz))

    inv8192 = m.const_f32(1.0 / 8192.0)
    ux = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, b, 0)), inv8192)
    uy = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, b, 1)), inv8192)
    gt1 = m.op(OP_FORD_GREATER_THAN, m.t_bool, ux, one)
    le2 = m.op(OP_FORD_LESS_EQUAL, m.t_bool, ux, m.const_f32(2.0))
    ux_w = m.op(OP_SELECT, m.t_f32, m.op(OP_LOGICAL_AND, m.t_bool, gt1, le2), m.op(OP_FSUB, m.t_f32, ux, one), ux)
    tex_mat = m.ubo_load(ubo, m.t_mat4, 2)
    tuv = m.op(OP_MATRIX_TIMES_VECTOR, m.t_vec4, tex_mat, m.construct(m.t_vec4, ux_w, uy, zero, one))
    m.store(out_uv, m.construct(m.t_vec4, m.extract(m.t_f32, tuv, 0), m.extract(m.t_f32, tuv, 1), zero, one))

    eight = m.const_f32(8.0)
    inv256 = m.const_f32(1.0 / 256.0)
    lx = m.op(OP_FMUL, m.t_f32, m.op(OP_FADD, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, b, 2)), eight), inv256)
    ly = m.op(OP_FMUL, m.t_f32, m.op(OP_FADD, m.t_f32, m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, b, 3)), eight), inv256)
    m.store(out_lightuv, m.construct(m.t_vec2, lx, ly))

    aw_u = m.op(OP_BITCAST, m.t_u32, m.extract(m.t_i32, a, 3))
    c16 = m.op(OP_BITWISE_XOR, m.t_u32, m.op(OP_BITWISE_AND, m.t_u32, aw_u, m.const_u32(0xFFFF)), m.const_u32(0x8000))
    rc = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_UTOF, m.t_f32, m.op(OP_BITWISE_AND, m.t_u32, m.op(OP_SHIFT_RIGHT_LOGICAL, m.t_u32, c16, m.const_u32(11)), m.const_u32(31))), m.const_f32(1.0 / 31.0))
    gc = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_UTOF, m.t_f32, m.op(OP_BITWISE_AND, m.t_u32, m.op(OP_SHIFT_RIGHT_LOGICAL, m.t_u32, c16, m.const_u32(5)),  m.const_u32(63))), m.const_f32(1.0 / 63.0))
    bc = m.op(OP_FMUL, m.t_f32, m.op(OP_CONVERT_UTOF, m.t_f32, m.op(OP_BITWISE_AND, m.t_u32, c16, m.const_u32(31))), m.const_f32(1.0 / 31.0))
    m.store(out_col, m.construct(m.t_vec4, rc, gc, bc, one))

    m.end_main()
    return m.build()


def gen_mc_world_frag():
    m = SpvModule(EXEC_FRAGMENT)
    m.init_body()
    ubo_members = [
        (m.t_vec4, 0, None),   # 0: u_StateColor
        (m.t_i32,  16, None),  # 1: u_HasTexture
        (m.t_i32,  20, None),  # 2: u_HasLightmap
        (m.t_i32,  24, None),  # 3: u_AlphaFunc
        (m.t_f32,  28, None),  # 4: u_AlphaCutoff
        (m.t_vec4, 32, None),  # 5: u_FogColor
        (m.t_i32,  48, None),  # 6: u_FogMode
        (m.t_f32,  52, None),  # 7: u_FogNear
        (m.t_f32,  56, None),  # 8: u_FogFar
        (m.t_f32,  60, None),  # 9: u_FogDensity
        (m.t_i32,  64, None),  # 10: u_UseProjection
        (m.t_vec4, 80, None),  # 11: u_SunDir
        (m.t_vec4, 96, None),  # 12: u_CameraPos
        (m.t_vec4, 112, None), # 13: u_SkyColor
        (m.t_vec4, 128, None), # 14: u_ShaderFogColor
        (m.t_vec4, 144, None), # 15: u_WorldMeta
        (m.t_vec4, 160, None), # 16: u_WaterTint
        (m.t_vec4, 176, None), # 17: u_TorchTint
    ]
    ps_ubo, _ = m.add_uniform_struct(ubo_members, 0, 1)
    tex0 = m.add_sampler2d(0, 2)
    tex1 = m.add_sampler2d(0, 3)

    in_uv      = m.add_io_var(SC_INPUT, m.t_vec4, location=0)
    in_lightuv = m.add_io_var(SC_INPUT, m.t_vec2, location=1)
    in_fogdist = m.add_io_var(SC_INPUT, m.t_f32,  location=2)
    in_col     = m.add_io_var(SC_INPUT, m.t_vec4, location=3)
    in_viewpos = m.add_io_var(SC_INPUT, m.t_vec3, location=4)
    out_color  = m.add_io_var(SC_OUTPUT, m.t_vec4, location=0)

    col_var = m.local_var(m.t_vec4)
    pass_var = m.local_var(m.t_bool)
    fog_var = m.local_var(m.t_f32)
    lit_var = m.local_var(m.t_vec3)

    m.begin_main()
    state_col = m.ubo_load(ps_ubo, m.t_vec4, 0)
    final0 = m.op(OP_FMUL, m.t_vec4, m.load(m.t_vec4, in_col), state_col)
    m.store(col_var, final0)

    has_tex = m.op(OP_INOT_EQUAL, m.t_bool, m.ubo_load(ps_ubo, m.t_i32, 1), m.const_i32(0))
    def sample_tex():
        uv4 = m.load(m.t_vec4, in_uv)
        uw = m.extract(m.t_f32, uv4, 3)
        use_proj = m.op(OP_INOT_EQUAL, m.t_bool, m.ubo_load(ps_ubo, m.t_i32, 10), m.const_i32(0))
        w_ok = m.op(OP_FORD_GREATER_THAN, m.t_bool, m.glsl(GLSL_FABS, m.t_f32, uw), m.const_f32(1e-5))
        do_div = m.op(OP_LOGICAL_AND, m.t_bool, use_proj, w_ok)
        uv2 = m.shuffle(m.t_vec2, uv4, uv4, 0, 1)
        uv_div = m.op(OP_FDIV, m.t_vec2, uv2, m.construct(m.t_vec2, uw, uw))
        final_uv = m.op(OP_SELECT, m.t_vec2, do_div, uv_div, uv2)
        samp = m.load(m.t_sampled_img2d, tex0)
        tc = m.op(OP_IMAGE_SAMPLE_IMPLICIT_LOD, m.t_vec4, samp, final_uv)
        m.store(col_var, m.op(OP_FMUL, m.t_vec4, m.load(m.t_vec4, col_var), tc))
        return False
    m.emit_if(has_tex, sample_tex)

    has_lm = m.op(OP_INOT_EQUAL, m.t_bool, m.ubo_load(ps_ubo, m.t_i32, 2), m.const_i32(0))
    def sample_lm():
        samp1 = m.load(m.t_sampled_img2d, tex1)
        lmc = m.op(OP_IMAGE_SAMPLE_IMPLICIT_LOD, m.t_vec4, samp1, m.load(m.t_vec2, in_lightuv))
        c = m.load(m.t_vec4, col_var)
        rgb = m.op(OP_FMUL, m.t_vec3, m.shuffle(m.t_vec3, c, c, 0, 1, 2), m.shuffle(m.t_vec3, lmc, lmc, 0, 1, 2))
        m.store(col_var, m.construct(m.t_vec4, m.extract(m.t_f32, rgb, 0), m.extract(m.t_f32, rgb, 1), m.extract(m.t_f32, rgb, 2), m.extract(m.t_f32, c, 3)))
        return False
    m.emit_if(has_lm, sample_lm)

    # Alpha test
    afunc = m.ubo_load(ps_ubo, m.t_i32, 3)
    acut = m.ubo_load(ps_ubo, m.t_f32, 4)
    not_always = m.op(OP_INOT_EQUAL, m.t_bool, afunc, m.const_i32(8))
    def do_alpha_test():
        a = m.extract(m.t_f32, m.load(m.t_vec4, col_var), 3)
        m.store(pass_var, m.const_bool(True))
        eps = m.const_f32(0.001)
        diff = m.glsl(GLSL_FABS, m.t_f32, m.op(OP_FSUB, m.t_f32, a, acut))
        cases = [
            (5, m.op(OP_FORD_GREATER_THAN, m.t_bool, a, acut)),
            (7, m.op(OP_FORD_GREATER_EQUAL, m.t_bool, a, acut)),
            (2, m.op(OP_FORD_LESS_THAN, m.t_bool, a, acut)),
            (4, m.op(OP_FORD_LESS_EQUAL, m.t_bool, a, acut)),
            (3, m.op(OP_FORD_LESS_THAN, m.t_bool, diff, eps)),
            (6, m.op(OP_FORD_GREATER_EQUAL, m.t_bool, diff, eps)),
            (1, m.const_bool(False)),
        ]
        for code, res_cond in cases:
            eq = m.op(OP_IEQUAL, m.t_bool, afunc, m.const_i32(code))
            m.store(pass_var, m.op(OP_SELECT, m.t_bool, eq, res_cond, m.load(m.t_bool, pass_var)))
        fail_cond = m.op(OP_LOGICAL_NOT, m.t_bool, m.load(m.t_bool, pass_var))
        def do_kill():
            m.emit(OP_KILL)
            return True
        m.emit_if(fail_cond, do_kill)
        return False
    m.emit_if(not_always, do_alpha_test)

    # Default Enhanced Shader Pass (active when u_WaterTint.w > 0.5 && u_WorldMeta.w > 0.5 && hasLightmap)
    u_sundir   = m.ubo_load(ps_ubo, m.t_vec4, 11)
    u_campos   = m.ubo_load(ps_ubo, m.t_vec4, 12)
    u_skycol   = m.ubo_load(ps_ubo, m.t_vec4, 13)
    u_wmeta    = m.ubo_load(ps_ubo, m.t_vec4, 15)
    u_watert   = m.ubo_load(ps_ubo, m.t_vec4, 16)
    u_torcht   = m.ubo_load(ps_ubo, m.t_vec4, 17)

    sh_mode_on = m.op(OP_FORD_GREATER_THAN, m.t_bool, m.extract(m.t_f32, u_watert, 3), m.const_f32(0.5))
    in_world   = m.op(OP_FORD_GREATER_THAN, m.t_bool, m.extract(m.t_f32, u_wmeta, 3), m.const_f32(0.5))
    sh_on = m.op(OP_LOGICAL_AND, m.t_bool, m.op(OP_LOGICAL_AND, m.t_bool, sh_mode_on, in_world), has_lm)
    def apply_default_shader():
        z0 = m.const_f32(0.0)
        o1 = m.const_f32(1.0)
        shader_active = o1
        v3_one = m.const_vec(m.t_vec3, [o1, o1, o1])
        v3_zero = m.const_vec(m.t_vec3, [z0, z0, z0])
        luv = m.load(m.t_vec2, in_lightuv)
        inv_range = m.const_f32(1.0 / 0.9375)
        bias_l = m.const_f32(0.03125)
        bl = m.glsl(GLSL_FCLAMP, m.t_f32, m.op(OP_FMUL, m.t_f32, m.op(OP_FSUB, m.t_f32, m.extract(m.t_f32, luv, 0), bias_l), inv_range), z0, o1)
        sl = m.glsl(GLSL_FCLAMP, m.t_f32, m.op(OP_FMUL, m.t_f32, m.op(OP_FSUB, m.t_f32, m.extract(m.t_f32, luv, 1), bias_l), inv_range), z0, o1)
        day_f = m.glsl(GLSL_FCLAMP, m.t_f32, m.extract(m.t_f32, u_sundir, 3), z0, o1)
        rain_l = m.glsl(GLSL_FCLAMP, m.t_f32, m.extract(m.t_f32, u_skycol, 3), z0, o1)
        t_sec = m.extract(m.t_f32, u_campos, 3)

        vpos = m.load(m.t_vec3, in_viewpos)
        dpdx = m.op(OP_DPDX, m.t_vec3, vpos)
        dpdy = m.op(OP_DPDY, m.t_vec3, vpos)
        cr = m.glsl(GLSL_CROSS, m.t_vec3, dpdx, dpdy)
        cr_len = m.glsl(GLSL_LENGTH, m.t_f32, cr)
        up_def = m.const_vec(m.t_vec3, [z0, o1, z0])
        cr_safe = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, cr, m.op(OP_FDIV, m.t_f32, o1, m.glsl(GLSL_FMAX, m.t_f32, cr_len, m.const_f32(1e-5))))
        n_ok = m.op(OP_FORD_GREATER_THAN, m.t_bool, cr_len, m.const_f32(1e-5))
        surf_n0 = m.op(OP_SELECT, m.t_vec3, m.construct(m.t_bvec3, n_ok, n_ok, n_ok), cr_safe, up_def)

        neg_vpos = m.op(OP_FADD, m.t_vec3, m.op(OP_FNEGATE, m.t_vec3, vpos), m.const_vec(m.t_vec3, [z0, z0, m.const_f32(1e-5)]))
        vdir = m.glsl(GLSL_NORMALIZE, m.t_vec3, neg_vpos)
        flip_n = m.op(OP_FORD_LESS_THAN, m.t_bool, m.op(OP_DOT, m.t_f32, surf_n0, vdir), z0)
        surf_n = m.op(OP_SELECT, m.t_vec3, m.construct(m.t_bvec3, flip_n, flip_n, flip_n), m.op(OP_FNEGATE, m.t_vec3, surf_n0), surf_n0)

        sun_v = m.glsl(GLSL_NORMALIZE, m.t_vec3, m.shuffle(m.t_vec3, u_sundir, u_sundir, 0, 1, 2))
        ndotl = m.glsl(GLSL_FCLAMP, m.t_f32, m.op(OP_DOT, m.t_f32, surf_n, sun_v), z0, o1)
        sun_str = m.op(OP_FMUL, m.t_f32,
            m.op(OP_FADD, m.t_f32, m.const_f32(0.35), m.op(OP_FMUL, m.t_f32, m.const_f32(0.65), day_f)),
            m.op(OP_FSUB, m.t_f32, o1, m.op(OP_FMUL, m.t_f32, m.const_f32(0.55), rain_l)))

        day_f3 = m.construct(m.t_vec3, day_f, day_f, day_f)
        moon_tint = m.const_vec(m.t_vec3, [m.const_f32(0.72), m.const_f32(0.80), m.const_f32(1.00)])
        warm_sun  = m.const_vec(m.t_vec3, [m.const_f32(1.12), m.const_f32(1.04), m.const_f32(0.92)])
        sun_tint  = m.glsl(GLSL_FMIX, m.t_vec3, moon_tint, warm_sun, day_f3)

        hemi_t = m.op(OP_FADD, m.t_f32, m.const_f32(0.5), m.op(OP_FMUL, m.t_f32, m.const_f32(0.5), m.extract(m.t_f32, surf_n, 1)))
        hemi_t3 = m.construct(m.t_vec3, hemi_t, hemi_t, hemi_t)
        hemi_lo = m.const_vec(m.t_vec3, [m.const_f32(0.78), m.const_f32(0.84), m.const_f32(0.96)])
        hemi_hi = m.const_vec(m.t_vec3, [m.const_f32(1.06), m.const_f32(1.03), m.const_f32(0.98)])
        hemi_tint = m.glsl(GLSL_FMIX, m.t_vec3, hemi_lo, hemi_hi, hemi_t3)

        dir_hi = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, sun_tint, m.op(OP_FADD, m.t_f32, m.const_f32(0.88), m.op(OP_FMUL, m.t_f32, m.const_f32(0.34), ndotl)))
        dir_lo = m.const_vec(m.t_vec3, [m.const_f32(0.86), m.const_f32(0.89), m.const_f32(0.96)])
        sl_s = m.op(OP_FMUL, m.t_f32, sl, sun_str)
        sl_s3 = m.construct(m.t_vec3, sl_s, sl_s, sl_s)
        dir_boost = m.glsl(GLSL_FMIX, m.t_vec3, dir_lo, dir_hi, sl_s3)

        phase = m.op(OP_FADD, m.t_f32,
            m.op(OP_FMUL, m.t_f32, t_sec, m.const_f32(6.5)),
            m.op(OP_FADD, m.t_f32,
                m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, vpos, 0), m.const_f32(1.7)),
                m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, vpos, 2), m.const_f32(1.3))))
        flicker = m.op(OP_FADD, m.t_f32, m.const_f32(0.96), m.op(OP_FMUL, m.t_f32, m.const_f32(0.04), m.glsl(GLSL_SIN, m.t_f32, phase)))
        bl_pow = m.glsl(GLSL_POW, m.t_f32, m.glsl(GLSL_FMAX, m.t_f32, bl, m.const_f32(1e-4)), m.const_f32(2.2))
        torch_mask = m.op(OP_FMUL, m.t_f32, m.op(OP_FMUL, m.t_f32, bl_pow, m.extract(m.t_f32, u_torcht, 3)), flicker)

        c4 = m.load(m.t_vec4, col_var)
        crgb = m.shuffle(m.t_vec3, c4, c4, 0, 1, 2)
        sa3 = m.construct(m.t_vec3, shader_active, shader_active, shader_active)
        dh = m.op(OP_FMUL, m.t_vec3, dir_boost, hemi_tint)
        lit0 = m.op(OP_FMUL, m.t_vec3, crgb, m.glsl(GLSL_FMIX, m.t_vec3, v3_one, dh, sa3))
        torch_add = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3,
            m.op(OP_FMUL, m.t_vec3, crgb, m.shuffle(m.t_vec3, u_torcht, u_torcht, 0, 1, 2)),
            m.op(OP_FMUL, m.t_f32, torch_mask, m.op(OP_FMUL, m.t_f32, m.const_f32(0.55), shader_active)))
        m.store(lit_var, m.op(OP_FADD, m.t_vec3, lit0, torch_add))

        ca = m.extract(m.t_f32, c4, 3)
        cr_c = m.extract(m.t_f32, c4, 0)
        cb_c = m.extract(m.t_f32, c4, 2)
        is_water = m.op(OP_LOGICAL_AND, m.t_bool,
            m.op(OP_LOGICAL_AND, m.t_bool,
                m.op(OP_FORD_GREATER_EQUAL, m.t_bool, ca, m.const_f32(0.10)),
                m.op(OP_FORD_LESS_EQUAL, m.t_bool, ca, m.const_f32(0.92))),
            m.op(OP_FORD_GREATER_THAN, m.t_bool, cb_c, m.op(OP_FADD, m.t_f32, cr_c, m.const_f32(0.05))))
        def do_water():
            ndotv = m.glsl(GLSL_FCLAMP, m.t_f32, m.op(OP_DOT, m.t_f32, surf_n, vdir), z0, o1)
            fres = m.glsl(GLSL_POW, m.t_f32, m.glsl(GLSL_FMAX, m.t_f32, m.op(OP_FSUB, m.t_f32, o1, ndotv), m.const_f32(1e-4)), m.const_f32(3.0))
            half_v = m.glsl(GLSL_NORMALIZE, m.t_vec3, m.op(OP_FADD, m.t_vec3, sun_v, vdir))
            wx = m.glsl(GLSL_SIN, m.t_f32, m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, vpos, 0), m.const_f32(2.4)), m.op(OP_FMUL, m.t_f32, t_sec, m.const_f32(2.6))))
            wz = m.glsl(GLSL_COS, m.t_f32, m.op(OP_FSUB, m.t_f32, m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, vpos, 2), m.const_f32(2.4)), m.op(OP_FMUL, m.t_f32, t_sec, m.const_f32(2.1))))
            wave = m.op(OP_FADD, m.t_f32, m.const_f32(0.5), m.op(OP_FMUL, m.t_f32, m.const_f32(0.5), m.op(OP_FMUL, m.t_f32, wx, wz)))
            ndoth = m.glsl(GLSL_FCLAMP, m.t_f32, m.op(OP_DOT, m.t_f32, surf_n, half_v), z0, o1)
            spec_p = m.glsl(GLSL_POW, m.t_f32, m.glsl(GLSL_FMAX, m.t_f32, ndoth, m.const_f32(1e-4)), m.const_f32(28.0))
            spec = m.op(OP_FMUL, m.t_f32, spec_p, m.op(OP_FMUL, m.t_f32, m.op(OP_FADD, m.t_f32, m.const_f32(0.55), m.op(OP_FMUL, m.t_f32, m.const_f32(0.45), wave)), sl_s))
            cur_lit = m.load(m.t_vec3, lit_var)
            tinted = m.op(OP_FMUL, m.t_vec3, cur_lit, m.shuffle(m.t_vec3, u_watert, u_watert, 0, 1, 2))
            sky_refl = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, m.shuffle(m.t_vec3, u_skycol, u_skycol, 0, 1, 2), m.const_f32(1.15))
            fm = m.op(OP_FMUL, m.t_f32, fres, m.op(OP_FMUL, m.t_f32, m.const_f32(0.42), sl))
            fm3 = m.construct(m.t_vec3, fm, fm, fm)
            w_lit = m.glsl(GLSL_FMIX, m.t_vec3, tinted, sky_refl, fm3)
            w_lit = m.op(OP_FADD, m.t_vec3, w_lit, m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, sun_tint, m.op(OP_FMUL, m.t_f32, spec, m.const_f32(0.65))))
            m.store(lit_var, w_lit)
            return False
        m.emit_if(is_water, do_water)

        # ACES-lite filmic tone mapping
        lit_f = m.load(m.t_vec3, lit_var)
        x = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, lit_f, m.const_f32(1.08))
        c251 = m.const_f32(2.51); c003 = m.const_vec(m.t_vec3, [m.const_f32(0.03)] * 3)
        c243 = m.const_f32(2.43); c059 = m.const_vec(m.t_vec3, [m.const_f32(0.59)] * 3)
        c014 = m.const_vec(m.t_vec3, [m.const_f32(0.14)] * 3)
        num = m.op(OP_FMUL, m.t_vec3, x, m.op(OP_FADD, m.t_vec3, m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, x, c251), c003))
        den = m.op(OP_FADD, m.t_vec3, m.op(OP_FMUL, m.t_vec3, x, m.op(OP_FADD, m.t_vec3, m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, x, c243), c059)), c014)
        mapped = m.glsl(GLSL_FCLAMP, m.t_vec3, m.op(OP_FDIV, m.t_vec3, num, den), v3_zero, v3_one)
        t45 = m.const_vec(m.t_vec3, [m.const_f32(0.45)] * 3)
        blended = m.glsl(GLSL_FMIX, m.t_vec3, lit_f, mapped, t45)
        out_rgb = m.glsl(GLSL_FMIX, m.t_vec3, crgb, blended, sa3)
        m.store(col_var, m.construct(m.t_vec4, m.extract(m.t_f32, out_rgb, 0), m.extract(m.t_f32, out_rgb, 1), m.extract(m.t_f32, out_rgb, 2), ca))
        return False
    m.emit_if(sh_on, apply_default_shader)

    # Fog
    fog_col4 = m.ubo_load(ps_ubo, m.t_vec4, 5)
    fog_on = m.op(OP_FORD_GREATER_THAN, m.t_bool, m.extract(m.t_f32, fog_col4, 3), m.const_f32(0.5))
    def apply_fog():
        fd = m.load(m.t_f32, in_fogdist)
        fmode = m.ubo_load(ps_ubo, m.t_i32, 6)
        is_exp = m.op(OP_IEQUAL, m.t_bool, fmode, m.const_i32(2))
        def exp_fog():
            dens = m.ubo_load(ps_ubo, m.t_f32, 9)
            neg_df = m.op(OP_FNEGATE, m.t_f32, m.op(OP_FMUL, m.t_f32, dens, fd))
            m.store(fog_var, m.glsl(GLSL_EXP, m.t_f32, neg_df))
            return False
        def lin_fog():
            fnear = m.ubo_load(ps_ubo, m.t_f32, 7)
            ffar = m.ubo_load(ps_ubo, m.t_f32, 8)
            denom = m.glsl(GLSL_FMAX, m.t_f32, m.op(OP_FSUB, m.t_f32, ffar, fnear), m.const_f32(1e-4))
            m.store(fog_var, m.op(OP_FDIV, m.t_f32, m.op(OP_FSUB, m.t_f32, ffar, fd), denom))
            return False
        m.emit_if(is_exp, exp_fog, lin_fog)
        zero = m.const_f32(0.0)
        one = m.const_f32(1.0)
        ff = m.glsl(GLSL_FCLAMP, m.t_f32, m.load(m.t_f32, fog_var), zero, one)
        c = m.load(m.t_vec4, col_var)
        crgb = m.shuffle(m.t_vec3, c, c, 0, 1, 2)
        frgb = m.shuffle(m.t_vec3, fog_col4, fog_col4, 0, 1, 2)
        ff3 = m.construct(m.t_vec3, ff, ff, ff)
        mixed = m.glsl(GLSL_FMIX, m.t_vec3, frgb, crgb, ff3)
        m.store(col_var, m.construct(m.t_vec4, m.extract(m.t_f32, mixed, 0), m.extract(m.t_f32, mixed, 1), m.extract(m.t_f32, mixed, 2), m.extract(m.t_f32, c, 3)))
        return False
    m.emit_if(fog_on, apply_fog)

    m.store(out_color, m.load(m.t_vec4, col_var))
    m.end_main()
    return m.build()


def add_gdraw_ubo(m: SpvModule) -> int:
    members = [
        (m.t_vec4, 0,   None),  # 0: world0
        (m.t_vec4, 16,  None),  # 1: world1
        (m.t_vec4, 32,  None),  # 2: x_off
        (m.t_vec4, 48,  None),  # 3: texgen_s
        (m.t_vec4, 64,  None),  # 4: texgen_t
        (m.t_vec4, 80,  None),  # 5: viewproj
        (m.t_vec4, 96,  None),  # 6: color_mul
        (m.t_vec4, 112, None),  # 7: color_add
        (m.t_vec4, 128, None),  # 8: focal
        (m.t_i32,  144, None),  # 9: tex0_mode
        (m.t_i32,  148, None),  # 10: additive_mode
        (m.t_i32,  152, None),  # 11: has_tex0
        (m.t_i32,  156, None),  # 12: pad
    ]
    vid, _ = m.add_uniform_struct(members, 0, 0)
    return vid


def gen_gdraw_vert(mode: str):
    """mode in ('basic', 'aa', 'tc2')"""
    m = SpvModule(EXEC_VERTEX)
    m.init_body()
    ubo = add_gdraw_ubo(m)

    in_pos = m.add_io_var(SC_INPUT, m.t_vec2, location=0)
    if mode == "aa":
        in_attr = m.add_io_var(SC_INPUT, m.t_ivec4, location=1)
    elif mode == "tc2":
        in_tc = m.add_io_var(SC_INPUT, m.t_vec2, location=1)

    out_gl_pos = m.add_io_var(SC_OUTPUT, m.t_vec4, builtin=BUILTIN_POSITION)
    out_tc     = m.add_io_var(SC_OUTPUT, m.t_vec4, location=0)

    wpos_var = m.local_var(m.t_vec4)
    tc_var   = m.local_var(m.t_vec4)

    m.begin_main()
    p2 = m.load(m.t_vec2, in_pos)
    zero = m.const_f32(0.0)
    one  = m.const_f32(1.0)
    pos4 = m.construct(m.t_vec4, m.extract(m.t_f32, p2, 0), m.extract(m.t_f32, p2, 1), zero, one)

    w0 = m.ubo_load(ubo, m.t_vec4, 0)
    w1 = m.ubo_load(ubo, m.t_vec4, 1)
    wx = m.op(OP_DOT, m.t_f32, w0, pos4)
    wy = m.op(OP_DOT, m.t_f32, w1, pos4)
    wz = m.extract(m.t_f32, w0, 2)
    m.store(wpos_var, m.construct(m.t_vec4, wx, wy, wz, one))

    ts = m.ubo_load(ubo, m.t_vec4, 3)
    tt = m.ubo_load(ubo, m.t_vec4, 4)
    s0 = m.op(OP_DOT, m.t_f32, ts, pos4)
    t0 = m.op(OP_DOT, m.t_f32, tt, pos4)
    if mode == "tc2":
        uv = m.load(m.t_vec2, in_tc)
        m.store(tc_var, m.construct(m.t_vec4, m.extract(m.t_f32, uv, 0), m.extract(m.t_f32, uv, 1), one, zero))
    else:
        m.store(tc_var, m.construct(m.t_vec4, s0, t0, one, zero))

    if mode == "aa":
        iv = m.load(m.t_ivec4, in_attr)
        aa_f = m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, iv, 0))
        dx_f = m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, iv, 1))
        dy_f = m.op(OP_CONVERT_STOF, m.t_f32, m.extract(m.t_i32, iv, 2))
        inv64 = m.const_f32(1.0 / 64.0)
        qx = m.op(OP_FMUL, m.t_f32, dx_f, inv64)
        qy = m.op(OP_FMUL, m.t_f32, dy_f, inv64)
        q2 = m.construct(m.t_vec2, qx, qy)
        qlen = m.glsl(GLSL_LENGTH, m.t_f32, q2)

        xoff = m.ubo_load(ubo, m.t_vec4, 2)
        px = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, qx, m.extract(m.t_f32, xoff, 0)), m.op(OP_FMUL, m.t_f32, qy, m.extract(m.t_f32, xoff, 1)))
        py = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, qx, m.extract(m.t_f32, xoff, 2)), m.op(OP_FMUL, m.t_f32, qy, m.extract(m.t_f32, xoff, 3)))
        p_in = m.construct(m.t_vec4, px, py, zero, zero)
        dox = m.op(OP_DOT, m.t_f32, w0, p_in)
        doy = m.op(OP_DOT, m.t_f32, w1, p_in)
        p2_out = m.construct(m.t_vec2, dox, doy)
        newlen = m.glsl(GLSL_LENGTH, m.t_f32, p2_out)
        nz_cond = m.op(OP_FORD_GREATER_THAN, m.t_bool, newlen, m.const_f32(1e-6))
        ratio = m.op(OP_FDIV, m.t_f32, qlen, m.glsl(GLSL_FMAX, m.t_f32, newlen, m.const_f32(1e-6)))
        scale = m.op(OP_SELECT, m.t_f32, nz_cond, ratio, zero)
        wpos = m.load(m.t_vec4, wpos_var)
        nwx = m.op(OP_FADD, m.t_f32, m.extract(m.t_f32, wpos, 0), m.op(OP_FMUL, m.t_f32, dox, scale))
        nwy = m.op(OP_FADD, m.t_f32, m.extract(m.t_f32, wpos, 1), m.op(OP_FMUL, m.t_f32, doy, scale))
        m.store(wpos_var, m.construct(m.t_vec4, nwx, nwy, wz, one))

        tc = m.load(m.t_vec4, tc_var)
        tcz = m.op(OP_FMUL, m.t_f32, aa_f, m.const_f32(1.0 / 32.0))
        m.store(tc_var, m.construct(m.t_vec4, m.extract(m.t_f32, tc, 0), m.extract(m.t_f32, tc, 1), tcz, m.extract(m.t_f32, tc, 3)))

    vp = m.ubo_load(ubo, m.t_vec4, 5)
    wpos_f = m.load(m.t_vec4, wpos_var)
    ndcx = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, wpos_f, 0), m.extract(m.t_f32, vp, 0)), m.extract(m.t_f32, vp, 2))
    ndcy = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, wpos_f, 1), m.extract(m.t_f32, vp, 1)), m.extract(m.t_f32, vp, 3))
    # Depth is in [0, 1] directly for Vulkan (world0.z is depth_from_id(id) * 0.5 + 0.5 or 0..1)
    m.store(out_gl_pos, m.construct(m.t_vec4, ndcx, ndcy, m.extract(m.t_f32, wpos_f, 2), one))
    m.store(out_tc, m.load(m.t_vec4, tc_var))
    m.end_main()
    return m.build()


def gen_gdraw_ui_frag():
    m = SpvModule(EXEC_FRAGMENT)
    m.init_body()
    ubo = add_gdraw_ubo(m)
    tex0 = m.add_sampler2d(0, 1)
    tex1 = m.add_sampler2d(0, 2)

    in_tc     = m.add_io_var(SC_INPUT, m.t_vec4, location=0)
    out_color = m.add_io_var(SC_OUTPUT, m.t_vec4, location=0)

    z_var   = m.local_var(m.t_vec4)
    tc0_var = m.local_var(m.t_vec2)
    t_var   = m.local_var(m.t_vec4)

    m.begin_main()
    cmul = m.ubo_load(ubo, m.t_vec4, 6)
    cadd = m.ubo_load(ubo, m.t_vec4, 7)
    focal = m.ubo_load(ubo, m.t_vec4, 8)
    tmode = m.ubo_load(ubo, m.t_i32, 9)
    amode = m.ubo_load(ubo, m.t_i32, 10)
    htex0 = m.ubo_load(ubo, m.t_i32, 11)

    is_add_alpha = m.op(OP_IEQUAL, m.t_bool, amode, m.const_i32(2))
    is_add_col   = m.op(OP_IEQUAL, m.t_bool, amode, m.const_i32(1))

    # Non-additive-alpha premultiplies color_mul initially
    m.store(z_var, cmul)
    not_add_alpha = m.op(OP_LOGICAL_NOT, m.t_bool, is_add_alpha)
    def premul_init():
        c = m.load(m.t_vec4, z_var)
        ca = m.extract(m.t_f32, c, 3)
        rgb = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, m.shuffle(m.t_vec3, c, c, 0, 1, 2), ca)
        m.store(z_var, m.construct(m.t_vec4, m.extract(m.t_f32, rgb, 0), m.extract(m.t_f32, rgb, 1), m.extract(m.t_f32, rgb, 2), ca))
        return False
    m.emit_if(not_add_alpha, premul_init)

    tc4 = m.load(m.t_vec4, in_tc)
    use_t0 = m.op(OP_INOT_EQUAL, m.t_bool, htex0, m.const_i32(0))
    def sample_t0():
        tc0 = m.shuffle(m.t_vec2, tc4, tc4, 0, 1)
        m.store(tc0_var, tc0)

        is_rad = m.op(OP_IEQUAL, m.t_bool, tmode, m.const_i32(3))
        def do_rad():
            c2 = m.load(m.t_vec2, tc0_var)
            r = m.glsl(GLSL_SQRT, m.t_f32, m.op(OP_DOT, m.t_f32, c2, c2))
            m.store(tc0_var, m.construct(m.t_vec2, r, r))
            return False
        m.emit_if(is_rad, do_rad)

        is_foc = m.op(OP_IEQUAL, m.t_bool, tmode, m.const_i32(4))
        def do_foc():
            c2 = m.load(m.t_vec2, tc0_var)
            cx = m.op(OP_FADD, m.t_f32, m.extract(m.t_f32, c2, 0), m.extract(m.t_f32, focal, 0))
            cy = m.extract(m.t_f32, c2, 1)
            tx = m.op(OP_FMUL, m.t_f32, cx, m.extract(m.t_f32, focal, 1))
            rad2 = m.op(OP_FADD, m.t_f32, m.op(OP_FMUL, m.t_f32, cx, cx), m.op(OP_FMUL, m.t_f32, cy, cy))
            ty = m.op(OP_FMUL, m.t_f32, rad2, m.extract(m.t_f32, focal, 2))
            u = m.op(OP_FSUB, m.t_f32, m.glsl(GLSL_SQRT, m.t_f32, m.op(OP_FADD, m.t_f32, ty, m.op(OP_FMUL, m.t_f32, tx, tx))), tx)
            m.store(tc0_var, m.construct(m.t_vec2, u, u))
            return False
        m.emit_if(is_foc, do_foc)

        samp0 = m.load(m.t_sampled_img2d, tex0)
        texel = m.op(OP_IMAGE_SAMPLE_IMPLICIT_LOD, m.t_vec4, samp0, m.load(m.t_vec2, tc0_var))
        m.store(t_var, texel)

        is_alpha_font = m.op(OP_LOGICAL_OR, m.t_bool,
            m.op(OP_IEQUAL, m.t_bool, tmode, m.const_i32(2)),
            m.op(OP_IEQUAL, m.t_bool, tmode, m.const_i32(5)))
        def mul_alpha_only():
            ta = m.extract(m.t_f32, m.load(m.t_vec4, t_var), 3)
            z = m.load(m.t_vec4, z_var)
            na = m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, z, 3), ta)
            def only_a():
                m.store(z_var, m.construct(m.t_vec4, m.extract(m.t_f32, z, 0), m.extract(m.t_f32, z, 1), m.extract(m.t_f32, z, 2), na))
                return False
            def all_rgba():
                m.store(z_var, m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec4, z, ta))
                return False
            m.emit_if(is_add_alpha, only_a, all_rgba)
            return False
        def mul_rgba():
            tv = m.load(m.t_vec4, t_var)
            def unpremul():
                ta = m.extract(m.t_f32, tv, 3)
                nz = m.op(OP_FORD_GREATER_THAN, m.t_bool, ta, m.const_f32(1e-5))
                inv = m.op(OP_FDIV, m.t_f32, m.const_f32(1.0), m.glsl(GLSL_FMAX, m.t_f32, ta, m.const_f32(1e-5)))
                sc = m.op(OP_SELECT, m.t_f32, nz, inv, m.const_f32(1.0))
                ur = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, m.shuffle(m.t_vec3, tv, tv, 0, 1, 2), sc)
                m.store(t_var, m.construct(m.t_vec4, m.extract(m.t_f32, ur, 0), m.extract(m.t_f32, ur, 1), m.extract(m.t_f32, ur, 2), ta))
                return False
            m.emit_if(is_add_alpha, unpremul)
            m.store(z_var, m.op(OP_FMUL, m.t_vec4, m.load(m.t_vec4, z_var), m.load(m.t_vec4, t_var)))
            return False
        m.emit_if(is_alpha_font, mul_alpha_only, mul_rgba)
        return False
    m.emit_if(use_t0, sample_t0)

    # AA LUT (tex1)
    samp1 = m.load(m.t_sampled_img2d, tex1)
    tc1 = m.shuffle(m.t_vec2, tc4, tc4, 2, 3)
    aa_t = m.op(OP_IMAGE_SAMPLE_IMPLICIT_LOD, m.t_vec4, samp1, tc1)
    def aa_add_alpha():
        z = m.load(m.t_vec4, z_var)
        na = m.op(OP_FMUL, m.t_f32, m.extract(m.t_f32, z, 3), m.extract(m.t_f32, aa_t, 3))
        m.store(z_var, m.construct(m.t_vec4, m.extract(m.t_f32, z, 0), m.extract(m.t_f32, z, 1), m.extract(m.t_f32, z, 2), na))
        return False
    def aa_mul():
        m.store(z_var, m.op(OP_FMUL, m.t_vec4, m.load(m.t_vec4, z_var), aa_t))
        return False
    m.emit_if(is_add_alpha, aa_add_alpha, aa_mul)

    # Additive color / alpha transform
    def apply_add_alpha():
        sum4 = m.op(OP_FADD, m.t_vec4, m.load(m.t_vec4, z_var), cadd)
        sa = m.extract(m.t_f32, sum4, 3)
        rgb = m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, m.shuffle(m.t_vec3, sum4, sum4, 0, 1, 2), sa)
        m.store(z_var, m.construct(m.t_vec4, m.extract(m.t_f32, rgb, 0), m.extract(m.t_f32, rgb, 1), m.extract(m.t_f32, rgb, 2), sa))
        return False
    def check_add_col():
        def apply_add_col():
            z = m.load(m.t_vec4, z_var)
            za = m.extract(m.t_f32, z, 3)
            zrgb = m.shuffle(m.t_vec3, z, z, 0, 1, 2)
            crgb = m.shuffle(m.t_vec3, cadd, cadd, 0, 1, 2)
            add_s = m.op(OP_FADD, m.t_vec3, zrgb, m.op(OP_VECTOR_TIMES_SCALAR, m.t_vec3, crgb, za))
            za3 = m.construct(m.t_vec3, za, za, za)
            cl = m.glsl(GLSL_FMIN, m.t_vec3, add_s, za3)
            m.store(z_var, m.construct(m.t_vec4, m.extract(m.t_f32, cl, 0), m.extract(m.t_f32, cl, 1), m.extract(m.t_f32, cl, 2), za))
            return False
        m.emit_if(is_add_col, apply_add_col)
        return False
    m.emit_if(is_add_alpha, apply_add_alpha, check_add_col)

    # Alpha test for GDRAW_TEXTURE_alpha_test (5)
    is_atest = m.op(OP_IEQUAL, m.t_bool, tmode, m.const_i32(5))
    def check_atest():
        za = m.extract(m.t_f32, m.load(m.t_vec4, z_var), 3)
        fail = m.op(OP_FORD_LESS_THAN, m.t_bool, za, m.const_f32(0.5))
        def do_kill():
            m.emit(OP_KILL)
            return True
        m.emit_if(fail, do_kill)
        return False
    m.emit_if(is_atest, check_atest)

    m.store(out_color, m.load(m.t_vec4, z_var))
    m.end_main()
    return m.build()


def main():
    out_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "Minecraft.Client", "Windows64", "Render", "vk_shaders_spv.h"
    )
    shaders = [
        ("spv_mc_std_vert",     gen_mc_std_vert()),
        ("spv_mc_comp_vert",    gen_mc_comp_vert()),
        ("spv_mc_world_frag",   gen_mc_world_frag()),
        ("spv_gdraw_basic_vert", gen_gdraw_vert("basic")),
        ("spv_gdraw_aa_vert",   gen_gdraw_vert("aa")),
        ("spv_gdraw_tc2_vert",  gen_gdraw_vert("tc2")),
        ("spv_gdraw_ui_frag",   gen_gdraw_ui_frag()),
    ]
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n")
        for name, words in shaders:
            f.write(f"static const uint32_t {name}[] = {{\n")
            for i in range(0, len(words), 8):
                chunk = ", ".join(f"0x{w:08X}u" for w in words[i:i + 8])
                f.write(f"\t{chunk},\n")
            f.write("};\n")
            f.write(f"static const size_t {name}_size = sizeof({name});\n\n")
    print(f"Generated {out_path} ({sum(len(w)*4 for _, w in shaders)} bytes across {len(shaders)} modules).")


if __name__ == "__main__":
    main()
