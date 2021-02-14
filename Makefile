all: pirds_logger pirds_webcgi

pirds_logger: Makefile pirds_logger.c PIRDS.o Makefile
	gcc -o pirds_logger pirds_logger.c PIRDS.o

pirds_webcgi: Makefile pirds_webcgi.c PIRDS.h PIRDS.o Makefile
	gcc -o pirds_webcgi pirds_webcgi.c PIRDS.o
