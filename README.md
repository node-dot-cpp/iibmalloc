# iibmalloc
An implementation of malloc() for Message-Passing Programs

Properties:
* do NOT support inter-thread malloc()/free()
* very fast (when simulating real-world loads, outperforms tcmalloc at least 1.5x; for test results, see an article in upcoming Overload journal scheduled for Aug'18 issue). 
* supports per-thread serialization
* planned to provide optional support for guaranteed-memory-safe C++ (see https://github.com/node-dot-cpp/safe-memory project)
