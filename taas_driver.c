/* SPDX-License-Identifier: GPL-2.0
 *
 * TaaS BCM2837 System Timer Driver
 *
 * This driver exposes the BCM2837 64-bit system timer to user space
 * via a read-only, non-cached MMIO mapping.
 *
 * Design goals:
 * - Deterministic access
 * - No kernel threads
 * - No scheduler interaction
 * - No abstraction over hardware
 *
 * This is NOT a general-purpose clocksource and is not intended
 * for upstream inclusion.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#define DEVICE_NAME "taas_timer"
#define BCM2837_ST_BASE 0x3F003000
#define ST_SIZE 0x10  /* Map enough for control/status and counter */

/*
 * MMIO base for the BCM2837 system timer.
 * Mapped once at init and never modified.
 */
static void __iomem *timer_base;

/*
 * dev_read - return a consistent 64-bit system timer value
 *
 * The BCM2837 exposes the system timer as two 32-bit registers
 * (low/high). Since the bus is 32-bit, a verification loop is
 * required to guarantee atomicity.
 *
 * No locking is required:
 * - Registers are read-only
 * - No shared mutable state exists
 * - Consistency is ensured by re-reading the high register
 */
static ssize_t dev_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    u32 low, high, high_verify;
    u64 timestamp;

    if (len < sizeof(u64))
        return -EINVAL;

    /* Atomic 64-bit read on 32-bit bus */
    do {
        high = ioread32(timer_base + 0x08);
        low  = ioread32(timer_base + 0x04);
        high_verify = ioread32(timer_base + 0x08);
    } while (high != high_verify);

    timestamp = ((u64)high << 32) | low;

    if (copy_to_user(buffer, &timestamp, sizeof(u64)))
        return -EFAULT;

    return sizeof(u64);
}

/*
 * dev_mmap - map system timer registers into user space
 *
 * The mapping is marked as non-cached to prevent stale reads.
 * User space is expected to perform direct MMIO loads only.
 *
 * No write access is provided.
 */
static int dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    
    return remap_pfn_range(vma, vma->vm_start, 
                           BCM2837_ST_BASE >> PAGE_SHIFT, 
                           size, vma->vm_page_prot);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = dev_read,
    .mmap  = dev_mmap,
};

static struct miscdevice taas_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &fops,
    .mode  = 0666,
};

/*
 * Module initialization:
 * - Map timer MMIO region
 * - Register misc device
 *
 * No background activity is started.
 */
static int __init taas_init(void)
{
    timer_base = ioremap(BCM2837_ST_BASE, ST_SIZE);
    if (!timer_base)
        return -ENOMEM;

    if (misc_register(&taas_misc)) {
        iounmap(timer_base);
        return -EBUSY;
    }

    pr_info("taas: BCM2837 system timer driver loaded\n");
    return 0;
}

/*
 * Module teardown:
 * - Unregister device
 * - Unmap MMIO region
 */
static void __exit taas_exit(void)
{
    misc_deregister(&taas_misc);
    iounmap(timer_base);
    pr_info("taas: driver unloaded\n");
}

module_init(taas_init);
module_exit(taas_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DavidDevGt");
MODULE_DESCRIPTION("Direct BCM2837 System Timer Access");
