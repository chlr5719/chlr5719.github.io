/********************************************************************
�ļ�����:   IsaSjaLib.h

�ļ�����:   ʵ�ֻ���ISA���ߵ�SJA1000оƬ����

�ļ�˵��:   ��VxWorks��A3CSD�忨�����������޸�

��ǰ�汾:   V2.0

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
********************************************************************/

#ifndef _ISASJALIB_H_
#define _ISASJALIB_H_

#include "rngLib.h"
#include "semLib.h"
#include "taskLib.h"

#pragma pack(1)

#define MAX_SJA_NUM                 8                       /* �豸�������ֵ */
#define CAN_FIFO_SIZE               (500 * CAN_FRAME_SIZE)  /* ������������С */
#define CAN_FRAME_SIZE              13
#define FPGA_BASE                   0xD600  /* FPGA����ַ */
#define FPGA_INT_STATE              1       /* �ж�״̬ƫ�� */

#define SUCCESS                       0
#define ERROR_BOARD_FAILURE         (-1)    /* �忨�쳣 */
#define ERROR_PARAMETER_ILLEGAL     (-2)    /* �����Ƿ� */
#define ERROR_CONFIG_TIMEOUT        (-3)    /* ������ʱ */
#define ERROR_MAX_DEV               (-4)    /* ��������豸�� */
#define ERROR_INSTALL_ISR_FAIL      (-6)    /* ��װ�жϷ������ʧ�� */
#define ERROR_QUEUE_NOT_EMPTY       (-7)    /* ���зǿ� */
#define ERROR_QUEUE_FULL            (-8)    /* �������� */
#define ERROR_QUEUE_EMPTY           (-9)    /* �����ѿ� */

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
