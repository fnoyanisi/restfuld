# restfuld
**restfuld** is a naive RESTful interface implementation for MySQL database server. 
It is designed to be simple and is currently used in a test environment. 
More work is required to make it production ready.

# How to compile
You need to have MySQL development files (`mysql.h` and `libmysqlclient`) installed on your system.

`clang server.c -L/usr/local/lib/mysql -I/usr/local/include/mysql -o server -Wall -lmysqlclient -O2`

The code has been tested on a FreeBSD host machine but you should be able to compile and run on any UNIX like system.

# How it works
After compiling the source file, you just need to run the resulting executable file from th command line.
The daemon process will start to listen the TCP port you specified for incoming HTTP GET requests.
Once a request is received, an appropriate SQL querys is constructed from the name-value pairs contained in the HTTP GET request and sent to the MySQL server.
The result is then sent back to the client in JSON format.

# License
**restfuld** is distributed under the 2-clause BSD License.
