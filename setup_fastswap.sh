swapon /root/swapfile && insmod /home/cc/wspace/fastswap/drivers/fastswap_rdma.ko sport=50000 cip=10.0.1.31 sip=10.0.1.32 && insmod /home/cc/wspace/fastswap/drivers/fastswap.ko && /home/cc/wspace/cfm_private/setup/init_bench_cgroups.sh && echo SUCCESS
