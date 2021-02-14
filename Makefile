all: pirds_logger

pirds_logger: Makefile pirds_logger.c PIRDS.o Makefile
	gcc -o pirds_logger pirds_logger.c PIRDS.o
