#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path
import shutil

top = '.'
out = 'build'


def _patch_clay_platforms():
    """pebble-clay (last published 2016) predates the gabbro/flint platforms.
    `pebble build` reinstalls/prunes node_modules before waf ever runs, which
    wipes anything tools/patch-clay-platforms.js added, so redo it here right
    before waf's per-platform include check. Clay is pure PebbleKit JS with no
    native code, so copying an existing platform's stub headers is safe.
    """
    clay_root = os.path.join('node_modules', 'pebble-clay', 'dist')
    # dist/binaries/<platform>/libpebble-clay.a is an empty archive (8-byte "!<arch>\n"
    # magic, identical bytes on every shipped platform) -- there's no native code to
    # be ABI-incompatible, so copying it across platforms is safe.
    for sub in ('include/pebble-clay', 'binaries'):
        root = os.path.join(clay_root, sub)
        src = os.path.join(root, 'emery')
        if not os.path.isdir(src):
            continue
        for platform in ('gabbro', 'flint'):
            dest = os.path.join(root, platform)
            if not os.path.isdir(dest):
                shutil.copytree(src, dest)


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    _patch_clay_platforms()
    ctx.load('pebble_sdk')


def build(ctx):
    _patch_clay_platforms()
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
