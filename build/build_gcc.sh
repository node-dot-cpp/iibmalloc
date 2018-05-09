
g++ ../../../3rdparty/cppformat/fmt/format.cc bucket_allocator_linux.cpp test_common.cpp random_test.cpp -std=c++11 -g -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-empty-body -DNDEBUG -O3 -flto -lpthread -o alloc.bin
