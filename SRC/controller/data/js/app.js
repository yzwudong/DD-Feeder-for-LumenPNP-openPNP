/* ============================================================
 * 多语言: 自动检测浏览器语言(zh->中文, 其他->English)，可手动切换
 * 用法: 静态文本 data-i18n="key"，动态文本 t('key')
 * ============================================================ */
/* 内置语言表：仅作为 /api/lang 拉取失败时的兜底。
 * 正式文案以 data/lang/<code>.txt 为准，改文案只需改 txt，不用动本文件。 */
const FALLBACK_I18N = {
  zh: {
    'app.title':'SMT 飞达控制器',
    'nav.overview':'总览','nav.wifi':'WiFi','nav.ota':'升级','nav.term':'终端','nav.evlog':'日志','nav.debug':'调测','nav.logout':'退出','nav.login':'登录设置',
    'role.admin':'管理员','role.operator':'操作员(只读)','role.viewer':'未登录(只读)',
    'meta.online':'在线','meta.fw':'固件','meta.rssi':'信号','meta.ip':'IP','meta.scan':'扫描设备','meta.scanning':'扫描中…','meta.feed':'推料',
    'meta.uninit':'⚠ 发现未初始化飞达','meta.conflict':'⚠ 地址冲突','meta.clearFeed':'清零计数','meta.power':'总线电源','meta.powerOn':'开','meta.powerOff':'关','meta.powerFault':'故障','meta.powerOffConfirm':'关闭总线电源？所有飞达将断电！','meta.buzzer':'蜂鸣器','meta.buzzerOn':'开','meta.buzzerOff':'关','nav.cancel':'取消',
    'meta.clearFeedConfirm':'确认清零所有飞达的推料计数？此操作不可恢复！','meta.cleared':'已清零',
    /* 日志页(事件) */
    'ev.title':'事件日志','ev.desc':'关键事件记录（电源故障 / OTA / 启动，掉电保留，最近 32 条）',
    'ev.refresh':'刷新','ev.empty':'暂无事件记录','ev.run':'运行','ev.timeUnknown':'无时间','ev.timeHint':'时间：联网(NTP)显示实时时间；未联网显示运行时长',
    'ev.tagErr':'故障','ev.tagOk':'正常','ev.tagInfo':'信息','ev.tagWarn':'注意',
    'ev.sysBoot':'系统启动','ev.sysBootFmt':'固件 {0}，重启原因：{1}',
    'ev.reason0':'上电','ev.reason1':'外部复位','ev.reason2':'软件复位','ev.reason3':'看门狗复位','ev.reason4':'异常崩溃','ev.reason5':'欠压复位','ev.reasonX':'其他',
    'ev.pwrFault':'总线电源故障','ev.pwrFaultReset':'总线电源故障（已自动复位 {0} 次，累计 {1} 次）','ev.pwrFaultOff':'总线电源故障（自动复位超限，已保持关断，累计 {0} 次）',
    'ev.pwrRecover':'总线电源故障恢复','ev.pwrOn':'总线电源已手动开启','ev.pwrLock':'总线电源自动重试终止（观察窗口内再次故障，已保持关断）',
    'ev.otaEspOk':'ESP32 固件升级成功（{0} → {1}）','ev.otaEspFail':'ESP32 固件升级失败：{0}',
    'ev.otaFsOk':'页面(FS)升级成功（{0}）','ev.otaFsFail':'页面(FS)升级失败：{0}',
    'ev.otaDevOk':'飞达设备升级成功（地址 {0}）','ev.otaDevFail':'飞达设备升级失败（地址 {0}）：{1}',
    'ev.unknown':'未知事件',
    /* 调测页 */
    'debug.title':'调测','debug.desc':'可视化手动测试：选择飞达后下发命令，回复显示在下方',
    'debug.device':'设备','debug.none':'未选择','debug.refresh':'刷新列表','debug.unlockHint':'写操作需先解锁',
    'debug.q':'查询 M602','debug.enable':'使能','debug.disable':'禁用','debug.feed8':'送料 F8','debug.feed4':'送料 F4','debug.retract':'取料回退','debug.angle':'舵机角度',
    'debug.blink':'闪烁定位','debug.blinkOff':'停止闪烁','debug.scan':'扫描','debug.calib':'舵机校准','debug.sent':'已发送','debug.noDev':'未选择设备',
    'debug.safeHint':'调测命令会实际驱动舵机/动作，请确认飞达处于空闲状态','debug.angleMin':'0°（最小值）','debug.angleMid':'90°（中点）','debug.angleMax':'180°（最大值）',
    'debug.calibWarn':'校准会反复转动舵机并按限位开关二分逼近 0°/90° 脉宽，结束后写回 V/W 参数。\n请确认贴片机已停止，飞达空闲且带反馈线(限位开关)。\nM602 应答需含 L 限位字段(下位机配合点)。','debug.calibStart':'校准已启动，正在二分…','debug.cancel':'取消',
    'row.a':'A 排','row.b':'B 排','row.temp':'临时飞达','row.tempHint':'地址99(未配置)或地址冲突 · 可进详情页配地址/校准后投入生产',
    'st.online':'在线','st.offline':'离线','st.enabled':'使能','st.disabled':'禁用','st.temp':'临时','st.conflict':'冲突',
    'login.pass':'密码','login.btn':'登录','login.err':'密码错误','login.hint':'请输入密码以访问控制界面',
    'login.auth':'需要登录验证','login.authOn':'登录验证已开启','login.authOff':'登录验证已关闭',
    'login.enter':'进入总览','login.toggleErr':'设置失败',

    'wifi.title':'WiFi 设置','wifi.saved':'已保存网络','wifi.sta':'当前连接','wifi.ap':'热点模式',
    'wifi.apssid':'热点名称','wifi.apip':'热点 IP','wifi.none':'未保存','wifi.disconnected':'未连接',
    'wifi.scanbtn':'扫描网络','wifi.sel':'选择网络','wifi.ssid':'WiFi 名称','wifi.pass':'WiFi 密码','wifi.passReq':'此网络需要密码',
    'wifi.connect':'连接并保存','wifi.connecting':'连接中，设备将切换到新网络，请用新 IP 重新访问',
    'wifi.fail':'连接失败，已保持热点模式','wifi.refresh':'刷新状态',
    'wifi.del':'删除','wifi.delConfirm':'删除已保存的网络“%s”？下次启动将不再尝试连接它',
    'wifi.delDone':'已删除','wifi.delFail':'删除失败',
    'wifi.apmode':'AP 模式','wifi.apmodeHint':'切换后设备以热点启动(SMT-Feeder-AP)，手机直连访问；连上 WiFi 后自动回到 STA 模式',
    'wifi.apmodeBtn':'切换到 AP 模式','wifi.apmodeConfirm':'确认切换到 AP 模式？设备将重启并以热点 SMT-Feeder-AP 启动，手机连接该热点后访问 http://192.168.4.1',

    'ota.title':'固件升级','ota.esp32':'ESP32 控制器','ota.device':'飞达设备',
    'ota.cur':'当前固件','ota.latest':'最新固件','ota.check':'检测更新','ota.upgrade':'升级固件','ota.fsCur':'当前页面','ota.fsLatest':'最新页面','ota.fsUp':'升级页面','ota.fsFound':'页面有更新',
    'ota.addr':'飞达地址','ota.devup':'飞达升级页','ota.none':'无','ota.n99Refresh':'刷新 N99','ota.n99Manual':'手动 N99',
  'ota.onlineTitle':'在线升级','ota.localTitle':'本地升级','ota.appTitle':'APP 固件','ota.fsTitle':'ESP32 页面（FS）','ota.localBtn':'本地上传 .bin 升级','ota.localHint':'选择 ESP32 OTA .bin（DDFC_*_OTA.bin），分片写入，期间请勿断电。',
    'ota.force':'强制升级（允许降级/同版本）','ota.localFsBtn':'本地上传 .bin 升级页面','ota.localFsHint':'选择页面 FS .bin（DDFC_*_FS.bin），分片写入 LittleFS，期间请勿断电。',
    'ota.devFound':'发现新飞达固件包，可逐个选择升级','ota.devNone':'暂无飞达固件包',
    'fup.target':'目标飞达','fup.chooseTarget':'选择目标（正式飞达 / N99 临时飞达）','fup.n99Manual':'手动 N99','fup.n99Scan':'刷新 N99','fup.addr':'485 地址','fup.chipid':'ChipID','fup.boardFw':'板上固件','fup.readBoard':'读取','fup.pkgFw':'包固件',
    'fup.source':'固件包来源','fup.srcLocal':'本地上传','fup.srcGithub':'GitHub 在线',
    'fup.pickHex':'选择从机固件 hex 文件（Intel HEX，含 DDFW: 版本标识）','fup.upload':'上传并校验','fup.localHint':'上传后仅保存在内存中，校验通过即可升级；不会写入文件系统。',
    'fup.ghCheck':'检查 GitHub 最新版','fup.ghAsset':'Release 固件','fup.ghVer':'远程版本',
    'fup.ctl':'升级','fup.force':'强制升级（允许同版本重刷/降级）','fup.go':'开始升级','fup.back':'返回详情页','fup.warn':'升级过程中请勿断电/断开连接；烧写完成后设备自动复位，回校版本一致即成功。','fup.log':'升级日志',
    'fup.state.idle':'就绪','fup.state.downloading':'下载中','fup.state.parsing':'解析包','fup.state.comparing':'版本比对','fup.state.isp':'ISP 烧写','fup.state.verify':'回校','fup.state.done':'完成','fup.state.error':'失败',
    'ota.uptodate':'已是最新版本','ota.found':'发现新版本','ota.upgrading':'升级中…',
    'ota.progress':'进度','ota.state.idle':'空闲','ota.state.downloading':'下载中',
    'ota.state.flashing':'烧写中','ota.state.done':'完成','ota.state.error':'失败',
    'ota.confirmEsp':'确认升级 ESP32 固件？期间设备会重启。','ota.confirmFs':'确认升级 Web 页面？页面文件将被替换，期间短暂重启。','ota.confirmDev':'确认升级该飞达固件？',
    'ota.needCheck':'请先点击「检测更新」',

    'feeder.title':'飞达控制','feeder.back':'返回总览','feeder.state':'状态',
    'feeder.chipid':'ChipID','feeder.ver':'配置版本','feeder.speed':'速度','feeder.pitch':'节距',
    'feeder.unknown':'未知','feeder.feedCnt':'推料次数','feeder.clearFeed':'清零计数','feeder.fw':'固件版本','feeder.cfgT':'N20超时','feeder.cfgTLabel':'T N20超时(0~5000)','feeder.cfgTHint':'T: N20 超时参数(0~5000)。T0=周期完成立即检查限位(不等待)；默认 1000。不同电机转速不同，超时可单独设置',
    'feeder.clearFeedConfirm':'确认清零当前飞达的推料计数？','feeder.cleared':'已清零','feeder.enable':'使能/禁用','feeder.disable':'禁用飞达','feeder.disabledFirst':'飞达已禁用，请先启用','feeder.enableBtn':'启用飞达','feeder.feed':'送料(mm)','feeder.feedGo':'送料','feeder.repeatX':'重复(次)',
    'feeder.feedTitle':'送料','feeder.servoTitle':'舵机','feeder.angle':'舵机角度(°)','feeder.setAngle':'设置角度','feeder.retract':'回退推杆','feeder.angleSet':'角度已发送','feeder.retracted':'回退指令已发送','feeder.repeating':'送料中(第 {n}/{x} 次)','feeder.feedSent':'已发送 {n}/{x}','feeder.feedStop':'停止','feeder.feedStopped':'已停止送料','feeder.feedProdStop':'检测到生产模式，自动送料已停止','feeder.feedFull':'已推进到 FULL，取料后请点“回退推杆”(M601)','feeder.replyTimeout':'设备应答超时，已中断','feeder.replyErr':'设备应答错误，已中断',
    'feeder.unload':'退料','feeder.reset':'恢复出厂',
    'feeder.cfgM620':'M620 参数配置','feeder.cfgA':'A 全进料角度','feeder.cfgB':'B 半进料角度',
    'feeder.cfgC':'C 回退角度','feeder.cfgF':'F 进料长度(mm)','feeder.cfgU':'U 稳定时间',
    'feeder.cfgV':'V 最小脉宽','feeder.cfgW':'W 最大脉宽','feeder.cfgX':'X 忽略反馈',
    'feeder.cfgX0':'0·有反馈线','feeder.cfgX1':'1·无反馈线(忽略)',
    'feeder.cfgChip':'ChipID(可选)','feeder.cfgHint':'仅对当前飞达生效；填 ChipID 强校验(N+P)',
    'feeder.applyCfg':'应用配置','feeder.cfgDef':'默认值','feeder.cfgDefDone':'已填入出厂默认值','feeder.cfgGet':'获取当前配置','feeder.cfgGot':'已获取当前配置','feeder.adminPw':'输入管理页密码','feeder.pwErr':'密码错误',
    'feeder.confirmReset':'确认恢复出厂设置？将重置当前飞达！',
    'feeder.cmdSent':'指令已发送','feeder.blink':'闪烁定位','feeder.blinkStop':'停止闪烁','feeder.up':'升级固件',
    'feeder.offline':'离线','feeder.busy':'送料中','feeder.noaddr':'缺少地址参数',
    'feeder.calib':'舵机校准','feeder.calibBusy':'校准进行中，请稍后','feeder.calibHint':'开始前请先接入校准设备（0°/90° 限位开关或校准工装），并确认贴片机已停止、该飞达空闲。校准会反复转动舵机，递加逼近防过冲，成功后才写回 V/W。',
    'feeder.calibWarn':'请先接入校准设备（0°/90° 限位开关或校准工装），再开始校准。\n校准会按限位开关递加逼近 0°/90° 脉宽(防过冲扫齿)，成功后才写回 V/W。\n请确认贴片机已停止、飞达空闲。\n校准进行中请勿关闭/切换本页面：页面失联 20 秒后自动中止(配置不回写)。',
    'feeder.calibKeepOpen':'校准进行中：请勿关闭或切换本页面，页面失联 20 秒后校准将自动中止（配置不会写回）。',
    'feeder.calibStart':'校准已启动…','feeder.calibStop':'终止校准','feeder.calibStopDone':'校准已终止','feeder.test':'测试功能','feeder.calibDone':'校准完成','feeder.calibGo':'开始校准','feeder.log485':'485 实时日志',

    'unlock.title':'解锁操作','unlock.warn':'解锁后将允许向 485 总线发送控制指令。\n请务必先停止贴片机飞达的当前动作，否则会导致动作混乱！',
    'unlock.btn':'确认解锁','unlock.cancel':'取消','unlock.locked':'已锁定','unlock.unlocked':'已解锁',
    'unlock.lockBtn':'锁定','unlock.unlockBtn':'解锁操作','unlock.fail':'解锁失败','unlock.need':'请先解锁操作',

    'log.title':'实时日志','log.hint':'实时显示 UART2(485 总线) 的发送与接收消息',
    'log.tx':'TX→','log.rx':'RX←','log.dbg':'LOG','log.clear':'清空',
    'term.dbgOn':'调试日志:开','term.dbgOff':'调试日志:关',
    'login.user':'用户名',
    'login.keep20':'20 分钟失效',
    'login.keepForever':'永久登录',
    'login.adminTitle':'账号与权限',
    'login.adminHint':'管理员用户名固定 admin；操作员账号仅可浏览/查询（写操作后端强制拒绝）。',
    'login.adminPwOld':'旧管理密码',
    'login.adminPwNew':'新管理密码（≥4 位）',
    'login.adminPwBtn':'修改管理密码',
    'login.opUserL':'操作员用户名',
    'login.opPassL':'操作员密码（≥4 位，留空并点"清除"即删除账号）',
    'login.opBtn':'保存操作员',
    'login.opClear':'清除操作员',
    'login.saveOk':'已保存',
    'login.saveErr':'保存失败',
    'login.adminPwErr':'旧密码错误或新密码太短(≥4位)',
    'login.factoryTitle':'恢复出厂设置',
    'login.factoryHint':'清空 WiFi / 账号密码 / 所有设置并重启，设备回到出厂默认（AP 热点 + 默认管理密码 123456）。此操作不可逆！',
    'login.factoryPw':'输入当前管理密码确认',
    'login.factoryBtn':'恢复出厂设置',
    'login.factoryConfirm':'确定恢复出厂？所有设置（WiFi/密码/配置/事件日志）将被清空，设备将重启！',
    'login.factoryDone':'已恢复出厂，设备即将重启…',
    'login.factoryErr':'密码错误或操作失败',
    'term.title':'终端',
    'term.cmdTitle':'发送 M-Code 命令',
    'term.send':'发送',
    'term.hint':'示例(点按填充):',
    'term.warn':'危险命令（M799 恢复出厂 / M788 ISP 复位 / M903 电源 / M905 清零 / 广播写命令）需输入管理密码。',
    'term.pwTitle':'需要管理密码',
    'term.pwHint':'此命令会同时影响多台飞达或恢复出厂，请输入管理密码确认。',
    'term.confirm':'确认发送',
    'term.sent':'已发送','term.fail':'发送失败',
    'term.adminErr':'请先登录：导航栏「登录设置」输入管理密码，或关闭登录开关',
    'term.bad':'命令格式无法解析',
  },
  en: {
    'app.title':'SMT Feeder Controller',
    'nav.overview':'Overview','nav.wifi':'WiFi','nav.ota':'Firmware','nav.term':'Terminal','nav.evlog':'Events','nav.debug':'Debug','nav.logout':'Logout','nav.login':'Login',
    'role.admin':'Admin','role.operator':'Operator (read-only)','role.viewer':'Guest (read-only)',
    'meta.online':'Online','meta.fw':'FW','meta.rssi':'RSSI','meta.ip':'IP','meta.scan':'Scan','meta.scanning':'Scanning…','meta.feed':'Feeds',
    'meta.uninit':'⚠ Uninitialized feeder(s) found','meta.conflict':'⚠ Address conflict','meta.clearFeed':'Clear feeds','meta.power':'Bus power','meta.powerOn':'ON','meta.powerOff':'OFF','meta.powerFault':'FAULT','meta.powerOffConfirm':'Turn OFF bus power? ALL feeders will lose power!','meta.buzzer':'Buzzer','meta.buzzerOn':'ON','meta.buzzerOff':'OFF','nav.cancel':'Cancel',
    'meta.clearFeedConfirm':'Clear feed counts for ALL feeders? This cannot be undone!','meta.cleared':'Cleared',
    /* Event log page */
    'ev.title':'Event Log','ev.desc':'Key events (power fault / OTA / boot), kept across reboots, latest 16',
    'ev.refresh':'Refresh','ev.empty':'No events yet','ev.run':'Uptime','ev.timeUnknown':'--','ev.timeHint':'Time: real time when NTP is synced; uptime otherwise',
    'ev.tagErr':'FAULT','ev.tagOk':'OK','ev.tagInfo':'INFO','ev.tagWarn':'WARN',
    'ev.sysBoot':'System boot','ev.sysBootFmt':'FW {0}, reset reason: {1}',
    'ev.reason0':'power-on','ev.reason1':'external reset','ev.reason2':'software reset','ev.reason3':'watchdog','ev.reason4':'crash','ev.reason5':'brownout','ev.reasonX':'other',
    'ev.pwrFault':'Bus power fault','ev.pwrFaultReset':'Bus power fault (auto-reset #{0}, total {1})','ev.pwrFaultOff':'Bus power fault (auto-reset limit reached, kept OFF, total {0})',
    'ev.pwrRecover':'Bus power recovered','ev.pwrOn':'Bus power turned ON manually','ev.pwrLock':'Bus power auto-retry stopped (repeat fault in watch window, kept OFF)',
    'ev.otaEspOk':'ESP32 firmware updated ({0} -> {1})','ev.otaEspFail':'ESP32 firmware update failed: {0}',
    'ev.otaFsOk':'Web pages (FS) updated ({0})','ev.otaFsFail':'Web pages (FS) update failed: {0}',
    'ev.otaDevOk':'Feeder firmware updated (addr {0})','ev.otaDevFail':'Feeder firmware update failed (addr {0}): {1}',
    'ev.unknown':'Unknown event',
    /* Debug page */
    'debug.title':'Debug','debug.desc':'Manual test: pick a feeder, send commands, replies shown below',
    'debug.device':'Device','debug.none':'none','debug.refresh':'Refresh list','debug.unlockHint':'Write actions require unlock first',
    'debug.q':'Query M602','debug.enable':'Enable','debug.disable':'Disable','debug.feed8':'Feed F8','debug.feed4':'Feed F4','debug.retract':'Retract','debug.angle':'Servo angle',
    'debug.blink':'Blink LED','debug.blinkOff':'Stop blink','debug.scan':'Scan','debug.calib':'Servo calibrate','debug.sent':'Sent','debug.noDev':'No device selected',
    'debug.safeHint':'Debug commands actually move the servo/feeder; make sure feeders are idle','debug.angleMin':'0° (min)','debug.angleMid':'90° (mid)','debug.angleMax':'180° (max)',
    'debug.calibWarn':'Calibration sweeps the servo and binary-searches the 0°/90° limit switch edge, then writes V/W back.\nMake sure the machine is stopped, feeder is idle with feedback line (limit switches).\nM602 reply must include the L limit field (firmware-side support).','debug.calibStart':'Calibration started…','debug.cancel':'Cancel',
    'row.a':'Row A','row.b':'Row B','row.temp':'Temp Feeders','row.tempHint':'addr=99 (unconfigured) or address conflict · configure/calibrate in detail page',
    'st.online':'Online','st.offline':'Offline','st.enabled':'Enabled','st.disabled':'Disabled','st.temp':'Temp','st.conflict':'Conflict',
    'login.pass':'Password','login.btn':'Sign in','login.err':'Wrong password','login.hint':'Enter password to access the control panel',
    'login.auth':'Require login','login.authOn':'Login verification is on','login.authOff':'Login verification is off',
    'login.enter':'Enter','login.toggleErr':'Setting failed',

    'wifi.title':'WiFi Settings','wifi.saved':'Saved network','wifi.sta':'Current','wifi.ap':'AP mode',
    'wifi.apssid':'AP SSID','wifi.apip':'AP IP','wifi.none':'None','wifi.disconnected':'Disconnected',
    'wifi.scanbtn':'Scan networks','wifi.sel':'Select network','wifi.ssid':'WiFi name','wifi.pass':'WiFi password','wifi.passReq':'This network requires a password',
    'wifi.connect':'Connect & save','wifi.connecting':'Connecting... device will switch to the new network, revisit with the new IP',
    'wifi.fail':'Connection failed, AP mode kept','wifi.refresh':'Refresh status',
    'wifi.del':'Delete','wifi.delConfirm':'Delete saved network "%s"? It will not be tried on next boot',
    'wifi.delDone':'Deleted','wifi.delFail':'Delete failed',
    'wifi.apmode':'AP Mode','wifi.apmodeHint':'Switch to hotspot (SMT-Feeder-AP); connect WiFi later to return to STA',
    'wifi.apmodeBtn':'Switch to AP Mode','wifi.apmodeConfirm':'Switch to AP mode? Device restarts as hotspot SMT-Feeder-AP. Connect and visit http://192.168.4.1',

    'ota.title':'Firmware Upgrade','ota.esp32':'ESP32 Controller','ota.device':'Feeder Device',
    'ota.cur':'Firmware','ota.latest':'Latest FW','ota.check':'Check','ota.upgrade':'Upgrade FW','ota.fsCur':'Pages','ota.fsLatest':'Latest Pages','ota.fsUp':'Upgrade Pages','ota.fsFound':'Pages update available',
    'ota.addr':'Feeder address','ota.devup':'Feeder upgrade page','ota.none':'None','ota.n99Refresh':'Refresh N99','ota.n99Manual':'Manual N99',
    'ota.onlineTitle':'Online upgrade','ota.localTitle':'Local upgrade','ota.appTitle':'APP firmware','ota.fsTitle':'ESP32 page (FS)','ota.localBtn':'Upload local .bin','ota.localHint':'Select ESP32 OTA .bin (DDFC_*_OTA.bin); chunks are written directly. Do not power off.',
    'ota.force':'Force upgrade (allow same/downgrade)','ota.localFsBtn':'Upload local FS .bin','ota.localFsHint':'Select DDFC_*_FS.bin; chunks are written to LittleFS. Do not power off.',
    'ota.devFound':'New feeder firmware package found, upgrade one by one','ota.devNone':'No feeder firmware package',
    'fup.target':'Target Feeder','fup.chooseTarget':'Target (normal / N99 temporary)','fup.n99Manual':'Manual N99','fup.n99Scan':'Refresh N99','fup.addr':'485 address','fup.chipid':'ChipID','fup.boardFw':'On-board FW','fup.readBoard':'Read','fup.pkgFw':'Package FW',
    'fup.source':'Firmware Source','fup.srcLocal':'Local upload','fup.srcGithub':'GitHub online',
    'fup.pickHex':'Pick slave hex file (Intel HEX with DDFW: version marker)','fup.upload':'Upload & verify','fup.localHint':'Kept in RAM only; upgrade after verification. Not written to filesystem.',
    'fup.ghCheck':'Check GitHub latest','fup.ghAsset':'Release firmware','fup.ghVer':'Remote version',
    'fup.ctl':'Upgrade','fup.force':'Force upgrade (allow same-version reflash / downgrade)','fup.go':'Start Upgrade','fup.back':'Back to detail','fup.warn':'Do not power off or disconnect during upgrade; device auto-resets after flashing, success confirmed by version re-check.','fup.log':'Upgrade Log',
    'fup.state.idle':'Idle','fup.state.downloading':'Downloading','fup.state.parsing':'Parsing','fup.state.comparing':'Comparing','fup.state.isp':'Flashing ISP','fup.state.verify':'Verifying','fup.state.done':'Done','fup.state.error':'Failed',
    'ota.uptodate':'Up to date','ota.found':'New version found','ota.upgrading':'Upgrading…',
    'ota.progress':'Progress','ota.state.idle':'Idle','ota.state.downloading':'Downloading',
    'ota.state.flashing':'Flashing','ota.state.done':'Done','ota.state.error':'Failed',
    'ota.confirmEsp':'Upgrade ESP32 firmware? Device will reboot.','ota.confirmFs':'Upgrade Web pages? Page files will be replaced and device restarts briefly.','ota.confirmDev':'Upgrade this feeder firmware?',
    'ota.needCheck':'Click "Check" first',

    'feeder.title':'Feeder Control','feeder.back':'Back to overview','feeder.state':'State',
    'feeder.chipid':'ChipID','feeder.ver':'Config ver','feeder.speed':'Speed','feeder.pitch':'Pitch',
    'feeder.unknown':'Unknown','feeder.feedCnt':'Feed count','feeder.clearFeed':'Clear count','feeder.fw':'Firmware','feeder.cfgT':'N20 Timeout','feeder.cfgTLabel':'T N20 Timeout (0~5000)','feeder.cfgTHint':'T: N20 timeout (0~5000). T0 = check limit immediately after cycle (no wait); default 1000. Timeout can differ per motor speed',
    'feeder.clearFeedConfirm':'Clear this feeder feed count?','feeder.cleared':'Cleared','feeder.enable':'Enable/Disable','feeder.disable':'Disable feeder','feeder.disabledFirst':'Feeder is disabled. Enable it first.','feeder.enableBtn':'Enable feeder','feeder.feed':'Feed (mm)','feeder.feedGo':'Feed','feeder.repeatX':'Repeat',
    'feeder.feedTitle':'Feed','feeder.servoTitle':'Servo','feeder.angle':'Servo angle (°)','feeder.setAngle':'Set angle','feeder.retract':'Retract','feeder.angleSet':'Angle sent','feeder.retracted':'Retract sent','feeder.repeating':'Feeding ({n}/{x})','feeder.feedSent':'Sent {n}/{x}','feeder.feedStop':'Stop','feeder.feedStopped':'Feed stopped','feeder.feedProdStop':'Production mode detected; auto feed stopped','feeder.replyTimeout':'Device reply timeout, feeding aborted','feeder.replyErr':'Device reply error, feeding aborted',
    'feeder.unload':'Unload','feeder.reset':'Factory Reset',
    'feeder.cfgM620':'M620 Parameters','feeder.cfgA':'A full angle','feeder.cfgB':'B half angle',
    'feeder.cfgC':'C retract angle','feeder.cfgF':'F feed length(mm)','feeder.cfgU':'U settle time',
    'feeder.cfgV':'V min pulse','feeder.cfgW':'W max pulse','feeder.cfgX':'X ignore feedback',
    'feeder.cfgX0':'0·feedback line','feeder.cfgX1':'1·no feedback (ignore)',
    'feeder.cfgChip':'ChipID (optional)','feeder.cfgHint':'Applies to this feeder only; ChipID adds N+P check',
    'feeder.applyCfg':'Apply','feeder.cfgDef':'Defaults','feeder.cfgDefDone':'Factory defaults filled','feeder.cfgGet':'Load current','feeder.cfgGot':'Current config loaded','feeder.adminPw':'Enter admin password','feeder.pwErr':'Wrong password',
    'feeder.confirmReset':'Reset this feeder to factory defaults?',
    'feeder.cmdSent':'Command sent','feeder.blink':'Blink locate','feeder.blinkStop':'Stop blink','feeder.up':'Upgrade FW',
    'feeder.offline':'Offline','feeder.busy':'Feeding','feeder.noaddr':'Missing address parameter',
    'feeder.calib':'Servo Calibrate','feeder.calibBusy':'Calibration in progress, try later','feeder.calibHint':'Connect the calibration fixture (0°/90° limit switches or calibration jig) first, then start. Calibration moves the servo repeatedly; incremental approach prevents over-shoot; V/W are written back only on success.',
    'feeder.calibWarn':'Connect the calibration fixture (0°/90° limit switches or calibration jig) before starting.\nCalibration approaches 0°/90° pulse widths incrementally (no over-shoot); V/W are written only on success.\nConfirm the machine is stopped and the feeder is idle.\nDo NOT close or switch away during calibration: it auto-aborts 20s after the page goes offline (config not written).',
    'feeder.calibKeepOpen':'Calibration running: keep this page open. If the page is lost for 20s, calibration auto-aborts (no config written).',
    'feeder.calibStart':'Calibration started…','feeder.calibStop':'Stop calibration','feeder.calibStopDone':'Calibration stopped','feeder.test':'Test feature','feeder.calibDone':'Calibration done','feeder.calibGo':'Start Calibration','feeder.log485':'485 Live Log',

    'unlock.title':'Unlock','unlock.warn':'Unlocking allows sending control commands to the 485 bus.\nMake sure the feeder has STOPPED all motion, otherwise actions may become chaotic!',
    'unlock.btn':'Unlock','unlock.cancel':'Cancel','unlock.locked':'Locked','unlock.unlocked':'Unlocked',
    'unlock.lockBtn':'Lock','unlock.unlockBtn':'Unlock','unlock.fail':'Unlock failed','unlock.need':'Unlock required',

    'log.title':'Live Log','log.hint':'Real-time TX/RX messages on UART2 (485 bus)',
    'log.tx':'TX→','log.rx':'RX←','log.dbg':'LOG','log.clear':'Clear',
    'term.dbgOn':'Debug Log:ON','term.dbgOff':'Debug Log:OFF',
    'login.user':'Username',
    'login.keep20':'20 min session',
    'login.keepForever':'Stay signed in',
    'login.adminTitle':'Accounts & Permissions',
    'login.adminHint':'Admin username is fixed to admin; operator accounts are read-only (writes are rejected by backend).',
    'login.adminPwOld':'Old admin password',
    'login.adminPwNew':'New admin password (>=4 chars)',
    'login.adminPwBtn':'Change admin password',
    'login.opUserL':'Operator username',
    'login.opPassL':'Operator password (>=4 chars; leave blank + click Clear to remove account)',
    'login.opBtn':'Save operator',
    'login.opClear':'Clear operator',
    'login.saveOk':'Saved',
    'login.saveErr':'Save failed',
    'login.adminPwErr':'Old password wrong or new password too short (>=4)',
    'login.factoryTitle':'Factory Reset',
    'login.factoryHint':'Clears WiFi / credentials / all settings and restarts. Device returns to factory defaults (AP hotspot + default admin password 123456). Irreversible!',
    'login.factoryPw':'Enter current admin password to confirm',
    'login.factoryBtn':'Factory Reset',
    'login.factoryConfirm':'Reset to factory? ALL settings (WiFi/password/config/event log) will be cleared and the device will restart!',
    'login.factoryDone':'Factory reset done, device restarting…',
    'login.factoryErr':'Wrong password or operation failed',
    'term.title':'Terminal',
    'term.cmdTitle':'Send M-Code Command',
    'term.send':'Send',
    'term.hint':'Examples (click to fill):',
    'term.warn':'Dangerous commands (M799 factory reset / M788 ISP reset / M903 power / M905 clear / broadcast writes) require the admin password.',
    'term.pwTitle':'Admin password required',
    'term.pwHint':'This command affects multiple feeders or resets factory settings. Enter the admin password to confirm.',
    'term.confirm':'Confirm send',
    'term.sent':'Sent','term.fail':'Send failed',
    'term.adminErr':'Wrong password or insufficient permission',
    'term.bad':'Unparsable command format'
  }
};

let LANG = (localStorage.getItem('lang')) ||
           ((navigator.language || '').toLowerCase().startsWith('zh') ? 'zh' : 'en');

/* 运行时语言包: GET /api/lang?code=xx 取 key=value 文本.
 * 优先用 localStorage 缓存渲染首屏, 再后台刷新; 拉取失败时回退 FALLBACK_I18N。 */
const I18N = { zh: {}, en: {} };
let _langReady = {};

function _langParse(txt) {
  const o = {};
  (txt || '').split('\n').forEach(function (line) {
    if (!line || line.charAt(0) === '#') return;
    const i = line.indexOf('=');
    if (i > 0) o[line.slice(0, i).trim()] = line.slice(i + 1);
  });
  return o;
}

function _langApplyText(code, txt) {
  const o = _langParse(txt);
  if (Object.keys(o).length < 20) return false;   /* 内容明显不全, 不覆盖 */
  I18N[code] = o;
  return true;
}

function loadLangPack(code) {
  if (_langReady[code]) return Promise.resolve();
  try {
    const cached = localStorage.getItem('lang_pack_' + code);
    if (cached) _langApplyText(code, cached);
  } catch (e) {}
  return fetch('/api/lang?code=' + encodeURIComponent(code))
    .then(function (r) { return r.ok ? r.text() : Promise.reject(); })
    .then(function (txt) {
      if (_langApplyText(code, txt)) {
        try { localStorage.setItem('lang_pack_' + code, txt); } catch (e) {}
      }
      _langReady[code] = true;
    })
    .catch(function () { _langReady[code] = true; });
}

function t(key) {
  const p = I18N[LANG] || {};
  if (p[key] !== undefined) return p[key];
  const f = FALLBACK_I18N[LANG] || {};
  return f[key] !== undefined ? f[key] : key;
}
window.t = t;

function applyLang() {
  document.documentElement.lang = LANG === 'zh' ? 'zh-CN' : 'en';
  document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = t(el.dataset.i18n); });
  document.querySelectorAll('[data-i18n-ph]').forEach(el => { el.placeholder = t(el.dataset.i18nPh); });
  document.querySelectorAll('[data-i18n-title]').forEach(el => { el.title = t(el.dataset.i18nTitle); });
  const lc = document.getElementById('langBtns');
  if (lc) lc.innerHTML = langBtn('zh') + langBtn('en');
  // 动态文字(非 data-i18n 静态)随语言刷新: 解锁条/总线电源按钮/蜂鸣器
  if (typeof updateUnlockBar === 'function') updateUnlockBar();
  if (typeof updatePowerBtn === 'function') updatePowerBtn();
  if (typeof updateBuzzer === 'function') updateBuzzer();
  /* 事件页是动态渲染，语言切换后立即重渲染，不等 5s 定时器 */
  if (typeof loadEv === 'function') loadEv();
  if (typeof loadProdInfo === 'function') loadProdInfo();
}

function setLang(l) {
  LANG = l;
  try { localStorage.setItem('lang', l); } catch (e) {}
  loadLangPack(l).then(applyLang).catch(applyLang);
}

function langBtn(l) {
  return `<button class="${LANG === l ? 'on' : ''}" data-lang="${l}" onclick="setLang('${l}')">${l === 'zh' ? '中文' : 'EN'}</button>`;
}

/* ===== i18n.js 已并入本文件, 不再单独请求 ===== */
/* ============================================================
 * 公共工具: 鉴权 API / toast / 转义 / 导航高亮 / 语言初始化
 * 依赖: i18n.js (必须先于本文件加载)
 * ============================================================ */
const $ = id => document.getElementById(id);

/* 会话 token 来源(可靠性从高到低):
 * 1. window.__TOKEN  服务器注入(登录跳转时写进 index.html 的占位符) - 隐私/无痕模式 100% 可靠
 * 2. location.search 登录跳转 URL 的 ?token=  - 常规模式可靠
 * 3. localStorage    正常模式跨页导航兜底
 * 拿到后尽力写回 localStorage(后续页面/导航直接用) */
let ROLE  = ''; try { ROLE = localStorage.getItem('role') || ''; } catch (e) {}
let _inj = (typeof window.__TOKEN === 'string' && window.__TOKEN && window.__TOKEN !== '<!--TOKEN-->') ? window.__TOKEN : '';
let TOKEN = _inj;
if (!TOKEN) { try { TOKEN = new URLSearchParams(location.search).get('token') || ''; } catch (e) {} }
if (TOKEN) { try { localStorage.setItem('token', TOKEN); } catch (e) {} }
if (!TOKEN) { try { TOKEN = localStorage.getItem('token') || ''; } catch (e) {} }
window.ST = TOKEN;   /* 供各页内联 JS(卡片跳转等)取用: 无痕下 localStorage 不可靠, 统一走这里 */

/* 角色权限: 登录开启且非 admin -> 隐藏所有 [data-w] 写控件(仅浏览/查询)。
 * 后端 /api/command 等写接口同样强制 403(双重保障, 防绕过)。
 * 注意: 只有"明确"拿到 operator/viewer 才隐藏; ROLE 为空(网络失败/尚未取到)
 * 时不要隐藏——否则一次 /api/auth/status 失败就会让所有写按钮永久消失,
 * 解锁看起来"完全没反应"(本轮实际踩到的坑)。 */
function applyRole() {
  const viewer = (ROLE === 'operator' || ROLE === 'viewer');
  document.querySelectorAll('[data-w]').forEach(el => {
    el.classList.toggle('hidden', viewer);   /* 只隐藏, 不动 disabled(避免覆盖 ota 等页自身状态管理) */
  });
  const bar = document.getElementById('roleBar');
  if (bar) {
    if (viewer) {
      bar.textContent = ROLE === 'operator' ? t('role.operator') : t('role.viewer');
      bar.classList.add('rolegray');
    } else {
      bar.textContent = t('role.admin');
      bar.classList.remove('rolegray');
    }
    bar.style.display = 'inline-block';
  }
}

/* 全局请求队列的队尾。必须定义在 initRole() 调用之前:
 * initRole() 在脚本求值阶段就会调用 api(), 而 api() 通过 __apiChain 入队;
 * 若 let 声明放在 initRole() 调用之后, 会触发 TDZ
 * "Cannot access '__apiChain' before initialization" -> /api/auth/status
 * 永远不发出 -> ROLE 回退 localStorage(常为空) -> applyRole() 隐藏全部写控件。 */
let __apiChain = Promise.resolve();

/* 初始化角色: 以后端 /api/auth/status 为准(免登录=auth 关闭 -> admin; 否则取后端 role)。
 * 不再信 localStorage 的残留 token/role(无痕/隐私模式不可靠, 曾致 admin 被误判 viewer 隐藏写控件)。
 * 走全局 API 队列: 与首屏请求天然串行, 不再抢连接。 */
async function initRole() {
  try {
    const d = await api('/api/auth/status');
    if (d.ok && !d.auth) ROLE = 'admin';
    else if (d.ok && d.role) ROLE = d.role;
    else ROLE = localStorage.getItem('role') || '';
  } catch (e) {
    console.warn('auth/status failed, keep controls visible', e);
    ROLE = localStorage.getItem('role') || '';
  }
  try { localStorage.setItem('role', ROLE); } catch (e) {}
  applyRole();
}
if (document.readyState === 'complete' || document.readyState === 'interactive') initRole();
else document.addEventListener('DOMContentLoaded', initRole);

/* 给导航栏/页面链接统一拼 ?token= (cookie/localStorage 失效时整页导航也能通过后端鉴权) */
function patchNavLinks() {
  const tok = TOKEN;
  if (!tok) return;
  document.querySelectorAll('a[href*=".html"], a[href="/"]').forEach(a => {
    let u = a.getAttribute('href');
    if (!u || u.includes('token=') || u === '/login.html') return;
    const sep = u.includes('?') ? '&' : '?';
    a.setAttribute('href', u + sep + 'token=' + tok);
  });
}
if (document.readyState === 'complete' || document.readyState === 'interactive') patchNavLinks();
else document.addEventListener('DOMContentLoaded', patchNavLinks);

/* ============================================================
 * 全局请求队列 + 状态码检查
 * ESP32 WebServer 单线程: 服务完一个请求会在 keep-alive 连接上
 * 空等最多 5s(HTTP_MAX_DATA_WAIT)才 accept 下一个 -> 并发请求按 5s 一跳排队,
 * 队尾请求极易超时(详情页/解锁失效的根因)。全站 API 经 FIFO 队列后
 * 同一时刻恒 1 个请求在飞, 服务器永远不排队。
 * 非 2xx 一律抛错(带 .status/.data), 401 跳登录; 不再把 403/409 当成功。
 * ============================================================ */

function api(path, opts = {}) {
  const run = async () => {
    const headers = Object.assign({ 'X-Auth-Token': TOKEN }, opts.headers || {});
    let body = opts.body;
    let url = path;
    if (body && typeof body === 'object' && !(body instanceof FormData) && !(body instanceof Blob)) {
      /* 现场实测: 部分浏览器/代理会把 POST body 发成 chunked 或干脆不带 body,
       * ESP32 WebServer 只认 Content-Length 的 plain body -> arg("plain") 为空,
       * /api/command 收到 body(0) -> 400, /api/unlock 被判 false -> "解锁失败"。
       * 因此把 body 的标量字段同时镜像到 URL query, 后端 query 兜底即可工作;
       * 密码类字段不进 URL(单独放 X-Admin-Pass 头), 避免日志/历史泄露。 */
      const pw = body.password || body.pass;
      if (pw) headers['X-Admin-Pass'] = String(pw);
      /* 解锁意图三通道: query + body + X-Unlock 头(现场 body 会丢, query 也可能被代理改写) */
      if (body.unlocked !== undefined) headers['X-Unlock'] = body.unlocked ? '1' : '0';
      /* 现场实测: body 和 query 都会丢, 但自定义请求头稳定到达(X-Unlock 已验证)。
       * 每个标量字段镜像成 X-<key> 头, 后端 header 兜底。
       * 注意: header 循环必须在 query 的 try 之外! 否则 URLSearchParams 一旦异常,
       * 连 header 也不会设置(现场 q= 为空而 X-Unlock 正常, 正是这个形态)。
       * 只镜像可打印 ASCII, 避免中文 SSID 等非法 header 值导致 fetch 抛错。 */
      for (const k of Object.keys(body)) {
        const v = body[k];
        if (v === null || v === undefined || typeof v === 'object') continue;
        if (/^(password|pass|old|fresh)$/i.test(k)) continue;
        if (/^[\x20-\x7E]*$/.test(String(v))) headers['X-' + k] = String(v);
      }
      try {
        const qp = new URLSearchParams();
        for (const k of Object.keys(body)) {
          const v = body[k];
          if (v === null || v === undefined || typeof v === 'object') continue;
          if (/^(password|pass|old|fresh)$/i.test(k)) continue;
          /* 路径里已有同名 query(如 /api/unlock?unlocked=1)就不要再追加, 避免重复参数 */
          if (url.indexOf('?' + k + '=') >= 0 || url.indexOf('&' + k + '=') >= 0) continue;
          qp.set(k, String(v));
        }
        const qs = qp.toString();
        if (qs) url += (url.indexOf('?') >= 0 ? '&' : '?') + qs;
      } catch (e) {}
      /* 对已支持 query/header 兜底的接口直接"不发 body": 现场 body 会丢,
       * 服务器读 body 还会白等 5s(HTTP_MAX_POST_WAIT)并 400/锁死。无 body 时
       * WebServer 立即按 Content-Length=0 处理, 参数从 query/请求头取, 稳定得多。
       * 其余接口(creds/wifi/ota 等)仍保留 JSON body。 */
      const bodyless = /^\/api\/(command|unlock|power|buzzer|feed\/clear|feeder\/up\/start|calib)(\?|$)/.test(url);
      if (bodyless) {
        body = undefined;
      } else {
        headers['Content-Type'] = 'application/json';
        body = JSON.stringify(body);
      }
    }
    // FormData 不手动设 Content-Type: fetch 自动带 multipart boundary
    const ms = opts.timeout || 10000;                 // 超时可覆盖(上传等长操作)
    const ac = new AbortController();
    const timer = setTimeout(() => ac.abort(), ms);
    try {
      const r = await fetch(url, Object.assign({}, opts, { headers, body, signal: ac.signal }));
      if (r.status === 401) { location.href = '/login.html'; throw Object.assign(new Error('unauthorized'), { status: 401 }); }
      const d = await r.json().catch((e) => {
        console.warn('[api] bad json', path, r.status, e && e.message);
        return { ok: false, msg: 'bad json', status: r.status };
      });
      if (!r.ok) throw Object.assign(new Error(d.msg || ('HTTP ' + r.status)), { status: r.status, data: d });
      return d;
    } finally { clearTimeout(timer); }
  };
  const p = __apiChain.then(run, run);   // 入队: 前一个无论成败, 下一个等它
  __apiChain = p.catch(() => {});        // 队列本身吞错, 每个调用者拿自己的结果
  return p;
}

/* 写操作包装: 后端返回 403 locked 时自动弹解锁并重试一次(只重试一次)。
 * 页面写操作统一走这里, 避免 403 被静默/提示成别的错误。 */
async function write(path, opts = {}) {
  try {
    return await api(path, opts);
  } catch (e) {
    if (e && e.status === 403 && e.data && e.data.msg === 'locked') {
      const ok = await ensureUnlock();
      if (!ok) throw e;
      return await api(path, opts);     // 解锁成功后重试一次
    }
    throw e;
  }
}

function toast(msg, isErr) {
  const t = $('toast');
  if (!t) return;
  t.textContent = msg;
  t.className = 'toast' + (isErr ? ' err' : '');
  t.style.display = 'block';
  clearTimeout(t._h);
  t._h = setTimeout(() => { t.style.display = 'none'; }, 3000);
}

/* 错误分类提示: 按 api() 抛出的 status/data 区分提示文案, 供各页 catch 统一调用 */
function errToast(e, fallback) {
  const s = e && e.status, d = e && e.data;
  if (s === 401) { location.href = '/login.html'; return; }
  if (s === 403 && d && d.msg === 'admin required') { toast(t('term.adminErr'), true); return; }
  if (s === 403 && d && d.msg === 'locked') { toast(t('unlock.need'), true); return; }
  if (s === 409 && d && d.msg === 'calib running') { toast(t('feeder.calibBusy'), true); return; }
  if (s === 404) { toast(t('feeder.unknown'), true); return; }
  toast(fallback || (e && e.message) || t('feeder.unknown'), true);
}

function esc(s) {
  return String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

/* 手动解除生产锁：独立于普通解锁，保持到下一条 UART1 M600。 */
async function releaseProduction() {
  if (!(await ensureUnlock())) return;
  if (!(await confirmModal(t('meta.releaseProd'), t('meta.releaseProdConfirm'),
                           t('meta.releaseProd'), t('nav.cancel')))) return;
  try {
    const d = await api('/api/production/release', { method: 'POST' });
    if (d && d.ok) { toast(t('meta.prodReleased')); await loadMeta(); }
    else toast((d && d.msg) || t('feeder.unknown'), true);
  } catch (e) { errToast(e); }
}

async function logout() {
  try { await api('/api/logout', { method: 'POST' }); } catch (e) {}
  localStorage.removeItem('token');
  localStorage.removeItem('role');
  location.href = '/login.html';
}

/* ChipID 统一 6 位大写 hex 显示(与协议应答 CHIP:F724C4 一致) */
function fmtChip(c) { return c ? ('000000' + Number(c).toString(16).toUpperCase()).slice(-6) : '-'; }

/* FW 固件版本: 2610 -> "26.10"; 0/-1 -> "-" */
/* 485 日志 ChipID -> 地址注解（P<hex> 对现场可读） */
let chipAddrMap = {};
async function loadChipMap() {
  try {
    const d = await api('/api/feeders');
    if (!d) return;
    const m = {};
    (Array.isArray(d.feeders) ? d.feeders : []).forEach(f => { if (f && f.chipid) m[f.chipid] = f.addr; });
    (Array.isArray(d.temp) ? d.temp : []).forEach(f => { if (f && f.chipid) m[f.chipid] = f.addr; });
    chipAddrMap = m;
  } catch (e) {}
}
function chipAddrTag(s) {
  const str = String(s || '');
  const m = /(?:^|[^0-9A-F])P([0-9A-F]{6})(?![0-9A-F])/i.exec(str) || /CHIP:([0-9A-F]{6})/i.exec(str);
  if (!m) return '';
  const a = chipAddrMap[parseInt(m[1], 16)];
  return a !== undefined ? ' [Addr ' + a + ']' : '';
}

function fmtFw(fw) { return (fw && fw > 0) ? (Math.floor(fw / 10000) + '.' + String(fw % 10000).padStart(4, '0')) : '-'; }

/* ============================================================
 * Web 写操作安全解锁
 * 向 485 发送控制指令前必须先"解锁操作"；
 * 解锁 = 确认飞达已停止动作。解锁 5 分钟无操作自动重新锁定。
 * 状态统一放 window.__unlocked(显式属性): 曾与页面兜底 window.UNLOCKED
 * 分成两份导致解锁后按钮不恢复(jsdom 实测两份状态不同步)。
 * ============================================================ */
window.__unlocked = false;

function updateUnlockBar() {
  const el = $('unlockBar');
  if (!el) return;
  el.className = 'unlockbar ' + (window.__unlocked ? 'unlocked' : 'locked');
  const on = $('unlockOn'), off = $('unlockOff'), btn = $('unlockBtn');
  if (on) on.style.display = window.__unlocked ? '' : 'none';
  if (off) off.style.display = window.__unlocked ? 'none' : '';
  if (on) on.textContent = t('unlock.unlocked');
  if (off) off.textContent = t('unlock.locked');
  if (btn) {
    btn.textContent = window.__unlocked ? t('unlock.lockBtn') : t('unlock.unlockBtn');
    btn.className = 'btn sm ' + (window.__unlocked ? '' : 'warn');
  }
}

async function pollUnlock() {
  try {
    const d = await api('/api/unlock/status');
    window.__unlocked = !!d.unlocked;
    updateUnlockBar();
  } catch (e) {}
}

/* 通用确认弹窗 */
function confirmModal(title, msg, okText, cancelText) {
  return new Promise(res => {
    const ov = document.createElement('div');
    ov.className = 'modal-ov';
    ov.innerHTML = `<div class="modal">
      <h3>${esc(title)}</h3>
      <p>${esc(msg)}</p>
      <div class="mbtns">
        <button class="btn" id="mNo">${esc(cancelText)}</button>
        <button class="btn danger" id="mYes">${esc(okText)}</button>
      </div></div>`;
    document.body.appendChild(ov);
    const close = ok => { ov.remove(); res(ok); };
    ov.querySelector('#mYes').onclick = () => close(true);
    ov.querySelector('#mNo').onclick = () => close(false);
    ov.addEventListener('click', e => { if (e.target === ov) close(false); });
  });
}

/* 确保已解锁：未解锁则弹确认框，确认后解锁；成功后重新 applyRole(曾被 hidden 的写控件恢复显示)
 * 解锁意图同时放 query(?unlocked=1)与 JSON body: 某些 ESP32 WebServer 固件在
 * keep-alive/弱网下 arg("plain") 偶发为空, 只看 body 会把解锁判成 false(假失败)。 */
async function ensureUnlock(force) {
  /* 设备 OTA 重启后服务端解锁状态清零；不能只信页面缓存的 __unlocked。
   * 先查 /api/unlock/status，否则升级 APP 后立即升 FS 会误判已解锁 -> "locked"。 */
  if (!force) {
    try {
      const st = await api('/api/unlock/status');
      window.__unlocked = !!(st && st.unlocked);
      updateUnlockBar();
      if (window.__unlocked) return true;
    } catch (e) {
      window.__unlocked = false;
      updateUnlockBar();
    }
  }
  const ok = await confirmModal(t('unlock.title'), t('unlock.warn'), t('unlock.btn'), t('unlock.cancel'));
  if (!ok) return false;
  try {
    const r = await api('/api/unlock?unlocked=1', { method: 'POST', body: { unlocked: true } });
    if (r.ok && r.unlocked) { window.__unlocked = true; updateUnlockBar(); applyRole(); return true; }
    console.warn('unlock rejected', r);
  } catch (e) { console.warn('unlock failed', e); }
  toast(t('unlock.fail'), true);
  return false;
}

/* 手动锁定 */
async function lockNow() {
  window.__unlocked = false;
  try { await api('/api/unlock?unlocked=0', { method: 'POST', body: { unlocked: false } }); } catch (e) {}
  updateUnlockBar();
}

/* 解锁条按钮: 已解锁->锁定, 未解锁->解锁 */
async function toggleUnlock() {
  if (window.__unlocked) await lockNow();
  else await ensureUnlock();
}

function fmtSig(rssi) {
  if (rssi === undefined || rssi === null || rssi === -128) return '-';
  let bars = '▁▂▃▄';
  const idx = rssi > -55 ? 3 : rssi > -65 ? 2 : rssi > -78 ? 1 : 0;
  return bars[idx] + ' ' + rssi + 'dBm';
}

/* 页面公共初始化: 导航高亮 + 语言
   (js 可能被页面动态延迟加载, 此时 DOMContentLoaded 已过 → readyState 立即执行) */
function initPage() {
  /* 语言包拉取完成后再渲染, 避免首屏显示 key */
  loadLangPack(LANG).then(applyLang).catch(applyLang);
  const here = location.pathname.split('/').pop() || 'index.html';
  document.querySelectorAll('nav a').forEach(a => {
    if (a.getAttribute('href') === here) a.classList.add('on');
  });
}
if (document.readyState === 'interactive' || document.readyState === 'complete') initPage();
else document.addEventListener('DOMContentLoaded', initPage);

window.__APP_OK__ = true;  // 加载成功标志(弱网自愈重试用)










