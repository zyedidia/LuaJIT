project('luajit', 'c',
    default_options: [
        'c_std=gnu99',
        'warning_level=1',
    ]
)

cc = meson.get_compiler('c')

# Common defines
c_args = [
    '-D_FILE_OFFSET_BITS=64',
    '-D_LARGEFILE_SOURCE',
    '-U_FORTIFY_SOURCE',
    '-DLUAJIT_UNWIND_EXTERNAL',
]

# Optimization flags
c_args += ['-O2', '-fomit-frame-pointer']

# Architecture detection
cpu_family = host_machine.cpu_family()
if cpu_family == 'x86_64'
    target_ljarch = 'x64'
    dasm_arch = 'x64'
    dasm_flags = ['-D', 'ENDIAN_LE', '-D', 'P64', '-D', 'JIT', '-D', 'FFI', '-D', 'FPU', '-D', 'HFABI', '-D', 'VER=80']
    c_args += ['-DLUAJIT_TARGET=LUAJIT_ARCH_x64', '-DLJ_ARCH_HASFPU=1', '-DLJ_ABI_SOFTFP=0']
elif cpu_family == 'x86'
    target_ljarch = 'x86'
    dasm_arch = 'x86'
    dasm_flags = ['-D', 'ENDIAN_LE', '-D', 'JIT', '-D', 'FFI', '-D', 'FPU', '-D', 'HFABI', '-D', 'VER=80']
    c_args += ['-DLUAJIT_TARGET=LUAJIT_ARCH_x86', '-DLJ_ARCH_HASFPU=1', '-DLJ_ABI_SOFTFP=0', '-march=i686', '-msse', '-msse2', '-mfpmath=sse']
elif cpu_family == 'aarch64'
    target_ljarch = 'arm64'
    dasm_arch = 'arm64'
    dasm_flags = ['-D', 'ENDIAN_LE', '-D', 'P64', '-D', 'JIT', '-D', 'FFI', '-D', 'FPU', '-D', 'HFABI', '-D', 'VER=80']
    c_args += ['-DLUAJIT_TARGET=LUAJIT_ARCH_arm64', '-DLJ_ARCH_HASFPU=1', '-DLJ_ABI_SOFTFP=0']
elif cpu_family == 'arm'
    target_ljarch = 'arm'
    dasm_arch = 'arm'
    dasm_flags = ['-D', 'ENDIAN_LE', '-D', 'JIT', '-D', 'FFI', '-D', 'FPU', '-D', 'HFABI', '-D', 'VER=70']
    c_args += ['-DLUAJIT_TARGET=LUAJIT_ARCH_arm', '-DLJ_ARCH_HASFPU=1', '-DLJ_ABI_SOFTFP=0']
else
    error('Unsupported architecture: ' + cpu_family)
endif

# Platform-specific
if host_machine.system() == 'darwin'
    ljvm_mode = 'machasm'
else
    ljvm_mode = 'elfasm'
endif

src_dir = meson.current_source_dir()
build_dir = meson.current_build_dir()

# Step 1: Build minilua (native host tool for running Lua scripts)
minilua_exe = executable('minilua',
    files('src/host/minilua.c'),
    c_args: ['-O2'],
    dependencies: cc.find_library('m', required: false),
    native: true,
)

# Step 2: Generate luajit.h using genversion.lua
# genversion.lua needs to run from src/ directory
luajit_h = custom_target('luajit.h',
    output: 'luajit.h',
    input: files('src/luajit_rolling.h', 'src/host/genversion.lua'),
    command: [
        'sh', '-c',
        'cd ' + src_dir + '/src && "$1" host/genversion.lua luajit_rolling.h "$2"/.relver "' + build_dir + '/luajit.h"',
        'sh',
        minilua_exe,
        src_dir,
    ],
)

# Step 3: Run dynasm to generate buildvm_arch.h
buildvm_arch_h = custom_target('buildvm_arch.h',
    output: 'buildvm_arch.h',
    input: files('src/vm_' + dasm_arch + '.dasc'),
    command: [
        minilua_exe,
        files('dynasm/dynasm.lua'),
        dasm_flags,
        '-o', '@OUTPUT@',
        '@INPUT@',
    ],
)

# Step 4: Build buildvm (native host tool)
buildvm_exe = executable('buildvm',
    files(
        'src/host/buildvm.c',
        'src/host/buildvm_asm.c',
        'src/host/buildvm_peobj.c',
        'src/host/buildvm_lib.c',
        'src/host/buildvm_fold.c',
    ),
    buildvm_arch_h,
    c_args: ['-O2'],
    include_directories: include_directories('src'),
    native: true,
)

# Library source files for buildvm
ljlib_c = files(
    'src/lib_base.c',
    'src/lib_math.c',
    'src/lib_bit.c',
    'src/lib_string.c',
    'src/lib_table.c',
    'src/lib_io.c',
    'src/lib_os.c',
    'src/lib_package.c',
    'src/lib_debug.c',
    'src/lib_jit.c',
    'src/lib_ffi.c',
    'src/lib_buffer.c',
    'src/lib_js.c',
)

# Step 5: Generate lj_vm.S
lj_vm_s = custom_target('lj_vm.S',
    output: 'lj_vm.S',
    command: [buildvm_exe, '-m', ljvm_mode, '-o', '@OUTPUT@'],
)

# Step 6: Generate header files using buildvm
lj_bcdef_h = custom_target('lj_bcdef.h',
    output: 'lj_bcdef.h',
    input: ljlib_c,
    command: [buildvm_exe, '-m', 'bcdef', '-o', '@OUTPUT@', '@INPUT@'],
)

lj_ffdef_h = custom_target('lj_ffdef.h',
    output: 'lj_ffdef.h',
    input: ljlib_c,
    command: [buildvm_exe, '-m', 'ffdef', '-o', '@OUTPUT@', '@INPUT@'],
)

lj_libdef_h = custom_target('lj_libdef.h',
    output: 'lj_libdef.h',
    input: ljlib_c,
    command: [buildvm_exe, '-m', 'libdef', '-o', '@OUTPUT@', '@INPUT@'],
)

lj_recdef_h = custom_target('lj_recdef.h',
    output: 'lj_recdef.h',
    input: ljlib_c,
    command: [buildvm_exe, '-m', 'recdef', '-o', '@OUTPUT@', '@INPUT@'],
)

lj_folddef_h = custom_target('lj_folddef.h',
    output: 'lj_folddef.h',
    input: files('src/lj_opt_fold.c'),
    command: [buildvm_exe, '-m', 'folddef', '-o', '@OUTPUT@', '@INPUT@'],
)

# Generate vmdef.lua for JIT debug modules
jit_vmdef_lua = custom_target('vmdef.lua',
    output: 'vmdef.lua',
    input: ljlib_c,
    command: [buildvm_exe, '-m', 'vmdef', '-o', '@OUTPUT@', '@INPUT@'],
    install: true,
    install_dir: get_option('datadir') / 'lunajs' / 'jit',
)

generated_headers = [
    luajit_h,
    lj_bcdef_h,
    lj_ffdef_h,
    lj_libdef_h,
    lj_recdef_h,
    lj_folddef_h,
]

# Core source files
ljcore_c = files(
    'src/lj_assert.c',
    'src/lj_gc.c',
    'src/lj_err.c',
    'src/lj_char.c',
    'src/lj_bc.c',
    'src/lj_obj.c',
    'src/lj_buf.c',
    'src/lj_str.c',
    'src/lj_tab.c',
    'src/lj_func.c',
    'src/lj_udata.c',
    'src/lj_meta.c',
    'src/lj_debug.c',
    'src/lj_prng.c',
    'src/lj_state.c',
    'src/lj_dispatch.c',
    'src/lj_vmevent.c',
    'src/lj_vmmath.c',
    'src/lj_strscan.c',
    'src/lj_strfmt.c',
    'src/lj_strfmt_num.c',
    'src/lj_serialize.c',
    'src/lj_api.c',
    'src/lj_profile.c',
    'src/lj_lex.c',
    'src/lj_parse.c',
    'src/lj_bcread.c',
    'src/lj_bcwrite.c',
    'src/lj_load.c',
    'src/lj_ir.c',
    'src/lj_opt_mem.c',
    'src/lj_opt_fold.c',
    'src/lj_opt_narrow.c',
    'src/lj_opt_dce.c',
    'src/lj_opt_loop.c',
    'src/lj_opt_split.c',
    'src/lj_opt_sink.c',
    'src/lj_mcode.c',
    'src/lj_snap.c',
    'src/lj_record.c',
    'src/lj_crecord.c',
    'src/lj_ffrecord.c',
    'src/lj_asm.c',
    'src/lj_trace.c',
    'src/lj_gdbjit.c',
    'src/lj_ctype.c',
    'src/lj_cdata.c',
    'src/lj_cconv.c',
    'src/lj_ccall.c',
    'src/lj_ccallback.c',
    'src/lj_carith.c',
    'src/lj_clib.c',
    'src/lj_cparse.c',
    'src/lj_lib.c',
    'src/lj_alloc.c',
    'src/lib_aux.c',
    'src/lib_init.c',
)

# Build static library
luajit_lib = static_library('luajit',
    ljcore_c,
    ljlib_c,
    lj_vm_s,
    generated_headers,
    c_args: c_args,
    include_directories: include_directories('src'),
    dependencies: cc.find_library('m', required: false),
)

luajit_dep = declare_dependency(
    link_with: luajit_lib,
    include_directories: include_directories('src'),
    sources: generated_headers,
)

# Install JIT debug modules (jit.v, jit.dump, etc.)
jit_modules = files(
    'src/jit/bc.lua',
    'src/jit/bcsave.lua',
    'src/jit/dis_arm.lua',
    'src/jit/dis_arm64.lua',
    'src/jit/dis_arm64be.lua',
    'src/jit/dis_mips.lua',
    'src/jit/dis_mipsel.lua',
    'src/jit/dis_mips64.lua',
    'src/jit/dis_mips64el.lua',
    'src/jit/dis_mips64r6.lua',
    'src/jit/dis_mips64r6el.lua',
    'src/jit/dis_ppc.lua',
    'src/jit/dis_x64.lua',
    'src/jit/dis_x86.lua',
    'src/jit/dump.lua',
    'src/jit/p.lua',
    'src/jit/v.lua',
    'src/jit/zone.lua',
)

install_data(jit_modules, install_dir: get_option('datadir') / 'lunajs' / 'jit')
