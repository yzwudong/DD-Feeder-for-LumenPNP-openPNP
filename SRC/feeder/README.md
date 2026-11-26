# 飞达固件工程

> **本 README 是早期 89C52 点灯例程遗留，不是飞达实际工程说明。**
>
> 飞达 App/BL 的编译、版本号和发布包生成，见：
> [docs/软件项目文档.md §8 构建与烧录](docs/软件项目文档.md)。
>
> 快速生成发布包：
>
> ```text
> VS Code: Terminal -> Run Task -> build+package feeder
> ```
>
> 如果提示“无法替换命令变量 eide.project.rebuild”，先执行
> `Developer: Reload Window`，旧 task 已被替换为 `scripts/build_app.ps1`
> （直接调用 EIDE `unify_builder.exe`）。
>
> 产物：`build/release/DDF_<yymmddnn>_{BL,APP,FULL}.hex`。
> 烧录：见 [docs/烧录说明.md](docs/烧录说明.md)。

---

# 89C52 例程

89C52 的点灯例程，使用 SDCC 编译器进行编译

```c
#include <reg52.h>
#include <stdio.h>

void delay_100ms(void) //@12MHz
{
    unsigned char i, j;

    i = 195;
    j = 138;

    do
    {
        while (--j)
            ;
    } while (--i);
}

void main(void)
{
    while (1)
    {
        P00 = !P00;
        delay_100ms();
    }
}
```

## SDCC 编译设置

默认设置：`--iram-size 256 --xram-size 0 --code-size 8192`，对应参数如下：

- IRAM  SIZE: 256  Bytes
- XRAM  SIZE: 0    Bytes
- FLASH SIZE: 8192 Bytes

要修改上述参数，请到 `构建器选项 -> Other Global Options` 中进行修改。
