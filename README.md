# restfuld
**restfuld** is a naive RESTful web service implementation for MySQL database server. 
It is designed to be simple and is currently used in a test environment. 
The code has been uploaded on to GitHub to make it publicly availabe but more work is required to make it production ready.

Please see the Issues section and feel free to create new ones or make contributions to the code.

# Installation
You need to have MySQL client development files (`mysql.h` and `libmysqlclient`) installed on your system. Please check with your operating system's package manager whether these files are installed with the MySQL client software.

To compile `restfuld`, simply type `make` inside the main directory.

The code has been tested and is developed on a FreeBSD host machine which uses Clang and BSD make, but you should be able to compile and run the source on any UNIX like system. FreeBSD `make` is derived from `pmake` not GNU make, which is the default `make` implementation on GNU/Linux and macOS/Darwin, hence slight modifications may be needed in the Makefile.

# How it works
After building the source, you just need to run the resulting executable from the command line.

The daemon process will start listening the TCP port you specified for incoming HTTP GET requests.
Once a request is received, an appropriate SQL query is constructed from the name-value pairs contained in the HTTP GET request and sent to MySQL server.

The response from the server is then sent back to the client in JSON format.

# License
**restfuld** is distributed under the 2-clause BSD License.
