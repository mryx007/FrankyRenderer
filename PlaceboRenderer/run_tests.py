"""Run native checks in a child with a disposable settings root."""
import ctypes,json,os,pathlib,subprocess,sys,uuid,winreg
ROOT=pathlib.Path(__file__).resolve().parent
def remove(path):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path,0,winreg.KEY_ALL_ACCESS) as h:
            while winreg.QueryInfoKey(h)[0]:remove(path+'\\'+winreg.EnumKey(h,0))
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER,path)
    except FileNotFoundError:pass
ctypes.windll.kernel32.SetErrorMode(0x8003)
key=r'Software\PlaceboRendererTest_'+uuid.uuid4().hex
env=os.environ.copy();env['PLACEBO_RENDERER_TEST_ROOT']=key
if '--live-ui' not in sys.argv:env['PLACEBO_RENDERER_TEST_DEFER_APPLY']='1'
name='test_sdr_present.exe' if '--sdr-present' in sys.argv else 'test_sdr_target.exe' if '--sdr-target' in sys.argv else 'test_tonal.exe' if '--tonal' in sys.argv else 'test_shader_migration.exe' if '--shader-migration' in sys.argv else 'test_gpu_formats.exe' if '--gpu-formats' in sys.argv else 'test_black_bars.exe' if '--black-bars' in sys.argv else 'test_playlist_window.exe' if '--playlist-window' in sys.argv else 'test_renderer_extras.exe' if '--renderer-extras' in sys.argv else 'test_color_mapping.exe' if '--color-mapping' in sys.argv else 'test_hdr_modes.exe' if '--hdr-modes' in sys.argv else 'test_peak_detection.exe' if '--peak-detection' in sys.argv else 'test_color_adjustment.exe' if '--color-adjustment' in sys.argv else 'test_debanding.exe' if '--debanding' in sys.argv else 'test_scaling.exe' if '--scaling' in sys.argv else 'test_sharpen.exe' if '--sharpen-backend' in sys.argv else 'test_hdr_controls.exe' if '--hdr-controls' in sys.argv else 'test_motion.exe' if '--motion-backend' in sys.argv else 'test_color.exe' if '--color' in sys.argv else 'test_native_lifetime.exe' if '--native-lifetime' in sys.argv else 'test_render_options.exe' if '--render-options' in sys.argv else 'test_backend.exe' if '--backend' in sys.argv else 'test_formats.exe' if '--formats' in sys.argv else 'test_hdr.exe' if '--hdr' in sys.argv else 'test_settings_race.exe' if '--race' in sys.argv else 'test_swapchain.exe' if '--swapchain' in sys.argv else 'test_filter.exe'
try:
    # Tests carry HDR metadata, but must never change the user's display mode.
    # This private root does not inherit the normal player's AutoHDR setting.
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER,key) as settings:
        winreg.SetValueEx(settings,'AutoHDR',0,winreg.REG_DWORD,0)
    args=[arg for arg in sys.argv[1:] if arg in ('--live-ui','--flush-race','--gpu','--nv12','--p016','--srv','--interlaced','--bff','--weave','--eos','--yadif','--force-bff','--motion','--sharpen','--measure','--startup')]
    if '--package' in sys.argv:args+=['--dll',str(ROOT/'PotPlayer-Testbuild'/'PlaceboRenderer.ax')]
    p=subprocess.Popen([str(ROOT/'build'/'bin'/name)]+args,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    print('Native test PID',p.pid,flush=True)
    try:output=p.communicate(timeout=6 if '--race' in sys.argv else 35)[0]
    except subprocess.TimeoutExpired:p.kill();output=p.communicate()[0]+b'\nTIMEOUT: own test child terminated\n'
    text=output.decode(errors='replace')+'\nEXIT '+hex(p.returncode&0xffffffff)
    tag='-'.join(arg[2:] for arg in sys.argv[1:]);print(text,flush=True);(ROOT/('last-'+name+('-'+tag if tag else '')+'.log')).write_text(text,encoding='utf-8')
finally:remove(key)
sys.exit(0 if p.returncode==0 else 1)
