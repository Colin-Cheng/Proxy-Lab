# Proxy-Lab

### This is a basic sequential proxy that handles HTTP/1.0 GET requests.

#### You can use `netcat` to run this project:
1. In the Linux command line, type `make` to compile the code.

2. Type `./proxy <port#>`. The `port#` is the port on which your proxy will listen for incoming connections.

3. Use `nc localhost <port#>` to open connections to servers.

4. Use `GET <url-you-want-to-go> HTTP/1.1` to send the HTTP request to the proxy.

You can also test the proxy using your web broswer. You can change your broswer's settings to configure it to work with a proxy. 
