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

## Customizing logging

The source includes a weak fallback implementation of `syslog2_`.  When linked
with the `syslog2` module or with your own `syslog2_` function, the fallback is
automatically replaced.  This keeps the library free of direct dependencies
while still allowing flexible log routing.

