# iibmalloc
An implementation of malloc() for Message-Passing Programs

Properties:
* intended for allocating persistent state and temporaries of Message-Passing Programs
  * does NOT support inter-thread malloc()/free(). To exchange messages between threads, a different (thread-aware) allocator is necessary (thread-aware one will be less efficient, but it won't be used much).
* testing shows it is very fast (when simulating real-world loads, outperforms tcmalloc at least 1.5x; for test results, see an article in upcoming Overload journal scheduled for Aug'18 issue). 
  * Uses cross-platform trickery (applies to most of MMU-enabled CPUs) which enables placing information into a dereferenceable pointer (see the same article for funny details). 
* supports per-thread serialization (enables serializing thread/(Re)Actor state)
* we're working on optional support for guaranteed-memory-safe C++ (see https://github.com/node-dot-cpp/safe-memory project)
