# -*- coding: utf-8 -*-
"""把固件 LOGD 调试日志中的中文改为英文(ASCII), 解决串口助手无 UTF-8 选项时的乱码。
只替换调试日志字符串, 不动 Web/串口屏 UI 文案。"""
import io, sys

ROOT = r"C:\Users\Administrator\Doubao\chats\2026-10-02\new-chat\smt_feeder_controller\src"

REPL = [
    # creds.cpp
    ('credsAdminPwOk(WEB_PASSWORD) ? "已写入" : "已修改"',
     'credsAdminPwOk(WEB_PASSWORD) ? "written" : "changed"'),
    ('[Creds] 管理密码已修改\\n', '[Creds] admin password changed\\n'),
    ('[Creds] 操作员账号已清除\\n', '[Creds] operator account cleared\\n'),
    ('[Creds] 操作员账号已设置: %s\\n', '[Creds] operator account set: %s\\n'),
    # factory_reset.cpp
    ('[FactoryReset] 按键异常: GPIO%d 开机为低(检查 10k 上拉/按键), 本次禁用硬件按键\\n',
     '[FactoryReset] key abnormal: GPIO%d LOW at boot (check 10k pullup/key), hw key disabled\\n'),
    ('[FactoryReset] 按键 GPIO%d, 长按 %dms 恢复出厂\\n',
     '[FactoryReset] key GPIO%d, hold %dms for factory reset\\n'),
    ('[FactoryReset] 硬件按键已禁用(FACTORY_RESET_PIN=-1)\\n',
     '[FactoryReset] hw key disabled (FACTORY_RESET_PIN=-1)\\n'),
    ('[FactoryReset] 执行恢复出厂: 清 NVS + 删 /apmode -> 重启\\n',
     '[FactoryReset] factory reset: erase NVS + rm /apmode -> reboot\\n'),
    # main.cpp
    ('[UART1] 外部主机 %d baud (TX=%d RX=%d)\\n', '[UART1] host port %d baud (TX=%d RX=%d)\\n'),
    ('[FS] LittleFS 挂载失败，Web 将显示提示页 (pio run -t uploadfs)\\n',
     '[FS] LittleFS mount FAILED, Web shows fallback (pio run -t uploadfs)\\n'),
    ('[Web] http://%s  (端口 %d)\\n', '[Web] http://%s  (port %d)\\n'),
    ('[Screen] 串口屏 %d baud (后端=%s)\\n', '[Screen] display %d baud (backend=%s)\\n'),
    ('SCREEN_BACKEND == SCREEN_BACKEND_UART0 ? "UART0" : "软串口"',
     'SCREEN_BACKEND == SCREEN_BACKEND_UART0 ? "UART0" : "soft-serial"'),
    # ota_manager.cpp
    ('[OTA] ESP32 下载 %s\\n', '[OTA] ESP32 download %s\\n'),
    ('[OTA] ESP32 升级完成，重启', '[OTA] ESP32 upgrade done, reboot'),
    ('[OTA] 设备升级 Addr %u\\n', '[OTA] device upgrade Addr %u\\n'),
    ('[OTA] 设备烧写 OK', '[OTA] device flash OK'),
    ('[OTA] FS 下载 %s\\n', '[OTA] FS download %s\\n'),
    ('[OTA] FS 升级完成，重启', '[OTA] FS upgrade done, reboot'),
    # stc8_isp.cpp
    ('[ISP] 握手失败', '[ISP] handshake failed'),
    ('[ISP] 握手 OK arg=0x%02X\\n', '[ISP] handshake OK arg=0x%02X\\n'),
    ('[ISP] 设波特率失败', '[ISP] set baud failed'),
    ('[ISP] 准备失败', '[ISP] prepare failed'),
    ('[ISP] 擦除失败', '[ISP] erase failed'),
    ('[ISP] 已擦除，开始下载', '[ISP] erased, downloading'),
    ('[ISP] 页%u 无响应\\n', '[ISP] page %u no response\\n'),
    ('[ISP] 页%u 校验失败\\n', '[ISP] page %u verify failed\\n'),
    ('[ISP] 下载完成', '[ISP] download done'),
    # wifi_utils.cpp
    ('[mDNS] http://%s.local (端口 %d)\\n', '[mDNS] http://%s.local (port %d)\\n'),
    ('[mDNS] 启动失败\\n', '[mDNS] start failed\\n'),
    ('[WiFi] 热点 %s IP=%s\\n', '[WiFi] AP %s IP=%s\\n'),
    ('[WiFi] 扫描完成: 共 %d 个网络\\n', '[WiFi] scan done: %d networks\\n'),
    ('[WiFi] 目标 \'%s\' 扫描到 %d 个 AP: ', '[WiFi] target \'%s\': %d APs: '),
    ('[WiFi] 未扫描到该 SSID(隐藏网络?), 普通连接\\n', '[WiFi] SSID not found (hidden?), plain connect\\n'),
    ('[WiFi] 尝试 AP#%d (ch=%d rssi=%ddBm)\\n', '[WiFi] trying AP#%d (ch=%d rssi=%ddBm)\\n'),
    ('[WiFi] AP#%d 超时, 尝试下一个\\n', '[WiFi] AP#%d timeout, next\\n'),
    ('[WiFi] 已连接 -> IP=%s RSSI=%ddBm\\n', '[WiFi] connected -> IP=%s RSSI=%ddBm\\n'),
    ('[WiFi] AP 模式标志存在, 以热点启动', '[WiFi] AP flag exists, start AP'),
    ('[WiFi] 无网络(仅串口可用)', '[WiFi] no network (serial only)'),
    ('[WiFi] 已保存 %s，正在连接\\n', '[WiFi] saved %s, connecting\\n'),
    ('[WiFi] 连接失败', '[WiFi] connect failed'),
    # web_server.cpp
    ('_authEnabled ? "开启" : "关闭"', '_authEnabled ? "on" : "off"'),
    ('[Unlock] Web 写操作已解锁(5分钟超时)', '[Unlock] web write unlocked (5min timeout)'),
    ('[Unlock] 已手动锁定', '[Unlock] manually locked'),
]

files = {
    'creds.cpp': 0, 'factory_reset.cpp': 0, 'main.cpp': 0, 'ota_manager.cpp': 0,
    'stc8_isp.cpp': 0, 'wifi_utils.cpp': 0, 'web_server.cpp': 0,
}

ok = True
for fn in files:
    path = ROOT + '\\' + fn
    with io.open(path, 'r', encoding='utf-8') as f:
        text = f.read()
    for old, new in REPL:
        cnt = text.count(old)
        if cnt == 0:
            print('MISS: %s in %s' % (old, fn))
            ok = False
            continue
        if cnt > 1:
            print('DUP(%d): %s in %s' % (cnt, old, fn))
        text = text.replace(old, new)
        files[fn] += 1
    with io.open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)

if not ok:
    sys.exit(1)
print('all replacements applied:', sum(files.values()))
