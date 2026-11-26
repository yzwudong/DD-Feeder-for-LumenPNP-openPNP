#ifndef __BOOT_PROTO_H__
#define __BOOT_PROTO_H__

/* 进入 Bootloader 协议循环；REBOOT 成功后返回 0。
 * 失败/超时返回 -1，由 main 决定是否留在 Bootloader。 */
int boot_proto_run(void);

#endif
