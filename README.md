# PIRDS-datalogger and webcgi
There are two related but separate programs: 1) the datalog_server, which will listen for, receive, parse and store data from VentMon devices and 2) data_webcgi, which reads stored data and returns raw, or json formatted data.

There are two versions of the data_webcgi (c program and bash script) - both are designed to be called from a web server (more specfically Apache) as a standard CGI program.
