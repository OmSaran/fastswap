#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/frontswap.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/page-flags.h>
#include <linux/memcontrol.h>
#include <linux/smp.h>
#include <linux/cdev.h>

#include "perf_ioctl.h"

#define B_DRAM 1
#define B_RDMA 2

#ifndef BACKEND
#error "Need to define BACKEND flag"
#endif

#if BACKEND == B_DRAM
#define DRAM
#include "fastswap_dram.h"
#elif BACKEND == B_RDMA
#define RDMA
#include "fastswap_rdma.h"
#else
#error "BACKEND can only be 1 (DRAM) or 2 (RDMA)"
#endif

static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int sswap_poll_load(int cpu);

dev_t dev = 0;
static struct cdev cdev;
static struct class *dev_class;

static atomic_long_t total_time_reads_sync;
static atomic_long_t total_time_writes;
static atomic_long_t total_time_reads_async;
static atomic_long_t total_num_reads_async;
static atomic_long_t total_num_reads_sync;
static atomic_long_t total_num_writes;
static bool measure = false;
static bool measure_write = false;

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = perf_ioctl,
};

static int sswap_store(unsigned type, pgoff_t pageid,
        struct page *page)
{
  struct timespec start, end;
  if(measure_write) {
    getrawmonotonic(&start);
  }
  if (sswap_rdma_write(page, pageid << PAGE_SHIFT)) {
    pr_err("could not store page remotely\n");
    return -1;
  }
  if(measure_write) {
    getrawmonotonic(&end);
    long duration = ((end.tv_sec - start.tv_sec) * 1000000000L) + (end.tv_nsec - start.tv_nsec);
    atomic_long_add(duration, &total_time_writes);
    atomic_long_add(1L, &total_num_writes);
  }

  return 0;
}

/*
 * return 0 if page is returned
 * return -1 otherwise
 */
static int sswap_load_async(unsigned type, pgoff_t pageid, struct page *page)
{
  struct timespec start, end;
  if(measure) {
    getrawmonotonic(&start);
  }
  if (unlikely(sswap_rdma_read_async(page, pageid << PAGE_SHIFT))) {
    pr_err("could not read page remotely\n");
    return -1;
  }

  //!FIXME: check if this is correct
  sswap_poll_load(smp_processor_id());

  if(measure) {
    getrawmonotonic(&end);
    long duration = ((end.tv_sec - start.tv_sec) * 1000000000L) + (end.tv_nsec - start.tv_nsec);
    atomic_long_add(duration, &total_time_reads_async);
    atomic_long_add(1L, &total_num_reads_async);
  }

  return 0;
}

static int sswap_load(unsigned type, pgoff_t pageid, struct page *page)
{
  struct timespec start, end;
  if(measure) {
    sswap_store(0, pageid, page);
    getrawmonotonic(&start);
  }
  if (unlikely(sswap_rdma_read_sync(page, pageid << PAGE_SHIFT))) {
    pr_err("could not read page remotely\n");
    return -1;
  }

  //!FIXME: check if this is correct
  sswap_poll_load(smp_processor_id());

  if(measure) {
    getrawmonotonic(&end);
    long duration = ((end.tv_sec - start.tv_sec) * 1000000000L) + (end.tv_nsec - start.tv_nsec);
    atomic_long_add(duration, &total_time_reads_sync);
    atomic_long_add(1L, &total_num_reads_sync);
  }

  return 0;
}

static int sswap_poll_load(int cpu)
{
  return sswap_rdma_poll_load(cpu);
}

static void sswap_invalidate_page(unsigned type, pgoff_t offset)
{
  return;
}

static void sswap_invalidate_area(unsigned type)
{
  pr_err("sswap_invalidate_area\n");
}

static void sswap_init(unsigned type)
{
  pr_info("sswap_init end\n");
}

static struct frontswap_ops sswap_frontswap_ops = {
  .init = sswap_init,
  .store = sswap_store,
  .load = sswap_load,
  .poll_load = sswap_poll_load,
  .load_async = sswap_load_async,
  .invalidate_page = sswap_invalidate_page,
  .invalidate_area = sswap_invalidate_area,

};

static int __init sswap_init_debugfs(void)
{
  return 0;
}

static void handle_test(void) {
    pr_info("Test ioctl\n");
}

static void handle_start(void) {
  measure = true;
  atomic_long_set(&total_num_reads_async, 0L);
  atomic_long_set(&total_num_reads_sync, 0L);
  atomic_long_set(&total_time_reads_async, 0L);
  atomic_long_set(&total_time_reads_sync, 0L);
}

static void handle_start_writes(void) {
  atomic_long_set(&total_num_writes, 0L);
  atomic_long_set(&total_time_writes, 0L);
  measure_write = true;
}

static int handle_stop_writes(struct perf_args *perf) {
  measure_write = false;
  perf->count_sync = atomic_long_read(&total_num_writes);
  perf->count_async = 0;
  perf->total_time_sync = atomic_long_read(&total_time_writes);
  perf->total_time_async = 0;

  atomic_long_set(&total_num_writes, 0L);
  atomic_long_set(&total_time_writes, 0L);
  return 0;
}

static int handle_stop(struct perf_args *perf) {
  measure = false;
  perf->count_sync = atomic_long_read(&total_num_reads_sync);
  perf->count_async = atomic_long_read(&total_num_reads_async);
  perf->total_time_sync = atomic_long_read(&total_time_reads_sync);
  perf->total_time_async = atomic_long_read(&total_time_reads_async);

  atomic_long_set(&total_num_reads_async, 0L);
  atomic_long_set(&total_num_reads_sync, 0L);
  atomic_long_set(&total_time_reads_async, 0L);
  atomic_long_set(&total_time_reads_sync, 0L);

  return 0;
}

static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct perf_args perf;
    switch(cmd) {
    case TEST:
        handle_test();
        break;
    case START:
        handle_start();
        break;
    case STOP:
        copy_from_user((void *)&perf, (const void *)arg, sizeof(perf));
        if(unlikely(handle_stop(&perf))) {
            pr_err("ioctl: failed to read remote mr\n");
            return -EINVAL;
        }
        copy_to_user((void *)arg, (const void*)&perf, sizeof(perf));
        break;
    case START_WRITES:
        handle_start_writes();
        break;
    case STOP_WRITES:
        copy_from_user((void *)&perf, (const void *)arg, sizeof(perf));
        if(unlikely(handle_stop_writes(&perf))) {
            pr_err("ioctl: failed to read remote mr\n");
            return -EINVAL;
        }
        copy_to_user((void *)arg, (const void*)&perf, sizeof(perf));
        break;
    default:
        pr_err("Unkown ioctl cmd: %u\n", cmd);
    }

    return 0;
}

static int __init perf_ioctl_init(void) {
    // Ask for device major/minor number
    if((alloc_chrdev_region(&dev, 0, 1, "fastswap_perf")) <0){
        pr_err("Cannot allocate major number\n");
        return -1;
    }
    pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

    // Initialize cdev struct and associate it with fops
    cdev_init(&cdev, &fops);

    /* Add to system */
    if((cdev_add(&cdev, dev, 1)) < 0){
        pr_err("Failed to add to the system\n");
        goto remove_device_region;
    }

    /* create /sys/class */
    if(IS_ERR(dev_class = class_create(THIS_MODULE, "fastswap_perf_class"))){
        pr_err("Cannot create class\n");
        goto remove_device_region;
    }

    /* create /dev */
    if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "fastswap_perf_device"))) {
        pr_err("Cannot create the Device 1\n");
        goto remove_class;
    }
    pr_info("Char device successfully installed\n");

    return 0;

remove_class:
    class_destroy(dev_class);
remove_device_region:
    unregister_chrdev_region(dev, 1);

    return -1;
}

static int __init init_sswap(void)
{
  atomic_long_set(&total_num_reads_async, 0L);
  atomic_long_set(&total_num_reads_sync, 0L);
  atomic_long_set(&total_time_reads_sync, 0L);
  atomic_long_set(&total_time_reads_async, 0L);
  atomic_long_set(&total_time_writes, 0L);
  atomic_long_set(&total_num_writes, 0L);
  frontswap_register_ops(&sswap_frontswap_ops);
  if (sswap_init_debugfs())
    pr_err("sswap debugfs failed\n");

  if(perf_ioctl_init())
    pr_err("failed to install char device");

  pr_info("sswap module loaded\n");
  return 0;
}

static void __exit exit_sswap(void)
{
  pr_info("unloading sswap\n");
  device_destroy(dev_class, dev);
  class_destroy(dev_class);
  cdev_del(&cdev);
  unregister_chrdev_region(dev, 1);
  pr_info("Character device successfully uninstalled\n");
}

module_init(init_sswap);
module_exit(exit_sswap);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Fastswap driver");
