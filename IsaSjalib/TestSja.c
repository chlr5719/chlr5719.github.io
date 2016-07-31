/********************************************************************
文件名称:   TestSja.c

文件功能:   提供CSD板卡功能演示

文件说明:   此文件提供用户界面，方面用户测试CSD板卡

当前版本:   V1.5

修改记录:   2012-12-06  V1.0    王鹤翔  创建
            2013-01-09  V1.1    王鹤翔  修改    根据新版驱动重新编写
            2014-02-25  V1.2    徐佳谋  修改    微调代码
            2015-12-25  V1.3    徐佳谋  修改    配合驱动修改
            2016-04-05  V1.4    徐佳谋  增加    配合驱动修改，同时增加计数等功能
            2016-04-14  V1.5    王  明  修改    配合驱动修改，修改测试程序，添加标准帧发送
********************************************************************/

#include "vxWorks.h"
#include "stdio.h"
#include "sysLib.h"
#include "ioLib.h"
#include "iv.h"
#include "semLib.h"

#include "IsaSjaLib.h"

/* 实际CAN通道数 */
#define CURRENT_SJA_NUM         (8)
#define DEBUG                   1


/* CSD板卡资源信息 */
typedef struct csd_info_s
{
    int base0;
    int base;
    int irq;
} csd_info_t;

csd_info_t g_infos[MAX_SJA_NUM] =
{
    {0x311, 0xd000, 5},
    {0x311, 0xd100, 5},
    {0x311, 0xd200, 5},
    {0x311, 0xd300, 5},
    {0x311, 0xd400, 5},
    {0x311, 0xd500, 5},
    {0x311, 0xd600, 5},
    {0x311, 0xd700, 5},
};

int g_csd_debug = 1;
long g_transmit_counts[MAX_SJA_NUM];
long g_received_counts[MAX_SJA_NUM];

int InitSja(int fd, int BaudRate);
int tCanRecv(int fd);
static int GetKeyInput(int radix);

/********************************************************************
函数名称:   GetKeyInput

函数功能:   获取键盘输入

参数名称        类型            输入/输出           含义
radix           int             input               获取数据的进制（10:十进制 16:十六进制）

返回值  :   输入的数值。

函数说明:   键盘输入字符转换后的十进制值。理论上可以用scanf获取用户输入
            但是scanf函数当用户输入错误的数据时会出现异常，进而导致程序异常
            不利于程序健壮性，所以写一个函数替换。函数可应用于VxWorks和Linux

修改记录:   2010-10-27      徐佳谋  创建
********************************************************************/
static int GetKeyInput(int radix)
{
    char input = 0;
    int value = 0;

    if (radix == 10)
    {
        while((input = getchar()) != 0x0a)
        {
            value = value * 10 + (input - 0x30);  /* 转换成十进制数 */
        }
    }
    else
    {
        while((input = getchar()) != 0x0a)
        {
            if (input <= 0x39)
            {
                input -= 48;
            }
            else if (input <= 0x46)
            {
                input -= 55;
            }
            else
            {
                input -= 87;
            }
            value = (value << 4) + input;  /* 转换成十六进制数 */
        }
    }

    return value;
}

/********************************************************************
函数名称:   TestSja

函数功能:   测试程序主函数

参数名称        类型            输入/输出           含义

返回值  :   根据实际情况自行定义

函数说明:   主函数先打开驱动程序，然后提供用户人机交互界面
            并根据用户选择进行不同的功能测试，具体的测试功能由单独的函数实现

修改记录:   2012-12-06      王鹤翔  创建
            2016-04-14      王  明  修改  根据修改后的驱动修改部分函数名称
********************************************************************/
int TestSja(void)
{
    int input = 0;
    int ret = 0;
    int i = 0;
    int recv_tid[CURRENT_SJA_NUM] = {0};

    memset(g_received_counts, 0, sizeof(g_received_counts));
    memset(g_transmit_counts, 0, sizeof(g_transmit_counts));

    /* 初始化SJA1000芯片 */
    for (i = 0; i < CURRENT_SJA_NUM; i++)
    {
        ret = InitSja(i, BAUDRATE_500K);
        if (ret != 0)
        {
            printf("InitSja failed!%d\n", ret);

            continue ;
        }
    }

    /* 创建接收任务 */
    for(i = 0; i < CURRENT_SJA_NUM; i++)
    {
        recv_tid[i] = taskSpawn("tCanRecv", 98, 0, 8192, tCanRecv, i, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    printf("\nISA Sja1000 Test Program\n");

    /* 打印菜单 */
    for(input = 100;;)
    {
        switch(input)
        {
        case 98:
            g_csd_debug = !g_csd_debug;
            break;

        case 99:
            printf("Exit Program, exit code = %d\n", CloseSja());
            goto Exit;

        case 1:
            printf("return = %d\n", TransmitSja());
            break;

        case 2: /* 显示接收计数 */
            for (i = 0; i < MAX_SJA_NUM; i++)
            {
                printf("R%d=%11d  ", i, g_received_counts[i]);
            }
            printf("\n");
            for (i = 0; i < MAX_SJA_NUM; i++)
            {
                printf("T%d=%11d  ", i, g_transmit_counts[i]);
            }
            printf("\n");
            break;

        case 3: /* 清除计数 */
            for (i = 0; i < MAX_SJA_NUM; i++)
            {
                g_received_counts[i] = 0;
                g_transmit_counts[i] = 0;

                /* 清除FIFO */
                ret = FlushIsaCan(i);
                if (ret != 0)
                {
                    printf("channel %d FlushIsaCan failed!%d\n", i, ret);
                }
            }
            printf("Counts are clean.\n");
            break;

        default:
            printf("1.  Transmit data\n"
                   "2.  Dispaly  Counts\n"
                   "3.  Clear    counts\n"
                   "98. Swtich debug onoff (debug = %d 0:off 1:on)\n"
                   "99. Quit\n", g_csd_debug);
        } /* end switch */

        printf("%d\n", input = GetKeyInput(10));
    } /* end for */

Exit:
    /* 删除接收任务 */
    for(i = 0; i < CURRENT_SJA_NUM; i++)
    {
        CloseSja(i);
        taskDelete(recv_tid[i]);
    }

    return 0;
}

/********************************************************************
函数名称:   TransmitSja

函数功能:   CAN总线发送测试

参数名称        类型            输入/输出           含义

返回值  :   0代表成功 非0代表失败

函数说明:   循环发送需要加延时

修改记录:   2012-12-06      王鹤翔  创建
            2013-01-09      王鹤翔  修改  根据新版驱动重新编写
            2016-04-14      王  明  修改  根据修改后的驱动修改部分函数名称，添加标准帧发送
********************************************************************/
int TransmitSja(void)
{
    int RunTimes = 0;
    int i = 0;
    int ret = 0;
    sja1000_frame_t frame;
    int fd = 0;
    int FrameFormat = 0;

    /* 清零发送数据 */
    memset(frame, 0, sizeof(sja1000_frame_t));

    /* 指定发送CAN口 */
    printf("Input Transmit CAN Number(0-%d: CAN1-can%d):\n", MAX_SJA_NUM - 1, MAX_SJA_NUM);
    fd = GetKeyInput(10) % MAX_SJA_NUM;

    /* 输入发送次数 */
    printf("CAN Test, Input Transmit Count(1-10000):\n");
    RunTimes = GetKeyInput(10) % 10000;

    printf("CAN Test, Input Transmit frame format (0:Standard frame 1:Extended frame):\n");
    FrameFormat = GetKeyInput(10) % 2;

    if(FrameFormat == 0) /* 标准帧，注意标准帧最长11字节，ID一共11位 */
    {
        /* header */
        frame.header = 0x08;

        /* ID */
        frame.buffer[0] = 0x00; /* ID */
        frame.buffer[1] = 0x00; /* ID 高3位有效 */

        /* DATA */
        frame.buffer[2] = 0x33;
        frame.buffer[3] = 0x40;
        frame.buffer[4] = 0x55;
        frame.buffer[5] = 0x66;
        frame.buffer[6] = 0x77;
    }
    else if(FrameFormat == 1) /* 扩展帧，注意扩展帧最长13字节，ID一共29位*/
    {
        /* header */
        frame.header = 0x88;

        /* ID */
        frame.buffer[0] = 0x11; /* ID */
        frame.buffer[1] = 0x22; /* ID */
        frame.buffer[2] = 0x33; /* ID */
        frame.buffer[3] = 0x40; /* ID 高5位有效 */

        /* DATA */
        frame.buffer[4] = 0x55;
        frame.buffer[5] = 0x66;
        frame.buffer[6] = 0x77;
        frame.buffer[7] = 0x88;
        frame.buffer[8] = 0x99;
    }

    for (i = 0; i < RunTimes; i++)
    {
        if(FrameFormat == 0)
        {
            frame.buffer[7] = i;
        }
        else
        {
            frame.buffer[9] = i;
        }

        ret |= WriteIsaCan(fd, &frame);
        if(ret != SUCCESS)
        {
            printf("can%d send failed! ret = %d, i = %d\n", fd, ret, i);
            return ret;
        }

        g_transmit_counts[fd] += 1;

        taskDelay(sysClkRateGet() / 2);    /* 这里需要加延时，否则芯片会一直产生接收中断 */
    }

    return ret;
}

/********************************************************************
函数名称:   InitSja

函数功能:   初始化驱动和SJA1000芯片

参数名称        类型            输入/输出           含义
BaudRate        int             input               波特率

返回值  :   0代表成功 非0代表失败

函数说明:   SJA1000芯片配置需要查阅手册

修改记录:   2012-12-06      王鹤翔  创建
            2013-01-09      王鹤翔  修改  根据新版驱动重新编写
            2016-04-14      王  明  修改  根据修改后的驱动修改代码
********************************************************************/
int InitSja(int fd, int BaudRate)
{
    int ret = 0;
    sja1000_config_t config;
    sja1000_filter_t AcceptFilter;


    /* 打开驱动 */
    ret = OpenIsaCan(fd, g_infos[fd].base0, g_infos[fd].base, g_infos[fd].irq);
    if(ret != SUCCESS)
    {
        printf("OpenIsaCan failed!fd = %d ret = %d\n", fd, ret);

        return -1;
    }

    /* 初始化SJA1000芯片 */
    config.uchMOD = 0x08;           /* 单滤波，唤醒，非自测，非监听模式 */
    config.uchBTR0   = BaudRate >> 8;
    config.uchBTR1   = BaudRate;    /* 波特率 */
    config.uchACR[0] = 0;
    config.uchACR[1] = 0;
    config.uchACR[2] = 0;
    config.uchACR[3] = 0;
    config.uchAMR[0] = 0xff;
    config.uchAMR[1] = 0xff;
    config.uchAMR[2] = 0xff;
    config.uchAMR[3] = 0xff;
    config.uchCDR = 0xc4;
    config.uchOCR = 0xda;
    config.uchIER = 0x83;

    ret = IoctlIsaCan(fd, IOCTL_INIT_SJA, &config);
        if(ret != SUCCESS)
    {
        printf("Init isa can Failed!fd = %d ret = %d\n", fd, ret);

        return -1;
    }

    /*  动态设置接收滤波器 */
    AcceptFilter.MaskMode = SINGLE_FILTER;
    AcceptFilter.uchACR[0] = 0x00;
    AcceptFilter.uchACR[1] = 0x00;
    AcceptFilter.uchACR[2] = 0x00;
    AcceptFilter.uchACR[3] = 0x00;
    AcceptFilter.uchAMR[0] = 0xff;
    AcceptFilter.uchAMR[1] = 0xff;
    AcceptFilter.uchAMR[2] = 0xff;
    AcceptFilter.uchAMR[3] = 0xff;

    ret = IoctlIsaCan(fd, SET_ACCEPT_FILTER, &AcceptFilter);
    if(ret != SUCCESS)
    {
        printf("Set mask Failed!fd = %d ret = %d\n", fd, ret);

        return -1;
    }

    return ret;
}

/********************************************************************
函数名称:   tCanRecv

函数功能:   接收任务

参数名称        类型            输入/输出           含义
fd              int             input               设备文件索引(0-4)

返回值  :   0代表成功 非0代表失败

函数说明:   当SJA1000接收到中断时，驱动会调用此函数。

修改记录:   2010-09-16      王鹤翔  创建
            2013-01-09      王鹤翔  修改    根据新版驱动重新编写
            2015-12-25      徐佳谋  修改    配合驱动修改
            2016-04-14      王  明  修改    将回调改为任务
********************************************************************/
int tCanRecv(int fd)
{
    sja1000_frame_t frame;
    int ret = 0;
    int i = 0;

    for(;;)
    {
        ret = ReadIsaCan(fd, &frame);    /* 读取接收数据 */
        if(ret <= 0)
        {
            continue;
        }

        g_received_counts[fd] += ret / CAN_FRAME_SIZE;
        if(g_csd_debug)
        {
            printf("--- ");
            printf("%02X ", frame.header);
            for (i = 0; i < CAN_FRAME_SIZE - 1; i++)
            {
                printf("%02X ", frame.buffer[i]);
            }
            printf("--- can%d received %4d frames\n", fd, g_received_counts[fd]);
        }
    }

    return 0;
}

/********************************************************************
函数名称:   CloseSja

函数功能:   关闭驱动

参数名称        类型            输入/输出           含义

返回值  :   0代表成功 非0代表失败

函数说明:   关闭驱动

修改记录:   2012-12-06      王鹤翔  创建
            2013-01-09      王鹤翔  修改  根据新版驱动重新编写
            2016-04-14      王  明  修改  根据修改后的驱动修改部分函数名称
********************************************************************/
int CloseSja(void)
{
    int i = 0;

    for(i = 0; i < MAX_SJA_NUM; i++)
    {
        CloseIsaCan(i);
    }

    return 0;
}
