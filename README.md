# Simple FTP server

Simple FTP server, both written in C. 

## What can and can not do this server

**Can**:
+ Log in with a USER and PASSword given at their related functions.
+ LIST all files in the directory given at line 170.
+ EXIT from the server.

**Can NOT**:
+ GET files from the server.
+ PUT, upload files to the server.
+ Handle more than one connection at a time.

## Compile and run:

```bash
make
./server
```

## References used to make this project

+ [Definition](https://en.wikipedia.org/wiki/File_Transfer_Protocol)
+ [FTP Commands](https://en.wikipedia.org/wiki/List_of_FTP_commands)
+ [FTP Return Codes](https://en.wikipedia.org/wiki/List_of_FTP_server_return_codes)
+ [File Transfer using TCP socket in C by Idiot Developer](https://youtu.be/7d7_G81uews)
+ [Minimalist Web Server in C by Nir Litchman](https://www.youtube.com/watch?v=2HrYIl6GpYg)
