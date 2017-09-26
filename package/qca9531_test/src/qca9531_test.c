/*
************************************************************************************
*
* QCA9531测试程序
* 描述：
*       用于进行QCA9531硬件功能的测试，以及写MAC等
* 使用：
*       1.将板子WAN和LAN口连接到交换机，电脑也连到交换机
*       2.给板子供电
*       3.配置电脑IP，要配置两个IP分别处于192.168.1.x网段和192.168.2.x网段，为了避免冲突，
*         统一取192.168.1.100和192.168.2.100
*       3.板子默认是处于路由模式，WAN口开启DHCP，LAN口为192.168.1.X网段。上电后若未
*         通过测试会自动跑测试程序qca9531_test，会将WAN口IP初始化为192.168.2.1，LAN口
*         的bridge IP初始化为192.168.1.1。PC客户端通过LAN口连接板子开启的TCP服务器
*         （192.168.1.1:4415），发送命令给板子，进行数据交互，最终完成测试。
*       4.网口要用“QCA9531网口测试.bat”进行测试
* 命令：
*       1.设置WiFi MAC地址，eth0地址在WiFi MAC上+1，eth1则+2
*         命令：set_wifi_mac 11:22:33:44:AA:BB\r\n
*         返回：ok\r\n 或 fail\r\n
*         注意：要求第一个字节的最低位一定是0，也就是说第一个字节的第二个数字一定是
*         0、2、4、6、8、A、C、E其中的一个
*       2.设置测试通过标志。实际上是往/etc/qca9531_test.conf文件写1，表示通过了测试，下次
*         上电不再自动启动测试程序
*         命令：test_pass\r\n
*         返回：ok\r\n
*       3.根据WiFi MAC地址设置eth0及eth1的地址，eth0地址在WiFi MAC上+1，eth1则+2
*         命令：set_ethx_mac\r\n
*         返回：ok\r\n 或 fail\r\n
*       4.获取当前MAC地址，包括WiFi，eth0及eth1地址
*         命令：get_mac\r\n
*         返回：12:11:11:11:11:11,12:11:11:11:11:12,12:11:11:11:11:13\r\n 依次为WiFi，eth0
*               及eth1的MAC，一般判断是否为依次加1就OK了;
*               fail\r\n
*       5.重启设备
*         命令：reboot\r\n
*         返回：ok\r\n
*       6.LED控制
*         命令：led_ctrl 1\r\n 全亮
*               led_ctrl 0\r\n 全灭
*         返回：ok\r\n
*               fail\r\n
* 作者：郑其墉
* 更新时间：2017-4-27 17:36:26
*
************************************************************************************
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <error.h>
#include <linux/sockios.h>

#include "led_test.h"

// 定义MAC地址在ART分区中的偏移量
#define ETH0_MAC_OFFSET  0x0000
#define ETH1_MAC_OFFSET  0x0006
#define ATH0_MAC_OFFSET  0x1002
#define ATH1_MAC_OFFSET  0x5002

// 服务器相关定义
#define SERVER_PORT       4415  //服务器端口
#define MAX_QUE_CONN_NM      1  //服务器最多的连接数
#define BUFFER_SIZE       1024  //接收数据缓存大小

// 命令定义
#define CMD_SET_WIFI_MAC      "set_wifi_mac"
#define RESPON_CMD_OK         "ok\r\n"
#define RESPON_CMD_FAIL       "fail\r\n"
#define CMD_TEST_PASS         "test_pass"
#define CMD_SET_ETHX_MAC      "set_ethx_mac"
#define CMD_GET_MAC           "get_mac"
#define CMD_REBOOT            "reboot"
#define CMD_LED_CTRL          "led_ctrl"

#define QCA9531_TEST_CONF_FILE    "/etc/qca9531_test.conf"

/*
* 命令参数存放结构体
*/
typedef struct cmd_param
{
    int cnt;
    //第1个序号为参数最多个数，第2个序号为单个参数最大长度
    char list[10][256];
} cmd_param_t;

/*
* 检查MAC合法性
* 输入：mac_str--MAC字符串，如11:AA:BB:CC:22:11 不区分大小写
* 返回：-1--失败，0--成功
*/
int check_mac_valid(char *mac_str)
{
    if (strlen(mac_str) != 17)
    {
        return -1;
    }

    char buf[20];
    strcpy(buf, mac_str);
    char *strtmp = strtok(buf, ":");
    int hexnum = 0;
    while (strtmp != NULL)
    {
        if (strlen(strtmp) != 2)
        {
            return -1;
        }
        if ((strtmp[0] >= '0' && strtmp[0] <= '9') || (strtmp[0] >= 'A' && strtmp[0] <= 'F') || (strtmp[0] >= 'a' && strtmp[0] <= 'f'))
        {
            if ((strtmp[1] >= '0' && strtmp[1] <= '9') || (strtmp[1] >= 'A' && strtmp[1] <= 'F') || (strtmp[1] >= 'a' && strtmp[1] <= 'f'))
            {
                hexnum ++;
                strtmp = strtok(NULL, ":");
            }
            else
            {
                return -1;
            }
        }
        else
        {
            return -1;
        }

    }
    if (hexnum != 6)
    {
        return -1;
    }

    return 0;
}

/*
* 将MAC字符串转化为数组
* 输入：str--MAC字符串，格式如：11:AA:BB:CC:DD:22
* 输出：mac--MAC数组，6字节
* 返回：-1--失败，0--成功
*/
int mac_str_to_array( char *str, unsigned char *mac)
{
    int i;
    char *s, *e;

    if ((mac == NULL) || (str == NULL))
    {
        return -1;
    }

    if (check_mac_valid(str) == -1)
    {
        return -1;
    }

    s = (char *) str;
    for (i = 0; i < 6; ++i)
    {
        mac[i] = s ? strtoul (s, &e, 16) : 0;
        if (s)
           s = (*e) ? e + 1 : e;
    }

    return 0;
}

/*
* 解析命令
* 输入：cmd_data--命令字符串，格式为：aaa bbb ccc\r\n，空格分开参数，\r\n结束命令
*       cmd_len--命令长度
* 输出：cmd_param--参数输出
* 返回：0--解析成功，-1--解析失败
*/
int decode_command(char *cmd_data, int cmd_len, cmd_param_t *pcmd_param)
{
    int i = 0, j = 0;
    int param_cnt = 0;
    int cmd_end_flag = 0;

    if (cmd_data == NULL)
    {
        return -1;
    }

    for (i=0; i<cmd_len; i++)
    {
        if (cmd_data[i] == ' ')
        {
            if (j != 0)
            {
                pcmd_param->list[param_cnt][j] = 0;
                param_cnt++;
                j = 0;
            }
            continue;
        }
        else if (cmd_data[i] == '\r')
        {
            if ( (i+1) < cmd_len)
            {
                if (cmd_data[i+1] == '\n')
                {
                     cmd_end_flag = 1;
                    if (j != 0)
                    {
                        pcmd_param->list[param_cnt][j] = 0;
                        param_cnt++;
                    }
                    break;
                }
            }
        }

        pcmd_param->list[param_cnt][j++] = cmd_data[i];
    }

    if ((param_cnt == 0)||(!cmd_end_flag))
    {
        return -1;
    }

    pcmd_param->cnt = param_cnt;
    return 0;
}

/*
* QCA9531写MAC
* 输入：nic--设备名称字符串，eth0，eth1或ath0
*       value--MAC数组，如value[6] = {1a,2b,3c,4d,5e,6f}
* 返回：0--成功，-1--失败
*/
int qca9531_mac_write(char *nic, char *value)
{
    int size = 0;
    struct mtd_info_user mtdInfo;
    struct erase_info_user mtdEraseInfo;
    int fd;
    unsigned char *buf, *ptr;
    int i;

    fd = open("/dev/mtd5", O_RDWR | O_SYNC);
    if(fd < 0)
    {
        return -1;
    }

    if(ioctl(fd, MEMGETINFO, &mtdInfo))
    {
        close(fd);
        return -1;
    }

    mtdEraseInfo.length = size = mtdInfo.erasesize;
    buf = (unsigned char *)malloc(size);
    if(NULL == buf)
    {
        close(fd);
        return -1;
    }

    if(read(fd, buf, size) != size)
    {
        goto write_fail;
    }

    mtdEraseInfo.start = 0x0;
    for (mtdEraseInfo.start; mtdEraseInfo.start < mtdInfo.size; mtdEraseInfo.start += mtdInfo.erasesize)
    {
        ioctl(fd, MEMUNLOCK, &mtdEraseInfo);
        if(ioctl(fd, MEMERASE, &mtdEraseInfo))
        {
            goto write_fail;
        }
    }

    if (!strcmp(nic, "eth0"))
    {
        ptr = buf + ETH0_MAC_OFFSET;
    }
    else if(!strcmp(nic, "eth1"))
    {
        ptr = buf + ETH1_MAC_OFFSET;
    }
    else if(!strcmp(nic, "ath0"))
    {
        ptr = buf + ATH0_MAC_OFFSET;
    }
    else if(!strcmp(nic, "ath1"))
    {
        ptr = buf + ATH1_MAC_OFFSET;
    }
    else
    {
        goto write_fail;
    }
    memcpy(ptr,value,6);

    lseek(fd, 0, SEEK_SET);
    if (write(fd, buf, size) != size)
    {
        goto write_fail;
    }

    close(fd);
    free(buf);
    return 0;

write_fail:
    close(fd);
    free(buf);
    return -1;
}

/*
* 实现MAC地址数组中MAC值加1
*/
void mac_array_plus1(unsigned char *mac)
{
    int i;

    for (i=5; i>=0; i--)
    {
        if (mac[i] < 255)
        {
            mac[i]++;
            break;
        }
        else
        {
            mac[i] = 0;
        }
    }
}

/*
* 获取WiFi或以太网口的MAC地址
* 输入：nic--设备名称字符串，eth0，eth1或ath0
*       mac--MAC地址数组，6字节
* 返回：-1失败，0-成功
*/
int get_wifi_eth_mac(unsigned char *nic, unsigned char *mac)
{
    int fd = -1;
    unsigned int mac_offset;

    fd = open("/dev/mtd5", O_RDWR | O_SYNC);
    if(fd < 0)
    {
        goto fail;
    }

    if (!strcmp(nic, "ath0"))
    {
        mac_offset = ATH0_MAC_OFFSET;
    }
    else if(!strcmp(nic, "eth0"))
    {
        mac_offset = ETH0_MAC_OFFSET;
    }
    else if(!strcmp(nic, "eth1"))
    {
        mac_offset = ETH1_MAC_OFFSET;
    }
    else
    {
        goto fail;
    }

    lseek(fd, mac_offset, SEEK_SET);
    if (read(fd, mac, 6) == 6)
    {
        goto ok;
    }
    else
    {
        goto fail;
    }

    fail:
    if(fd >= 0)
    {
        close(fd);
    }
    return -1;

    ok:
    close(fd);
    return 0;
}

/*
* 处理命令
*/
void process_command(cmd_param_t *pcmd_param, int client_fd)
{
    int param_cnt = pcmd_param->cnt;
    unsigned char wifi_mac[6];
    unsigned char eth0_mac[6];
    unsigned char eth1_mac[6];
    unsigned char wifi_mac_buf[20];
    unsigned char eth0_mac_buf[20];
    unsigned char eth1_mac_buf[20];
    unsigned char buf[100];

    if (pcmd_param == NULL)
    {
        return;
    }

    if (param_cnt > 0)
    {
        /*设置WiFi MAC地址命令*/
        if ((strcmp(&pcmd_param->list[0][0], CMD_SET_WIFI_MAC)==0) && (param_cnt==2))
        {
            if (mac_str_to_array(&pcmd_param->list[1][0], wifi_mac) == 0)
            {
                qca9531_mac_write("ath0", wifi_mac);
                mac_array_plus1(wifi_mac);
                qca9531_mac_write("eth0", wifi_mac);
                mac_array_plus1(wifi_mac);
                qca9531_mac_write("eth1", wifi_mac);
                sync();

                send(client_fd, RESPON_CMD_OK, strlen(RESPON_CMD_OK), 0);
            }
            else
            {
                send(client_fd, RESPON_CMD_FAIL, strlen(RESPON_CMD_FAIL), 0);
            }
        }
        /*测试通过命令*/
        else if ((strcmp(&pcmd_param->list[0][0], CMD_TEST_PASS)==0) && (param_cnt==1))
        {
            system("echo 1 > " QCA9531_TEST_CONF_FILE);
            sync();

            send(client_fd, RESPON_CMD_OK, strlen(RESPON_CMD_OK), 0);
        }
        /*设置以太网口MAC命令*/
        else if ((strcmp(&pcmd_param->list[0][0], CMD_SET_ETHX_MAC)==0) && (param_cnt==1))
        {
            if (get_wifi_eth_mac("ath0", wifi_mac) == 0)
            {
                mac_array_plus1(wifi_mac);
                qca9531_mac_write("eth0", wifi_mac);
                mac_array_plus1(wifi_mac);
                qca9531_mac_write("eth1", wifi_mac);
                sync();

                send(client_fd, RESPON_CMD_OK, strlen(RESPON_CMD_OK), 0);
            }
            else
            {
                send(client_fd, RESPON_CMD_FAIL, strlen(RESPON_CMD_FAIL), 0);
            }
        }
        /*获取MAC命令*/
        else if ((strcmp(&pcmd_param->list[0][0], CMD_GET_MAC)==0) && (param_cnt==1))
        {
            if ((get_wifi_eth_mac("ath0", wifi_mac)==0) && (get_wifi_eth_mac("eth0", eth0_mac)==0) && (get_wifi_eth_mac("eth1", eth1_mac)==0))
            {
                sprintf(wifi_mac_buf,"%02x:%02x:%02x:%02x:%02x:%02x",wifi_mac[0],wifi_mac[1],wifi_mac[2],wifi_mac[3],wifi_mac[4],wifi_mac[5]);
                sprintf(eth0_mac_buf,"%02x:%02x:%02x:%02x:%02x:%02x",eth0_mac[0],eth0_mac[1],eth0_mac[2],eth0_mac[3],eth0_mac[4],eth0_mac[5]);
                sprintf(eth1_mac_buf,"%02x:%02x:%02x:%02x:%02x:%02x",eth1_mac[0],eth1_mac[1],eth1_mac[2],eth1_mac[3],eth1_mac[4],eth1_mac[5]);
                sprintf(buf, "%s,%s,%s\r\n", wifi_mac_buf, eth0_mac_buf, eth1_mac_buf);
                send(client_fd, buf, strlen(buf), 0);
            }
            else
            {
                send(client_fd, RESPON_CMD_FAIL, strlen(RESPON_CMD_FAIL), 0);
            }
        }
        /*重启命令*/
        else if ((strcmp(&pcmd_param->list[0][0], CMD_REBOOT)==0) && (param_cnt==1))
        {
            send(client_fd, RESPON_CMD_OK, strlen(RESPON_CMD_OK), 0);
            sleep(3);
            system("reboot");
        }
        /*LED控制命令*/
        if ((strcmp(&pcmd_param->list[0][0], CMD_LED_CTRL)==0) && (param_cnt==2))
        {
            int fd;
            fd = open("/dev/led_test" , O_RDWR);
            if (fd < 0)
            {
                send(client_fd, RESPON_CMD_FAIL, strlen(RESPON_CMD_FAIL), 0);
                return;
            }

            if (pcmd_param->list[1][0] == '1')
            {
                ioctl(fd, SET_GPIO_LED_WAN_OUT, 0);
                ioctl(fd, SET_GPIO_LED_LAN_OUT, 0);
                ioctl(fd, SET_GPIO_LED_WLAN_OUT, 0);
                ioctl(fd, SET_GPIO_LED_STAT_OUT, 0);
                send(client_fd, RESPON_CMD_OK, strlen(RESPON_CMD_OK), 0);
            }
            else if(pcmd_param->list[1][0] == '0')
            {
                ioctl(fd, SET_GPIO_LED_WAN_OUT, 1);
                ioctl(fd, SET_GPIO_LED_LAN_OUT, 1);
                ioctl(fd, SET_GPIO_LED_WLAN_OUT, 1);
                ioctl(fd, SET_GPIO_LED_STAT_OUT, 1);
                send(client_fd, RESPON_CMD_OK, strlen(RESPON_CMD_OK), 0);
            }

            close(fd);
        }
    }
}

int main(void)
{
    struct sockaddr_in server_sockaddr, client_sockaddr;
    int sin_size, recvbytes;
    int sockfd, client_fd;
    char buf[BUFFER_SIZE];
    cmd_param_t cmd_param;

    /*配置好WAN口和LAN口IP*/
    system("ifconfig eth0 192.168.2.1 up && ifconfig br-lan 192.168.1.1 up");
    sleep(3);

    /*建立socket连接*/
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        exit(1);
    }

    /*设置sockaddr_in结构体相关参数*/
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(SERVER_PORT);
    server_sockaddr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(server_sockaddr.sin_zero), 8);

    int i = 1; /*允许重复使用本地地址与套接字进行绑定*/
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

    /*绑定函数bind()*/
    if (bind(sockfd, (struct sockaddr *)&server_sockaddr, sizeof(struct sockaddr)) == -1)
    {
        exit(1);
    }

    /*调用listen()函数，创建未处理请求的队列*/
    if (listen(sockfd, MAX_QUE_CONN_NM) == -1)
    {
        exit(1);
    }

    while (1)
    {
        /*调用accept()函数，等待客户端的连接*/
        client_fd = accept(sockfd, (struct sockaddr*)&client_sockaddr, &sin_size);
        if (client_fd == -1)
        {
            exit(1);
        }

        while (1)
        {
            /*调用recv()函数接收客户端的请求*/
            recvbytes = recv(client_fd, buf, BUFFER_SIZE, 0);
            if (recvbytes == -1)
            {
                exit(1);
            }
            else if (recvbytes == 0)
            {
                break;  //客户端断开连接，服务器重新接受连接
            }

            /*解析命令*/
            if (decode_command(buf, recvbytes, &cmd_param) == 0)
            {
                /*处理命令*/
                process_command(&cmd_param, client_fd);
            }
        }
    }

    close(sockfd);
    exit(0);
}
