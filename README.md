# DNS Server Multithreaded
Is a RFC 1035 compatible Domain Name System Server, that implements concurrent query processing
and non-blocking cache read access, if available.
## Usage
Binary has 3 positional arguments:
```
$ dns_server port "hosts_file_path" "forward_server_addr:fwd_srv_port"(optional)
```
where:
 * port - port number for listening
 * hosts_file_path - path for dns cache 
 * forward_server_addr:fwd_srv_port - optional external DNS server, to forward queries to, 
 if cache entry is missing, default is Google DNS
 
Example usage:
```
$ dns_server 53 "hosts" "127.0.0.1:53"
```
## Features
 * Supports Normal Host Address Internet Queries and Responses
 * Implements DNS caching, in format of system [hosts example](hosts). Cache file is updated on server shutdown
 * Supports forwarding queries to Forward Server, hence related argument option

## Dependencies
1. A C++ compiler that supports C++17 standard.
The following compilers should work:

  * [gcc 4+](https://gcc.gnu.org/)

  * [clang 7+](https://clang.llvm.org/)

2. [CMake 3.5+](https://cmake.org/)
## Build
Target platform is Linux, POSIX is used. You can build main target via CMake.

For example, from build directory:
```
 $ cmake "path to the CMakeLists.txt" -DCMAKE_CXX_COMPILER:STRING=/usr/bin/clang++-11 -DCMAKE_BUILD_TYPE:String=Release
 $ make
```
dns_server target will be built.
## Testing
Test server response via "dig" client from local machine.
Example:
```
$ dig @127.0.0.1 www.example.com -p "your_server_port"
```
Expected output:
```
$ dig @127.0.0.1 www.example.com  -p 10000

; <<>> DiG 9.16.1-Ubuntu <<>> @127.0.0.1 www.example.com -p 10000
; (1 server found)
;; global options: +cmd
;; Got answer:
;; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 42801
;; flags: qr rd; QUERY: 1, ANSWER: 1, AUTHORITY: 0, ADDITIONAL: 0

;; QUESTION SECTION:
;www.example.com.		IN	A

;; ANSWER SECTION:
www.example.com.	60	IN	A	93.184.216.34

;; Query time: 27 msec
;; SERVER: 127.0.0.1#10000(127.0.0.1)
;; WHEN: Tue Feb 21 16:04:59 MSK 2023
;; MSG SIZE  rcvd: 49
```
## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
