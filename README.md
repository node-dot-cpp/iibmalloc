# iibmalloc
An implementation of malloc() for Message-Passing Programs

## Properties

* intended for allocating persistent state and temporaries of Message-Passing Programs
  * does NOT support inter-thread malloc()/free(). To exchange messages between threads, a different (thread-aware) allocator is necessary (thread-aware one will be less efficient, but it won't be used much).
* testing shows it is very fast (when simulating real-world loads, outperforms tcmalloc at least 1.5x; for test results, see an article in upcoming Overload journal scheduled for Aug'18 issue). 
  * Uses cross-platform trickery (applies to most of MMU-enabled CPUs) which enables placing information into a dereferenceable pointer (see the same article for funny details). 
* supports per-thread serialization (enables serializing thread/(Re)Actor state)
* we're working on optional support for guaranteed-memory-safe C++ (see https://github.com/node-dot-cpp/safe-memory project)

# Current Status

* Master branch contains supposedly-usable malloc()/free() (No Known Bugs)
* WIP: postponed free (necessary for memory-safe C++)
* WIP: allocator-level serialization


## Getting Started
Clone this repository with modules
```
git clone --recursive https://github.com/node-dot-cpp/iibmalloc.git
```
Enter to the poject directory :
```
cd iibmalloc
```
### Build project

#### Linux

To build library in Linux

**Debug and Release version**
Run in console 
```
cmake .
make
```
or
```
mkdir build
cd build 
cmake ..
```
And you will get both debug and release library in `iibmalloc/build/lib` folder


If you want to build `test_iibmalloc.bintest.bin` just run in console
```
make  test_iibmalloc.bin
```
If you want to build `test_iibmalloc_d.bin` just run in console
```
make  test_iibmalloc_d.bin
```
All binaries will be located in `iibmalloc/test/build/bin`

**Run tests**

Run in console
```
make test
```
It must passed 8 tests.