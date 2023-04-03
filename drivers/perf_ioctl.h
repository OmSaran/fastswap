#define DEV_NAME "scalemem_rdma"
#define DEV_PATH "/dev/scalemem_rdma"
#define FASTSWAP_MAGIC 0x40
#define FASTSWAP_MAGIC_RDMA 0x41

#include <linux/mm_types.h>

struct perf_args {
    u64 count_sync;
    u64 count_async;
    u64 total_time_sync;
    u64 total_time_async;
};



#define TEST _IO(FASTSWAP_MAGIC, 0x0)
#define START _IO(FASTSWAP_MAGIC, 0x1)
#define STOP _IOWR(FASTSWAP_MAGIC, 0x2, struct perf_args)
#define START_WRITES _IO(FASTSWAP_MAGIC, 0x3)
#define STOP_WRITES _IOWR(FASTSWAP_MAGIC, 0x4, struct perf_args)


struct perf_args_rdma {
    u64 count_ops;
    u64 count_async;
    u64 total_time_ops;
    u64 total_time_async;
};

#define TEST_RDMA _IO(FASTSWAP_MAGIC_RDMA, 0x0)
#define START_RDMA _IO(FASTSWAP_MAGIC_RDMA, 0x1)
#define STOP_RDMA _IOWR(FASTSWAP_MAGIC_RDMA, 0x2, struct perf_args_rdma)