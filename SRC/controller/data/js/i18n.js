/* ============================================================
 * 多语言: 自动检测浏览器语言(zh->中文, 其他->English)，可手动切换
 * 用法: 静态文本 data-i18n="key"，动态文本 t('key')
 * ============================================================ */
const I18N = {
  zh: {
    'app.title':'SMT 飞达控制器',
    'nav.overview':'总览','nav.wifi':'WiFi','nav.ota':'升级','nav.term':'终端','nav.evlog':'日志','nav.debug':'调测','nav.logout':'退出','nav.login':'登录设置',
    'meta.online':'在线','meta.fw':'固件','meta.rssi':'信号','meta.ip':'IP','meta.scan':'扫描设备','meta.scanning':'扫描中…','meta.feed':'推料','meta.power':'总线电源','meta.powerOn':'开','meta.powerOff':'关','meta.powerFault':'故障','meta.powerOffConfirm':'关闭总线电源？所有飞达将断电！','nav.cancel':'取消','meta.clearFeed':'清零计数',
    'ev.title':'事件日志','ev.desc':'关键事件记录（电源故障 / OTA / 启动，掉电保留，最近 32 条）','ev.refresh':'刷新',
    'row.a':'A 排','row.b':'B 排',
    'st.online':'在线','st.offline':'离线','st.enabled':'使能','st.temp':'临时','st.conflict':'冲突','st.boot':'BOOT 待升级',
    'login.pass':'密码','login.btn':'登录','login.err':'密码错误','login.hint':'请输入密码以访问控制界面',
    'login.auth':'需要登录验证','login.authOn':'登录验证已开启','login.authOff':'登录验证已关闭',
    'login.enter':'进入总览','login.toggleErr':'设置失败',
    'login.user':'用户名','login.keep20':'20 分钟失效','login.keepForever':'永久登录',
    'login.adminTitle':'账号与权限','login.adminHint':'管理员用户名固定 admin；操作员账号仅可浏览/查询（写操作后端强制拒绝）。',
    'login.adminPwOld':'旧管理密码','login.adminPwNew':'新管理密码（≥4 位）','login.adminPwBtn':'修改管理密码',
    'login.opUserL':'操作员用户名','login.opPassL':'操作员密码（≥4 位，留空并点"清除"即删除账号）',
    'login.opBtn':'保存操作员','login.opClear':'清除操作员',
    'login.saveOk':'已保存','login.saveErr':'保存失败','login.adminPwErr':'旧密码错误或新密码太短(≥4位)',
    'login.factoryTitle':'恢复出厂设置','login.factoryHint':'清空 WiFi / 账号密码 / 所有设置并重启，设备回到出厂默认（AP 热点 + 默认管理密码 123456）。此操作不可逆！',
    'login.factoryPw':'输入当前管理密码确认','login.factoryBtn':'恢复出厂设置',
    'login.factoryConfirm':'确定恢复出厂？所有设置（WiFi/密码/配置/事件日志）将被清空，设备将重启！',
    'login.factoryDone':'已恢复出厂，设备即将重启…','login.factoryErr':'密码错误或操作失败',

    'wifi.title':'WiFi 设置','wifi.saved':'已保存网络','wifi.sta':'当前连接','wifi.ap':'热点模式',
    'wifi.apssid':'热点名称','wifi.apip':'热点 IP','wifi.none':'未保存','wifi.disconnected':'未连接',
    'wifi.scanbtn':'扫描网络','wifi.sel':'选择网络','wifi.ssid':'WiFi 名称','wifi.pass':'WiFi 密码',
    'wifi.connect':'连接并保存','wifi.connecting':'连接中，设备将切换到新网络，请用新 IP 重新访问',
    'wifi.fail':'连接失败，已保持热点模式','wifi.refresh':'刷新状态',
    'wifi.del':'删除','wifi.delConfirm':'删除已保存的网络“%s”？下次启动将不再尝试连接它',
    'wifi.delDone':'已删除','wifi.delFail':'删除失败',
    'wifi.apmode':'AP 模式','wifi.apmodeHint':'切换后设备以热点启动(SMT-Feeder-AP)，手机直连访问；连上 WiFi 后自动回到 STA 模式','wifi.apmodeBtn':'切换到 AP 模式',

    'ota.title':'固件升级','ota.esp32':'ESP32 控制器','ota.device':'飞达设备',
    'ota.cur':'当前固件','ota.latest':'最新固件','ota.check':'检测更新','ota.upgrade':'升级固件','ota.fsCur':'当前页面','ota.fsLatest':'最新页面','ota.fsUp':'升级页面','ota.fsFound':'页面有更新',
    'ota.addr':'飞达地址','ota.devup':'升级所选飞达','ota.none':'无',
  'ota.devFound':'发现新飞达固件包，可逐个选择升级','ota.devNone':'暂无飞达固件包',
    'ota.uptodate':'已是最新版本','ota.found':'发现新版本','ota.upgrading':'升级中…',
    'ota.progress':'进度','ota.state.idle':'空闲','ota.state.downloading':'下载中',
    'ota.state.flashing':'烧写中','ota.state.done':'完成','ota.state.error':'失败',
    'ota.confirmEsp':'确认升级 ESP32 固件？期间设备会重启。','ota.confirmFs':'确认升级 Web 页面？页面文件将被替换，期间短暂重启。','ota.confirmDev':'确认升级该飞达固件？',
    'ota.needCheck':'请先点击「检测更新」',

    'feeder.title':'飞达控制','feeder.back':'返回总览','feeder.state':'状态',
    'feeder.chipid':'ChipID','feeder.ver':'配置版本','feeder.speed':'速度','feeder.pitch':'节距',
    'feeder.unknown':'未知','feeder.enable':'使能/禁用','feeder.disable':'禁用飞达','feeder.enableBtn':'启用飞达','feeder.feed':'送料(mm)','feeder.feedGo':'送料','feeder.repeatX':'重复(次)','feeder.repeating':'送料中(第 {n}/{x} 次)',
    'feeder.fw':'固件版本','feeder.cfgT':'N20超时','feeder.cfgTLabel':'T N20超时(0~5000)','feeder.cfgTHint':'T: N20 超时参数(0~5000)。T0=周期完成立即检查限位(不等待)；默认 800。不同电机转速不同，超时可单独设置',
    'feeder.unload':'退料','feeder.reset':'恢复出厂',
    'feeder.cfgM620':'M620 参数配置','feeder.cfgA':'A 全进料角度','feeder.cfgB':'B 半进料角度',
    'feeder.cfgC':'C 回退角度','feeder.cfgF':'F 进料长度(mm)','feeder.cfgU':'U 稳定时间',
    'feeder.cfgV':'V 最小脉宽','feeder.cfgW':'W 最大脉宽','feeder.cfgX':'X 忽略反馈',
    'feeder.cfgX0':'0·有反馈线','feeder.cfgX1':'1·无反馈线(忽略)',
    'feeder.cfgChip':'ChipID(可选)','feeder.cfgHint':'仅对当前飞达生效；填 ChipID 强校验(N+P)',
    'feeder.applyCfg':'应用配置','feeder.cfgDef':'默认值','feeder.cfgDefDone':'已填入出厂默认值','feeder.cfgGet':'获取当前配置','feeder.cfgGot':'已获取当前配置','feeder.adminPw':'输入管理页密码','feeder.pwErr':'密码错误',
    'feeder.up':'升级固件',
    'feeder.confirmReset':'确认恢复出厂设置？该命令为广播，将重置所有飞达并加载默认参数！',
    'feeder.cmdSent':'指令已发送','feeder.blink':'闪烁定位','feeder.blinkStop':'停止闪烁',
    'feeder.offline':'离线','feeder.busy':'送料中','feeder.noaddr':'缺少地址参数',
    'feeder.calib':'舵机校准','feeder.calibHint':'校准会反复转动舵机，请确认贴片机已停止、该飞达空闲。0°/90° 限位开关直连 ESP32，递加逼近防过冲，成功后才写回 V/W。',
    'feeder.calibKeepOpen':'校准进行中：请勿关闭或切换本页面，页面失联 20 秒后校准将自动中止（配置不会写回）。',
    'feeder.clearFeed':'清零计数',
    'feeder.calibWarn':'校准会按限位开关递加逼近 0°/90° 脉宽(防过冲扫齿)，成功后才写回 V/W。\n请确认贴片机已停止，飞达空闲且已接 0°/90° 限位开关(直连 ESP32 GPIO32/33)。',
    'feeder.calibStart':'校准已启动…','feeder.calibDone':'校准完成','feeder.calibGo':'开始校准','feeder.log485':'485 实时日志','log.clear':'清空',

    'unlock.title':'解锁操作','unlock.warn':'解锁后将允许向 485 总线发送控制指令。\n请务必先停止贴片机飞达的当前动作，否则会导致动作混乱！',
    'unlock.btn':'确认解锁','unlock.cancel':'取消','unlock.locked':'已锁定','unlock.unlocked':'已解锁',
    'unlock.lockBtn':'锁定','unlock.unlockBtn':'解锁操作','unlock.fail':'解锁失败','unlock.need':'请先解锁操作',

    'fup.target':'目标飞达','fup.addr':'485 地址','fup.chipid':'ChipID','fup.boardFw':'板上固件','fup.readBoard':'读取','fup.pkgFw':'包固件',
    'fup.source':'固件包来源','fup.srcLocal':'本地上传','fup.srcGithub':'GitHub 在线',
    'fup.pickHex':'选择从机固件 hex 文件（Intel HEX，含 DDFW: 版本标识）','fup.upload':'上传并校验','fup.localHint':'上传后仅保存在内存中，校验通过即可升级；不会写入文件系统。',
    'fup.ghCheck':'检查 GitHub 最新版','fup.ghAsset':'Release 固件','fup.ghVer':'远程版本',
    'fup.ctl':'升级','fup.force':'强制降级（包版本低于板上时仍执行）','fup.go':'开始升级','fup.back':'返回详情页','fup.warn':'升级过程中请勿断电/断开连接；烧写完成后设备自动复位，回校版本一致即成功。','fup.log':'升级日志',
    'fup.state.idle':'就绪','fup.state.downloading':'下载中','fup.state.parsing':'解析包','fup.state.comparing':'版本比对','fup.state.isp':'ISP 烧写','fup.state.verify':'回校','fup.state.done':'完成','fup.state.error':'失败',

    'log.title':'实时日志','log.hint':'实时显示 UART2(485 总线) 的发送与接收消息',
    'log.tx':'TX→','log.rx':'RX←','log.dbg':'LOG','log.clear':'清空',
    'term.title':'终端','term.cmdTitle':'发送 M-Code 命令','term.send':'发送',
    'term.hint':'示例(点按填充):','term.warn':'危险命令（M799 恢复出厂 / M788 ISP 复位 / M903 电源 / M905 清零 / 广播写命令）需输入管理密码。',
    'term.pwTitle':'需要管理密码','term.pwHint':'此命令会同时影响多台飞达或恢复出厂，请输入管理密码确认。',
    'term.confirm':'确认发送',
    'term.sent':'已发送','term.adminErr':'密码错误或权限不足','term.bad':'命令格式无法解析',
    'term.dbgOn':'调试日志:开','term.dbgOff':'调试日志:关'
  },
  en: {
    'app.title':'SMT Feeder Controller',
    'nav.overview':'Overview','nav.wifi':'WiFi','nav.ota':'Firmware','nav.term':'Terminal','nav.evlog':'Events','nav.debug':'Debug','nav.logout':'Logout','nav.login':'Login',
    'meta.online':'Online','meta.fw':'FW','meta.rssi':'RSSI','meta.ip':'IP','meta.scan':'Scan','meta.scanning':'Scanning…','meta.feed':'Feeds','meta.power':'Bus power','meta.powerOn':'ON','meta.powerOff':'OFF','meta.powerFault':'FAULT','meta.powerOffConfirm':'Turn OFF bus power? ALL feeders will lose power!','nav.cancel':'Cancel','meta.clearFeed':'Clear counter',
    'ev.title':'Event Log','ev.desc':'Key events (power fault / OTA / boot, kept across power-off, latest 16)','ev.refresh':'Refresh',
    'row.a':'Row A','row.b':'Row B',
    'st.online':'Online','st.offline':'Offline','st.enabled':'Enabled','st.temp':'Temp','st.conflict':'Conflict','st.boot':'BOOT',
    'login.pass':'Password','login.btn':'Sign in','login.err':'Wrong password','login.hint':'Enter password to access the control panel',
    'login.auth':'Require login','login.authOn':'Login verification is on','login.authOff':'Login verification is off',
    'login.enter':'Enter','login.toggleErr':'Setting failed',
    'login.user':'Username','login.keep20':'20 min session','login.keepForever':'Stay signed in',
    'login.adminTitle':'Accounts & Permissions','login.adminHint':'Admin username is fixed to admin; operator accounts are read-only (writes are rejected by backend).',
    'login.adminPwOld':'Old admin password','login.adminPwNew':'New admin password (>=4 chars)','login.adminPwBtn':'Change admin password',
    'login.opUserL':'Operator username','login.opPassL':'Operator password (>=4 chars; leave blank + click Clear to remove account)',
    'login.opBtn':'Save operator','login.opClear':'Clear operator',
    'login.saveOk':'Saved','login.saveErr':'Save failed','login.adminPwErr':'Old password wrong or new password too short (>=4)',
    'login.factoryTitle':'Factory Reset','login.factoryHint':'Clears WiFi / credentials / all settings and restarts. Device returns to factory defaults (AP hotspot + default admin password 123456). Irreversible!',
    'login.factoryPw':'Enter current admin password to confirm','login.factoryBtn':'Factory Reset',
    'login.factoryConfirm':'Reset to factory? ALL settings (WiFi/password/config/event log) will be cleared and the device will restart!',
    'login.factoryDone':'Factory reset done, device restarting…','login.factoryErr':'Wrong password or operation failed',

    'wifi.title':'WiFi Settings','wifi.saved':'Saved network','wifi.sta':'Current','wifi.ap':'AP mode',
    'wifi.apssid':'AP SSID','wifi.apip':'AP IP','wifi.none':'None','wifi.disconnected':'Disconnected',
    'wifi.scanbtn':'Scan networks','wifi.sel':'Select network','wifi.ssid':'WiFi name','wifi.pass':'WiFi password',
    'wifi.connect':'Connect & save','wifi.connecting':'Connecting... device will switch to the new network, revisit with the new IP',
    'wifi.fail':'Connection failed, AP mode kept','wifi.refresh':'Refresh status',
    'wifi.del':'Delete','wifi.delConfirm':'Delete saved network "%s"? It will not be tried on next boot',
    'wifi.delDone':'Deleted','wifi.delFail':'Delete failed',
    'wifi.apmode':'AP Mode','wifi.apmodeHint':'Switching boots the device as AP (SMT-Feeder-AP) for direct phone access; returns to STA mode once WiFi is connected','wifi.apmodeBtn':'Switch to AP Mode',

    'ota.title':'Firmware Upgrade','ota.esp32':'ESP32 Controller','ota.device':'Feeder Device',
    'ota.cur':'Firmware','ota.latest':'Latest FW','ota.check':'Check','ota.upgrade':'Upgrade FW','ota.fsCur':'Pages','ota.fsLatest':'Latest Pages','ota.fsUp':'Upgrade Pages','ota.fsFound':'Pages update available',
    'ota.addr':'Feeder address','ota.devup':'Upgrade selected feeder','ota.none':'None',
    'ota.devFound':'New feeder firmware package found, upgrade one by one','ota.devNone':'No feeder firmware package',
    'ota.uptodate':'Up to date','ota.found':'New version found','ota.upgrading':'Upgrading…',
    'ota.progress':'Progress','ota.state.idle':'Idle','ota.state.downloading':'Downloading',
    'ota.state.flashing':'Flashing','ota.state.done':'Done','ota.state.error':'Failed',
    'ota.confirmEsp':'Upgrade ESP32 firmware? Device will reboot.','ota.confirmFs':'Upgrade Web pages? Page files will be replaced and device restarts briefly.','ota.confirmDev':'Upgrade this feeder firmware?',
    'ota.needCheck':'Click "Check" first',

    'feeder.title':'Feeder Control','feeder.back':'Back to overview','feeder.state':'State',
    'feeder.chipid':'ChipID','feeder.ver':'Config ver','feeder.speed':'Speed','feeder.pitch':'Pitch',
    'feeder.unknown':'Unknown','feeder.enable':'Enable/Disable','feeder.disable':'Disable feeder','feeder.enableBtn':'Enable feeder','feeder.feed':'Feed (mm)','feeder.feedGo':'Feed','feeder.repeatX':'Repeat','feeder.repeating':'Feeding ({n}/{x})',
    'feeder.fw':'Firmware','feeder.cfgT':'N20 Timeout','feeder.cfgTLabel':'T N20 Timeout (0~5000)','feeder.cfgTHint':'T: N20 timeout (0~5000). T0 = check limit immediately after cycle (no wait); default 800. Timeout can differ per motor speed',
    'feeder.unload':'Unload','feeder.reset':'Factory Reset',
    'feeder.cfgM620':'M620 Parameters','feeder.cfgA':'A full angle','feeder.cfgB':'B half angle',
    'feeder.cfgC':'C retract angle','feeder.cfgF':'F feed length(mm)','feeder.cfgU':'U settle time',
    'feeder.cfgV':'V min pulse','feeder.cfgW':'W max pulse','feeder.cfgX':'X ignore feedback',
    'feeder.cfgX0':'0·feedback line','feeder.cfgX1':'1·no feedback (ignore)',
    'feeder.cfgChip':'ChipID (optional)','feeder.cfgHint':'N255 = broadcast all; ChipID matches a specific device',
    'feeder.applyCfg':'Apply','feeder.cfgDef':'Defaults','feeder.cfgDefDone':'Factory defaults filled','feeder.cfgGet':'Load current','feeder.cfgGot':'Current config loaded','feeder.adminPw':'Enter admin password','feeder.pwErr':'Wrong password',
    'feeder.up':'Upgrade firmware',
    'feeder.confirmReset':'Reset ALL feeders to factory defaults (broadcast)?',
    'feeder.cmdSent':'Command sent','feeder.blink':'Blink locate','feeder.blinkStop':'Stop blink',
    'feeder.offline':'Offline','feeder.busy':'Feeding','feeder.noaddr':'Missing address parameter',
    'feeder.calib':'Servo Calibrate','feeder.calibHint':'Calibration moves the servo repeatedly. Confirm the machine is stopped and this feeder is idle. 0°/90° limit switches connect directly to ESP32; incremental approach prevents over-shoot; V/W are written back only on success.',
    'feeder.calibKeepOpen':'Calibration in progress: do NOT close or leave this page. If the page loses contact for 20s, calibration aborts automatically (settings are NOT written back).',
    'feeder.clearFeed':'Clear counter',
    'feeder.calibWarn':'Calibration will approach 0°/90° pulse widths incrementally by limit switch (no over-shoot). V/W are written back only on success.\nConfirm the machine is stopped, the feeder is idle and 0°/90° limit switches are connected (ESP32 GPIO32/33).',
    'feeder.calibStart':'Calibration started…','feeder.calibDone':'Calibration done','feeder.calibGo':'Start Calibration','feeder.log485':'485 Live Log','log.clear':'Clear',

    'unlock.title':'Unlock','unlock.warn':'Unlocking allows sending control commands to the 485 bus.\nMake sure the feeder has STOPPED all motion, otherwise actions may become chaotic!',
    'unlock.btn':'Unlock','unlock.cancel':'Cancel','unlock.locked':'Locked','unlock.unlocked':'Unlocked',
    'unlock.lockBtn':'Lock','unlock.unlockBtn':'Unlock','unlock.fail':'Unlock failed','unlock.need':'Unlock required',

    'fup.target':'Target Feeder','fup.addr':'485 Addr','fup.chipid':'ChipID','fup.boardFw':'Board FW','fup.readBoard':'Read','fup.pkgFw':'Package FW',
    'fup.source':'Package source','fup.srcLocal':'Local file','fup.srcGithub':'GitHub online',
    'fup.pickHex':'Pick slave firmware hex (Intel HEX, must contain DDFW: version tag)','fup.upload':'Upload & verify','fup.localHint':'Uploaded package is kept in RAM only; verified then ready to flash. Nothing is written to the file system.',
    'fup.ghCheck':'Check GitHub latest','fup.ghAsset':'Release asset','fup.ghVer':'Remote version',
    'fup.ctl':'Upgrade','fup.force':'Force downgrade (run even if package is older than board)','fup.go':'Start upgrade','fup.back':'Back to feeder','fup.warn':'Do NOT power off or disconnect during upgrade; the device resets after flashing and verifies the version matches.','fup.log':'Upgrade log',
    'fup.state.idle':'Idle','fup.state.downloading':'Downloading','fup.state.parsing':'Parsing','fup.state.comparing':'Comparing','fup.state.isp':'ISP flashing','fup.state.verify':'Verify','fup.state.done':'Done','fup.state.error':'Failed',

    'log.title':'Live Log','log.hint':'Real-time TX/RX messages on UART2 (485 bus)',
    'log.tx':'TX→','log.rx':'RX←','log.dbg':'LOG','log.clear':'Clear',
    'term.title':'Terminal','term.cmdTitle':'Send M-Code Command','term.send':'Send',
    'term.hint':'Examples (click to fill):','term.warn':'Dangerous commands (M799 factory reset / M788 ISP reset / M903 power / M905 clear / broadcast writes) require the admin password.',
    'term.pwTitle':'Admin password required','term.pwHint':'This command affects multiple feeders or resets factory settings. Enter the admin password to confirm.',
    'term.confirm':'Confirm send',
    'term.sent':'Sent','term.adminErr':'Wrong password or insufficient permission','term.bad':'Unparsable command format',
    'term.dbgOn':'Debug Log:ON','term.dbgOff':'Debug Log:OFF'
  }
};

let LANG = (localStorage.getItem('lang')) ||
           ((navigator.language || '').toLowerCase().startsWith('zh') ? 'zh' : 'en');

function t(key) { return (I18N[LANG] && I18N[LANG][key]) || key; }

function applyLang() {
  document.documentElement.lang = LANG === 'zh' ? 'zh-CN' : 'en';
  document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = t(el.dataset.i18n); });
  document.querySelectorAll('[data-i18n-ph]').forEach(el => { el.placeholder = t(el.dataset.i18nPh); });
  document.querySelectorAll('[data-i18n-title]').forEach(el => { el.title = t(el.dataset.i18nTitle); });
  const lc = document.getElementById('langBtns');
  if (lc) lc.innerHTML = langBtn('zh') + langBtn('en');
}

function setLang(l) { LANG = l; localStorage.setItem('lang', l); applyLang(); }

function langBtn(l) {
  return `<button class="${LANG === l ? 'on' : ''}" data-lang="${l}" onclick="setLang('${l}')">${l === 'zh' ? '中文' : 'EN'}</button>`;
}




