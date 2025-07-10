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
Before using the library functions you may call `netlink_getlink_mod_init()`
to inject a custom logger. The function accepts a pointer to
`netlink_getlink_mod_init_args_t` with a single field `syslog2_func`.  Passing
`NULL` uses the default `syslog2` logger.

```c
static void my_logger(int pri, const char *func, const char *file,
                      int line, const char *fmt, bool nl, va_list ap) {
  vfprintf(stderr, fmt, ap);
  if (nl) fprintf(stderr, "\n");
}

netlink_getlink_mod_init(&(netlink_getlink_mod_init_args_t){
  .syslog2_func = my_logger,
});
```

