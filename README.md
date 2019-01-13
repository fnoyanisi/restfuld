# restfuld
**restfuld** is a naive RESTful interface implementation for MySQL database server. 
It is designed to be simple and is currently used in a test environment. 
The code has been uploaded on to GitHub to make it publicly availabe but more work is required to make it production ready.

# How to compile
You need to have MySQL client development files (`mysql.h` and `libmysqlclient`) installed on your system. Please check with your operating system's package manager whether these files are installed with MySQL client software.

To compile `restfuld`, simply type `make` inside the main directory.

The code has been tested on a FreeBSD host machine but you should be able to compile and run on any UNIX like system.

# How it works
After compiling the source, you just need to run the resulting executable file from the command line.

The daemon process will start to listen the TCP port you specified for incoming HTTP GET requests.
Once a request is received, an appropriate SQL querys is constructed from the name-value pairs contained in the HTTP GET request and sent to the MySQL server.

The response from MySQL server is then sent back to the client in JSON format.

# License
**restfuld** is distributed under the 2-clause BSD License.
