# arp cache getter via netlink kernel socket
No dependencies program.

### Initialization

Use `nlarpcache_mod_init()` to override logging or time callbacks:

```c
nlarpcache_mod_init(&(netlink_arp_cache_mod_init_args_t){
  .log = custom_log,
  .get_time = custom_time});
```

## Howto build
```
git clone <repo_url>
cd <repo_dir>
make arp
```
Binary is in the build directory

## Demonstration

![](https://github.com/legale/netlink_arp_cache/blob/main/demo.gif)
