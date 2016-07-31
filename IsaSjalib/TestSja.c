/********************************************************************
�ļ�����:   TestSja.c

�ļ�����:   �ṩCSD�忨������ʾ

�ļ�˵��:   ���ļ��ṩ�û����棬�����û�����CSD�忨

��ǰ�汾:   V1.5

�޸ļ�¼:   2012-12-06  V1.0    ������  ����
            2013-01-09  V1.1    ������  �޸�    �����°��������±�д
            2014-02-25  V1.2    ���ı  �޸�    ΢������
            2015-12-25  V1.3    ���ı  �޸�    ��������޸�
            2016-04-05  V1.4    ���ı  ����    ��������޸ģ�ͬʱ���Ӽ����ȹ���
            2016-04-14  V1.5    ��  ��  �޸�    ��������޸ģ��޸Ĳ��Գ�����ӱ�׼֡����
********************************************************************/

#include "vxWorks.h"
#include "stdio.h"
#include "sysLib.h"
#include "ioLib.h"
#include "iv.h"
#include "semLib.h"

#include "IsaSjaLib.h"

/* ʵ��CANͨ���� */
#define CURRENT_SJA_NUM         (8)
#define DEBUG                   1


/* CSD�忨��Դ��Ϣ */
typedef struct csd_info_s
{
    int base0;
    int base;
    int irq;
} csd_info_t;

csd_info_t g_infos[MAX_SJA_NUM] =
{
    {0x311, 0xd000, 5},
    {0x311, 0xd100, 5},
    {0x311, 0xd200, 5},
    {0x311, 0xd300, 5},
    {0x311, 0xd400, 5},
    {0x311, 0xd500, 5},
    {0x311, 0xd600, 5},
    {0x311, 0xd700, 5},
};

int g_csd_debug = 1;
long g_transmit_counts[MAX_SJA_NUM];
long g_received_counts[MAX_SJA_NUM];

int InitSja(int fd, int BaudRate);
int tCanRecv(int fd);
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
            2016-04-14      ��  ��  �޸�  �����޸ĺ�������޸Ĳ��ֺ�������
********************************************************************/
int TestSja(void)
{
    int input = 0;
    int ret = 0;
    int i = 0;
    int recv_tid[CURRENT_SJA_NUM] = {0};

    memset(g_received_counts, 0, sizeof(g_received_counts));
    memset(g_transmit_counts, 0, sizeof(g_transmit_counts));

    /* ��ʼ��SJA1000оƬ */
    for (i = 0; i < CURRENT_SJA_NUM; i++)
    {
        ret = InitSja(i, BAUDRATE_500K);
        if (ret != 0)
        {
            printf("InitSja failed!%d\n", ret);

            continue ;
        }
    }

    /* ������������ */
    for(i = 0; i < CURRENT_SJA_NUM; i++)
    {
        recv_tid[i] = taskSpawn("tCanRecv", 98, 0, 8192, tCanRecv, i, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    printf("\nISA Sja1000 Test Program\n");

    /* ��ӡ�˵� */
    for(input = 100;;)
    {
        switch(input)
        {
        case 98:
            g_csd_debug = !g_csd_debug;
            break;

        case 99:
            printf("Exit Program, exit code = %d\n", CloseSja());
            goto Exit;

        case 1:
            printf("return = %d\n", TransmitSja());
            break;

        case 2: /* ��ʾ���ռ��� */
            for (i = 0; i < MAX_SJA_NUM; i++)
            {
                printf("R%d=%11d  ", i, g_received_counts[i]);
            }
            printf("\n");
            for (i = 0; i < MAX_SJA_NUM; i++)
            {
                printf("T%d=%11d  ", i, g_transmit_counts[i]);
            }
            printf("\n");
            break;

        case 3: /* ������� */
            for (i = 0; i < MAX_SJA_NUM; i++)
            {
                g_received_counts[i] = 0;
                g_transmit_counts[i] = 0;

                /* ���FIFO */
                ret = FlushIsaCan(i);
                if (ret != 0)
                {
                    printf("channel %d FlushIsaCan failed!%d\n", i, ret);
                }
            }
            printf("Counts are clean.\n");
            break;

        default:
            printf("1.  Transmit data\n"
                   "2.  Dispaly  Counts\n"
                   "3.  Clear    counts\n"
                   "98. Swtich debug onoff (debug = %d 0:off 1:on)\n"
                   "99. Quit\n", g_csd_debug);
        } /* end switch */

        printf("%d\n", input = GetKeyInput(10));
    } /* end for */

Exit:
    /* ɾ���������� */
    for(i = 0; i < CURRENT_SJA_NUM; i++)
    {
        CloseSja(i);
        taskDelete(recv_tid[i]);
    }

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
            2016-04-14      ��  ��  �޸�  �����޸ĺ�������޸Ĳ��ֺ������ƣ���ӱ�׼֡����
********************************************************************/
int TransmitSja(void)
{
    int RunTimes = 0;
    int i = 0;
    int ret = 0;
    sja1000_frame_t frame;
    int fd = 0;
    int FrameFormat = 0;

    /* ���㷢������ */
    memset(frame, 0, sizeof(sja1000_frame_t));

    /* ָ������CAN�� */
    printf("Input Transmit CAN Number(0-%d: CAN1-can%d):\n", MAX_SJA_NUM - 1, MAX_SJA_NUM);
    fd = GetKeyInput(10) % MAX_SJA_NUM;

    /* ���뷢�ʹ��� */
    printf("CAN Test, Input Transmit Count(1-10000):\n");
    RunTimes = GetKeyInput(10) % 10000;

    printf("CAN Test, Input Transmit frame format (0:Standard frame 1:Extended frame):\n");
    FrameFormat = GetKeyInput(10) % 2;

    if(FrameFormat == 0) /* ��׼֡��ע���׼֡�11�ֽڣ�IDһ��11λ */
    {
        /* header */
        frame.header = 0x08;

        /* ID */
        frame.buffer[0] = 0x00; /* ID */
        frame.buffer[1] = 0x00; /* ID ��3λ��Ч */

        /* DATA */
        frame.buffer[2] = 0x33;
        frame.buffer[3] = 0x40;
        frame.buffer[4] = 0x55;
        frame.buffer[5] = 0x66;
        frame.buffer[6] = 0x77;
    }
    else if(FrameFormat == 1) /* ��չ֡��ע����չ֡�13�ֽڣ�IDһ��29λ*/
    {
        /* header */
        frame.header = 0x88;

        /* ID */
        frame.buffer[0] = 0x11; /* ID */
        frame.buffer[1] = 0x22; /* ID */
        frame.buffer[2] = 0x33; /* ID */
        frame.buffer[3] = 0x40; /* ID ��5λ��Ч */

        /* DATA */
        frame.buffer[4] = 0x55;
        frame.buffer[5] = 0x66;
        frame.buffer[6] = 0x77;
        frame.buffer[7] = 0x88;
        frame.buffer[8] = 0x99;
    }

    for (i = 0; i < RunTimes; i++)
    {
        if(FrameFormat == 0)
        {
            frame.buffer[7] = i;
        }
        else
        {
            frame.buffer[9] = i;
        }

        ret |= WriteIsaCan(fd, &frame);
        if(ret != SUCCESS)
        {
            printf("can%d send failed! ret = %d, i = %d\n", fd, ret, i);
            return ret;
        }

        g_transmit_counts[fd] += 1;

        taskDelay(sysClkRateGet() / 2);    /* ������Ҫ����ʱ������оƬ��һֱ���������ж� */
    }

    return ret;
}

/********************************************************************
��������:   InitSja

��������:   ��ʼ��������SJA1000оƬ

��������        ����            ����/���           ����
BaudRate        int             input               ������

����ֵ  :   0����ɹ� ��0����ʧ��

����˵��:   SJA1000оƬ������Ҫ�����ֲ�

�޸ļ�¼:   2012-12-06      ������  ����
            2013-01-09      ������  �޸�  �����°��������±�д
            2016-04-14      ��  ��  �޸�  �����޸ĺ�������޸Ĵ���
********************************************************************/
int InitSja(int fd, int BaudRate)
{
    int ret = 0;
    sja1000_config_t config;
    sja1000_filter_t AcceptFilter;


    /* ������ */
    ret = OpenIsaCan(fd, g_infos[fd].base0, g_infos[fd].base, g_infos[fd].irq);
    if(ret != SUCCESS)
    {
        printf("OpenIsaCan failed!fd = %d ret = %d\n", fd, ret);

        return -1;
    }

    /* ��ʼ��SJA1000оƬ */
    config.uchMOD = 0x08;           /* ���˲������ѣ����Բ⣬�Ǽ���ģʽ */
    config.uchBTR0   = BaudRate >> 8;
    config.uchBTR1   = BaudRate;    /* ������ */
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
    config.uchIER = 0x83;

    ret = IoctlIsaCan(fd, IOCTL_INIT_SJA, &config);
        if(ret != SUCCESS)
    {
        printf("Init isa can Failed!fd = %d ret = %d\n", fd, ret);

        return -1;
    }

    /*  ��̬���ý����˲��� */
    AcceptFilter.MaskMode = SINGLE_FILTER;
    AcceptFilter.uchACR[0] = 0x00;
    AcceptFilter.uchACR[1] = 0x00;
    AcceptFilter.uchACR[2] = 0x00;
    AcceptFilter.uchACR[3] = 0x00;
    AcceptFilter.uchAMR[0] = 0xff;
    AcceptFilter.uchAMR[1] = 0xff;
    AcceptFilter.uchAMR[2] = 0xff;
    AcceptFilter.uchAMR[3] = 0xff;

    ret = IoctlIsaCan(fd, SET_ACCEPT_FILTER, &AcceptFilter);
    if(ret != SUCCESS)
    {
        printf("Set mask Failed!fd = %d ret = %d\n", fd, ret);

        return -1;
    }

    return ret;
}

/********************************************************************
��������:   tCanRecv

��������:   ��������

��������        ����            ����/���           ����
fd              int             input               �豸�ļ�����(0-4)

����ֵ  :   0����ɹ� ��0����ʧ��

����˵��:   ��SJA1000���յ��ж�ʱ����������ô˺�����

�޸ļ�¼:   2010-09-16      ������  ����
            2013-01-09      ������  �޸�    �����°��������±�д
            2015-12-25      ���ı  �޸�    ��������޸�
            2016-04-14      ��  ��  �޸�    ���ص���Ϊ����
********************************************************************/
int tCanRecv(int fd)
{
    sja1000_frame_t frame;
    int ret = 0;
    int i = 0;

    for(;;)
    {
        ret = ReadIsaCan(fd, &frame);    /* ��ȡ�������� */
        if(ret <= 0)
        {
            continue;
        }

        g_received_counts[fd] += ret / CAN_FRAME_SIZE;
        if(g_csd_debug)
        {
            printf("--- ");
            printf("%02X ", frame.header);
            for (i = 0; i < CAN_FRAME_SIZE - 1; i++)
            {
                printf("%02X ", frame.buffer[i]);
            }
            printf("--- can%d received %4d frames\n", fd, g_received_counts[fd]);
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
            2016-04-14      ��  ��  �޸�  �����޸ĺ�������޸Ĳ��ֺ�������
********************************************************************/
int CloseSja(void)
{
    int i = 0;

    for(i = 0; i < MAX_SJA_NUM; i++)
    {
        CloseIsaCan(i);
    }

    return 0;
}
