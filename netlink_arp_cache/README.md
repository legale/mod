# arp cache getter via netlink kernel socket
No dependencies program.

### Customization

Logging and timing helpers are provided via weak fallback functions such as
`syslog2_` and `get_current_time_ms`.  Linking with the `syslog2` and
`timeutil` modules or supplying your own implementations overrides these stubs.

## Howto build
```
git clone <repo_url>
cd <repo_dir>
make arp
```
Binary is in the build directory

## Demonstration

![](https://github.com/legale/netlink_arp_cache/blob/main/demo.gif)
