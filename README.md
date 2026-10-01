# PlayStation CDROM-XA format decoder library
``libxa_decode`` is a single-header library used to decode a PlayStation disc's XA audio format data in from a game disc's sector data

## Usage
First, include the library, if you want to embed it into your program's source
```c
#define XA_DECODE_IMPLEMENTATION // if you're linking against a pre-built version of the library,
                                 // you want to omit this #define
#include "xa_decode.h"
```

- use ``xa_decode_init`` to initialize the decoder
- use ``xa_decode_sector`` to decode sector data from a single, 2352-byte sector
- use ``xa_decode_available_samples`` to query the number of output samples available in the output buffer
- use ``xa_decode_get_sample[x]`` to pull one (stereo) sample from the output buffer, it is recommended to pull 16-bit samples, which will perform no conversion, for greater speed
- use ``xa_decode_uninit`` to free allocated memory, in case you did not specify an output buffer

## Compiling an object or library file
If you just want to embed the library directly into your program, you will not need this step, the ``xa_decode.c`` file is unnecessary
<br>
for an object file:
```
cc -c xa_decode.c
```

for a shared library (on Linux):
```
cc -shared -fPIC xa_decode.c -o libxa_decode.so
```

