/********************************************************************
文件名称:   IsaSjaLib.c

文件功能:   实现基于ISA总线的SJA1000芯片驱动

文件说明:   在VxWorks下A3CSD板卡驱动基础上修改

当前版本:   V2.2

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
            2016-04-05  V2.1    徐佳谋  增加    增加flush函数
            2016-04-21  V2.2    王  明  修改    发送和初始化函数添加互斥体;将IinitIsaCan改为IoctlIsaCan函数
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
#include "msgQLib.h"

#include "IsaSjaLib.h"

typedef struct can_device_s
{
    unsigned int  board;        /* 板卡基地址 */
    unsigned long CanAddr;      /* 板卡CAN口地址 */
    unsigned int  CanIrq;       /* 板卡CAN口中断 */
    int           OpenFlag;     /* 该设备是否打开标志 */
    RING_ID       CanRngID;     /* 接收FIFO使用ID */
    RING_ID       CanRngID2;    /* 发送FIFO使用ID */
    SEM_ID        ReadSemID;    /* 读数据函数信号量ID */
    SEM_ID        mutex;        /* 驱动使用互斥体 */
    SEM_ID        SendSemID;    /* 发送使用信号量 */
    SEM_ID        SendOkSemID;  /* 发送完成使用信号量 */
    int           tCanTaskID;   /* 任务使用ID */
    MSG_Q_ID      msg;          /* 驱动程序使用消息 */
} can_device_t;

#define DEBUG               0       /* 调试宏 */
#define DEBUG1              1
#define SJA_MAPPING_TYPE    1       /* 1:MEM 2:IO 3:CUSTOM */
#define INT_NUM_IRQ0        0x20    /* 此处注意当不是使用PIC模式时需要修改 */
#define INT_VEC_GET(irq)    (INT_NUM_IRQ0 + irq)

#define WRITE_REGISTER_UCHAR(address, value)    (*(unsigned char *)(address) = (value))
#define READ_REGISTER_UCHAR(address)            (*(unsigned char *)(address))

#if SJA_MAPPING_TYPE == 1
#define PEEKB(seg, offset)                      (READ_REGISTER_UCHAR((seg << 4) + offset))
#define POKEB(seg, offset, value)               (WRITE_REGISTER_UCHAR(((seg << 4) + offset), value))
#elif SJA_MAPPING_TYPE == 2
#define PEEKB(seg, offset)                      sysInByte(seg + offset)
#define POKEB(seg, offset, value)               sysOutByte(seg + offset, value)
#else SJA_MAPPING_TYPE == 3
#define PEEKB(seg, offset)                      CustomRead(seg, offset)
#define POKEB(seg, offset, value)               CustomWrite(seg, offset, value)
#endif

can_device_t g_Devices[MAX_SJA_NUM]; /* 驱动设备表 */

/* 共享中断设备信息 */
typedef struct share_info_s
{
    int fds[MAX_SJA_NUM];
    int number;
    int irq; /* 共享的中断号 */
    int UsingFlag; /* 是否正在使用 */
} share_info_t;

share_info_t s_infos[MAX_SJA_NUM] = {0};

void CanInterrupt(int fd);
void CanInterrupt2(int group);
unsigned char CustomRead(unsigned int seg, unsigned char offset);
void CustomWrite(unsigned int seg, unsigned char offset, unsigned char value);
int tTaskSend(MSG_Q_ID msg);

/********************************************************************
函数名称:   OpenIsaCan

函数功能:   打开CAN通道

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)
board           unsigned short  input           板卡基地址(没有时填0)
base            unsigned short  input           CAN基地址
irq             int             input           CAN端口使用中断号


返回值  :   SUCCESS                     操作成功
            ERROR_BOARD_FAILURE         板卡异常(CPU的BIOS或者CSD异常)
            ERROR_PARAMETER_ILLEGAL     中断参数错误
            ERROR_INSTALL_ISR_FAIL      注册中断失败

函数说明:   函数没有对EEPROM进行操作。

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
********************************************************************/
int OpenIsaCan(int fd, unsigned short board, unsigned short base, int irq)
{
    int i = 0;
    int j = 0;

    can_device_t *pDevice = NULL;
    can_device_t *pDeviceIndex = NULL;

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

    if (board != 0)
    {
        /* 使能串口和CAN口中断 */
        sysOutByte(board + 2, 0x7f);
    }

    /* 芯片在板检查 */
    if (PEEKB(pDevice->CanAddr, 2) == 0xFF)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 安装中断服务程序 */
    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(pDevice->CanIrq)), CanInterrupt, fd))
    {
        return ERROR_INSTALL_ISR_FAIL;
    }

    /* 初始化接收队列 */
    pDevice->CanRngID = rngCreate(CAN_RX_FIFO_SIZE);

    /* 初始化发送队列 */
    pDevice->CanRngID2 = rngCreate(CAN_TX_FIFO_SIZE);

    /* 创建二值信号量 */
    pDevice->ReadSemID = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* 创建二值信号量 */
    pDevice->SendSemID = semBCreate(SEM_Q_FIFO, SEM_FULL);

    /* 创建二值信号量 */
    pDevice->SendOkSemID = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* 创建互斥体 */
    pDevice->mutex = semMCreate(SEM_Q_FIFO | SEM_DELETE_SAFE);

    /* 创建消息队列 */
    pDevice->msg = msgQCreate(100, 32, MSG_Q_PRIORITY);

    /* 创建发送任务 */
    pDevice->tCanTaskID = taskSpawn("tTaskSend", 100, (VX_SUPERVISOR_MODE | VX_UNBREAKABLE), 8192, tTaskSend, (int)pDevice->msg, 0, 0, 0, 0, 0, 0, 0, 0, 0);


    /* 标志驱动已打开 */
    pDevice->OpenFlag = 1;

    /* 使能中断 */
    sysIntEnablePIC(pDevice->CanIrq);

    /* 查询是否为共享中断如何是则注册CanInterrupt2 */
    SetShareInterrupt(fd,irq);

    return SUCCESS;
}

/********************************************************************
函数名称:   InitIsaCan

函数功能:   配置CSD板卡CAN寄存器。

参数名称        类型                输入/输出       含义
fd              int                 input           设备文件索引(0-8)
pConfig         sja1000_config_t*   input           SJA初始化配置结构体

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误
            ERROR_CONFIG_TIMEOUT      进入RESET模式超时

函数说明:   对SJA1000芯片进行初始化操作。

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
            2016-04-21      王  明  修改    增加互斥，保护共享资源
********************************************************************/
int InitIsaCan(int fd, sja1000_config_t *pConfig)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 保证访问互斥 */
    semTake(pDevice->mutex, WAIT_FOREVER);

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

    /* 睡眠模式必须延时设置 */
    if((pConfig->uchMOD & 0x10) == 0x10)
    {
        taskDelay(sysClkRateGet());
        POKEB(pDevice->CanAddr, 0, pConfig->uchMOD);
    }

    POKEB(pDevice->CanAddr, 4, pConfig->uchIER); /* 使能需求的中断 */

    /* 进入正常模式 */
    POKEB(pDevice->CanAddr, 0, pConfig->uchMOD & 0xfe);

    if((PEEKB(pDevice->CanAddr, 0) & 0x01) == 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* 配置失败 */
    }

    /* 释放互斥 */
    semGive(pDevice->mutex);

    return SUCCESS; /* 配置成功返回SUCCESS */
}

/********************************************************************
函数名称:   WriteIsaCan

函数功能:   发送CAN报文

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)
pFrame          sja1000_frame_t*input           CAN发送数据包

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误 或 pFrame->uchFF大于8
            ERROR_CONFIG_TIMEOUT      缓冲区锁定无法发送

函数说明:   函数用查询方式发送数据。

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
            2016-04-21      王  明  修改    增加互斥，保护共享资源
********************************************************************/
int WriteIsaCan(int fd, sja1000_frame_t *pFrame)
{
    int i = 0;
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

#if DEBUG
    printf("pFrame->header = %#x\n", pFrame->header);
    for(i = 0; i < 12; i++)
    {
        printf("%#x ", pFrame->buffer[i]);
    }
    printf("WriteIsaCan run! fd = %d\n", fd);
#endif

    /* 保证访问互斥 */
    semTake(pDevice->mutex, WAIT_FOREVER);

    /* 等待缓冲有空余 */
    semTake(pDevice->SendSemID, WAIT_FOREVER);

    /* 发送数据放入FIFO */
    rngBufPut(pDevice->CanRngID2, (char *)pFrame, sizeof(sja1000_frame_t));

    /* 触发发送任务 */
    msgQSend(pDevice->msg, (char *)&pDevice, sizeof(pDevice), NO_WAIT, MSG_PRI_URGENT);

    /* 判断缓冲区是否有空余 */
    if (rngFreeBytes(pDevice->CanRngID2) >= sizeof(sja1000_frame_t))
    {
        /* 使能发送 */
        semGive(pDevice->SendSemID);
    }

    /* 释放互斥 */
    semGive(pDevice->mutex);

    return SUCCESS;
}

/********************************************************************
函数名称:   DrainCan

函数功能:   读空CAN报文

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)

返回值  :   SUCCESS                   操作成功
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误
            ERROR_CONFIG_TIMEOUT      操作超时

函数说明:   此函数在中断服务程序中使用，不对外提供，仅将数据放入ringbuf中，
            供ReadIsaCan函数读取。

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
********************************************************************/
int DrainCan(int fd)
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
函数名称:   CanInterrupt2

函数功能:   CAN共享中断服务程序

参数名称        类型            输入/输出       含义
group           int             input               共享中断设备组号(0-8)

返回值  :   无

函数说明:   在CAN口需要共享中断的时候，安装此中断服务程序。

修改记录:   2013-01-11      王鹤翔  创建
            2016-01-31      徐佳谋  修改
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
********************************************************************/
void CanInterrupt2(int group)
{
    int i = 0;
    int j = 0;
    int fd;
    unsigned char uch = 0;
    unsigned char uch2 = 0;

    can_device_t *pDevice;

#if HAVE_FPGA
    for(j = 0; ; j++)
    {
#if (FPGA_MAPPING_TYPE == 1)
        uch = (PEEKB(FPGA_BASE, FPGA_INT_STATE) & FPGA_INT_STATE_MASK);
#else
        uch = (sysInByte(FPGA_BASE + FPGA_INT_STATE) & FPGA_INT_STATE_MASK);
#endif /* endif FPGA_MAPPING_TYPE*/

#if DEBUG
        logMsg("uch = 0x%02X\n", uch, 0, 0, 0, 0, 0);
#endif /* endif DEBUG*/
        if (uch == 0)
        {
            break;
        }
#else
    for(j = 0; j < 4; j++)
    {
#endif /* endif HAVE_FPGA*/
        for (i = 0; i < s_infos[group].number; i++)
        {
            /* 防止参数异常 */
            fd = s_infos[group].fds[i] % MAX_SJA_NUM;
            pDevice = &g_Devices[fd];

            /* 读取CAN中断状态 */
            uch = PEEKB(pDevice->CanAddr, 3) & 0xff;
#if DEBUG
            logMsg("i = %d uch = 0x%02X CanAddr = 0x%04X\n", i, uch, pDevice->CanAddr, 0, 0, 0);
#endif /* endif DEBUG*/
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

                /* 发送完成,防止总线错误后一直阻塞 */
                semGive(pDevice->SendOkSemID);

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
                DrainCan(fd);
            }

            if (uch & 0x02)             /* 发送完成 */
            {
                semGive(pDevice->SendOkSemID);
            }

            semGive(pDevice->ReadSemID);/* 释放信号量 */
        }
    } /* end for(;;) */

    return;
}

/********************************************************************
函数名称:   CanInterrupt

函数功能:   CAN中断服务程序

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)

返回值  :   无

函数说明:   目前只对接收中断进行处理，其余中断只报错

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
********************************************************************/
void CanInterrupt(int fd)
{
    unsigned char uch = 0;
    unsigned char uch2 = 0;
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    for(;;)
    {
        uch = PEEKB(pDevice->CanAddr, 3);
        if (uch == 0x00)
        {
            break;
        }

        if(uch & 0x80)           /* 总线错误 */
        {
            /* 自动复位SJA芯片 */
            uch2 = PEEKB(pDevice->CanAddr, 0);
            POKEB(pDevice->CanAddr, 0, uch2 | 0x01);
            POKEB(pDevice->CanAddr, 0, uch2 & 0xfe);

            /* 发送完成,防止总线错误后一直阻塞 */
            semGive(pDevice->SendOkSemID);

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
            DrainCan(fd);        /* 读取缓冲区数据放入fifo */
        }

        if (uch & 0x02)          /* 发送完成 */
        {
            semGive(pDevice->SendOkSemID);
        }
    }

    semGive(pDevice->ReadSemID); /* 释放信号量，执行回调函数 */

    return;
}

/********************************************************************
函数名称:   ReadIsaCan

函数功能:   获取CAN报文

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)
pFrame          sja1000_frame_t*output          接收数据缓冲区

返回值  :   成功:返回读取的字节数  失败:返回错误码

函数说明:   驱动程序接收到数据后会先存入软件缓冲区，本函数只从软件缓冲区
            中读取数据。

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-11      王  明  修改    获取信号量读取软件FIFO数据，取消回调读取方式
********************************************************************/
int ReadIsaCan(int fd, sja1000_frame_t *pFrame)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 将FIFO读空后一直等待信号量 */
    if(rngIsEmpty(pDevice->CanRngID))
    {
        /* 获取信号量 */
        if (semTake(pDevice->ReadSemID, WAIT_FOREVER) == -1)
        {
            return -1;
        }
    }

    return rngBufGet(pDevice->CanRngID, (char *)pFrame, sizeof(sja1000_frame_t));
}

/********************************************************************
函数名称:   CloseIsaCan

函数功能:   关闭CAN驱动

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)

返回值  :   SUCCESS                   操作成功，FIFO已空
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)
            ERROR_PARAMETER_ILLEGAL   数据结构体参数错误

函数说明:   关闭中断，释放信号量。

修改记录:   2013-01-08      王鹤翔  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
********************************************************************/
int CloseIsaCan(int fd)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];
    if(pDevice->OpenFlag == 0)
    {
        return SUCCESS;
    }

    POKEB(pDevice->CanAddr, 0, 0x01);     /* 使两个CAN通道处于复位状态 防止驱动关闭后 通道依然发送数据 */
    sysIntDisablePIC(pDevice->CanIrq);

    taskDelete(pDevice->tCanTaskID);
    
    semDelete(pDevice->ReadSemID);        /* 删除信号量 */

    semDelete(pDevice->SendSemID);        /* 删除信号量 */

    semDelete(pDevice->SendOkSemID);      /* 删除信号量 */

    semDelete(pDevice->mutex);            /* 删除互斥信号量 */

    rngDelete(pDevice->CanRngID);         /* 删除ringbuf */
    
    rngDelete(pDevice->CanRngID2);        /* 删除ringbuf */

    msgQDelete(pDevice->msg);

    
    pDevice->OpenFlag = 0;

    memset(s_infos, 0, sizeof(s_infos));

    return SUCCESS;
}

/********************************************************************
函数名称:   CustomRead

函数功能:   读取SJA1000寄存器

参数名称        类型            输入/输出           含义
seg             unsigned int    input               SJA1000映射地址
offset          unsigned char   input               寄存器偏移

返回值  :   读取的寄存器值(8bit)

函数说明:   客户定制底板SJA1000寄存器采用映射方式访问。
            通过两个寄存器映射SJA1000芯片寄存器的索引和数据。

修改记录:   2013-06-25  徐佳谋  创建
            2016-04-14  王  明  修改    按公司编码规范修改函数名称
********************************************************************/
unsigned char CustomRead(unsigned int seg, unsigned char offset)
{
    sysOutByte(seg, offset);
    return sysInByte(seg + 1);
}

/********************************************************************
函数名称:   CustomWrite

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
void CustomWrite(unsigned int seg, unsigned char offset, unsigned char value)
{
    sysOutByte(seg, offset);
    sysOutByte(seg + 1, value);

    return ;
}

/********************************************************************
函数名称:   FlushIsaCan

函数功能:   清除缓冲区

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)

返回值  :   SUCCESS                   操作成功，FIFO已空
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)

函数说明:   清空芯片和软件缓冲区。

修改记录:   2016-04-05      徐佳谋  创建
            2016-04-14      王  明  修改    按公司编码规范修改函数名称
            2016-04-21      王  明  修改    增加互斥，保护共享资源
********************************************************************/
int FlushIsaCan(int fd)
{
    can_device_t *pDevice = NULL;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];
    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 保证访问互斥 */
    semTake(pDevice->mutex, WAIT_FOREVER);

    POKEB(pDevice->CanAddr, 1, 0x04);

    rngFlush(pDevice->CanRngID);

    /* 使能发送 */
    semGive(pDevice->SendSemID);
    semGive(pDevice->SendOkSemID);

    /* 释放互斥 */
    semGive(pDevice->mutex);

    return SUCCESS;
}

/********************************************************************
函数名称:   SetAcceptFilter

函数功能:   设置接收滤波器

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)

返回值  :   SUCCESS                   操作成功，FIFO已空
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)

函数说明:   设置接收滤波器

修改记录:   2016-04-05      徐佳谋  创建
********************************************************************/
int SetAcceptFilter(int fd, sja1000_filter_t* pSjaMask)
{
    can_device_t *pDevice = NULL;
    unsigned char uchMode = 0;
    unsigned char uch = 0;

    /* 防止参数异常 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* 保证访问互斥 */
    semTake(pDevice->mutex, WAIT_FOREVER);

    /* 读取当前模式 */
    uchMode = PEEKB(pDevice->CanAddr, 0);
    POKEB(pDevice->CanAddr, 0, (uchMode | 0x01));

    /* 如果不能进入reset返回报错 */
    if((PEEKB(pDevice->CanAddr, 0) & 0x01) != 0x01)
    {
        return ERROR_CONFIG_TIMEOUT; /* 配置失败 */
    }


    /* 设置单滤波、双滤波 */
    if(pSjaMask->MaskMode == SINGLE_FILTER)
    {
        uchMode = PEEKB(pDevice->CanAddr, 0);
        POKEB( pDevice->CanAddr, 0, (uchMode | SINGLE_FILTER) );
    }
    else if(pSjaMask -> MaskMode == DUAL_FILTER)
    {
        uchMode = PEEKB(pDevice->CanAddr, 0);
        POKEB(pDevice->CanAddr, 0, (uchMode & DUAL_FILTER) );
    }

    /* 以下code 和 mask 设置是在扩展的can的reset模式下的设置值，在operate模式下无效 */
    POKEB(pDevice->CanAddr, 16, pSjaMask->uchACR[0]);
    POKEB(pDevice->CanAddr, 17, pSjaMask->uchACR[1]);
    POKEB(pDevice->CanAddr, 18, pSjaMask->uchACR[2]);
    POKEB(pDevice->CanAddr, 19, pSjaMask->uchACR[3]);
    POKEB(pDevice->CanAddr, 20, pSjaMask->uchAMR[0]);
    POKEB(pDevice->CanAddr, 21, pSjaMask->uchAMR[1]);
    POKEB(pDevice->CanAddr, 22, pSjaMask->uchAMR[2]);
    POKEB(pDevice->CanAddr, 23, pSjaMask->uchAMR[3]);

    /* 进入正常模式 */
    uchMode = PEEKB(pDevice->CanAddr, 0);
    POKEB(pDevice->CanAddr, 0, (uchMode & 0xfe) );

    if((PEEKB(pDevice->CanAddr, 0) & 0x01) == 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* 配置失败 */
    }

    /* 释放互斥 */
    semGive(pDevice->mutex);

    return SUCCESS; /* 配置成功返回SUCCESS */
}

/********************************************************************
函数名称:   SetBaudRate

函数功能:   设置波特率

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)

返回值  :   SUCCESS                   操作成功，FIFO已空
            ERROR_BOARD_FAILURE       板卡异常(板卡未打开)

函数说明:   设置接收滤波器

修改记录:   2016-04-05      徐佳谋  创建
********************************************************************/
int SetBaudRate(int fd, enum Baud_Rate baud)
{
    return 0;
}

/********************************************************************
函数名称:   IoctlIsaCan

函数功能:   实现不同功能的命令字

参数名称        类型            输入/输出           含义
fd              int             input               设备描述符
cmd             int             input               命令字
p               void*           input/output        命令字对应参数

返回值  :   0:成功 非0:失败

函数说明:   函数通过不同的命令字与参数，实现不同的功能.
            CMD                             FUNCTION
            OCTL_INIT_SJA：                 初始化芯片
            IOCTL_QUERY_SEND_RESULT：       查询发送结果
            IOCTL_CAN_RESET_FIFO：          清空FIFO

修改记录:   2016-04-21  王  明  创建
********************************************************************/
int IoctlIsaCan(int fd, int cmd, void *p)
{
    can_device_t *pDevice = NULL;
    int ret = 0;

    /* 确认设备有效 */
    fd = fd > (MAX_SJA_NUM - 1) ? 0 : fd;
    pDevice = &g_Devices[fd];

    /* 根据命令字 */
    switch(cmd & 0x00FF)
    {
    case IOCTL_INIT_SJA:                        /* 初始化SJA控制器 */
        ret = InitIsaCan(fd, (sja1000_config_t *)p);
        break;

    case IOCTL_CAN_RESET_FIFO:                  /* 清空FIFO */
        ret = FlushIsaCan(fd);
        break;

    case SET_BAUD_RATE:                         /* 设置波特率 */
        /* ret = SetBaudRate(fd, *p); */
        break;

    case SET_ACCEPT_FILTER:
        ret = SetAcceptFilter(fd, (sja1000_filter_t*)p); /* 设置验收滤波器 */
        break;

    default:
        ret = ERROR_PARAMETER_IOCTL;
        break;
    }

    return ret;
}

/********************************************************************
函数名称:   SetShareInterrupt

函数功能:   查询注册共享中断

参数名称        类型            输入/输出       含义
fd              int             input           设备文件索引(0-8)
irq             int             input           CAN端口使用中断号

返回值  :   SUCCESS                     操作成功

函数说明:   查询注册共享中断，查询是否为共享中断如何是则注册CanInterrupt2

修改记录:   2016-04-21  王鹤翔  创建
********************************************************************/
int SetShareInterrupt(int fd, int irq)
{
    int i = 0;
    int j = 0;
    int ret = -1;

    can_device_t *pDeviceIndex = NULL;

    /* 遍历g_Devices数组 */
    for(i = 0; i < MAX_SJA_NUM; i++)
    {
        pDeviceIndex = &g_Devices[i];

        if((pDeviceIndex -> OpenFlag == 1) && (pDeviceIndex -> CanIrq == irq))
        {
            /* 遍历数组s_infos查看该中断号是否已经共享 */
            for(j = 0; j < MAX_SJA_NUM; j++)
            {
                /* 该中断已经被共享 */
                if(s_infos[j].irq == irq && s_infos[j].UsingFlag == 1)
                {
                    s_infos[j].fds[s_infos[j].number] = fd;
                    s_infos[j].number++;

                    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(irq)), CanInterrupt2, j))
                    {
                        return ERROR_INSTALL_ISR_FAIL;
                    }

                    ret = 0;
                    goto EXIT;
                }
                else
                {
                    /* 第一次查询到该中断号，将其设置为对应的fd保存在s_infos[j].fds[0]中 */
                    if(s_infos[j].UsingFlag == 0)
                    {
                        s_infos[j].UsingFlag = 1;
                        s_infos[j].fds[0] = fd;
                        s_infos[j].irq = irq;
                        s_infos[j].number = 1;

                        ret = 0;
                        goto EXIT;
                    }
                }
            }
        }
    }

EXIT:
    return ret;
}

/********************************************************************
函数名称:   tTaskSend

函数功能:   数据发送任务

参数名称        类型            输入/输出           含义
msg             MSG_Q_ID        input               串口使用的消息队列

返回值  :   0:成功 非0:失败

函数说明:   使用任务发送数据，可以防止出现发送优先级过高问题。

修改记录:   2014-08-19  徐佳谋  创建
********************************************************************/
int tTaskSend(MSG_Q_ID msg)
{
    int i = 0;
    int DataLength = 0;
    can_device_t *pDevice = NULL;
    sja1000_frame_t frame;

    for (;;)
    {
        /* 使用消息队列 */
        if (msgQReceive(msg, (char *)&pDevice, sizeof(pDevice), WAIT_FOREVER) == -1)
        {
            break;
        }

        for (;;)
        {
            if (rngIsEmpty(pDevice->CanRngID2))
            {
                break;
            }

            rngBufGet(pDevice->CanRngID2, (char *)&frame, sizeof(sja1000_frame_t));

            /*将数据包放入内存*/
            POKEB(pDevice->CanAddr, 16, frame.header);

            for(i = 0; i < 12; i++)
            {
                POKEB(pDevice->CanAddr, 17 + i, frame.buffer[i]);
            }

            /* 发送数据 使用中止发送 */
            POKEB(pDevice->CanAddr, 0x1, 0x03);

            /* 等待缓冲区可用,发送完成中断触发 */
            semTake(pDevice->SendOkSemID, WAIT_FOREVER);
        }

        /* 判断缓冲区是否有空余 */
        if (rngFreeBytes(pDevice->CanRngID2) >= sizeof(sja1000_frame_t))
        {
            /* 使能发送 */
            semGive(pDevice->SendSemID);
        }
    }

    return 0;
}
