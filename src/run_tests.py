"""Run current native checks with disposable renderer settings."""

import argparse
import ctypes
import os
import pathlib
import subprocess
import sys
import uuid
import winreg

ROOT = pathlib.Path(__file__).resolve().parent
TESTS = {
    'sdr-present': 'test_sdr_present', 'sdr-target': 'test_sdr_target',
    'tonal': 'test_tonal', 'color-mapping': 'test_color_mapping',
    'motion-timing': 'test_motion_timing', 'monitor-change': 'test_monitor_change',
    'statistics': 'test_statistics_bitmap', 'native-hdr': 'test_native_hdr',
    'display-profiles': 'test_display_profiles', 'direct2d-model': 'test_direct2d_model',
    'direct2d-page': 'test_direct2d_page', 'choice-popup': 'test_choice_popup',
    'settings-ini': 'test_settings_ini', 'rtx-scaling': 'test_rtx_scaling',
    'registration': 'test_registration',
}
FILTER_FLAGS = (
    'flush-race', 'gpu', 'nv12', 'p016', 'srv', 'interlaced', 'bff', 'weave',
    'eos', 'yadif', 'force-bff', 'motion', 'sharpen', 'measure', 'startup',
    'mpcbe-owner', 'rtx', 'focus',
)


def remove_test_settings(path):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS) as key:
            while winreg.QueryInfoKey(key)[0]:
                remove_test_settings(path + '\\' + winreg.EnumKey(key, 0))
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
    except FileNotFoundError:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selected = parser.add_mutually_exclusive_group()
    for flag, name in TESTS.items():
        selected.add_argument('--' + flag, dest='test', action='store_const', const=name)
    parser.set_defaults(test='test_filter')
    parser.add_argument('--package', action='store_true')
    for flag in FILTER_FLAGS:
        parser.add_argument('--' + flag, action='store_true')
    options = parser.parse_args()
    forwarded = ['--' + flag for flag in FILTER_FLAGS if getattr(options, flag.replace('-', '_'))]
    if options.test != 'test_filter' and (forwarded or options.package):
        parser.error('Playback and --package options require the default test_filter target.')
    executable = ROOT / 'build' / 'bin' / (options.test + '.exe')
    if not executable.is_file():
        parser.error(f'Build {options.test}.exe before running it.')
    if options.package:
        forwarded += ['--dll', str(ROOT.parent / 'dist' / 'FrankyRenderer.ax')]
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    key = r'Software\PlaceboRendererTest_' + uuid.uuid4().hex
    env = os.environ.copy()
    env['PLACEBO_RENDERER_TEST_ROOT'] = key
    env['PLACEBO_RENDERER_TEST_DEFER_APPLY'] = '1'
    try:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as settings:
            winreg.SetValueEx(settings, 'AutoHDR', 0, winreg.REG_DWORD, 0)
        process = subprocess.Popen(
            [str(executable)] + forwarded, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        print('Native test PID', process.pid, flush=True)
        timed_out = False
        try:
            output = process.communicate(timeout=6 if options.flush_race else 35)[0]
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            output = process.communicate()[0] + b'\nTIMEOUT: own test child terminated\n'
        text = output.decode(errors='replace') + f'\nEXIT {process.returncode & 0xffffffff:#x}\n'
        print(text, flush=True)
        (ROOT / ('last-' + options.test + '.log')).write_text(text, encoding='utf-8')
        return 0 if process.returncode == 0 and not timed_out else 1
    finally:
        remove_test_settings(key)


if __name__ == '__main__':
    sys.exit(main())
