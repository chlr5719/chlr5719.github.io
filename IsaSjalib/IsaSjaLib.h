/********************************************************************
�ļ�����:   IsaSjaLib.h

�ļ�����:   ʵ�ֻ���ISA���ߵ�SJA1000оƬ����

�ļ�˵��:   ��VxWorks��A3CSD�忨�����������޸�

��ǰ�汾:   V2.2

�޸ļ�¼��  2011-12-26  V1.0    ������  ����
            2012-01-20  V1.1    ������  �޸�    ���ֱ�������������׼������ԭCsd_CanConfig��ֳ�CanConfig��SjaConfig
            2012-02-02  V1.2    ������  �޸�    �޸ĺ����ɹ��ķ���ֵΪ0���޸��жϷ������
            2012-05-02  V1.3    ���ı  �޸�    �޸��жϷ������
            2013-01-08  V1.4    ������  �޸�    �޸��˳���ṹ��֧�ֶ��忨
            2013-06-25  V1.5    ���ı  ����    ���ӿͻ����Ƶװ�SJA1000ӳ����ʺ���
            2014-02-26  V1.6    ���ı  ����    ���Ӵ������޼Ĵ�����֧�ֹ����ж�
            2014-08-01  V1.7    ���ı  ����    �ж������������
            2015-06-23  V1.8    ���ı  ����    ����оƬ��λ���
            2015-12-25  V1.9    ���ı  �޸�    �޸ĳ���ṹ
            2016-01-31  V2.0    ���ı  ����    ���Ӷ�ͨ�������ж�֧��
            2016-04-05  V2.1    ���ı  ����    ����flush����
            2016-04-21  V2.2    ��  ��  �޸�    ���ͺͳ�ʼ��������ӻ�����;���IoctlIsaCan����
********************************************************************/

#ifndef _ISASJALIB_H_
#define _ISASJALIB_H_

#pragma pack(1)

#define MAX_SJA_NUM                 8                       /* �豸�������ֵ */
#define CAN_RX_FIFO_SIZE            (5000 * CAN_FRAME_SIZE) /* ������������С */
#define CAN_TX_FIFO_SIZE            (13 * 64)
#define CAN_FRAME_SIZE              13
#define HAVE_FPGA                   1       /* �Ƿ���FPGA�����ж�״̬ */

#if HAVE_FPGA
#define FPGA_MAPPING_TYPE           2       /* 1:��CAN�Ĵ�����ʽһ�� 2:ӳ��ģʽΪI/O */
#define FPGA_BASE                   0x311   /* FPGA����ַ */
#define FPGA_INT_STATE              1       /* �ж�״̬ƫ�� */
#define FPGA_INT_STATE_MASK         0xff    /* �ж�״̬�Ĵ������� */
#endif

/* ���������� */
#define SUCCESS                     0
#define ERROR_BOARD_FAILURE         (-1)    /* �忨�쳣 */
#define ERROR_PARAMETER_ILLEGAL     (-2)    /* �����Ƿ� */
#define ERROR_CONFIG_TIMEOUT        (-3)    /* ������ʱ */
#define ERROR_MAX_DEV               (-4)    /* ��������豸�� */
#define ERROR_INSTALL_ISR_FAIL      (-6)    /* ��װ�жϷ������ʧ�� */
#define ERROR_PARAMETER_IOCTL       (-10)    /* ��װ�жϷ������ʧ�� */

/* ioctl������ */
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
