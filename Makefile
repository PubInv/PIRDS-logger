all: pirds_logger pirds_webcgi

pirds_logger: pirds_logger.c PIRDS-1-1.h PIRDS-1-1.o Makefile
	gcc -o pirds_logger pirds_logger.c PIRDS-1-1.o

pirds_webcgi: pirds_webcgi.c PIRDS-1-1.h PIRDS-1-1.o Makefile
	gcc -o pirds_webcgi pirds_webcgi.c PIRDS-1-1.o
