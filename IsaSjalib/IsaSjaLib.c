/********************************************************************
文件名称:   IsaSjaLib.c

文件功能:   实现基于ISA总线的SJA1000芯片驱动

文件说明:   在VxWorks下A3CSD板卡驱动基础上修改

当前版本:   V2.0

修改记录：  2011-12-26  V1.0    王鹤翔  创建
            2012-01-20  V1.1    王鹤翔  修改    部分变量、函数名标准化、将原Csd_CanConfig拆分成CanConfig和SjaConfig
            2012-02-02  V1.2    王鹤翔  修改    修改函数成功的返回值为0、修改中断服务程序
            2012-05-02  V1.3    徐佳谋  修改    修改中断服务程序
            2013-01-08  V1.4    王鹤翔  修改    修改了程序结构，支持多块板卡
            2013-06-25  V1.5    徐佳谋  增加    增加客户定制底板SJA1000映射访问函数
            2014-02-26  V1.6    徐佳谋  增加    增加门限寄存器配置，支持共享中断
            2014-08-01  V1.7    徐佳谋  增加    中断任务参数设置
            2015-06-23  V1.8    徐佳谋  增加    增加芯片在位检查
            2015-12-25  V1.9    徐佳谋  修改    修改程序结构
            2016-01-31  V2.0    徐佳谋  增加    增加多通道共享中断支持
********************************************************************/

#include "vxWorks.h"
#include "stdio.h"
#include "iv.h"
#include "intLib.h"
#include "logLib.h"
#include "sysLib.h"
#include "rngLib.h"
#include "semLib.h"
#include "taskLib.h"

#include "IsaSjaLib.h"

typedef struct can_device_s
{
    unsigned int  board;        /* 板卡基地址 */
    unsigned long CanAddr;      /* 板卡CAN口地址 */
    unsigned int  CanIrq;       /* 板卡CAN口中断 */
    int          (*CallBack)(int);/* 应用程序回调函数 */
    RING_ID       CanRngID;     /* FIFO使用ID */
    SEM_ID        CanSemID;     /* 信号量使用ID */
    int           CanTaskID;    /* 任务使用ID */
    int           OpenFlag;     /* 该设备是否打开标志 */
} can_device_t;

#define MAPPING_TYPE        1       /* 1:MEM 2:IO 3:CUSTOM */
#define INT_NUM_IRQ0        0x20    /* 此处注意当不是使用PIC模式时需要修改 */
#define INT_VEC_GET(irq)    (INT_NUM_IRQ0 + irq)

#define WRITE_REGISTER_UCHAR(address, value)    (*(unsigned char *)(address) = (value))
#define READ_REGISTER_UCHAR(address)            (*(unsigned char *)(address))

#if MAPPING_TYPE == 1
#define PEEKB(seg, offset)                      (READ_REGISTER_UCHAR((seg << 4) + offset))
#define POKEB(seg, offset, value)               (WRITE_REGISTER_UCHAR(((seg << 4) + offset), value))
#elif MAPPING_TYPE == 2
#define PEEKB(seg, offset)                      sysInByte(seg + offset)
#define POKEB(seg, offset, value)               sysOutByte(seg + offset, value)
#else MAPPING_TYPE == 3
#define PEEKB(seg, offset)                      customRead(seg, offset)
#define POKEB(seg, offset, value)               customWrite(seg, offset, value)
#endif

can_device_t g_Devices[MAX_SJA_NUM]; /* 驱动设备表 */
static int s_fds[MAX_SJA_NUM];
static int s_number;

int tCanRecv(int fd);
void CAN_Interrupt(int fd);
void CAN_Interrupt2(int *fds);
unsigned char customRead(unsigned int seg, unsigned char offset);
void customWrite(unsigned int seg, unsigned char offset, unsigned char value);

/********************************************************************
函数名称:   CAN_Open

函数功能:   配置CSD板卡CAN寄存器。

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)
board           unsigned short  input           板卡基地址
base            unsigned short  input           CAN基地址
irq             int             input           CAN端口使用中断号
stack           int             input           中断后处理任务栈大小
priority        int             input           中断后处理任务栈优先级

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(CPU的BIOS或者CSD异常)
            ERROR_PARAMETER_ILLEGAL   中断参数错误

函数说明:   函数没有对EEPROM进行操作。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_Open(int fd, unsigned short board, unsigned short base, int irq, int stack, int priority)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 1)
    {
        return SUCCESS;
    }

    switch (irq)
    {
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 9:
    case 10:
    case 11:
    case 15:
        break;
    default:
        return ERROR_PARAMETER_ILLEGAL;     /* 中断参数错误，返回-2*/
    }

    pDevice->CanAddr = base;    /* CAN地址 */
    pDevice->CanIrq = irq;      /* CAN中断 */

    /* 芯片在板检查 */
    if (PEEKB(pDevice->CanAddr, 2) == 0xFF)
    {
        return ERROR_BOARD_FAILURE;
    }

    if (board != 0)
    {
        /* 使能串口和CAN口中断 */
        POKEB(board, 2, 0x7f);
    }

    /* 安装中断服务程序 */
    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(pDevice->CanIrq)), CAN_Interrupt, fd))
    {
        return ERROR_INSTALL_ISR_FAIL;
    }

    /* 初始化队列 */
    pDevice->CanRngID = rngCreate(CAN_FIFO_SIZE);

    /* 创建信号量 */
    pDevice->CanSemID = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* 标志驱动已打开 */
    pDevice->OpenFlag = 1;

    /* 创建接收任务 */
    pDevice->CanTaskID = taskSpawn("tCanRecv", priority, 0, stack, tCanRecv, fd, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    /* 使能中断 */
    sysIntEnablePIC(pDevice->CanIrq);

    return SUCCESS;
}

/********************************************************************
函数名称:   CAN_Init

函数功能:   配置CSD板卡CAN寄存器。

参数名称        类型                输入/输出       含义
fd              int                 input           设备文件索引(0-4)
pConfig         sja1000_config_t*   input           SJA初始化配置

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误
            ERROR_CONFIG_TIMEOUT      进入RESET模式超时
            ERROR_INSTALL_ISR_FAIL    安装中断服务程序失败

函数说明:   对SJA1000芯片进行初始化操作。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_Init(int fd, sja1000_config_t *pConfig)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 开始进入reset模式配置 */
    POKEB(pDevice->CanAddr, 0, 0x01);

    /* 如果不能进入reset模式，多尝试几次 */
    if((PEEKB(pDevice->CanAddr, 0) & 0x01) != 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* 配置失败 */
    }

    POKEB(pDevice->CanAddr, 31, pConfig->uchCDR);    /* 0xc4 set clock divider PELICAN MODE */
    POKEB(pDevice->CanAddr, 8, pConfig->uchOCR);     /* 0xda set output control */

    /* 配置mode 波特率 */
    POKEB(pDevice->CanAddr, 0, pConfig->uchMOD | 0x01);
    POKEB(pDevice->CanAddr, 6, pConfig->uchBTR0);
    POKEB(pDevice->CanAddr, 7, pConfig->uchBTR1);

    /* 错误门限寄存器 */
    POKEB(pDevice->CanAddr, 13, pConfig->uchEWLR);

    /* 清除错误计数器 */
    POKEB(pDevice->CanAddr, 14, 0);
    POKEB(pDevice->CanAddr, 15, 0);

    /* 以下code 和 mask 设置是在扩展的can的reset模式下的设置值，在operate模式下无效 */
    POKEB(pDevice->CanAddr, 16, pConfig->uchACR[0]);   /* set code and mask */
    POKEB(pDevice->CanAddr, 17, pConfig->uchACR[1]);
    POKEB(pDevice->CanAddr, 18, pConfig->uchACR[2]);
    POKEB(pDevice->CanAddr, 19, pConfig->uchACR[3]);
    POKEB(pDevice->CanAddr, 20, pConfig->uchAMR[0]);
    POKEB(pDevice->CanAddr, 21, pConfig->uchAMR[1]);
    POKEB(pDevice->CanAddr, 22, pConfig->uchAMR[2]);
    POKEB(pDevice->CanAddr, 23, pConfig->uchAMR[3]);

    /* 进入正常模式 */
    POKEB(pDevice->CanAddr, 0, pConfig->uchMOD & 0xfe);

    /* 睡眠模式必须延时设置 */
    if((pConfig->uchMOD & 0x10) == 0x10)
    {
        taskDelay(sysClkRateGet());
        POKEB(pDevice->CanAddr, 0, pConfig->uchMOD);
    }

    POKEB(pDevice->CanAddr, 4, pConfig->uchIER); /* 使能需求的中断 */

    return SUCCESS; /* 配置成功返回SUCCESS */
}

/********************************************************************
函数名称:   CAN_InstallCallBack

函数功能:   安装回调函数

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)
CallBack        int(*)(int)     input           回调函数入口地址

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误

函数说明:   当驱动接收到中断后，会触发内部任务，内部任务会调用回调函数。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_InstallCallBack(int fd, int (*CallBack)(int fd))
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    pDevice->CallBack = CallBack;

    return SUCCESS;
}

/********************************************************************
函数名称:   CAN_SendMsg

函数功能:   发送CAN报文

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)
canpacket       Can_TPacket_t*  input           CAN发送数据包

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误 或 canpacket->uchFF大于8
            ERROR_CONFIG_TIMEOUT      缓冲区锁定无法发送

函数说明:   函数用查询方式发送数据。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_SendMsg(int fd, Can_TPacket_t *canpacket)
{
    int DataLength = 0;
    int i = 0;
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 判断缓冲区是否锁定 */
    if(((PEEKB(pDevice->CanAddr, 2) & 0x04)) == 0x00)
    {
        return ERROR_CONFIG_TIMEOUT;    /* 发送就绪状态位没有置位 */
    }

    /*将数据包放入内存*/
    POKEB(pDevice->CanAddr, 16, canpacket->uchFF);

    /*扩展帧*/
    if((canpacket->uchFF & 0x80) == 0x80)
    {
        for(i = 0; i < 4; i++)
        {
            POKEB(pDevice->CanAddr, 17 + i, canpacket->uchID[i]);
        }

        DataLength = canpacket->uchFF & 0x0F;
        if(DataLength > 8)
        {
            return ERROR_PARAMETER_ILLEGAL;
        }

        for(i = 0; i < DataLength; i++)
        {
            POKEB(pDevice->CanAddr, 21 + i, canpacket->uchDATA[i]);
        }
    }
    else    /* 标准帧 */
    {
        for(i = 0; i < 2; i++)
        {
            POKEB(pDevice->CanAddr, 17 + i, canpacket->uchID[i]);
        }

        DataLength = canpacket->uchFF & 0x0F;
        if(DataLength > 8)
        {
            return ERROR_PARAMETER_ILLEGAL;
        }

        for(i = 0; i < DataLength; i++)
        {
            POKEB(pDevice->CanAddr, 19 + i, canpacket->uchDATA[i]);
        }
    }

    /* 发送数据包 */
    POKEB(pDevice->CanAddr, 0x1, 0x01);

    return SUCCESS;
}

/********************************************************************
函数名称:   CAN_Drain

函数功能:   读空CAN报文

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误
            ERROR_CONFIG_TIMEOUT      操作超时

函数说明:   此函数在中断服务程序中使用，不对外提供，仅将数据放入ringbuf中，
            供CAN_ReadMsg函数读取。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_Drain(int fd)
{
    int i = 0;
    unsigned char RecvData[CAN_FRAME_SIZE] = {0};
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    if((PEEKB(pDevice->CanAddr, 2) & 0x01) != 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* 缓冲区状态位没有置位 */
    }

    for(i = 0; i < CAN_FRAME_SIZE; i++)
    {
        RecvData[i] = PEEKB(pDevice->CanAddr, 16 + i);
    }

    /* 释放接收缓冲区 */
    POKEB(pDevice->CanAddr, 0x01, 0x04);

    if(rngBufPut(pDevice->CanRngID, (char *)RecvData, sizeof(RecvData)) == 0)   /* rngBuffer满 */
    {
        /* 如果FIFO满了 提示信息 */
        logMsg("ring buffer is overflow!\n", 0, 0, 0, 0, 0, 0);

        /* 清除FIFO */
        rngFlush(pDevice->CanRngID);
    }

    return SUCCESS;
}

/********************************************************************
函数名称:   CAN_Interrupt2

函数功能:   CAN共享中断服务程序

参数名称        类型            输入/输出       含义
fds             int*            input           设备文件索引数数组

返回值  :   无

函数说明:   在CAN口需要共享中断的时候，安装此中断服务程序。

修改记录:   2013-01-11      王鹤翔  创建
            2016-01-31      徐佳谋  修改
********************************************************************/
void CAN_Interrupt2(int *fds)
{
    int fd;
    unsigned char uch = 0;
    unsigned char uch2 = 0;
    int i = 0;
    can_device_t *pDevice;
    int j;

    for(j = 0; ; j++)
    {
        /* 读取中断状态寄存器 */
        uch = PEEKB(FPGA_BASE, FPGA_INT_STATE);
#if 0
        logMsg("uch = 0x%02X\n", uch, 0, 0, 0, 0, 0);
#endif
        if (uch == 0)
        {
            break;
        }

        for (i = 0; i < s_number; i++)
        {
            /* 防止参数异常 */
            fd = fds[i] % MAX_SJA_NUM;
            pDevice = &g_Devices[fd];

            /* 读取CAN中断状态 */
            uch = PEEKB(pDevice->CanAddr, 3) & 0xff;
#if 0
            logMsg("i = %d uch = 0x%02X CanAddr = 0x%04X\n", i, uch, pDevice->CanAddr, 0, 0, 0);
#endif
            if(uch == 0)
            {
                continue ;
            }

            /* 处理设备中断 */
            if(uch & 0x80)              /* 总线错误 */
            {
                /* 自动复位SJA芯片 */
                uch2 = PEEKB(pDevice->CanAddr, 0);
                POKEB(pDevice->CanAddr, 0, uch2 | 0x01);
                POKEB(pDevice->CanAddr, 0, uch2 & 0xfe);
                logMsg("can%d Bus Error!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x40)              /* 仲裁丢失 */
            {
                logMsg("can%d Arbitration Lost!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x20)              /* 被动错误 */
            {
                logMsg("can%d Error Passive!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x08)              /* 数据溢出 */
            {
                logMsg("can%d Data Overrun!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x04)              /* 错误警告 */
            {
                logMsg("can%d Error Warning!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x01)              /* 接收中断 */
            {
                CAN_Drain(fd);
            }

            semGive(pDevice->CanSemID);    /* 释放信号量，执行回调函数 */
        }
    } /* end for(;;) */

    return;
}

/********************************************************************
函数名称:   CAN_Interrupt

函数功能:   CAN中断服务程序

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)

返回值  :   无

函数说明:   目前只对接收中断进行处理，其余中断只报错

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
void CAN_Interrupt(int fd)
{
    unsigned char uch = 0;
    unsigned char uch2 = 0;
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    while(PEEKB(pDevice->CanAddr, 3) != 0)
    {
        uch = PEEKB(pDevice->CanAddr, 3);
        if(uch & 0x80)           /* 总线错误 */
        {
            /* 自动复位SJA芯片 */
            uch2 = PEEKB(pDevice->CanAddr, 0);
            POKEB(pDevice->CanAddr, 0, uch2 | 0x01);
            POKEB(pDevice->CanAddr, 0, uch2 & 0xfe);
            logMsg("can%d Bus Error!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x40)           /* 仲裁丢失 */
        {
            logMsg("can%d Arbitration Lost!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x20)           /* 被动错误 */
        {
            logMsg("can%d Error Passive!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x08)           /* 数据溢出 */
        {
            logMsg("can%d Data Overrun!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x04)           /* 错误警告 */
        {
            logMsg("can%d Error Warning!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x01)           /* 接收中断 */
        {
            CAN_Drain(fd);     /* 读取缓冲区数据放入fifo */
        }
    }

    semGive(pDevice->CanSemID);     /* 释放信号量，执行回调函数 */

    return;
}

/********************************************************************
函数名称:   CAN_ReadMsg

函数功能:   获取CAN报文

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)
uchBuffer       unsigned char*  output          接收数据区
nBytes          int             input           接收数据量

返回值  :   从软件缓冲区中读到的字节数。

函数说明:   驱动程序接收到数据后会先存入软件缓冲区，本函数只从软件缓冲区
            中读取数据。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_ReadMsg(int fd, unsigned char *uchBuffer, int nBytes)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    return rngBufGet(pDevice->CanRngID, (char *)uchBuffer, nBytes);
}

/********************************************************************
函数名称:   CAN_Close

函数功能:   关闭CAN驱动

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)

返回值  :   SUCCESS                   操作成功，FIFO已空
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误

函数说明:   关闭中断，释放信号量。

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int CAN_Close(int fd)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];
    if(pDevice->OpenFlag == 0)
    {
        return SUCCESS;
    }

    POKEB(pDevice->CanAddr, 0, 0x01);    /* 使两个CAN通道处于复位状态 防止驱动关闭后 通道依然发送数据 */
    sysIntDisablePIC(pDevice->CanIrq);

    semDelete(pDevice->CanSemID);        /* 删除信号量 */
    rngDelete(pDevice->CanRngID);        /* 删除ringbuf */

    pDevice->OpenFlag = 0;

    return SUCCESS;
}

/********************************************************************
函数名称:   tCanRecv

函数功能:   CAN接收任务

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-4)

返回值  :   0:成功

函数说明:   驱动关闭后该任务会自动退出

修改记录:   2013-01-08      王鹤翔  创建
********************************************************************/
int tCanRecv(int fd)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    for (;;)
    {
        /* 阻塞等待中断服务程序触发信号量 */
        if (semTake(pDevice->CanSemID, WAIT_FOREVER) == ERROR)
        {
            return ;  /* 信号量被删除时 程序不会进入死循环 */
        }

        if (pDevice->CallBack != NULL)
        {
            (*pDevice->CallBack)(fd);
        }
    }

    return 0;
}

/********************************************************************
函数名称:   customRead

函数功能:   读取SJA1000寄存器

参数名称        类型            输入/输出           含义
seg             unsigned int    input               SJA1000映射地址
offset          unsigned char   input               寄存器偏移

返回值  :   读取的寄存器值(8bit)

函数说明:   客户定制底板SJA1000寄存器采用映射方式访问。
            通过两个寄存器映射SJA1000芯片寄存器的索引和数据。

修改记录:   2013-06-25  徐佳谋  创建
********************************************************************/
unsigned char customRead(unsigned int seg, unsigned char offset)
{
    sysOutByte(seg, offset);
    return sysInByte(seg + 1);
}

/********************************************************************
函数名称:   customWrite

函数功能:   写入SJA1000寄存器

参数名称        类型            输入/输出           含义
seg             unsigned int    input               SJA1000映射地址
offset          unsigned char   input               寄存器偏移
value           unsigned char   input               写入寄存器值

返回值  :   无

函数说明:   客户定制底板SJA1000寄存器采用映射方式访问。
            通过两个寄存器映射SJA1000芯片寄存器的索引和数据。

修改记录:   2013-06-25  徐佳谋  创建
********************************************************************/
void customWrite(unsigned int seg, unsigned char offset, unsigned char value)
{
    sysOutByte(seg, offset);
    sysOutByte(seg + 1, value);

    return ;
}

/********************************************************************
函数名称:   CAN_ShareInterrupt

函数功能:   设置多通道CAN共享中断

参数名称        类型            输入/输出           含义
fds             int*            input               设备文件索引数数组
number          int             input               共享中断设备数(2-8)
irq             int             input               共享中断号

返回值  :   无

函数说明:   当多个CAN通道共享一个中断时调用此函数，把设备

修改记录:   2016-01-31  徐佳谋  创建
********************************************************************/
int CAN_ShareInterrupt(int *fds, int number, int irq)
{
    int i = 0;
    int fd = 0;
    can_device_t *pDevice = NULL;

    for (i = 0; i < number; i++)
    {
        /* 防止参数异常 */
        fd = fds[i] % MAX_SJA_NUM;
        pDevice = &g_Devices[fd];

        if(pDevice->OpenFlag == 0)
        {
            return ERROR_BOARD_FAILURE;
        }
    }

    /* 保存参数 */
    memcpy(s_fds, fds, number * sizeof(int));
    s_number = number;

    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(irq)), CAN_Interrupt2, (int)s_fds))
    {
        return ERROR_INSTALL_ISR_FAIL;
    }

    return 0;
}
