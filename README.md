# rdma-communication for incast
N1      ----
N2      ----     [Master]
N3      ----
# Create RXE interface

`sudo rdma link add <netdev>rxe type rxe netdev <netdev>`

sudo apt install ibverbs-utils

Prerequisite:
1. Create protection domain (PD)
2. Create send/receive completion queue (CQ)
3. Create queue pairs (QP)
4. Pooling for events


PowerPoint: https://docs.google.com/presentation/d/1no1rfRhp0-FFuKN-RnxrxktSyOTxnhS5j40Wv3FD5EU/edit?usp=sharing
