/********************************************************************
文件名称:   TestSja.c

文件功能:   提供CSD板卡功能演示

文件说明:   此文件提供用户界面，方面用户测试CSD板卡

当前版本:   V1.3

修改记录:   2012-12-06  V1.0    王鹤翔  创建
            2013-01-09  V1.1    王鹤翔  修改    根据新版驱动重新编写
            2014-02-25  V1.2    徐佳谋  修改    微调代码
            2015-12-25  V1.3    徐佳谋  修改    配合驱动修改
********************************************************************/

#include "vxWorks.h"
#include "stdio.h"
#include "sysLib.h"
#include "ioLib.h"
#include "iv.h"
#include "semLib.h"

#include "IsaSjaLib.h"

int g_csd_debug = 1;
int g_counts[MAX_SJA_NUM];

int CallBack(int fdv);
int InitSja(void);
int WriteCan(void);
int CloseCan(void);

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
********************************************************************/
int TestSja(void)
{
    int select = 0;
    int ret = 0;

    printf("\nISA Sja1000 Test Program\n");

    InitSja();

    /* 打印菜单 */
    for (;;)
    {
        printf("\n"
               "1. CAN Test\n"
               "99. Exit\n");

        printf("Your choice: %d\n", select = GetKeyInput(10));

        switch (select)
        {
        case 99:
        {
            printf("Exit Program, exit code = %d\n", CloseSja());
            goto Exit;
        }
        case 1:
        {
            printf("return = %d\n", TransmitSja());
            break;
        }
        default:
        {
            printf("Invalid select, retry!\n");
            break;
        }
        } /* end switch */
    } /* end for */

Exit:
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
********************************************************************/
int TransmitSja(void)
{
    int RunTimes = 0;
    int i = 0;
    int ret = 0;
    Can_TPacket_t TxPacket;
    int fd = 0;

    /* 指定发送CAN口 */
    printf("Input Transmit CAN Number(0-%d: CAN1-can%d):\n", MAX_SJA_NUM - 1, MAX_SJA_NUM);
    fd = GetKeyInput(10) % MAX_SJA_NUM;

    /* 输入发送次数 */
    printf("CAN Test, Input Transmit Count(1-10000):\n");
    RunTimes = GetKeyInput(10) % 10000;

    TxPacket.uchFF = 0x88;
    TxPacket.uchID[0] = 0x11;
    TxPacket.uchID[1] = 0x22;
    TxPacket.uchID[2] = 0x33;
    TxPacket.uchID[3] = 0x44;
    memset(TxPacket.uchDATA, 0x55, 8);

    for (i = 0; i < RunTimes; i++)
    {
        TxPacket.uchDATA[7] = i;
        ret |= CAN_SendMsg(fd, &TxPacket);
        if(ret != SUCCESS)
        {
            printf("can%d send failed! ret = %d, i = %d\n", fd, ret, i);
            return ret;
        }
        taskDelay(sysClkRateGet() / 2);    /* 这里需要加延时，否则芯片会一直产生接收中断，
                               驱动会不停的进入中断服务程序，导致不能调用回调函数 */
    }

    return ret;
}

/********************************************************************
函数名称:   InitSja

函数功能:   初始化驱动和SJA1000芯片

参数名称        类型            输入/输出           含义

返回值  :   0代表成功 非0代表失败

函数说明:   SJA1000芯片配置需要查阅手册

修改记录:   2012-12-06      王鹤翔  创建
            2013-01-09      王鹤翔  修改  根据新版驱动重新编写
********************************************************************/
int InitSja(void)
{
    int ret = 0;
    int i = 0;
    sja1000_config_t config;

    config.uchMOD = 0x09;   /* 进入reset模式，单滤波，唤醒，非自测，非监听模式 */
    config.uchBTR0 = 0x00;  /* 500Kbps */
    config.uchBTR1 = 0x1C;
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
    config.uchIER = 0x81;

    /* 打开驱动 */
    ret = CAN_Open(0, 0x300, 0xd400, 5, 4096, 49);
    if(ret != SUCCESS)
    {
        printf("Can%d CAN_Open failed!%d\n", 0, ret);

        return ;
    }

    /* 初始化SJA1000芯片 */
    ret = CAN_Init(0, &config);
    if(ret != SUCCESS)
    {
        printf("Can%d CAN_Init Failed!%d\n", 0, ret);
        
        return ;
    }

    /* 安装回调函数 */
    CAN_InstallCallBack(0, CallBack);

    /* 打开驱动 */
    ret = CAN_Open(1, 0x300, 0xd800, 7, 4096, 49);
    if(ret != SUCCESS)
    {
        printf("Can%d CAN_Open failed!%d\n", 1, ret);

        return ;
    }

    /* 初始化SJA1000芯片 */
    ret = CAN_Init(1, &config);
    if(ret != SUCCESS)   /* 初始化SJA1000芯片 */
    {
        printf("Can%d CAN_Init Failed!%d\n", 1, ret);
        
        return ;
    }

    /* 安装回调函数 */
    CAN_InstallCallBack(1, CallBack);

    return ret;
}

/********************************************************************
函数名称:   CallBack

函数功能:   回调函数

参数名称        类型            输入/输出           含义
fd              int             input               设备文件索引(0-4)

返回值  :   0代表成功 非0代表失败

函数说明:   当SJA1000接收到中断时，驱动会调用此函数。

修改记录:   2010-09-16      王鹤翔  创建
            2013-01-09      王鹤翔  修改    根据新版驱动重新编写
            2015-12-25      徐佳谋  修改    配合驱动修改
********************************************************************/
int CallBack(int fd)
{
    unsigned char RxBuffer[CAN_FRAME_SIZE] = {0};
    int ret = 0;
    int i = 0;

    for(;;)
    {
        memset(RxBuffer, 0, sizeof(RxBuffer));
        ret = CAN_ReadMsg(fd, RxBuffer, sizeof(RxBuffer));    /* 读取接收数据 */
        if (ret <= 0)
        {
            break;
        }

        g_counts[fd] += ret / CAN_FRAME_SIZE;
        if (g_csd_debug)
        {
            printf("--- ");
            for (i = 0; i < CAN_FRAME_SIZE; i++)
            {
                printf("%02X ", RxBuffer[i]);
            }
            printf("--- can%d received %4d frames\n", fd, g_counts[fd]);
        }
        else
        {
            printf("can%d received %4d frames\n", fd, g_counts[fd]);
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
********************************************************************/
int CloseSja(void)
{
    int i = 0;

    for(i = 0; i < MAX_SJA_NUM; i++)
    {
        CAN_Close(i);
    }

    return 0;
}
