# Chat server/client

Build requirements:
- C++11 compiler
- CMake > 3.11.0
- asio library (for async I/O)
- google protobuf v2 (for serialization)

Other 3rd party libraries used (sources are provided):
- CLI11 (command line parameters parser)
- spdlog (logger)

Run build.sh script from project directory:

    > ./build.sh

It will create <project>/build directory and build binaries for client and server.

# Logging

All logging information is written into 'log.txt' in the running directory. Could be changed to either console or syslog by uncommenting code in Log.hpp.
