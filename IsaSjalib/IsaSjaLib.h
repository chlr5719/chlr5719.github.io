/********************************************************************
文件名称:   IsaSjaLib.h

文件功能:   实现基于ISA总线的SJA1000芯片驱动

文件说明:   在VxWorks下A3CSD板卡驱动基础上修改

当前版本:   V2.0

修改记录：  2011-12-26  V1.0    王鹤翔  创建
            2012-01-20  V1.1    王鹤翔  修改    部分变量、函数名标准化、将原Csd_CanConfig拆分成CanConfig和SjaConfig
            2012-02-02  V1.2    王鹤翔  修改    修改函数成功的返回值为0、修改中断服务程序
            2012-05-02  V1.3    徐佳谋  修改    修改中断服务程序
            2013-01-08  V1.4    王鹤翔  修改    修改了程序结构，支持多块板卡
            2013-06-25  V1.5    徐佳谋  增加    增加客户定制底板SJA1000映射访问函数
            2014-02-26  V1.6    徐佳谋  增加    增加错误门限寄存器，支持共享中断
            2014-08-01  V1.7    徐佳谋  增加    中断任务参数设置
            2015-06-23  V1.8    徐佳谋  增加    增加芯片在位检查
            2015-12-25  V1.9    徐佳谋  修改    修改程序结构
            2016-01-31  V2.0    徐佳谋  增加    增加多通道共享中断支持
********************************************************************/

#ifndef _ISASJALIB_H_
#define _ISASJALIB_H_

#include "rngLib.h"
#include "semLib.h"
#include "taskLib.h"

#pragma pack(1)

#define MAX_SJA_NUM                 8                       /* 设备数量最大值 */
#define CAN_FIFO_SIZE               (500 * CAN_FRAME_SIZE)  /* 驱动缓冲区大小 */
#define CAN_FRAME_SIZE              13
#define FPGA_BASE                   0xD600  /* FPGA基地址 */
#define FPGA_INT_STATE              1       /* 中断状态偏移 */

#define SUCCESS                       0
#define ERROR_BOARD_FAILURE         (-1)    /* 板卡异常 */
#define ERROR_PARAMETER_ILLEGAL     (-2)    /* 参数非法 */
#define ERROR_CONFIG_TIMEOUT        (-3)    /* 操作超时 */
#define ERROR_MAX_DEV               (-4)    /* 超过最大设备量 */
#define ERROR_INSTALL_ISR_FAIL      (-6)    /* 安装中断服务程序失败 */
#define ERROR_QUEUE_NOT_EMPTY       (-7)    /* 队列非空 */
#define ERROR_QUEUE_FULL            (-8)    /* 队列已满 */
#define ERROR_QUEUE_EMPTY           (-9)    /* 队列已空 */

typedef struct sja1000_config_s
{
    unsigned char uchMOD;       /* Mode Register */
    unsigned char uchCDR;       /* Clock Divider Register */
    unsigned char uchOCR;       /* Output Control Register */
    unsigned char uchBTR0;      /* Bus0 Timing Register */
    unsigned char uchBTR1;      /* Bus1 Timing Register */
    unsigned char uchACR[4];    /* Acceptance Code Register */
    unsigned char uchAMR[4];    /* Acceptance Mask Register */
    unsigned char uchIER;       /* Interrupt Enable Register */
    unsigned char uchRXERR;     /* RX Error Counter Register */
    unsigned char uchTXERR;     /* TX Error Counter Register */
    unsigned char uchEWLR;      /* Error Warning Limit Register */
} sja1000_config_t;

typedef struct
{
    unsigned char uchFF;
    unsigned char uchID[4];
    unsigned char uchDATA[8];
} Can_TPacket_t;

#ifdef __cplusplus
extern "C" {
#endif

int CAN_Open(int fd, unsigned short board, unsigned short base, int irq, int stack, int priority);
int CAN_Init(int fd, sja1000_config_t *pConfig);
int CAN_InstallCallBack(int fd, int (*CallBack)(int fd));
int CAN_SendMsg(int fd, Can_TPacket_t *packet);
int CAN_ReadMsg(int fd, unsigned char *uchBuffer, int nBytes);
int CAN_Close(int fd);
int CAN_ShareInterrupt(int *fds, int number, int irq);

#ifdef __cplusplus
}
#endif

#endif
