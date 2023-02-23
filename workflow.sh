CLIENT_IP="10.0.2.21"
SERVER_IP="10.0.2.22"

# Ensure root
if [ $(whoami) != "root" ]; then
	echo "ERROR: You must be root to run this script"
	exit 1
fi

# Ensure server is running
ssh cc@$SERVER_IP "ps aux" | grep "rmserver 50000"
if [ $? -ne 0 ]; then
	echo "ERROR: server not running!"
	exit 1
fi

# Install swap
swapon /home/cc/swapfile | grep swapfile
if [ $? -ne 0 ]; then
	swapon /home/cc/swapfile
fi

# Install the fastswap rdma drivers
insmod /home/cc/fastswap/drivers/fastswap_rdma.ko sport=50000 sip=$CLIENT_IP cip=$SERVER_IP nq=48
if [ $? -ne 0]; then
	echo "ERROR: Failed to install driver fastswap rdma driver, restart is recommended"
	exit 1
fi

insmod /home/cc/fastswap/drivers/fastswap.ko
if [ $? -ne 0]; then
	echo "ERROR: Failed to install fastswap.ko driver, reboot is recommended"
	exit 1
fi
