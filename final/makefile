CC = gcc
CFLAGS = -c -pthread  # Added -pthread for portability
LDFLAGS = -pthread  # Explicitly link pthread

all: ktp_client ktp_receiver

ktp_client: ktp_lib.o user1.o
	$(CC) $(LDFLAGS) ktp_lib.o user1.o -o ktp_client

ktp_receiver: ktp_lib.o user2.o
	$(CC) $(LDFLAGS) ktp_lib.o user2.o -o ktp_receiver

ktp_lib.o: ktp_lib.c
	$(CC) $(CFLAGS) ktp_lib.c -o ktp_lib.o

user1.o: user1.c
	$(CC) $(CFLAGS) user1.c -o user1.o

user2.o: user2.c
	$(CC) $(CFLAGS) user2.c -o user2.o

run: all
	@echo "Run like: ./ktp_client 127.0.0.1 5000 127.0.0.1 6000 and ./ktp_receiver 127.0.0.1 6000 127.0.0.1 5000"

clean:
	rm -f *.o ktp_client ktp_receiver
