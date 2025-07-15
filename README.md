# dwvw - an encoder / decoder for the DWVW audio format

The dwvw utility converts AIFF files to and from the custom DWVW format of [Typhoon](https://nuedge.net/typhoon2000/Typhoon2000.htm) OS for the Yamaha TX16W and  the [Cyclone](https://soniccharge.com/forum/topic/433-a-blast-from-the-past) emulator software.

DWVW was invented 1991 by Magnus Lidström and is copyright 1993 by NuEdge
Development.

DWVW is a lossless audio compression codec encapsulated in the AIFC audio file format.

If you're looking at a DWVW implementation to use in your own program you might want to look at [libsndfile](https://github.com/libsndfile/libsndfile) instead.

## Usage

```
dwvw compress input.aiff output.C01
```

```
dwvw decompress input.C01 output.aiff
```

## Bit depth

The `compress` subcommand generates 12-bit audio. This is the native audio bit depth of the Yamaha TX16W. If the bit depth of the source audio is higher than 12 bits then `compress` will truncate to 12 bits. There is no dithering.

The `decompress` subcommand rounds the input bit depth up to the nearest multiple of 8. That means that DWVW 12-bit audio comes out as 16-bit AIFF.





