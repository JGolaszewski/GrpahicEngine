# MikkTSpace reference implementation

The `mikktspace.cpp` and `mikktspace.h` files are from the official
[MikkTSpace repository](https://github.com/mmikk/MikkTSpace), commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`. The upstream `.c` source is kept
unmodified and compiled as C++ within the asset module. The upstream license
and copyright notice remain in both source files. The implementation is
compiled with MSVC warnings disabled because the unaltered reference code
triggers the engine's `/W4 /WX` shadowing warnings.
