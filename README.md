# rdma-communication
client - server communication

# Create RXE interface

`sudo rdma link add <netdev>rxe type rxe netdev <netdev>`

sudo apt install ibverbs-utils

Prerequisite:
1. Create protection domain (PD)
2. Create send/receive completion queue (CQ)
3. Create queue pairs (QP)
4. Pooling for events
