#!/bin/bash
#
# File: /usr/sbin/lc_broadcast.sh
#
# UDP broadcast for Lap Counter host
# This script needs to start on boot, see lc_broadcast.service
#
# Waits until a packet is received from the client
# then sends config info to client(s) (via broadcast)

# network device (may need to be adjusted to suit)
DEV=eth0
# id to broadcast (the client identifies the correct host)
HOSTID="LC1"
# hostname
HOSTNAME=`hostname`
# broadcast port (the client listens for broadcast packets on this port)
BC_PORT=2000
# telemetry port (where the server listens for telemetry packets)
TELEMETRY_PORT=2006
# server URL
SERVERURL="support.rossw.net"
SERVERPAGE="erwintest?"
SERVERPORT=80
#broadcast interval in seconds (must be less than the timeout specified in the client)
INTERVAL=10

# wait for system to settle down
sleep 20

#get the broadcast address for the specified ethernet device
bc_address=`/bin/ip a s dev $DEV | awk '/inet / {print $4}'`
echo "Broadcasting on $bc_address"

# broadcast endless loop
while true
do
    # wait for a packet to arrive at broadcast port
    nc_output="$(/bin/nc -u -l -p $BC_PORT -w 0)"
    # echo "packet received"
    # split client string into array
    read -r -a client_data <<< "$nc_output"
    # check array size
    # echo "array size: " ${#client_data[@]}
    if [ ${#client_data[@]} -eq 1 ]; then
        # send client config data packet
        echo -e "$HOSTID\t$TELEMETRY_PORT\t$HOSTNAME\t$SERVERURL\t$SERVERPORT\t$SERVERPAGE\t" | /bin/nc -ub -w0 $bc_address $BC_PORT
        echo "answered request from " ${client_data[0]}
    fi
    # sleep $INTERVAL
done

echo "Broadcast exited"
