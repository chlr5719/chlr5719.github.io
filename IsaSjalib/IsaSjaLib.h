/********************************************************************
文件名称:   IsaSjaLib.h

文件功能:   实现基于ISA总线的SJA1000芯片驱动

文件说明:   在VxWorks下A3CSD板卡驱动基础上修改

当前版本:   V2.2

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
            2016-04-05  V2.1    徐佳谋  增加    增加flush函数
            2016-04-21  V2.2    王  明  修改    发送和初始化函数添加互斥体;添加IoctlIsaCan函数
********************************************************************/

#ifndef _ISASJALIB_H_
#define _ISASJALIB_H_

#pragma pack(1)

#define MAX_SJA_NUM                 8                       /* 设备数量最大值 */
#define CAN_RX_FIFO_SIZE            (5000 * CAN_FRAME_SIZE) /* 驱动缓冲区大小 */
#define CAN_TX_FIFO_SIZE            (13 * 64)
#define CAN_FRAME_SIZE              13
#define HAVE_FPGA                   1       /* 是否有FPGA保存中断状态 */

#if HAVE_FPGA
#define FPGA_MAPPING_TYPE           2       /* 1:与CAN寄存器方式一致 2:映射模式为I/O */
#define FPGA_BASE                   0x311   /* FPGA基地址 */
#define FPGA_INT_STATE              1       /* 中断状态偏移 */
#define FPGA_INT_STATE_MASK         0xff    /* 中断状态寄存器掩码 */
#endif

/* 驱动错误码 */
#define SUCCESS                     0
#define ERROR_BOARD_FAILURE         (-1)    /* 板卡异常 */
#define ERROR_PARAMETER_ILLEGAL     (-2)    /* 参数非法 */
#define ERROR_CONFIG_TIMEOUT        (-3)    /* 操作超时 */
#define ERROR_MAX_DEV               (-4)    /* 超过最大设备量 */
#define ERROR_INSTALL_ISR_FAIL      (-6)    /* 安装中断服务程序失败 */
#define ERROR_PARAMETER_IOCTL       (-10)    /* 安装中断服务程序失败 */

/* ioctl控制码 */
#define IOCTL_INIT_SJA              (1)
#define IOCTL_CAN_RESET_FIFO        (2)
#define SET_BAUD_RATE               (3)
#define SET_ACCEPT_FILTER           (4)

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
    unsigned char header;
    unsigned char buffer[12];
} sja1000_frame_t;

enum Baud_Rate
{
    BAUDRATE_1000K   = 0x0014,
    BAUDRATE_800K    = 0x0016,
    BAUDRATE_500K    = 0x001C,
    BAUDRATE_250K    = 0x011C,
};

enum FILTER_MODE
{
    SINGLE_FILTER    = 0x08,
    DUAL_FILTER      = 0xF7,
};

typedef struct
{
    enum FILTER_MODE MaskMode;
    unsigned char uchACR[4];    /* Acceptance Code Register */
    unsigned char uchAMR[4];    /* Acceptance Mask Register */
} sja1000_filter_t;


#ifdef __cplusplus
extern "C" {
#endif

int OpenIsaCan(int fd, unsigned short board, unsigned short base, int irq);
int IoctlIsaCan(int fd, int cmd, void *p);
int WriteIsaCan(int fd, sja1000_frame_t *pFrame);
int ReadIsaCan(int fd, sja1000_frame_t *pFrame);
int CloseIsaCan(int fd);

#ifdef __cplusplus
}
#endif

#endif
