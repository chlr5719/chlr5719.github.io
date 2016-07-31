/********************************************************************
�ļ�����:   IsaSjaLib.c

�ļ�����:   ʵ�ֻ���ISA���ߵ�SJA1000оƬ����

�ļ�˵��:   ��VxWorks��A3CSD�忨�����������޸�

��ǰ�汾:   V2.2

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
            2016-04-05  V2.1    ���ı  ����    ����flush����
            2016-04-21  V2.2    ��  ��  �޸�    ���ͺͳ�ʼ��������ӻ�����;��IinitIsaCan��ΪIoctlIsaCan����
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
    unsigned int  board;        /* �忨����ַ */
    unsigned long CanAddr;      /* �忨CAN�ڵ�ַ */
    unsigned int  CanIrq;       /* �忨CAN���ж� */
    int           OpenFlag;     /* ���豸�Ƿ�򿪱�־ */
    RING_ID       CanRngID;     /* ����FIFOʹ��ID */
    RING_ID       CanRngID2;    /* ����FIFOʹ��ID */
    SEM_ID        ReadSemID;    /* �����ݺ����ź���ID */
    SEM_ID        mutex;        /* ����ʹ�û����� */
    SEM_ID        SendSemID;    /* ����ʹ���ź��� */
    SEM_ID        SendOkSemID;  /* �������ʹ���ź��� */
    int           tCanTaskID;   /* ����ʹ��ID */
    MSG_Q_ID      msg;          /* ��������ʹ����Ϣ */
} can_device_t;

#define DEBUG               0       /* ���Ժ� */
#define DEBUG1              1
#define SJA_MAPPING_TYPE    1       /* 1:MEM 2:IO 3:CUSTOM */
#define INT_NUM_IRQ0        0x20    /* �˴�ע�⵱����ʹ��PICģʽʱ��Ҫ�޸� */
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

can_device_t g_Devices[MAX_SJA_NUM]; /* �����豸�� */

/* �����ж��豸��Ϣ */
typedef struct share_info_s
{
    int fds[MAX_SJA_NUM];
    int number;
    int irq; /* ������жϺ� */
    int UsingFlag; /* �Ƿ�����ʹ�� */
} share_info_t;

share_info_t s_infos[MAX_SJA_NUM] = {0};

void CanInterrupt(int fd);
void CanInterrupt2(int group);
unsigned char CustomRead(unsigned int seg, unsigned char offset);
void CustomWrite(unsigned int seg, unsigned char offset, unsigned char value);
int tTaskSend(MSG_Q_ID msg);

/********************************************************************
��������:   OpenIsaCan

��������:   ��CANͨ��

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)
board           unsigned short  input           �忨����ַ(û��ʱ��0)
base            unsigned short  input           CAN����ַ
irq             int             input           CAN�˿�ʹ���жϺ�


����ֵ  :   SUCCESS                     �����ɹ�
            ERROR_BOARD_FAILURE         �忨�쳣(CPU��BIOS����CSD�쳣)
            ERROR_PARAMETER_ILLEGAL     �жϲ�������
            ERROR_INSTALL_ISR_FAIL      ע���ж�ʧ��

����˵��:   ����û�ж�EEPROM���в�����

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
********************************************************************/
int OpenIsaCan(int fd, unsigned short board, unsigned short base, int irq)
{
    int i = 0;
    int j = 0;

    can_device_t *pDevice = NULL;
    can_device_t *pDeviceIndex = NULL;

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

    if (board != 0)
    {
        /* ʹ�ܴ��ں�CAN���ж� */
        sysOutByte(board + 2, 0x7f);
    }

    /* оƬ�ڰ��� */
    if (PEEKB(pDevice->CanAddr, 2) == 0xFF)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* ��װ�жϷ������ */
    if(0 != intConnect(INUM_TO_IVEC(INT_VEC_GET(pDevice->CanIrq)), CanInterrupt, fd))
    {
        return ERROR_INSTALL_ISR_FAIL;
    }

    /* ��ʼ�����ն��� */
    pDevice->CanRngID = rngCreate(CAN_RX_FIFO_SIZE);

    /* ��ʼ�����Ͷ��� */
    pDevice->CanRngID2 = rngCreate(CAN_TX_FIFO_SIZE);

    /* ������ֵ�ź��� */
    pDevice->ReadSemID = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* ������ֵ�ź��� */
    pDevice->SendSemID = semBCreate(SEM_Q_FIFO, SEM_FULL);

    /* ������ֵ�ź��� */
    pDevice->SendOkSemID = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* ���������� */
    pDevice->mutex = semMCreate(SEM_Q_FIFO | SEM_DELETE_SAFE);

    /* ������Ϣ���� */
    pDevice->msg = msgQCreate(100, 32, MSG_Q_PRIORITY);

    /* ������������ */
    pDevice->tCanTaskID = taskSpawn("tTaskSend", 100, (VX_SUPERVISOR_MODE | VX_UNBREAKABLE), 8192, tTaskSend, (int)pDevice->msg, 0, 0, 0, 0, 0, 0, 0, 0, 0);


    /* ��־�����Ѵ� */
    pDevice->OpenFlag = 1;

    /* ʹ���ж� */
    sysIntEnablePIC(pDevice->CanIrq);

    /* ��ѯ�Ƿ�Ϊ�����ж��������ע��CanInterrupt2 */
    SetShareInterrupt(fd,irq);

    return SUCCESS;
}

/********************************************************************
��������:   InitIsaCan

��������:   ����CSD�忨CAN�Ĵ�����

��������        ����                ����/���       ����
fd              int                 input           �豸�ļ�����(0-8)
pConfig         sja1000_config_t*   input           SJA��ʼ�����ýṹ��

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������
            ERROR_CONFIG_TIMEOUT      ����RESETģʽ��ʱ

����˵��:   ��SJA1000оƬ���г�ʼ��������

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
            2016-04-21      ��  ��  �޸�    ���ӻ��⣬����������Դ
********************************************************************/
int InitIsaCan(int fd, sja1000_config_t *pConfig)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* ��֤���ʻ��� */
    semTake(pDevice->mutex, WAIT_FOREVER);

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

    /* ˯��ģʽ������ʱ���� */
    if((pConfig->uchMOD & 0x10) == 0x10)
    {
        taskDelay(sysClkRateGet());
        POKEB(pDevice->CanAddr, 0, pConfig->uchMOD);
    }

    POKEB(pDevice->CanAddr, 4, pConfig->uchIER); /* ʹ��������ж� */

    /* ��������ģʽ */
    POKEB(pDevice->CanAddr, 0, pConfig->uchMOD & 0xfe);

    if((PEEKB(pDevice->CanAddr, 0) & 0x01) == 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* ����ʧ�� */
    }

    /* �ͷŻ��� */
    semGive(pDevice->mutex);

    return SUCCESS; /* ���óɹ�����SUCCESS */
}

/********************************************************************
��������:   WriteIsaCan

��������:   ����CAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)
pFrame          sja1000_frame_t*input           CAN�������ݰ�

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ��������� �� pFrame->uchFF����8
            ERROR_CONFIG_TIMEOUT      �����������޷�����

����˵��:   �����ò�ѯ��ʽ�������ݡ�

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
            2016-04-21      ��  ��  �޸�    ���ӻ��⣬����������Դ
********************************************************************/
int WriteIsaCan(int fd, sja1000_frame_t *pFrame)
{
    int i = 0;
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
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

    /* ��֤���ʻ��� */
    semTake(pDevice->mutex, WAIT_FOREVER);

    /* �ȴ������п��� */
    semTake(pDevice->SendSemID, WAIT_FOREVER);

    /* �������ݷ���FIFO */
    rngBufPut(pDevice->CanRngID2, (char *)pFrame, sizeof(sja1000_frame_t));

    /* ������������ */
    msgQSend(pDevice->msg, (char *)&pDevice, sizeof(pDevice), NO_WAIT, MSG_PRI_URGENT);

    /* �жϻ������Ƿ��п��� */
    if (rngFreeBytes(pDevice->CanRngID2) >= sizeof(sja1000_frame_t))
    {
        /* ʹ�ܷ��� */
        semGive(pDevice->SendSemID);
    }

    /* �ͷŻ��� */
    semGive(pDevice->mutex);

    return SUCCESS;
}

/********************************************************************
��������:   DrainCan

��������:   ����CAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)

����ֵ  :   SUCCESS                   �����ɹ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������
            ERROR_CONFIG_TIMEOUT      ������ʱ

����˵��:   �˺������жϷ��������ʹ�ã��������ṩ���������ݷ���ringbuf�У�
            ��ReadIsaCan������ȡ��

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
********************************************************************/
int DrainCan(int fd)
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
��������:   CanInterrupt2

��������:   CAN�����жϷ������

��������        ����            ����/���       ����
group           int             input               �����ж��豸���(0-8)

����ֵ  :   ��

����˵��:   ��CAN����Ҫ�����жϵ�ʱ�򣬰�װ���жϷ������

�޸ļ�¼:   2013-01-11      ������  ����
            2016-01-31      ���ı  �޸�
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
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
            /* ��ֹ�����쳣 */
            fd = s_infos[group].fds[i] % MAX_SJA_NUM;
            pDevice = &g_Devices[fd];

            /* ��ȡCAN�ж�״̬ */
            uch = PEEKB(pDevice->CanAddr, 3) & 0xff;
#if DEBUG
            logMsg("i = %d uch = 0x%02X CanAddr = 0x%04X\n", i, uch, pDevice->CanAddr, 0, 0, 0);
#endif /* endif DEBUG*/
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

                /* �������,��ֹ���ߴ����һֱ���� */
                semGive(pDevice->SendOkSemID);

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
                DrainCan(fd);
            }

            if (uch & 0x02)             /* ������� */
            {
                semGive(pDevice->SendOkSemID);
            }

            semGive(pDevice->ReadSemID);/* �ͷ��ź��� */
        }
    } /* end for(;;) */

    return;
}

/********************************************************************
��������:   CanInterrupt

��������:   CAN�жϷ������

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)

����ֵ  :   ��

����˵��:   Ŀǰֻ�Խ����жϽ��д��������ж�ֻ����

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
********************************************************************/
void CanInterrupt(int fd)
{
    unsigned char uch = 0;
    unsigned char uch2 = 0;
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    for(;;)
    {
        uch = PEEKB(pDevice->CanAddr, 3);
        if (uch == 0x00)
        {
            break;
        }

        if(uch & 0x80)           /* ���ߴ��� */
        {
            /* �Զ���λSJAоƬ */
            uch2 = PEEKB(pDevice->CanAddr, 0);
            POKEB(pDevice->CanAddr, 0, uch2 | 0x01);
            POKEB(pDevice->CanAddr, 0, uch2 & 0xfe);

            /* �������,��ֹ���ߴ����һֱ���� */
            semGive(pDevice->SendOkSemID);

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
            DrainCan(fd);        /* ��ȡ���������ݷ���fifo */
        }

        if (uch & 0x02)          /* ������� */
        {
            semGive(pDevice->SendOkSemID);
        }
    }

    semGive(pDevice->ReadSemID); /* �ͷ��ź�����ִ�лص����� */

    return;
}

/********************************************************************
��������:   ReadIsaCan

��������:   ��ȡCAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)
pFrame          sja1000_frame_t*output          �������ݻ�����

����ֵ  :   �ɹ�:���ض�ȡ���ֽ���  ʧ��:���ش�����

����˵��:   ����������յ����ݺ���ȴ��������������������ֻ�����������
            �ж�ȡ���ݡ�

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-11      ��  ��  �޸�    ��ȡ�ź�����ȡ���FIFO���ݣ�ȡ���ص���ȡ��ʽ
********************************************************************/
int ReadIsaCan(int fd, sja1000_frame_t *pFrame)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* ��FIFO���պ�һֱ�ȴ��ź��� */
    if(rngIsEmpty(pDevice->CanRngID))
    {
        /* ��ȡ�ź��� */
        if (semTake(pDevice->ReadSemID, WAIT_FOREVER) == -1)
        {
            return -1;
        }
    }

    return rngBufGet(pDevice->CanRngID, (char *)pFrame, sizeof(sja1000_frame_t));
}

/********************************************************************
��������:   CloseIsaCan

��������:   �ر�CAN����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)

����ֵ  :   SUCCESS                   �����ɹ���FIFO�ѿ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)
            ERROR_PARAMETER_ILLEGAL   ���ݽṹ���������

����˵��:   �ر��жϣ��ͷ��ź�����

�޸ļ�¼:   2013-01-08      ������  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
********************************************************************/
int CloseIsaCan(int fd)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];
    if(pDevice->OpenFlag == 0)
    {
        return SUCCESS;
    }

    POKEB(pDevice->CanAddr, 0, 0x01);     /* ʹ����CANͨ�����ڸ�λ״̬ ��ֹ�����رպ� ͨ����Ȼ�������� */
    sysIntDisablePIC(pDevice->CanIrq);

    taskDelete(pDevice->tCanTaskID);
    
    semDelete(pDevice->ReadSemID);        /* ɾ���ź��� */

    semDelete(pDevice->SendSemID);        /* ɾ���ź��� */

    semDelete(pDevice->SendOkSemID);      /* ɾ���ź��� */

    semDelete(pDevice->mutex);            /* ɾ�������ź��� */

    rngDelete(pDevice->CanRngID);         /* ɾ��ringbuf */
    
    rngDelete(pDevice->CanRngID2);        /* ɾ��ringbuf */

    msgQDelete(pDevice->msg);

    
    pDevice->OpenFlag = 0;

    memset(s_infos, 0, sizeof(s_infos));

    return SUCCESS;
}

/********************************************************************
��������:   CustomRead

��������:   ��ȡSJA1000�Ĵ���

��������        ����            ����/���           ����
seg             unsigned int    input               SJA1000ӳ���ַ
offset          unsigned char   input               �Ĵ���ƫ��

����ֵ  :   ��ȡ�ļĴ���ֵ(8bit)

����˵��:   �ͻ����Ƶװ�SJA1000�Ĵ�������ӳ�䷽ʽ���ʡ�
            ͨ�������Ĵ���ӳ��SJA1000оƬ�Ĵ��������������ݡ�

�޸ļ�¼:   2013-06-25  ���ı  ����
            2016-04-14  ��  ��  �޸�    ����˾����淶�޸ĺ�������
********************************************************************/
unsigned char CustomRead(unsigned int seg, unsigned char offset)
{
    sysOutByte(seg, offset);
    return sysInByte(seg + 1);
}

/********************************************************************
��������:   CustomWrite

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
void CustomWrite(unsigned int seg, unsigned char offset, unsigned char value)
{
    sysOutByte(seg, offset);
    sysOutByte(seg + 1, value);

    return ;
}

/********************************************************************
��������:   FlushIsaCan

��������:   ���������

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)

����ֵ  :   SUCCESS                   �����ɹ���FIFO�ѿ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)

����˵��:   ���оƬ�������������

�޸ļ�¼:   2016-04-05      ���ı  ����
            2016-04-14      ��  ��  �޸�    ����˾����淶�޸ĺ�������
            2016-04-21      ��  ��  �޸�    ���ӻ��⣬����������Դ
********************************************************************/
int FlushIsaCan(int fd)
{
    can_device_t *pDevice = NULL;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];
    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* ��֤���ʻ��� */
    semTake(pDevice->mutex, WAIT_FOREVER);

    POKEB(pDevice->CanAddr, 1, 0x04);

    rngFlush(pDevice->CanRngID);

    /* ʹ�ܷ��� */
    semGive(pDevice->SendSemID);
    semGive(pDevice->SendOkSemID);

    /* �ͷŻ��� */
    semGive(pDevice->mutex);

    return SUCCESS;
}

/********************************************************************
��������:   SetAcceptFilter

��������:   ���ý����˲���

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)

����ֵ  :   SUCCESS                   �����ɹ���FIFO�ѿ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)

����˵��:   ���ý����˲���

�޸ļ�¼:   2016-04-05      ���ı  ����
********************************************************************/
int SetAcceptFilter(int fd, sja1000_filter_t* pSjaMask)
{
    can_device_t *pDevice = NULL;
    unsigned char uchMode = 0;
    unsigned char uch = 0;

    /* ��ֹ�����쳣 */
    fd %= MAX_SJA_NUM;
    pDevice = &g_Devices[fd];

    if(pDevice->OpenFlag == 0)
    {
        return ERROR_BOARD_FAILURE;
    }

    /* ��֤���ʻ��� */
    semTake(pDevice->mutex, WAIT_FOREVER);

    /* ��ȡ��ǰģʽ */
    uchMode = PEEKB(pDevice->CanAddr, 0);
    POKEB(pDevice->CanAddr, 0, (uchMode | 0x01));

    /* ������ܽ���reset���ر��� */
    if((PEEKB(pDevice->CanAddr, 0) & 0x01) != 0x01)
    {
        return ERROR_CONFIG_TIMEOUT; /* ����ʧ�� */
    }


    /* ���õ��˲���˫�˲� */
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

    /* ����code �� mask ����������չ��can��resetģʽ�µ�����ֵ����operateģʽ����Ч */
    POKEB(pDevice->CanAddr, 16, pSjaMask->uchACR[0]);
    POKEB(pDevice->CanAddr, 17, pSjaMask->uchACR[1]);
    POKEB(pDevice->CanAddr, 18, pSjaMask->uchACR[2]);
    POKEB(pDevice->CanAddr, 19, pSjaMask->uchACR[3]);
    POKEB(pDevice->CanAddr, 20, pSjaMask->uchAMR[0]);
    POKEB(pDevice->CanAddr, 21, pSjaMask->uchAMR[1]);
    POKEB(pDevice->CanAddr, 22, pSjaMask->uchAMR[2]);
    POKEB(pDevice->CanAddr, 23, pSjaMask->uchAMR[3]);

    /* ��������ģʽ */
    uchMode = PEEKB(pDevice->CanAddr, 0);
    POKEB(pDevice->CanAddr, 0, (uchMode & 0xfe) );

    if((PEEKB(pDevice->CanAddr, 0) & 0x01) == 0x01)
    {
        return ERROR_CONFIG_TIMEOUT;    /* ����ʧ�� */
    }

    /* �ͷŻ��� */
    semGive(pDevice->mutex);

    return SUCCESS; /* ���óɹ�����SUCCESS */
}

/********************************************************************
��������:   SetBaudRate

��������:   ���ò�����

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)

����ֵ  :   SUCCESS                   �����ɹ���FIFO�ѿ�
            ERROR_BOARD_FAILURE       �忨�쳣(�忨δ��)

����˵��:   ���ý����˲���

�޸ļ�¼:   2016-04-05      ���ı  ����
********************************************************************/
int SetBaudRate(int fd, enum Baud_Rate baud)
{
    return 0;
}

/********************************************************************
��������:   IoctlIsaCan

��������:   ʵ�ֲ�ͬ���ܵ�������

��������        ����            ����/���           ����
fd              int             input               �豸������
cmd             int             input               ������
p               void*           input/output        �����ֶ�Ӧ����

����ֵ  :   0:�ɹ� ��0:ʧ��

����˵��:   ����ͨ����ͬ���������������ʵ�ֲ�ͬ�Ĺ���.
            CMD                             FUNCTION
            OCTL_INIT_SJA��                 ��ʼ��оƬ
            IOCTL_QUERY_SEND_RESULT��       ��ѯ���ͽ��
            IOCTL_CAN_RESET_FIFO��          ���FIFO

�޸ļ�¼:   2016-04-21  ��  ��  ����
********************************************************************/
int IoctlIsaCan(int fd, int cmd, void *p)
{
    can_device_t *pDevice = NULL;
    int ret = 0;

    /* ȷ���豸��Ч */
    fd = fd > (MAX_SJA_NUM - 1) ? 0 : fd;
    pDevice = &g_Devices[fd];

    /* ���������� */
    switch(cmd & 0x00FF)
    {
    case IOCTL_INIT_SJA:                        /* ��ʼ��SJA������ */
        ret = InitIsaCan(fd, (sja1000_config_t *)p);
        break;

    case IOCTL_CAN_RESET_FIFO:                  /* ���FIFO */
        ret = FlushIsaCan(fd);
        break;

    case SET_BAUD_RATE:                         /* ���ò����� */
        /* ret = SetBaudRate(fd, *p); */
        break;

    case SET_ACCEPT_FILTER:
        ret = SetAcceptFilter(fd, (sja1000_filter_t*)p); /* ���������˲��� */
        break;

    default:
        ret = ERROR_PARAMETER_IOCTL;
        break;
    }

    return ret;
}

/********************************************************************
��������:   SetShareInterrupt

��������:   ��ѯע�Ṳ���ж�

��������        ����            ����/���       ����
fd              int             input           �豸�ļ�����(0-8)
irq             int             input           CAN�˿�ʹ���жϺ�

����ֵ  :   SUCCESS                     �����ɹ�

����˵��:   ��ѯע�Ṳ���жϣ���ѯ�Ƿ�Ϊ�����ж��������ע��CanInterrupt2

�޸ļ�¼:   2016-04-21  ������  ����
********************************************************************/
int SetShareInterrupt(int fd, int irq)
{
    int i = 0;
    int j = 0;
    int ret = -1;

    can_device_t *pDeviceIndex = NULL;

    /* ����g_Devices���� */
    for(i = 0; i < MAX_SJA_NUM; i++)
    {
        pDeviceIndex = &g_Devices[i];

        if((pDeviceIndex -> OpenFlag == 1) && (pDeviceIndex -> CanIrq == irq))
        {
            /* ��������s_infos�鿴���жϺ��Ƿ��Ѿ����� */
            for(j = 0; j < MAX_SJA_NUM; j++)
            {
                /* ���ж��Ѿ������� */
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
                    /* ��һ�β�ѯ�����жϺţ���������Ϊ��Ӧ��fd������s_infos[j].fds[0]�� */
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
��������:   tTaskSend

��������:   ���ݷ�������

��������        ����            ����/���           ����
msg             MSG_Q_ID        input               ����ʹ�õ���Ϣ����

����ֵ  :   0:�ɹ� ��0:ʧ��

����˵��:   ʹ�����������ݣ����Է�ֹ���ַ������ȼ��������⡣

�޸ļ�¼:   2014-08-19  ���ı  ����
********************************************************************/
int tTaskSend(MSG_Q_ID msg)
{
    int i = 0;
    int DataLength = 0;
    can_device_t *pDevice = NULL;
    sja1000_frame_t frame;

    for (;;)
    {
        /* ʹ����Ϣ���� */
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

            /*�����ݰ������ڴ�*/
            POKEB(pDevice->CanAddr, 16, frame.header);

            for(i = 0; i < 12; i++)
            {
                POKEB(pDevice->CanAddr, 17 + i, frame.buffer[i]);
            }

            /* �������� ʹ����ֹ���� */
            POKEB(pDevice->CanAddr, 0x1, 0x03);

            /* �ȴ�����������,��������жϴ��� */
            semTake(pDevice->SendOkSemID, WAIT_FOREVER);
        }

        /* �жϻ������Ƿ��п��� */
        if (rngFreeBytes(pDevice->CanRngID2) >= sizeof(sja1000_frame_t))
        {
            /* ʹ�ܷ��� */
            semGive(pDevice->SendSemID);
        }
    }

    return 0;
}
