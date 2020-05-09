The PIRDS logger is mad up of two parts: 1) a udp/tcp server that listens and stores PIRDS formatted data sent by the VentMon firmware; 2) an Apache CGI compatible program that provides an API to retrieve the stored PIRDS data in raw or JSON format.

## Installing the udp/tcp server
This is a stand alone server.  It is NOT daemonized.  Once compiled it can be put just about anywhere. It stores the data and debug information in the same directory as the executable. Do understand that a typical VentMon device will generate ~83KB of data per minute or ~120MB/day. With debugging ON this jumps to ~335KB/min or ~438MB/day.  

To run the udp/tcp server just run the executable.  It will by default listen for UDB packets on port 6111.  

If you want to change the port just put the port number on the command line. 

To listen for TCP data add -T.  This is not recommended but is available, but the ventmon sketch needs to also be changed to send tcp.

To turn on debugging information use the -D option.

It is possible to have the logger started from rc.local or from systemd.

Here is an example systemd configuration file:

```
[Unit]
Description=VentMon Data Server

[Service]
WorkingDirectory=/store/ventmon
User=www-data
Group=www-data
ExecStart=/store/ventmon/pirds_logger -D -D 6111
StandardOutput=file:/store/ventmon/pirds_logger.out
StandardError=file:/store/ventmon/pirds_logger.error

[Install]
WantedBy=multi-user.target
```
## Installing the web cgi program
There are two versions of the web cgi program: 1) c version and 2) Bash script

Both are usable and compatible with the Apache cgi interface.  The Bash script uses quite a lot of resources when used in conjunction with the VentMon visualization web page.

To install either, put the script or executable in the same directory as the logger and the logger files.

Create a symlink from the program to index.cgi

You need to add some lines to your Apache .conf files to make the cgi program work.

Add a script alias lines for example:
 `ScriptAliasMatch "/*" "/var/www/ventmon/index.cgi"`
 
 and
```
         <Directory /var/www/ventmon>
          AllowOverride None
          Options +ExecCGI -MultiViews
          AddHandler cgi-script .cgi
        </Directory>
```





