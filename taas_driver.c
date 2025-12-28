/*
 * TaaS Kernel Driver
 * -----------------
 * Provides direct MMIO access to BCM2837 System Timer (64-bit)
 *
 * Interface:
 *  - /dev/taas_timer
 *
 * Build: make
 * Deploy: setup_taas.sh
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#define DEVICE_NAME "taas_timer"
#define BCM2837_ST_BASE 0x3F003000

static void __iomem *timer_base;

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    uint32_t low, high, high_v;
    uint64_t full_ts;
    if (len < 8) return -EINVAL;
    do {
        high = ioread32(timer_base + 0x08);
        low = ioread32(timer_base + 0x04);
        high_v = ioread32(timer_base + 0x08);
    } while (high != high_v);
    full_ts = ((uint64_t)high << 32) | low;
    if (copy_to_user(buffer, &full_ts, 8)) return -EFAULT;
    return 8;
}

static int dev_mmap(struct file *filp, struct vm_area_struct *vma) {
    unsigned long size = vma->vm_end - vma->vm_start;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    return remap_pfn_range(vma, vma->vm_start, BCM2837_ST_BASE >> PAGE_SHIFT, size, vma->vm_page_prot);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .mmap = dev_mmap,
};

static struct miscdevice taas_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &fops,
    .mode = 0666,
};

static int __init taas_init(void) {
    timer_base = ioremap(BCM2837_ST_BASE, 12);
    if (!timer_base) return -ENOMEM;
    if (misc_register(&taas_misc)) {
        iounmap(timer_base);
        return -1;
    }
    printk(KERN_INFO "--- [TaaS] DRIVER 64-BIT EMPRESARIAL CARGADO ---\n");
    return 0;
}

static void __exit taas_exit(void) {
    misc_deregister(&taas_misc);
    iounmap(timer_base);
    printk(KERN_INFO "--- [TaaS] DRIVER REMOVIDO ---\n");
}

module_init(taas_init);
module_exit(taas_exit);
MODULE_LICENSE("GPL");
