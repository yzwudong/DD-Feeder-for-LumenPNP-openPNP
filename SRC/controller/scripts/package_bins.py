# -*- coding: utf-8 -*-
"""打包发布固件: 由 platformio.ini 构建完成后自动调用(每次 pio run / buildfs 后)

产物 (bin/):
  DDFC_<yymmddnn>_FULL.bin  完整烧录镜像: bootloader+partitions+boot_app0+app+fs 合并
  DDFC_<yymmddnn>_OTA.bin   OTA 固件(app 分区镜像, 不含 bootloader/fs), 经 Web/GitHub OTA 使用
  DDFC_<fsver>_FS.bin           LittleFS 文件系统镜像(Web 页面), 单独烧录: esptool write_flash 0x390000
                              (页面版本= data/ 内容哈希, 见 scripts/gen_fsver.py)

注意: OTA 固件(_OTA.bin) 只含 app 分区, 不含文件系统!
      页面有新版时发布 DDFC_<fsver>_FS.bin, 设备 Web 升级页对比 data/fsver.txt 判断并 OTA 升级。
      _FULL.bin 才包含 FS, 用 esptool write_flash 0x0 整片烧。
"""
try:
    Import("env")                       # PlatformIO/SCons 环境注入
except Exception:
    env = None                          # 独立运行 (python scripts/package_bins.py)
import os
import re
import shutil
import subprocess

try:
    PROJ = env.subst("$PROJECT_DIR")
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # 独立运行
try:
    BUILD = env.subst("$BUILD_DIR")
except Exception:
    BUILD = os.path.join(PROJ, ".pio", "build", "esp32dev")
DIST = os.path.join(PROJ, "bin")
HOME = os.path.expanduser("~")

# 分区表: 偏移要与 default_ota.csv 一致
PART_APP = 0x10000      # app0
PART_FS = 0x390000      # spiffs/littlefs

def _read_ver():
    """从 firmware.bin 内提取实际版本串 (打包名与固件内容严格一致)
    main.cpp boot 打印里含 "DDfeeder<ver>" 字符串, 保证在二进制内可见"""
    fw = os.path.join(BUILD, "firmware.bin")
    if not os.path.exists(fw):
        return None
    with open(fw, "rb") as f:
        data = f.read()
    m = re.search(rb"DDfeeder(\d{8}\d{1,4})", data)   # 序号 1~4 位(可超 99)
    return m.group(1).decode() if m else None

def _penv_python():
    return os.path.join(HOME, ".platformio", "penv", "Scripts", "python.exe")

def _esptool():
    return os.path.join(HOME, ".platformio", "packages", "tool-esptoolpy", "esptool.py")

def _merge_full(ver, fwver):
    """esptool merge_bin 合并完整镜像"""
    boot = os.path.join(BUILD, "bootloader.bin")
    part = os.path.join(BUILD, "partitions.bin")
    app = os.path.join(BUILD, "firmware.bin")
    fs = os.path.join(BUILD, "littlefs.bin")
    bootapp0 = os.path.join(HOME, ".platformio", "packages",
                            "framework-arduinoespressif32", "tools",
                            "partitions", "boot_app0.bin")

    out = os.path.join(DIST, "DDFC_%s_FULL.bin" % fwver)
    args = [_penv_python(), _esptool(), "--chip", "esp32", "merge_bin", "-o", out,
            "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB",
            "0x1000", boot,
            "0x8000", part,
            "0xe000", bootapp0,
            hex(PART_APP), app]
    if os.path.exists(fs):
        args += [hex(PART_FS), fs]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("[PKG] merge_bin 失败: %s" % r.stderr[-400:])
        return False
    print("[PKG] DDFC_%s_FULL.bin (%d KB)" % (fwver, os.path.getsize(out) // 1024))
    return True


# ---- LittleFS 页面镜像: 每次打包都从 data/ 重建, 防止旧镜像冒充新版本 ----
DATA = os.path.join(PROJ, "data")
FS_SIZE = 0x70000          # default_ota.csv: spiffs 分区 448KB
LFS_BLOCK = 4096
LFS_PAGE = 256

def _data_fsver():
    try:
        with open(os.path.join(DATA, "fsver.txt"), "r", encoding="utf-8") as f:
            return f.read().strip()
    except Exception:
        return ""

def _mklittlefs():
    name = "mklittlefs.exe" if os.name == "nt" else "mklittlefs"
    direct = os.path.join(HOME, ".platformio", "packages", "tool-mklittlefs", name)
    if os.path.exists(direct):
        return direct
    import glob
    cands = glob.glob(os.path.join(HOME, ".platformio", "packages", "tool-mklittlefs*", name))
    return max(cands, key=os.path.getmtime) if cands else None

def _rebuild_fs(fsver):
    """从 data/ 重建 littlefs.bin, 并校验镜像内确实含本次 fsver。
    背景: pio run 只编译固件, 不会重建 FS; 旧逻辑直接拷贝 .pio/build/littlefs.bin,
    会把旧页面按新 data/fsver.txt 的版本号打包 -> DDfs 资产名与包内 fsver 不符,
    设备 OTA 校验 _scanFsVerOk() 必然拒绝("页面包校验失败"), 页面修复永远到不了设备。"""
    tool = _mklittlefs()
    if not tool:
        print("[PKG] 找不到 mklittlefs, 无法重建页面镜像")
        return False
    os.makedirs(BUILD, exist_ok=True)
    fs = os.path.join(BUILD, "littlefs.bin")
    args = [tool, "-c", DATA, "-b", str(LFS_BLOCK), "-p", str(LFS_PAGE), "-s", str(FS_SIZE), fs]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("[PKG] 页面镜像构建失败: %s" % (r.stderr or r.stdout)[-400:])
        return False
    try:
        with open(fs, "rb") as f:
            blob = f.read()
    except Exception as e:
        print("[PKG] 读取页面镜像失败: %s" % e)
        return False
    if not fsver or fsver.encode("ascii", "ignore") not in blob:
        print("[PKG] 页面镜像校验失败: 镜像内找不到 fsver=%s (拒绝发布旧镜像)" % (fsver or "?"))
        return False
    print("[PKG] 页面镜像重建 ok: fsver=%s (%d KB)" % (fsver, len(blob) // 1024))
    return True

def package(proj_dir=None):
    proj = proj_dir or PROJ
    app = os.path.join(BUILD, "firmware.bin")
    if not os.path.exists(app):
        print("[PKG] firmware.bin 不存在, 先编译固件")
        return
    ver = _read_ver()
    if not ver:
        print("[PKG] 固件内未找到版本串(DDfeeder...), 请确认 main.cpp 打印包含 FW_VER_FULL")
        return
    os.makedirs(DIST, exist_ok=True)

    fwver = ver[2:]
    shutil.copy(app, os.path.join(DIST, "DDFC_%s_OTA.bin" % fwver))
    print("[PKG] DDFC_%s_OTA.bin (app, 不含 FS)" % fwver)

    fsver = _data_fsver()
    if not _rebuild_fs(fsver):
        print("[PKG] 页面镜像不可用: 不生成 DDfs/_full, 避免旧页面被当成新版本发布")
        return
    fs = os.path.join(BUILD, "littlefs.bin")
    fsname = "DDFC_%s_FS.bin" % fsver
    shutil.copy(fs, os.path.join(DIST, fsname))
    print("[PKG] %s (%d KB, LittleFS 页面)" % (fsname, os.path.getsize(fs) // 1024))

    _merge_full(ver, fwver)

def _package_action(target, source, env):
    """SCons action: pio run -t package"""
    package()
    return 0

# ---- upload_full: 烧录最新完整镜像(bin/*_FULL.bin), VSCode: PROJECT TASKS -> esp32dev -> Custom -> Upload Full ----
def _detect_serial_port():
    try:
        import serial.tools.list_ports as lp
        for p in lp.comports():
            desc = p.description or ""
            if any(k in desc for k in ("CH340", "CP210", "CP210x", "USB Serial", "USB-SERIAL")):
                return p.device
    except Exception:
        pass
    return None

def _upload_full_action(target, source, env):
    import subprocess
    # 先重新编译页面(FS) + 固件, 触发自动打包, 再烧录最新 full —— 保证烧的是刚编译的版本
    # (不加 -t upload_full, 不会递归; buildfs 生成 littlefs.bin, build 编译固件并打包 full)
    pioexe = os.path.join(os.path.dirname(_penv_python()), "pio.exe")
    latest = ""
    if os.path.isdir(DIST):
        fs = [f for f in os.listdir(DIST) if f.endswith("_FULL.bin")]
        if fs:
            latest = max(fs, key=lambda f: os.path.getmtime(os.path.join(DIST, f)))
    print("\n[UPLOAD] ============ Upload Full ============")
    print("[UPLOAD] 当前完整包: %s" % (latest or "无"))
    for t in ("buildfs", None):
        print("[UPLOAD] 步骤 %s: %s …" % ("1/2 编译页面(FS)" if t else "2/2 编译固件+自动打包", ("pio run -t " + t) if t else "pio run"))
        r = subprocess.run([pioexe, "run"] + (["-t", t] if t else []), cwd=PROJ)
        if r.returncode != 0:
            print("[UPLOAD] 编译失败(%s), 取消烧录" % (t or "build"))
            return 1
    fulls = []
    if os.path.isdir(DIST):
        for f in os.listdir(DIST):
            if f.endswith("_FULL.bin"):
                fulls.append(os.path.join(DIST, f))
    if not fulls:
        print("[UPLOAD] bin/ 下没有 _FULL.bin, 请先 pio run (编译+自动打包)")
        return 1
    full = max(fulls, key=os.path.getmtime)
    port = env.subst("$UPLOAD_PORT").strip() if env else ""
    if not port or port.lower() in ("auto", "default", ""):
        port = _detect_serial_port()
    if not port:
        print("[UPLOAD] 未检测到串口(CH340/CP210x), 请用 --upload-port COMx 指定")
        return 1
    print("[UPLOAD] 烧录完整镜像: %s (%d KB) -> %s" % (os.path.basename(full), os.path.getsize(full) // 1024, port))
    args = [_penv_python(), _esptool(), "--chip", "esp32", "--port", port, "--baud", "921600",
            "write_flash", "--flash_mode", "dio", "--flash_freq", "80m", "0x0", full]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("[UPLOAD] 失败: %s" % r.stderr[-500:])
        return 1
    print("[UPLOAD] 烧录完成")
    return 0

if env is not None:
    # 被 PlatformIO 作为 extra_scripts 加载:
    # 1) 固件/页面任一构建完成 → 自动打包 (AddPostAction 挂在产物生成后)
    # 2) 保留手动 target: pio run -t package (打包现有产物)
    try:
        bd = env.subst("$BUILD_DIR").replace("\\", "/")
        fw_bin = bd + "/${PROGNAME}.bin"
        fs_bin = bd + "/littlefs.bin"
        env.AddPostAction(fw_bin, _package_action)
        env.AddPostAction(fs_bin, _package_action)
        env.AddCustomTarget(
            "package", [], _package_action,
            "打包发布固件到 bin/ (DDFC_<yymmddnn>_{FULL,OTA,FS}.bin)")
        env.AddCustomTarget(
            "package_bins", [], _package_action,
            "同 package：打包 bin/ 下 DDFC 固件")
        env.AddCustomTarget(
            "upload_full", [], _upload_full_action,
            "烧录完整镜像(bin/ 最新 DDFC_*_FULL.bin, 固件+页面一次写入)")
    except Exception as e:
        print("[PKG] 注册失败: %s" % e)
else:
    # 独立运行: python scripts/package_bins.py --proj <工程目录>
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--proj", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    a = ap.parse_args()
    package(a.proj)





