my_printf:用来自由输出调试字符
sendAnswer：固定文本应答，没有格式化占位符，只传静态字符串。
sendAnswerFmt：带格式化（% d %02X），可以拼接变量，专门给 M602 这种需要打印地址、错误码、ChipID 的场景。
两者输出协议格式完全一样：
error=0 → ok 内容\r\n
error=1 → error 内容\r\n
只是内容来源不同。
举例：
sendAnswer(0,"ready"); → ok ready\r\n
sendAnswerFmt(0, "addr:%d error:%d ChipID:%02X%02X%02X", addr, err, c0,c1,c2);


MCODE定义和应答

#define MCODE_ADVANCE              600   // 进料指令：M600 [F<mm>] [N<id>]
示例：

#define MCODE_RETRACT_POST_PICK    601   // 取料后回退：M601 [N<id>]
示例
M601 N1 S1：地址 1 执行
M601 P112233 S1：chipid 匹配执行
M601 N1 P112233 S1：地址 1 AND chipid 匹配才执行
M601 N255 P112233 S1：广播 + 指定 chipid，只有该 ID 设备执行


#define MCODE_FEEDER_IS_OK         602   // 查询飞达配置和状态：M602 [N<id>] 或者M602 P<chipid>


#define MCODE_SERVO_SET_ANGLE      603   // 手动设置舵机角度：M603 A<angle> [N<id>]



#define MCODE_SET_FEEDER_ENABLE    610   // 启用/禁用飞达：M610 S<0|1> [N<id>]
示例：
M610 S1（无 N）：所有飞达执行使能（广播）
M610 S0（无 N）：所有飞达执行禁用（广播）
M610 N255 S1：显式广播，全部飞达使能
M610 N3 S1：仅地址 3 飞达执行使能
也可以带上P


S 只能是 0/1，S=-1 不再作为查询，S 传入其他值 → 返回参数错误



#define MCODE_UPDATE_FEEDER_CONFIG 620   // 更新飞达配置参数：M620 ...

#define MCODE_GET_ADC_RAW          143   // 获取 ADC 原始值：M143 [A<channel>]
#define MCODE_GET_ADC_SCALED       144   // 获取 ADC 换算后的物理量：M144 [A<channel>]
#define MCODE_SET_SCALING          145   // 设置 ADC 缩放因子：M145 A<ch> S<factor> O<offset>

#define MCODE_SET_POWER_OUTPUT     155   // 设置电源输出：M155 S<value>
// --- 系统控制相关 M-Code ---
#define MCODE_FACTORY_RESET        799   // 恢复出厂设置：M799 (写回出厂默认值 + 自动软复位)

M799 时序（X61）：应答 "ok EEPROM invalidated, restart now" → 写默认配置到 EEPROM(扇区0)
→ 延时 5ms 确保应答最后一位发完 → SWBS=0 + SWRST=1 软复位回用户程序区
→ 重启后 config_ver=CONFIG_VERSION 校验通过，直接加载出厂配置(addr=99)，
  数码管 ~1.2s 后显示 99（禁用态 1s 慢闪），并重新上报 "ok ready addr:99 ChipID:xxxxxx"。
注意：重启窗口约 1.2s，期间上位机发来的命令会丢失，需等待 ready 行或稍后重发。