all: pirds_logger pirds_webcgi

pirds_logger: pirds_logger.c
	gcc -o pirds_logger pirds_logger.c

pirds_webcgi: pirds_webcgi.c
	gcc -o pirds_webcgi pirds_webcgi.c
