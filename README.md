# gpio_press_count

## About

Just a simple C Linux application that can be used to detect sequences of edges
on GPIO inputs. There are many similar applications/scripts on the internet, but
they are either written/need high-level language like Python, or use low-level
IO primitives (select, poll, epoll, etc.) directly resulting in almost incomprehensible code.
Here I settled on C and [libdill](http://libdill.org/). Structured concurrency, lexically scoped threads and
channels, really simplify IO-heavy code.

## Usage

When run, `gpio_press_count` excepts at least two arguments. First one is the GPIO number.
All the other are the counts we want to recognize. So for example:
```
gpio_press_count 4 3 5
```
will configure GPIO4 as input and count rising edges on this pin that are not more than
1s from each other. If the count produced this way matches any of the numbers passed
as the other arguments, the application will exit with exit code matching the count.
`powerbutton.sh` is an example script that illustrates how to act on those exit codes.

## Compiling

Below description is aimed at compiling for OpenWRT. The process for other distros/compilers
should be similar.
 
 1. Export the OpenWRT toolchain paths:
 ```
 export PATH=$PATH:/path/to/openwrt/staging_dir/toolchain-xxx/bin
 export STAGING_DIR=/path/to/openwrt/staging_dir/
 ```
 2. Clone and compile [libdill](http://libdill.org/) (adjust `--host` and `--build`):
 ```
 git clone -b 2.14 git@github.com:sustrik/libdill.git
 cd libdill
 ./autogen.sh
 ./configure --prefix=/path/to/openwrt/staging_dir/toolchain-xxx/bin \
             --build=x86_64-pc-linux-gnu --host=aarch64-openwrt-linux-musl
 ```

 3. Compile the main project via CMake:
 ```
 mkdir build
 cd build
 cmake -DCMAKE_C_COMPILER=aarch64-openwrt-linux-gcc -DCMAKE_BUILD_TYPE=Release ..
 make
 ```

The default CMake configuration links with `libdill` statically.
VSCode's CMake extension should pick-up the configuration (re-scanning CMake kits might be needed).

## TODO

 - [ ] add file-based configuration
 - [ ] expose all configuration options (e.g. regarding LED control)
 - [ ] add redirecting logs to system log
 - [ ] running in  background (standalone service daemon, no need for 'external' shell script)

