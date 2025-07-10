# libnetlink_getlink
netlink socket based net devices names and low level (mac) addresses getter

## Howto build
```
git clone <repo_url>
cd <repo_dir>
make
```
Binary is in the build directory

## Howto use
`make` and run `./build/getlink_shared`

or make with leakcheck `make LEAKCHECK=1`
run `./build/getlink_shared` and check
leakcheck report `cat /tmp/leak_info.txt`

## Initialization
Library initialization happens automatically on first use with
`pthread_once`. You may optionally call `netlink_getlink_mod_init()`
beforehand to supply a custom logging function. Passing `NULL` keeps the
default `syslog2` logger.

