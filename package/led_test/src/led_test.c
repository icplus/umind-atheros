/*
************************************************************************************
*
* QCA9531 LED测试驱动程序
* 描述：用于进行QCA9531 LED的测试，主要就是点亮或关闭某个LED
* 作者：郑其墉
* 更新时间：2017-4-27 16:18:36
*
************************************************************************************
*/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include<linux/miscdevice.h>
#include<linux/gpio.h>
#include<linux/fs.h>

#include "led_test.h"

#define GPIO_OUT_FUNCTION1    0x18040030
#define GPIO_OUT_FUNCTION4    0x1804003C
#define GPIO_LED_WLAN         12
#define GPIO_LED_STAT         13
#define GPIO_LED_WAN          4
#define GPIO_LED_LAN          16

extern void gpio_set_value(unsigned gpio, int value);  //其他内核模块导出的函数

static int led_test_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
    switch (cmd)
    {
    case SET_GPIO_LED_WAN_OUT:
        gpio_set_value(GPIO_LED_WAN, arg);
        break;
    case SET_GPIO_LED_LAN_OUT:
        gpio_set_value(GPIO_LED_LAN, arg);
        break;
    case SET_GPIO_LED_WLAN_OUT:
        gpio_set_value(GPIO_LED_WLAN, arg);
        break;
    case SET_GPIO_LED_STAT_OUT:
        gpio_set_value(GPIO_LED_STAT, arg);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = led_test_ioctl,  //Linux-2.3.36之后的内核这部分不能写为ioctl了！
};

static struct miscdevice misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = LED_TEST_DEVICE_NAME,
    .fops = &dev_fops,
};

static void qca9531_set_reg(unsigned int reg_addr, unsigned int val)
{
    unsigned int virt_regaddr = 0;

    virt_regaddr = ioremap (reg_addr, sizeof (unsigned int));
    *(volatile unsigned int*)virt_regaddr = val;
    iounmap (virt_regaddr);
}

static unsigned int qca9531_read_reg(unsigned int reg_addr)
{
    unsigned int virt_regaddr = 0;
    unsigned int val = 0;

    virt_regaddr = ioremap (reg_addr, sizeof (unsigned int));
    val = *(volatile unsigned int *)virt_regaddr;
    iounmap (virt_regaddr);

    return val;
}

static int __init led_test_init(void)
{
    int ret = 0;
    unsigned int tmp;

    ret=misc_register(&misc);

    // 系统启动时将一些GPIO配置成了特定模式，没法进行IO电平输出，故这里要先修改寄存器
    tmp = qca9531_read_reg(GPIO_OUT_FUNCTION1);
    qca9531_set_reg(GPIO_OUT_FUNCTION1, tmp & 0xffffff00);
    tmp = qca9531_read_reg(GPIO_OUT_FUNCTION4);
    qca9531_set_reg(GPIO_OUT_FUNCTION4, tmp & 0xffffff00);
    // 关闭所有LED
    gpio_set_value(GPIO_LED_WAN, 1);
    gpio_set_value(GPIO_LED_LAN, 1);
    gpio_set_value(GPIO_LED_WLAN, 1);
    gpio_set_value(GPIO_LED_STAT, 1);

    return ret;
}

static void __exit led_test_exit(void)
{
    misc_deregister(&misc);
}

module_init(led_test_init);
module_exit(led_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zqy");
