/********************************************************************
�ļ�����:   TestSja.c

�ļ�����:   �ṩCSD�忨������ʾ

�ļ�˵��:   ���ļ��ṩ�û����棬�����û�����CSD�忨

��ǰ�汾:   V1.3

�޸ļ�¼:   2012-12-06  V1.0    ������  ����
            2013-01-09  V1.1    ������  �޸�    �����°��������±�д
            2014-02-25  V1.2    ���ı  �޸�    ΢������
            2015-12-25  V1.3    ���ı  �޸�    ��������޸�
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
��������:   GetKeyInput

��������:   ��ȡ��������

��������        ����            ����/���           ����
radix           int             input               ��ȡ���ݵĽ��ƣ�10:ʮ���� 16:ʮ�����ƣ�

����ֵ  :   �������ֵ��

����˵��:   ���������ַ�ת�����ʮ����ֵ�������Ͽ�����scanf��ȡ�û�����
            ����scanf�������û�������������ʱ������쳣���������³����쳣
            �����ڳ���׳�ԣ�����дһ�������滻��������Ӧ����VxWorks��Linux

�޸ļ�¼:   2010-10-27      ���ı  ����
********************************************************************/
static int GetKeyInput(int radix)
{
    char input = 0;
    int value = 0;

    if (radix == 10)
    {
        while((input = getchar()) != 0x0a)
        {
            value = value * 10 + (input - 0x30);  /* ת����ʮ������ */
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
            value = (value << 4) + input;  /* ת����ʮ�������� */
        }
    }

    return value;
}

/********************************************************************
��������:   TestSja

��������:   ���Գ���������

��������        ����            ����/���           ����

����ֵ  :   ����ʵ��������ж���

����˵��:   �������ȴ���������Ȼ���ṩ�û��˻���������
            �������û�ѡ����в�ͬ�Ĺ��ܲ��ԣ�����Ĳ��Թ����ɵ����ĺ���ʵ��

�޸ļ�¼:   2012-12-06      ������  ����
********************************************************************/
int TestSja(void)
{
    int select = 0;
    int ret = 0;

    printf("\nISA Sja1000 Test Program\n");

    InitSja();

    /* ��ӡ�˵� */
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
��������:   TransmitSja

��������:   CAN���߷��Ͳ���

��������        ����            ����/���           ����

����ֵ  :   0����ɹ� ��0����ʧ��

����˵��:   ѭ��������Ҫ����ʱ

�޸ļ�¼:   2012-12-06      ������  ����
            2013-01-09      ������  �޸�  �����°��������±�д
********************************************************************/
int TransmitSja(void)
{
    int RunTimes = 0;
    int i = 0;
    int ret = 0;
    Can_TPacket_t TxPacket;
    int fd = 0;

    /* ָ������CAN�� */
    printf("Input Transmit CAN Number(0-%d: CAN1-can%d):\n", MAX_SJA_NUM - 1, MAX_SJA_NUM);
    fd = GetKeyInput(10) % MAX_SJA_NUM;

    /* ���뷢�ʹ��� */
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
        taskDelay(sysClkRateGet() / 2);    /* ������Ҫ����ʱ������оƬ��һֱ���������жϣ�
                               �����᲻ͣ�Ľ����жϷ�����򣬵��²��ܵ��ûص����� */
    }

    return ret;
}

/********************************************************************
��������:   InitSja

��������:   ��ʼ��������SJA1000оƬ

��������        ����            ����/���           ����

����ֵ  :   0����ɹ� ��0����ʧ��

����˵��:   SJA1000оƬ������Ҫ�����ֲ�

�޸ļ�¼:   2012-12-06      ������  ����
            2013-01-09      ������  �޸�  �����°��������±�д
********************************************************************/
int InitSja(void)
{
    int ret = 0;
    int i = 0;
    sja1000_config_t config;

    config.uchMOD = 0x09;   /* ����resetģʽ�����˲������ѣ����Բ⣬�Ǽ���ģʽ */
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

    /* ������ */
    ret = CAN_Open(0, 0x300, 0xd400, 5, 4096, 49);
    if(ret != SUCCESS)
    {
        printf("Can%d CAN_Open failed!%d\n", 0, ret);

        return ;
    }

    /* ��ʼ��SJA1000оƬ */
    ret = CAN_Init(0, &config);
    if(ret != SUCCESS)
    {
        printf("Can%d CAN_Init Failed!%d\n", 0, ret);
        
        return ;
    }

    /* ��װ�ص����� */
    CAN_InstallCallBack(0, CallBack);

    /* ������ */
    ret = CAN_Open(1, 0x300, 0xd800, 7, 4096, 49);
    if(ret != SUCCESS)
    {
        printf("Can%d CAN_Open failed!%d\n", 1, ret);

        return ;
    }

    /* ��ʼ��SJA1000оƬ */
    ret = CAN_Init(1, &config);
    if(ret != SUCCESS)   /* ��ʼ��SJA1000оƬ */
    {
        printf("Can%d CAN_Init Failed!%d\n", 1, ret);
        
        return ;
    }

    /* ��װ�ص����� */
    CAN_InstallCallBack(1, CallBack);

    return ret;
}

/********************************************************************
��������:   CallBack

��������:   �ص�����

��������        ����            ����/���           ����
fd              int             input               �豸�ļ�����(0-4)

����ֵ  :   0����ɹ� ��0����ʧ��

����˵��:   ��SJA1000���յ��ж�ʱ����������ô˺�����

�޸ļ�¼:   2010-09-16      ������  ����
            2013-01-09      ������  �޸�    �����°��������±�д
            2015-12-25      ���ı  �޸�    ��������޸�
********************************************************************/
int CallBack(int fd)
{
    unsigned char RxBuffer[CAN_FRAME_SIZE] = {0};
    int ret = 0;
    int i = 0;

    for(;;)
    {
        memset(RxBuffer, 0, sizeof(RxBuffer));
        ret = CAN_ReadMsg(fd, RxBuffer, sizeof(RxBuffer));    /* ��ȡ�������� */
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
��������:   CloseSja

��������:   �ر�����

��������        ����            ����/���           ����

����ֵ  :   0����ɹ� ��0����ʧ��

����˵��:   �ر�����

�޸ļ�¼:   2012-12-06      ������  ����
            2013-01-09      ������  �޸�  �����°��������±�д
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
