# -*- coding: utf-8 -*-
"""串口屏控件/脚本生成器 v4
   变更: 删 Page6 并入参数页; 参数拆 A(1)/B(15); 网格筛选改 全部/在线/禁用;
         问题网格只收 未配置/冲突; 单台各页加定位; X 双态; 校准确认对话框
"""
import os, io, sys, csv, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

OUT = r"D:\3d打印\AS2 飞达\smt_feeder_controller\docs"
LANGDIR = r"D:\3d打印\AS2 飞达\smt_feeder_controller\data\lang"
W = []

def add(p, n, t, x, y, w, h, f, tx, ev="", note=""):
    W.append(dict(page=p, name=n, typ=t, x=x, y=y, w=w, h=h, font=f, text=tx, ev=ev, note=note))

def cmd(expr):
    return 't_cmd.txt=%s\nprints t_cmd.txt,0\nprinth 0a' % expr

def raw(s):
    return 'prints "%s",0\nprinth 0a' % s

def go(pg):
    return 'prints "#PAGE %d",0\nprinth 0a\npage %d' % (pg, pg)

def tabs(pg, active):
    for i, (suf, lab, tgt) in enumerate([("Run","运行",0),("Par","参数",1),("Log","日志",2),("Set","设置",3),("Ota","升级",5)]):
        add(pg, "bTab%d%s" % (pg, suf), "按钮", i*64, 0, 64, 30, 11, lab, go(tgt),
            "激活" if i == active else "")

def back(pg, name, tgt):
    add(pg, name, "按钮", 250, 4, 64, 24, 12, "返回", "page %d" % tgt)

def wake(pg):
    add(pg, "hsWake", "触摸热区", 0, 30, 320, 210, 10, "",
        'dim=va6.val\nhsWake.vis=0\nprints "#WAKE",0\nprinth 0a', "vis=0")

# ===== P0 状态 =====
tabs(0, 0)
add(0, "t_online", "文本", 6, 34, 150, 18, 15, "在线 32/64", "", "动态")
add(0, "t_prob", "文本", 156, 34, 84, 18, 15, "问题 0", "", "动态")
add(0, "t_state", "文本", 244, 34, 70, 18, 13, "空闲", "", "动态")
add(0, "hs_online", "触摸热区", 4, 30, 150, 26, 10, "", go(8), "点在线数")
add(0, "hs_prob", "触摸热区", 154, 30, 90, 26, 10, "", go(13), "点问题数")
add(0, "t1", "文本", 6, 56, 308, 14, 10, "STA 已连", "", "动态")
add(0, "t_volt", "文本", 6, 78, 308, 16, 11, "总线电源：开", "", "动态")
add(0, "t_feed", "文本", 6, 98, 308, 16, 11, "总推料：0   开机后：0", "", "动态")
add(0, "b_single", "按钮", 6, 122, 308, 42, 15, "进入单台操作", go(8), "导航")
add(0, "t2", "文本", 6, 172, 308, 14, 10, "> M600 N5 F4", "", "动态")
add(0, "t5", "文本", 6, 190, 308, 12, 10, "v0", "", "动态")
add(0, "t_stat2", "文本", 6, 206, 308, 12, 10, "故障 0 次   运行 00:00", "", "动态")
add(0, "t_err", "文本", 6, 222, 308, 12, 10, "", "", "动态: 临时错误提示, 红字")
wake(0)

# ===== P1 参数 A =====
tabs(1, 1)
add(1, "t_status", "文本", 6, 34, 308, 16, 10, "就绪 · 剩余 1766/3000", "", "动态: 状态+推料数")
add(1, "l_addr1", "文本", 6, 60, 36, 14, 10, "地址", "")
add(1, "t_addr1", "文本", 46, 54, 74, 26, 14, "5", "", "键盘输入 0-63")
add(1, "l_v", "文本", 164, 60, 34, 14, 10, "V", "")
add(1, "t_v", "文本", 202, 54, 74, 26, 14, "550", "", "键盘输入")
add(1, "l_step", "文本", 6, 92, 36, 14, 10, "F", "")
add(1, "t_f", "文本", 46, 86, 74, 26, 14, "4", "", "键盘输入")
add(1, "l_w", "文本", 164, 92, 34, 14, 10, "W", "")
add(1, "t_w", "文本", 202, 86, 74, 26, 14, "2400", "", "键盘输入")
add(1, "l_n20", "文本", 6, 124, 36, 14, 10, "T", "")
add(1, "t_t", "文本", 46, 118, 74, 26, 14, "1000", "", "键盘输入")
add(1, "l_x", "文本", 164, 124, 34, 14, 10, "X", "")
add(1, "b_x", "按钮", 202, 118, 74, 26, 12, "检测",
    'prints "#XTGL",0\nprinth 0a', "文字由 ESP32 推")
add(1, "b_read", "按钮", 6, 158, 74, 30, 12, "读取", cmd('"M602 N"+t_addr1.txt'))
add(1, "b_save", "按钮", 84, 158, 74, 30, 12, "保存",
    cmd('"M620 N"+t_addr1.txt+" F"+t_f.txt+" T"+t_t.txt+" V"+t_v.txt+" W"+t_w.txt'))
add(1, "b_reset", "按钮", 162, 158, 74, 30, 12, "复位",
    cmd('"M620 N"+t_addr1.txt+" F4 T1000 V550 W2400 X0"'))
add(1, "b_locate", "按钮", 240, 158, 74, 30, 12, "定位", cmd('"#BLINK "+t_addr1.txt'))
add(1, "b_more", "按钮", 6, 194, 150, 28, 12, "更多参数 A/B/C/U", "page 15")
add(1, "t_cmd1", "文本", 250, 226, 64, 12, 10, "", "", "隐藏 vis=0")
wake(1)

# ===== P2 日志 =====
tabs(2, 2)
add(2, "t6", "文本", 6, 36, 120, 16, 11, "日志: 关", "", "动态")
add(2, "b_clr", "按钮", 250, 34, 64, 22, 11, "清屏", 't3.txt=""')
add(2, "t3", "滚动文本", 6, 60, 308, 96, 10, "", "", "ESP32 按行推")
add(2, "b_logon", "按钮", 6, 164, 96, 32, 12, "日志 开", raw("#LOG 1"))
add(2, "b_logoff", "按钮", 112, 164, 96, 32, 12, "日志 关", raw("#LOG 0"))
add(2, "b_back", "按钮", 218, 164, 96, 32, 12, "返回", "page 0")
add(2, "l_loghint", "文本", 6, 208, 308, 12, 10, "日志开关等价 M906 S1/S0", "")
add(2, "msk2", "文本", 6, 80, 308, 90, 10, "", "", "bco 深色 vis=0")
add(2, "t_msk3", "文本", 20, 92, 280, 16, 11, "开启调试日志？", "", "vis=0")
add(2, "t_msk4", "文本", 20, 114, 280, 14, 10, "会持续占用串口", "", "vis=0")
add(2, "b_msk_yes", "按钮", 24, 128, 120, 32, 12, "开启",
    "msk2.vis=0\nt_msk3.vis=0\nt_msk4.vis=0\nb_msk_yes.vis=0\nb_msk_no.vis=0")
add(2, "b_msk_no", "按钮", 176, 128, 120, 32, 12, "取消",
    "msk2.vis=0\nt_msk3.vis=0\nt_msk4.vis=0\nb_msk_yes.vis=0\nb_msk_no.vis=0")
wake(2)

# ===== P3 设置 =====
tabs(3, 3)
add(3, "l_fw", "文本", 8, 38, 44, 16, 10, "固件", "")
add(3, "t_fw", "文本", 56, 38, 200, 16, 11, "v0", "", "动态")
add(3, "l_ip", "文本", 8, 60, 44, 16, 10, "IP", "")
add(3, "t_ip", "文本", 56, 60, 200, 16, 11, "-", "", "动态")
add(3, "l_online", "文本", 8, 82, 44, 16, 10, "在线", "")
add(3, "t_online2", "文本", 56, 82, 130, 16, 11, "0/64", "", "动态")
add(3, "b_issue", "按钮", 200, 80, 114, 22, 11, "问题 --", go(13), "进问题网格")
add(3, "l_dim", "文本", 8, 110, 40, 16, 10, "亮度", "")
add(3, "h_dim", "滑块", 52, 106, 190, 20, 10, "",
    'dim=h_dim.val\ncovx h_dim.val,t_cmd3.txt,0,0\nt_cmd3.txt="#DIM "+t_cmd3.txt\nprints t_cmd3.txt,0\nprinth 0a',
    "弹起事件")
add(3, "t_dim", "文本", 248, 110, 60, 16, 10, "80%", "", "动态")
add(3, "l_slp", "文本", 8, 138, 40, 16, 10, "息屏", "")
add(3, "h_slp", "滑块", 52, 134, 190, 20, 10, "",
    'covx h_slp.val,t_cmd3.txt,0,0\nt_cmd3.txt="#SLEEP "+t_cmd3.txt\nprints t_cmd3.txt,0\nprinth 0a',
    "弹起事件")
add(3, "t_slp", "文本", 248, 138, 60, 16, 10, "5 分", "", "动态")
add(3, "b_pwr", "按钮", 6, 160, 150, 28, 12, "电源",
    "msk3.vis=1\nt_msk5.vis=1\nt_msk6.vis=1\nb_myes.vis=1\nb_mno.vis=1", "先弹确认")
add(3, "b_scan", "按钮", 164, 160, 72, 28, 12, "扫描", raw("#SCAN"))
add(3, "b_cal", "按钮", 242, 160, 72, 28, 12, "校准", "page 7")
add(3, "b_factory", "按钮", 6, 192, 308, 28, 12, "恢复出厂", "page 4")
add(3, "l_fachint", "文本", 6, 222, 308, 12, 10, "电源开关会断开所有飞达", "")
add(3, "t_cmd3", "文本", 250, 226, 64, 12, 10, "", "", "隐藏 vis=0")
add(3, "msk3", "文本", 6, 70, 308, 96, 10, "", "", "bco 深色 vis=0")
add(3, "t_msk5", "文本", 20, 84, 280, 16, 11, "切换总线电源？", "", "vis=0")
add(3, "t_msk6", "文本", 20, 106, 280, 14, 10, "所有飞达会断电或上电", "", "vis=0")
add(3, "b_myes", "按钮", 24, 128, 120, 28, 12, "确认切换",
    'prints "#PWRT",0\nprinth 0a\nmsk3.vis=0\nt_msk5.vis=0\nt_msk6.vis=0\nb_myes.vis=0\nb_mno.vis=0', "vis=0")
add(3, "b_mno", "按钮", 176, 128, 120, 28, 12, "取消",
    "msk3.vis=0\nt_msk5.vis=0\nt_msk6.vis=0\nb_myes.vis=0\nb_mno.vis=0", "vis=0")
wake(3)

# ===== P4 密码 =====
add(4, "t_title4", "文本", 8, 8, 150, 20, 15, "恢复出厂", "")
add(4, "b_home4", "按钮", 194, 4, 48, 24, 11, "首页", "page 0")

back(4, "b_back4", 3)
add(4, "t7", "文本", 46, 56, 200, 32, 18, "", "", "键盘输入")
add(4, "b_clear4", "按钮", 254, 56, 60, 32, 13, "清空", 't7.txt=""')
add(4, "b_pwd", "按钮", 8, 108, 150, 44, 15, "确认密码", cmd('"#PWD "+t7.txt'))
add(4, "b_do", "按钮", 170, 108, 144, 44, 15, "执行复位", cmd('"M799"'))
add(4, "t_cmd4", "文本", 250, 226, 64, 12, 10, "", "", "隐藏 vis=0")

# ===== P5 升级 =====
tabs(5, 4)
add(5, "t_hdr1", "文本", 4, 32, 50, 14, 10, "组件", "")
add(5, "t_hdr2", "文本", 58, 32, 68, 14, 10, "当前", "")
add(5, "t_hdr3", "文本", 130, 32, 112, 14, 10, "最新", "")
for i, (lb, tx, key) in enumerate([("l_esp","ESP32","esp"),("l_fs","页面","fs"),("l_ddf","飞达","ddf"),
                                   ("l_tft","屏幕","tft"),("l_font","字库","font")]):
    y = 50 + i*32
    add(5, lb, "文本", 4, y+6, 50, 16, 10, tx, "")
    add(5, "t_%s_cur" % key, "文本", 58, y+6, 68, 16, 10, "-", "", "动态")
    add(5, "t_%s_new" % key, "文本", 130, y+6, 112, 16, 10, "-", "", "动态")
    add(5, "b_up_%s" % key, "按钮", 246, y, 68, 26, 11, "升级",
        'prints "#OTA %s",0\nprinth 0a' % key.upper())
add(5, "l_otahint", "文本", 4, 212, 312, 12, 10, "飞达升级需先确保总线空闲", "")
wake(5)

# ===== P7 校准 =====
add(7, "t_title7", "文本", 6, 8, 150, 16, 13, "伺服校准", "")
add(7, "b_home7", "按钮", 196, 4, 48, 24, 11, "首页", "page 0")

back(7, "b_back7", 1)
add(7, "l_caddr", "文本", 6, 38, 40, 16, 10, "地址", "")
add(7, "t_caddr", "文本", 46, 32, 56, 24, 14, "5", "", "键盘输入")
add(7, "b_calq", "按钮", 106, 32, 52, 24, 11, "查询", cmd('"#CALPROBE "+t_caddr.txt'))
add(7, "t_calchip", "文本", 164, 38, 150, 16, 11, "------", "", "动态")
add(7, "j0", "进度条", 6, 62, 308, 16, 10, "", "", "0-100")
add(7, "b_cal0", "按钮", 6, 88, 96, 36, 13, "0° 校准", cmd('"#CALIB "+t_caddr.txt+" 0"'))
add(7, "b_cal90", "按钮", 108, 88, 96, 36, 13, "90° 校准", cmd('"#CALIB "+t_caddr.txt+" 1"'))
add(7, "b_calmov", "按钮", 210, 88, 104, 36, 12, "定位", cmd('"#BLINK "+t_caddr.txt'))
add(7, "b_calall", "按钮", 6, 130, 150, 36, 13, "全部校准", cmd('"#CALIB "+t_caddr.txt+" 2"'))
add(7, "b_calstop", "按钮", 164, 130, 150, 36, 13, "停止", raw("#CALIB STOP"))
add(7, "l_calhint", "文本", 6, 176, 308, 12, 10, "确认不在生产过程中再校准", "")
add(7, "t_cmd7", "文本", 250, 226, 64, 12, 10, "", "", "隐藏 vis=0")
# 校准结果确认框
add(7, "msk7", "文本", 6, 70, 308, 110, 10, "", "", "bco 深色 vis=0")
add(7, "t_dlg1", "文本", 20, 82, 280, 18, 13, "校准完成", "", "vis=0")
add(7, "t_dlg2", "文本", 20, 106, 280, 16, 11, "V=550  W=2400  正常", "", "vis=0")
add(7, "t_dlg3", "文本", 20, 126, 280, 14, 10, "点击应用写入参数", "", "vis=0")
add(7, "b_apply", "按钮", 30, 146, 120, 26, 12, "应用",
    'prints "#CALIB APPLY",0\nprinth 0a\nmsk7.vis=0\nt_dlg1.vis=0\nt_dlg2.vis=0\nt_dlg3.vis=0\nb_apply.vis=0\nb_discard.vis=0', "vis=0")
add(7, "b_discard", "按钮", 170, 146, 120, 26, 12, "放弃",
    'prints "#CALIB DISCARD",0\nprinth 0a\nmsk7.vis=0\nt_dlg1.vis=0\nt_dlg2.vis=0\nt_dlg3.vis=0\nb_apply.vis=0\nb_discard.vis=0', "vis=0")
wake(7)

# ===== P8 单台操作 =====
add(8, "t_title8", "文本", 6, 8, 140, 16, 13, "单台操作", "")
add(8, "b_select", "按钮", 6, 4, 76, 24, 11, "选择飞达", "page 9")
add(8, "b_home8", "按钮", 96, 4, 48, 24, 11, "首页", "page 0")
back(8, "b_back8", 0)
add(8, "l_uaddr", "文本", 96, 38, 36, 16, 11, "地址", "")
add(8, "t_uaddr", "文本", 136, 32, 60, 24, 15, "5", "", "键盘输入")
add(8, "t_ustat", "文本", 200, 38, 118, 16, 10, "就绪 剩1766/3000", "", "动态")
add(8, "b_feed", "按钮", 6, 66, 150, 44, 16, "进料", cmd('"M600 N"+t_uaddr.txt+" F4"'))
add(8, "b_en", "按钮", 164, 66, 150, 44, 15, "已就绪",
    'prints "#ENTGL",0\nprinth 0a', "双态, 文字由 ESP32 推")
add(8, "b_wind", "按钮", 6, 116, 150, 40, 16, "卷膜",
    '按下:\n' + cmd('"M604 N"+t_uaddr.txt+" S1"') + '\n弹起:\n' + cmd('"M604 N"+t_uaddr.txt+" S0"'), "需 M604")
add(8, "b_reel", "按钮", 164, 116, 150, 40, 15, "换料", "page 16")
add(8, "b_locate", "按钮", 6, 164, 150, 30, 12, "定位", cmd('"#BLINK "+t_uaddr.txt'))
add(8, "b_para", "按钮", 164, 164, 150, 30, 12, "参数", go(1))
add(8, "l_uhint", "文本", 6, 202, 308, 12, 10, "点「已就绪」可切换使能", "")
add(8, "t_err8", "文本", 6, 220, 308, 12, 10, "", "", "动态: 临时错误提示")
add(8, "t_cmd8", "文本", 250, 226, 64, 12, 10, "", "", "隐藏 vis=0")
wake(8)

# ===== P9-12 选择飞达（全部/在线/禁用）=====
for g in range(4):
    pg = 9 + g
    add(pg, "t_gtitle", "文本", 6, 8, 120, 16, 13, "选择飞达 %d/4" % (g+1), "")
    back(pg, "b_gback", 8)
    for fi, (lab, key) in enumerate([("全部","0"),("在线","1"),("禁用","2")]):
        add(pg, "b_f%s" % key, "按钮", 4 + fi*104, 30, 100, 22, 11, lab, raw("#GRIDF " + key),
            "筛选")
    for i in range(16):
        a = g*16 + i
        add(pg, "b_g%02d" % a, "按钮", 4 + (i % 4)*78, 56 + (i//4)*40, 76, 36, 13, "%02d" % a,
            'prints "#SEL %d",0\nprinth 0a\npage 1' % a, "离线格 tsw=0")
    add(pg, "t_ghint", "文本", 4, 222, 180, 12, 10, "灰=离线（不可点）", "")
    if g > 0: add(pg, "b_gprev", "按钮", 160, 220, 74, 18, 10, "< 上一组", "page %d" % (pg-1))
    if g < 3: add(pg, "b_gnext", "按钮", 240, 220, 74, 18, 10, "下一组 >", "page %d" % (pg+1))
    wake(pg)

# ===== P13-14 问题飞达（未配置/冲突）=====
for h in range(2):
    pg = 13 + h
    add(pg, "t_ptitle", "文本", 6, 8, 160, 16, 13, "问题飞达 %d/2" % (h+1), "")
    back(pg, "b_pback", 1)
    for fi, (lab, key) in enumerate([("全部","0"),("未配置","1"),("冲突","2")]):
        add(pg, "b_pf%s" % key, "按钮", 4 + fi*104, 30, 100, 22, 11, lab, raw("#PGRIDF " + key))
    for i in range(16):
        add(pg, "b_p%02d" % i, "按钮", 4 + (i % 4)*78, 56 + (i//4)*40, 76, 36, 13, "--",
            'prints "#PSEL %d",0\nprinth 0a\npage 1' % (h*16 + i), "文字/颜色由 ESP32 推")
    add(pg, "t_phint", "文本", 4, 222, 200, 12, 10, "紫=未配置 红=冲突", "")
    wake(pg)

# ===== P15 参数 B =====
add(15, "t_title15", "文本", 6, 8, 120, 16, 13, "参数 B", "")
add(15, "b_home15", "按钮", 196, 4, 48, 24, 11, "首页", "page 0")

back(15, "b_back15", 1)
P15 = [("l_a", "A 全进角", "t_a", "90"), ("l_u", "U 稳定", "t_u", "80"),
       ("l_b", "B 半进角", "t_b", "44"), ("l_c", "C 回退角", "t_c", "30")]
for i, (lb, txt, ctl, val) in enumerate(P15):
    col, row = i % 2, i // 2
    y = 44 + row * 40
    lx = 6 if col == 0 else 158
    ix = 84 if col == 0 else 224
    add(15, lb, "文本", lx, y + 8, 76 if col == 0 else 60, 16, 10, txt, "")
    add(15, ctl, "文本", ix, y, 66, 28, 14, val, "", "键盘输入")
for i, (lab, ev) in enumerate([("保存", cmd('"M620 N"+t_addr15.txt+" A"+t_a.txt+" B"+t_b.txt+" C"+t_c.txt+" U"+t_u.txt')),
                               ("定位", cmd('"#BLINK "+t_addr15.txt')),
                               ("校准", "page 7")]):
    add(15, "b_b%d" % i, "按钮", 6 + i * 106, 132, 96, 32, 12, lab, ev)
add(15, "l_hint15", "文本", 6, 176, 308, 12, 10, "盘容量/预警数在「换料」页设置", "")
add(15, "t_addr15", "文本", 250, 226, 64, 12, 10, "5", "", "隐藏 vis=0")
add(15, "t_cmd15", "文本", 250, 200, 64, 12, 10, "", "", "隐藏 vis=0")
wake(15)

# ===== P16 换料 =====
add(16, "t_title16", "文本", 6, 8, 120, 16, 13, "换料", "")
add(16, "b_home16", "按钮", 196, 4, 48, 24, 11, "首页", "page 0")

back(16, "b_back16", 8)
add(16, "l_r_addr", "文本", 6, 38, 40, 16, 11, "地址", "")
add(16, "t_r_addr", "文本", 48, 32, 56, 24, 14, "5", "", "只读, 由 P8 带入")
add(16, "t_r_stat", "文本", 114, 38, 200, 16, 10, "就绪 · 本盘已推 1234", "", "动态")
add(16, "l_reel", "文本", 6, 80, 80, 16, 11, "盘容量", "")
add(16, "t_reel", "文本", 92, 74, 100, 28, 15, "3000", "", "键盘输入 0=不预警")
add(16, "l_warn", "文本", 6, 122, 80, 16, 11, "预警数", "")
add(16, "t_warn", "文本", 92, 116, 100, 28, 15, "300", "", "0=按盘容量10%")
add(16, "l_used", "文本", 6, 164, 80, 16, 11, "本盘已推", "")
add(16, "t_r_used", "文本", 92, 158, 100, 28, 14, "1234", "", "只读")
add(16, "b_reelok", "按钮", 204, 74, 110, 34, 13, "确认换料",
    "msk16.vis=1\nt_m16a.vis=1\nt_m16b.vis=1\nb_ok16.vis=1\nb_no16.vis=1", "先弹确认")
add(16, "msk16", "文本", 6, 70, 308, 110, 10, "", "", "bco 深色 vis=0")
add(16, "t_m16a", "文本", 20, 84, 280, 18, 13, "确认换料？", "", "vis=0")
add(16, "t_m16b", "文本", 20, 108, 280, 14, 10, "本盘已推将清零", "", "vis=0")
add(16, "b_ok16", "按钮", 30, 136, 120, 30, 12, "确定",
    cmd('"#REELSET "+t_r_addr.txt+" "+t_reel.txt+" "+t_warn.txt') +
    '\nmsk16.vis=0\nt_m16a.vis=0\nt_m16b.vis=0\nb_ok16.vis=0\nb_no16.vis=0', "vis=0")
add(16, "b_no16", "按钮", 170, 136, 120, 30, 12, "取消",
    "msk16.vis=0\nt_m16a.vis=0\nt_m16b.vis=0\nb_ok16.vis=0\nb_no16.vis=0", "vis=0")
add(16, "b_reelno", "按钮", 204, 116, 110, 34, 13, "取消", "page 8")
add(16, "t_r_hint", "文本", 6, 200, 308, 12, 10, "确认后本盘已推清零；预警数填 0 自动取 10%", "")
add(16, "t_cmd16", "文本", 250, 226, 64, 12, 10, "", "", "隐藏 vis=0")
wake(16)

# ================= 输出 =================
def lang(name):
    d = {}
    fp = os.path.join(LANGDIR, name)
    if os.path.exists(fp):
        for line in open(fp, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1); d[k.strip()] = v
    return d

ZH, EN = lang("zh.txt"), lang("en.txt")
key_of = lambda n: "bTab" + re.match(r"^bTab\d(\w+)$", n).group(1) if re.match(r"^bTab\d(\w+)$", n) else n
tw = lambda s, fs: sum(fs * (1.0 if ord(c) > 0x2000 else 0.55) for c in s)

BG, PANEL, TABOFF, TABON = 2212, 4357, 4291, 8782
WHITE, GRAY, GREEN, RED, AMBER, CYAN, BTN = 61342, 36020, 28305, 60269, 60842, 26236, 6504
ACT = {0:"Run",1:"Par",2:"Log",3:"Set",5:"Ota"}
PCO = {"b_feed":WHITE,"b_en":GREEN,"b_dis":RED,"b_wind":AMBER,"b_scan":CYAN,"b_read":WHITE,
       "b_save":GREEN,"b_reset":RED,"b_locate":AMBER,"b_locate15":AMBER,"b_blink":WHITE,
       "b_set":CYAN,"b_back":WHITE,"b_back4":WHITE,"b_back8":WHITE,"b_back15":WHITE,
       "b_logon":GREEN,"b_logoff":WHITE,"b_clr":WHITE,"b_msk_yes":GREEN,"b_msk_no":WHITE,
       "b_clear4":WHITE,"b_pwd":CYAN,"b_do":RED,"b_factory":RED,"b_tft":WHITE,"b_issue":AMBER,
       "b_x":CYAN,"b_apply":GREEN,"b_discard":WHITE,"b_para":WHITE,"b_more":CYAN,
       "b_select":CYAN,"b_calq":WHITE,"b_calmov":AMBER,"b_cal0":WHITE,"b_cal90":WHITE,
       "b_calall":GREEN,"b_calstop":RED,"b_cal15":WHITE,"b_myes":GREEN,"b_mno":WHITE}
INBOX = {"t_addr1","t_f","t_t","t_v","t_w","t_a","t_b","t_c","t_u","t7","t_caddr","t_uaddr","t_addr15"}

HIDDEN_PREFIX = ("t_cmd","msk","t_msk","t_dlg","t_m16","b_ok16","b_no16","b_myes","b_mno","b_apply","b_discard","b_msk")
def _hidden(n):
    return n == "hsWake" or n == "t_addr15" or n.startswith(HIDDEN_PREFIX)

BTN_BG = {
 "b_feed":6630,"b_en":6630,"b_save":6630,"b_logon":6630,"b_calall":6630,"b_apply":6630,
 "b_myes":6630,"b_reelok":6630,"b_ok16":6630,
 "b_dis":16612,"b_reset":16612,"b_do":16612,"b_factory":16612,"b_calstop":16612,
 "b_wind":16803,"b_locate":16803,"b_locate15":16803,"b_pwr":16803,"b_calmov":16803,"b_issue":16803,
 "b_scan":4455,"b_cal":4455,"b_reel":4455,"b_para":4455,"b_select":4455,"b_x":4455,
 "b_calq":4455,"b_more":4455,"b_b0":6630,"b_b1":16803,"b_b2":4455,"b_b3":61342,
}
TPCO = {"t_online":28305,"t_prob":60842,"t_ustat":28305,"t_status":28305,
        "t_volt":28305,"t_calchip":28305,"t_chip":26236,"t_msg":60842,
        "t_dim":26236,"t_slp":26236,"t_cal":60842}
def colors(d):
    n, t, pg = d["name"], d["typ"], d["page"]
    if t == "触摸热区": return ("","","","")
    if n.startswith("bTab"):
        suf = re.match(r"^bTab\d(\w+)$", n).group(1); on = (ACT.get(pg) == suf)
        return (TABON if on else TABOFF, TABON, WHITE if on else GRAY, WHITE)
    if t == "按钮": return (BTN_BG.get(n, BTN), BG, PCO.get(n, WHITE), WHITE)
    if t == "滑块": return (PANEL, "", CYAN, "")
    if t == "滚动文本": return (PANEL, "", WHITE, "")
    if n in INBOX: return (PANEL, "", WHITE, "")
    if n.startswith(("msk",)): return (PANEL, "", "", "")
    if t == "文本": return (BG, "", GRAY if n.startswith("l_") else TPCO.get(n, WHITE), "")
    return ("","","","")

csv_path = os.path.join(OUT, "串口屏控件清单.csv")
with open(csv_path, "w", encoding="utf-8-sig", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["页码","控件名","类型","x","y","w","h","字号","语言键","中文","英文","溢出",
                "bco","bco2","pco","pco2","vis","事件脚本","备注"])
    for d in W:
        k = key_of(d["name"]); zt = ZH.get(k, d["text"]); et = EN.get(k, "")
        chk = ""
        if et and tw(et, d["font"]) > d["w"]-4: chk = "英超宽"
        elif zt and tw(zt, d["font"]) > d["w"]-4: chk = "中超宽"
        c = colors(d)
        w.writerow([d["page"],d["name"],d["typ"],d["x"],d["y"],d["w"],d["h"],d["font"],
                    k,zt,et,chk,c[0],c[1],c[2],c[3],0 if _hidden(d["name"]) else 1,d["ev"],d["note"]])
print("CSV:", len(W), "控件")

names = {0:"P0 状态",1:"P1 参数A",2:"P2 日志",3:"P3 设置",4:"P4 密码",5:"P5 升级",
         7:"P7 校准",8:"P8 单台操作",9:"P9 选择1/4",10:"P10 选择2/4",11:"P11 选择3/4",
         12:"P12 选择4/4",13:"P13 问题1/2",14:"P14 问题2/2",15:"P15 参数B",16:"P16 换料"}
md = ["# 串口屏脚本清单\n", "> 由 `tools/gen_screen.py` 生成，勿手改。\n", "## Program.s\n",
      "```\nbaud=115200\ndim=80\nbkcmd=0\npage 0\n```\n", "## 各页脚本\n"]
for pg in sorted(names):
    rows = [d for d in W if d["page"] == pg and d["ev"]]
    if not rows: continue
    md.append("### %s\n" % names[pg])
    md.append("| 控件 | 类型 | 坐标 | 事件 |\n|---|---|---|---|")
    for d in rows:
        md.append("| %s | %s | %d,%d,%d,%d | %s |" % (d["name"], d["typ"], d["x"], d["y"],
                  d["w"], d["h"], d["ev"].replace("|","\\|").replace("\n","<br>")))
    md.append("")
open(os.path.join(OUT, "串口屏脚本清单.md"), "w", encoding="utf-8", newline="\n").write("\n".join(md)+"\n")
print("MD ok")
