# PIRDS-datalogger and webcgi
There are two related but separate programs: 1) the pirds_logger, which will listen for, receive, parse and store data from VentMon devices and 2) pirds_webcgi, which reads stored data and returns raw, or json formatted data.

There are two versions of the pirds_webcgi (c program and bash script) - both are designed to be called from a web server (more specifically Apache) as a standard CGI program.
