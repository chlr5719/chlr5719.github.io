/********************************************************************
�ļ�����:   IsaSjaLib.c

�ļ�����:   ʵ�ֻ���ISA���ߵ�SJA1000оƬ����

�ļ�˵��:   ��VxWorks��A3CSD�忨�����������޸�

��ǰ�汾:   V2.0

�޸ļ�¼��  2011-12-26  V1.0    ������  ����
            2012-01-20  V1.1    ������  �޸�    ���ֱ�������������׼������ԭCsd_CanConfig��ֳ�CanConfig��SjaConfig
            2012-02-02  V1.2    ������  �޸�    �޸ĺ����ɹ��ķ���ֵΪ0���޸��жϷ������
            2012-05-02  V1.3    ���ı  �޸�    �޸��жϷ������
            2013-01-08  V1.4    ������  �޸�    �޸��˳���ṹ��֧�ֶ��忨
            2013-06-25  V1.5    ���ı  ����    ���ӿͻ����Ƶװ�SJA1000ӳ����ʺ���
            2014-02-26  V1.6    ���ı  ����    �������޼Ĵ������ã�֧�ֹ����ж�
            2014-08-01  V1.7    ���ı  ����    �ж������������
            2015-06-23  V1.8    ���ı  ����    ����оƬ��λ���
            2015-12-25  V1.9    ���ı  �޸�    �޸ĳ���ṹ
            2016-01-31  V2.0    ���ı  ����    ���Ӷ�ͨ�������ж�֧��
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
    unsigned int  board;        /* �忨����ַ */
    unsigned long CanAddr;      /* �忨CAN�ڵ�ַ */
    unsigned int  CanIrq;       /* �忨CAN���ж� */
    int          (*CallBack)(int);/* Ӧ�ó���ص����� */
    RING_ID       CanRngID;     /* FIFOʹ��ID */
    SEM_ID        CanSemID;     /* �ź���ʹ��ID */
    int           CanTaskID;    /* ����ʹ��ID */
    int           OpenFlag;     /* ���豸�Ƿ�򿪱�־ */
} can_device_t;

#define MAPPING_TYPE        1       /* 1:MEM 2:IO 3:CUSTOM */
#define INT_NUM_IRQ0        0x20    /* �˴�ע�⵱����ʹ��PICģʽʱ��Ҫ�޸� */
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

can_device_t g_Devices[MAX_SJA_NUM]; /* �����豸�� */
static int s_fds[MAX_SJA_NUM];
static int s_number;

int tCanRecv(int fd);
void CAN_Interrupt(int fd);
void CAN_Interrupt2(int *fds);
unsigned char customRead(unsigned int seg, unsigned char offset);
void customWrite(unsigned int seg, unsigned char offset, unsigned char value);

/********************************************************************
��������:   CAN_Open

��������:   ����CSD�忨CAN�Ĵ�����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)
board           unsigned short  input           �忨����ַ
base            unsigned short  input           CAN����ַ
irq             int             input           CAN�˿�ʹ���жϺ�
stack           int             input           �жϺ�������ջ��С
priority        int             input           �жϺ�������ջ���ȼ�

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(CPU��BIOS����CSD�쳣)
            ERROR_PARAMETER_ILLEGAL   �жϲ�������

����˵��:   ����û�ж�EEPROM���в�����

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_Open(int fd, unsigned short board, unsigned short base, int irq, int stack, int priority)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
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
        return ERROR_PARAMETER_ILLEGAL;     /* �жϲ������󣬷���-2*/
    }

    pDevice->CanAddr = base;    /* CAN��ַ */
    pDevice->CanIrq = irq;      /* CAN�ж� */

    /* оƬ�ڰ��� */
    if (PEEKB(pDevice->CanAddr, 2) == 0xFF)
    {
        return ERROR_BOARD_FAILURE;
    }

    if (board != 0)
    {
        /* ʹ�ܴ��ں�CAN���ж� */
        POKEB(board, 2, 0x7f);
    }

    /* ��װ�жϷ������ */
    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(pDevice->CanIrq)), CAN_Interrupt, fd))
    {
        return ERROR_INSTALL_ISR_FAIL;
    }

    /* ��ʼ������ */
    pDevice->CanRngID = rngCreate(CAN_FIFO_SIZE);

    /* �����ź��� */
    pDevice->CanSemID = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* ��־�����Ѵ� */
    pDevice->OpenFlag = 1;

    /* ������������ */
    pDevice->CanTaskID = taskSpawn("tCanRecv", priority, 0, stack, tCanRecv, fd, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    /* ʹ���ж� */
    sysIntEnablePIC(pDevice->CanIrq);

    return SUCCESS;
}

/********************************************************************
��������:   CAN_Init

��������:   ����CSD�忨CAN�Ĵ�����

��������        ����                ����/���       ����
fd              int                 input           �豸�ļ�����(0-4)
pConfig         sja1000_config_t*   input           SJA��ʼ������

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������
            ERROR_CONFIG_TIMEOUT      ����RESETģʽ��ʱ
            ERROR_INSTALL_ISR_FAIL    ��װ�жϷ������ʧ��

����˵��:   ��SJA1000оƬ���г�ʼ��������

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_Init(int fd, sja1000_config_t *pConfig)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* ��ʼ����resetģʽ���� */
    POKEB(pDevice->CanAddr, 0, 0x01);

    /* ������ܽ���resetģʽ���ೢ�Լ��� */
    if((PEEKB(pDevice->CanAddr, 0) & 0x01) != 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* ����ʧ�� */
    }

    POKEB(pDevice->CanAddr, 31, pConfig->uchCDR);    /* 0xc4 set clock divider PELICAN MODE */
    POKEB(pDevice->CanAddr, 8, pConfig->uchOCR);     /* 0xda set output control */

    /* ����mode ������ */
    POKEB(pDevice->CanAddr, 0, pConfig->uchMOD | 0x01);
    POKEB(pDevice->CanAddr, 6, pConfig->uchBTR0);
    POKEB(pDevice->CanAddr, 7, pConfig->uchBTR1);

    /* �������޼Ĵ��� */
    POKEB(pDevice->CanAddr, 13, pConfig->uchEWLR);

    /* ������������ */
    POKEB(pDevice->CanAddr, 14, 0);
    POKEB(pDevice->CanAddr, 15, 0);

    /* ����code �� mask ����������չ��can��resetģʽ�µ�����ֵ����operateģʽ����Ч */
    POKEB(pDevice->CanAddr, 16, pConfig->uchACR[0]);   /* set code and mask */
    POKEB(pDevice->CanAddr, 17, pConfig->uchACR[1]);
    POKEB(pDevice->CanAddr, 18, pConfig->uchACR[2]);
    POKEB(pDevice->CanAddr, 19, pConfig->uchACR[3]);
    POKEB(pDevice->CanAddr, 20, pConfig->uchAMR[0]);
    POKEB(pDevice->CanAddr, 21, pConfig->uchAMR[1]);
    POKEB(pDevice->CanAddr, 22, pConfig->uchAMR[2]);
    POKEB(pDevice->CanAddr, 23, pConfig->uchAMR[3]);

    /* ��������ģʽ */
    POKEB(pDevice->CanAddr, 0, pConfig->uchMOD & 0xfe);

    /* ˯��ģʽ������ʱ���� */
    if((pConfig->uchMOD & 0x10) == 0x10)
    {
        taskDelay(sysClkRateGet());
        POKEB(pDevice->CanAddr, 0, pConfig->uchMOD);
    }

    POKEB(pDevice->CanAddr, 4, pConfig->uchIER); /* ʹ��������ж� */

    return SUCCESS; /* ���óɹ�����SUCCESS */
}

/********************************************************************
��������:   CAN_InstallCallBack

��������:   ��װ�ص�����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)
CallBack        int(*)(int)     input           �ص�������ڵ�ַ

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������

����˵��:   ���������յ��жϺ󣬻ᴥ���ڲ������ڲ��������ûص�������

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_InstallCallBack(int fd, int (*CallBack)(int fd))
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
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
��������:   CAN_SendMsg

��������:   ����CAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)
canpacket       Can_TPacket_t*  input           CAN�������ݰ�

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ��������� �� canpacket->uchFF����8
            ERROR_CONFIG_TIMEOUT      �����������޷�����

����˵��:   �����ò�ѯ��ʽ�������ݡ�

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_SendMsg(int fd, Can_TPacket_t *canpacket)
{
    int DataLength = 0;
    int i = 0;
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* �жϻ������Ƿ����� */
    if(((PEEKB(pDevice->CanAddr, 2) & 0x04)) == 0x00)
    {
        return ERROR_CONFIG_TIMEOUT;    /* ���;���״̬λû����λ */
    }

    /*�����ݰ������ڴ�*/
    POKEB(pDevice->CanAddr, 16, canpacket->uchFF);

    /*��չ֡*/
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
    else    /* ��׼֡ */
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

    /* �������ݰ� */
    POKEB(pDevice->CanAddr, 0x1, 0x01);

    return SUCCESS;
}

/********************************************************************
��������:   CAN_Drain

��������:   ����CAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������
            ERROR_CONFIG_TIMEOUT      ������ʱ

����˵��:   �˺������жϷ��������ʹ�ã��������ṩ���������ݷ���ringbuf�У�
            ��CAN_ReadMsg������ȡ��

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_Drain(int fd)
{
    int i = 0;
    unsigned char RecvData[CAN_FRAME_SIZE] = {0};
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    if((PEEKB(pDevice->CanAddr, 2) & 0x01) != 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* ������״̬λû����λ */
    }

    for(i = 0; i < CAN_FRAME_SIZE; i++)
    {
        RecvData[i] = PEEKB(pDevice->CanAddr, 16 + i);
    }

    /* �ͷŽ��ջ����� */
    POKEB(pDevice->CanAddr, 0x01, 0x04);

    if(rngBufPut(pDevice->CanRngID, (char *)RecvData, sizeof(RecvData)) == 0)   /* rngBuffer�� */
    {
        /* ���FIFO���� ��ʾ��Ϣ */
        logMsg("ring buffer is overflow!\n", 0, 0, 0, 0, 0, 0);

        /* ���FIFO */
        rngFlush(pDevice->CanRngID);
    }

    return SUCCESS;
}

/********************************************************************
��������:   CAN_Interrupt2

��������:   CAN�����жϷ������

��������        ����            ����/���       ����
fds             int*            input           �豸�ļ�����������

����ֵ  :   ��

����˵��:   ��CAN����Ҫ�����жϵ�ʱ�򣬰�װ���жϷ������

�޸ļ�¼:   2013-01-11      ������  ����
            2016-01-31      ���ı  �޸�
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
        /* ��ȡ�ж�״̬�Ĵ��� */
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
            /* ��ֹ�����쳣 */
            fd = fds[i] % MAX_SJA_NUM;
            pDevice = &g_Devices[fd];

            /* ��ȡCAN�ж�״̬ */
            uch = PEEKB(pDevice->CanAddr, 3) & 0xff;
#if 0
            logMsg("i = %d uch = 0x%02X CanAddr = 0x%04X\n", i, uch, pDevice->CanAddr, 0, 0, 0);
#endif
            if(uch == 0)
            {
                continue ;
            }

            /* �����豸�ж� */
            if(uch & 0x80)              /* ���ߴ��� */
            {
                /* �Զ���λSJAоƬ */
                uch2 = PEEKB(pDevice->CanAddr, 0);
                POKEB(pDevice->CanAddr, 0, uch2 | 0x01);
                POKEB(pDevice->CanAddr, 0, uch2 & 0xfe);
                logMsg("can%d Bus Error!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x40)              /* �ٲö�ʧ */
            {
                logMsg("can%d Arbitration Lost!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x20)              /* �������� */
            {
                logMsg("can%d Error Passive!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x08)              /* ������� */
            {
                logMsg("can%d Data Overrun!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x04)              /* ���󾯸� */
            {
                logMsg("can%d Error Warning!\n", fd, 0, 0, 0, 0, 0);
                break;
            }

            if(uch & 0x01)              /* �����ж� */
            {
                CAN_Drain(fd);
            }

            semGive(pDevice->CanSemID);    /* �ͷ��ź�����ִ�лص����� */
        }
    } /* end for(;;) */

    return;
}

/********************************************************************
��������:   CAN_Interrupt

��������:   CAN�жϷ������

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)

����ֵ  :   ��

����˵��:   Ŀǰֻ�Խ����жϽ��д��������ж�ֻ����

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
void CAN_Interrupt(int fd)
{
    unsigned char uch = 0;
    unsigned char uch2 = 0;
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    while(PEEKB(pDevice->CanAddr, 3) != 0)
    {
        uch = PEEKB(pDevice->CanAddr, 3);
        if(uch & 0x80)           /* ���ߴ��� */
        {
            /* �Զ���λSJAоƬ */
            uch2 = PEEKB(pDevice->CanAddr, 0);
            POKEB(pDevice->CanAddr, 0, uch2 | 0x01);
            POKEB(pDevice->CanAddr, 0, uch2 & 0xfe);
            logMsg("can%d Bus Error!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x40)           /* �ٲö�ʧ */
        {
            logMsg("can%d Arbitration Lost!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x20)           /* �������� */
        {
            logMsg("can%d Error Passive!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x08)           /* ������� */
        {
            logMsg("can%d Data Overrun!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x04)           /* ���󾯸� */
        {
            logMsg("can%d Error Warning!\n", fd, 0, 0, 0, 0, 0);
            break;
        }

        if(uch & 0x01)           /* �����ж� */
        {
            CAN_Drain(fd);     /* ��ȡ���������ݷ���fifo */
        }
    }

    semGive(pDevice->CanSemID);     /* �ͷ��ź�����ִ�лص����� */

    return;
}

/********************************************************************
��������:   CAN_ReadMsg

��������:   ��ȡCAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)
uchBuffer       unsigned char*  output          ����������
nBytes          int             input           ����������

����ֵ  :   ������������ж������ֽ�����

����˵��:   ����������յ����ݺ���ȴ��������������������ֻ�����������
            �ж�ȡ���ݡ�

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_ReadMsg(int fd, unsigned char *uchBuffer, int nBytes)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    return rngBufGet(pDevice->CanRngID, (char *)uchBuffer, nBytes);
}

/********************************************************************
��������:   CAN_Close

��������:   �ر�CAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)

����ֵ  :   SUCCESS                   �����ɹ���FIFO�ѿ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������

����˵��:   �ر��жϣ��ͷ��ź�����

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int CAN_Close(int fd)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];
    if(pDevice->OpenFlag == 0)
    {
        return SUCCESS;
    }

    POKEB(pDevice->CanAddr, 0, 0x01);    /* ʹ����CANͨ�����ڸ�λ״̬ ��ֹ�����رպ� ͨ����Ȼ�������� */
    sysIntDisablePIC(pDevice->CanIrq);

    semDelete(pDevice->CanSemID);        /* ɾ���ź��� */
    rngDelete(pDevice->CanRngID);        /* ɾ��ringbuf */

    pDevice->OpenFlag = 0;

    return SUCCESS;
}

/********************************************************************
��������:   tCanRecv

��������:   CAN��������

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-4)

����ֵ  :   0:�ɹ�

����˵��:   �����رպ��������Զ��˳�

�޸ļ�¼:   2013-01-08      ������  ����
********************************************************************/
int tCanRecv(int fd)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    for (;;)
    {
        /* �����ȴ��жϷ�����򴥷��ź��� */
        if (semTake(pDevice->CanSemID, WAIT_FOREVER) == ERROR)
        {
            return ;  /* �ź�����ɾ��ʱ ���򲻻������ѭ�� */
        }

        if (pDevice->CallBack != NULL)
        {
            (*pDevice->CallBack)(fd);
        }
    }

    return 0;
}

/********************************************************************
��������:   customRead

��������:   ��ȡSJA1000�Ĵ���

��������        ����            ����/���           ����
seg             unsigned int    input               SJA1000ӳ���ַ
offset          unsigned char   input               �Ĵ���ƫ��

����ֵ  :   ��ȡ�ļĴ���ֵ(8bit)

����˵��:   �ͻ����Ƶװ�SJA1000�Ĵ�������ӳ�䷽ʽ���ʡ�
            ͨ�������Ĵ���ӳ��SJA1000оƬ�Ĵ��������������ݡ�

�޸ļ�¼:   2013-06-25  ���ı  ����
********************************************************************/
unsigned char customRead(unsigned int seg, unsigned char offset)
{
    sysOutByte(seg, offset);
    return sysInByte(seg + 1);
}

/********************************************************************
��������:   customWrite

��������:   д��SJA1000�Ĵ���

��������        ����            ����/���           ����
seg             unsigned int    input               SJA1000ӳ���ַ
offset          unsigned char   input               �Ĵ���ƫ��
value           unsigned char   input               д��Ĵ���ֵ

����ֵ  :   ��

����˵��:   �ͻ����Ƶװ�SJA1000�Ĵ�������ӳ�䷽ʽ���ʡ�
            ͨ�������Ĵ���ӳ��SJA1000оƬ�Ĵ��������������ݡ�

�޸ļ�¼:   2013-06-25  ���ı  ����
********************************************************************/
void customWrite(unsigned int seg, unsigned char offset, unsigned char value)
{
    sysOutByte(seg, offset);
    sysOutByte(seg + 1, value);

    return ;
}

/********************************************************************
��������:   CAN_ShareInterrupt

��������:   ���ö�ͨ��CAN�����ж�

��������        ����            ����/���           ����
fds             int*            input               �豸�ļ�����������
number          int             input               �����ж��豸��(2-8)
irq             int             input               �����жϺ�

����ֵ  :   ��

����˵��:   �����CANͨ������һ���ж�ʱ���ô˺��������豸

�޸ļ�¼:   2016-01-31  ���ı  ����
********************************************************************/
int CAN_ShareInterrupt(int *fds, int number, int irq)
{
    int i = 0;
    int fd = 0;
    can_device_t *pDevice = NULL;

    for (i = 0; i < number; i++)
    {
        /* ��ֹ�����쳣 */
        fd = fds[i] % MAX_SJA_NUM;
        pDevice = &g_Devices[fd];

        if(pDevice->OpenFlag == 0)
        {
            return ERROR_BOARD_FAILURE;
        }
    }

    /* ������� */
    memcpy(s_fds, fds, number * sizeof(int));
    s_number = number;

    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(irq)), CAN_Interrupt2, (int)s_fds))
    {
        return ERROR_INSTALL_ISR_FAIL;
    }

    return 0;
}
