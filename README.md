### uhid_fake_battery

Creates a fake battery on uhid for the hid-input driver, takes tcp commands to change states

Currently only supports changing charging state and level, the only two items supported by the hid-input driver

Mostly for forwarding battery state into VMs that does not provide battery state forwarding, will likely also need guest configurations to use the states

#### Example

```
# build the project, requires g++
bash build.sh

# create a fake battery, listen on all ipv4 address and tcp port 7777
# requires access to /dev/uhid, either tag it in udev or run this as root
./uhid_fake_battery 0.0.0.0 7777

# set fake battery level to 75%
echo 'l75' | netcat -N 127.0.0.1 7777

# set fake battery charging state to charging
echo 'c1' | netcat -N 127.0.0.1 7777

# set fake battery charging state to not charging
echo 'c0' | netcat -N 127.0.0.1 7777

# poll files for fake battery's level and charging state
./poll /sys/class/power_supply/BAT0/capacity /sys/class/power_supply/AC/online 127.0.0.1 7777
```

