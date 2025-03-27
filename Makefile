# Compiler and flags
CC = gcc
CFLAGS = -g -Wall -I. -pthread -Wno-unused-variable
LDFLAGS = -lnsl -pthread

# RPC-related tools
RPCGEN = rpcgen

# Paths to client and server directories
CLIENT_DIR = Client
SERVER_DIR = Server

# RPC generated files with specific destinations
RPC_HEADER_CLIENT = $(CLIENT_DIR)/replicator.h
RPC_HEADER_SERVER = $(SERVER_DIR)/replicator.h
CLIENT_STUB = $(CLIENT_DIR)/replicator_clnt.c
SERVER_STUB = $(SERVER_DIR)/replicator_svc.c
XDR_CLIENT = $(CLIENT_DIR)/replicator_xdr.c
XDR_SERVER = $(SERVER_DIR)/replicator_xdr.c

# RPC files generated in root
RPC_FILES_ROOT = replicator.h replicator_clnt.c replicator_svc.c replicator_xdr.c

# Source RPC file
RPC_SRC = replicator.x

# Targets to build
all: replicator_client replicator_server

# Generate RPC stubs in root and copy to directories
$(CLIENT_STUB) $(SERVER_STUB) $(XDR_CLIENT) $(XDR_SERVER) $(RPC_HEADER_CLIENT) $(RPC_HEADER_SERVER): $(RPC_SRC)
	$(RPCGEN) -C $(RPC_SRC)
	cp replicator_clnt.c $(CLIENT_DIR)/
	cp replicator_svc.c $(SERVER_DIR)/
	cp replicator_xdr.c $(CLIENT_DIR)/
	cp replicator_xdr.c $(SERVER_DIR)/
	cp replicator.h $(CLIENT_DIR)/
	cp replicator.h $(SERVER_DIR)/

# Client executable (CLI-based)
replicator_client: $(CLIENT_DIR)/replicator_client.c $(CLIENT_STUB) $(XDR_CLIENT) $(RPC_HEADER_CLIENT)
	$(CC) $(CFLAGS) -I$(CLIENT_DIR) -o $@ $(CLIENT_DIR)/replicator_client.c $(CLIENT_STUB) $(XDR_CLIENT) $(LDFLAGS)

# Server executable
replicator_server: $(SERVER_DIR)/replicator_server.c $(SERVER_STUB) $(XDR_SERVER) $(RPC_HEADER_SERVER)
	$(CC) $(CFLAGS) -I$(SERVER_DIR) -o $@ $(SERVER_DIR)/replicator_server.c $(SERVER_STUB) $(XDR_SERVER) $(LDFLAGS)

# Clean generated files and executables
clean:
	rm -f replicator_client replicator_server $(RPC_HEADER_CLIENT) $(RPC_HEADER_SERVER) $(CLIENT_STUB) $(SERVER_STUB) $(XDR_CLIENT) $(XDR_SERVER) $(RPC_FILES_ROOT) *.o

.PHONY: all clean