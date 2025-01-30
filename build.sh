g++ main.cpp -o uhid_fake_battery -static -lrt -pthread -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
g++ poll.cpp -o poll -static
g++ android/hwclock_loop.cpp -o android/hwclock_loop -static
