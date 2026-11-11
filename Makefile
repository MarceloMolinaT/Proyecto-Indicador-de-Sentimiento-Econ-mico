CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -pthread
PROGS   = generadorCSV agentePais servidorISE

all: $(PROGS)

generadorCSV: generadorCSV.c ise.h
	$(CC) $(CFLAGS) -o $@ generadorCSV.c

agentePais: agentePais.c ise.h
	$(CC) $(CFLAGS) -o $@ agentePais.c

servidorISE: servidorISE.c ise.h
	$(CC) $(CFLAGS) -o $@ servidorISE.c

clean:
	rm -f $(PROGS) encuestas_*.csv

.PHONY: all clean
