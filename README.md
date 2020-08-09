# MBus collectd plugin

Steps:
 - Install libmbus (see https://github.com/rscada/libmbus)
 - Compile
 - Copy mbus.so to the collectd plugin directory (e.g. /usr/lib/collectd)
 - Copy types.mbus.db to /usr/share/collectd/
 - Configure collectd to use the plugin

## Example config

For more info see https://github.com/collectd/collectd/pull/65


```
<Plugin mbus>
    IsSerial     true
    SerialDevice "/mnt/mbus"
    Host         "none"
    Port         256
    <Slave 59>
        IgnoreSelected false
        Record         0
        Record         2
        Record         3
        Record         4
    </Slave>
</Plugin>
```
