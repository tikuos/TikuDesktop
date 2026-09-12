# Developer Calculator

Build with `make -C applications apps`, then open **Applications > Developer
Calculator** in TikuDesktop. No device is required.

- Exact unsigned 8-, 16-, 32- and 64-bit words; hexadecimal, decimal, octal and
  binary views. Input radix and word width are explicit. Overflow and negative
  input are rejected, not rounded or silently masked.
- Click a bit to toggle it, extract inclusive high:low bit ranges, or explicitly
  reverse the word's byte order. Bits outside the selected width are disabled.
- Calculate **CRC-32/ISO-HDLC** and **CRC-16/CCITT-FALSE** over literal UTF-8
  bytes or hexadecimal byte pairs. No newline or terminator is implicitly added.
  Input is limited to 255 UTF-8 bytes (127 bytes when entered as contiguous hex).
- Copy conversions or checksums to the desktop clipboard. Tab navigates controls;
  standard selection, copy/paste and undo work in fields. Escape or Done closes.

The standard `123456789` check values are `CBF43926` and `29B1`, respectively.
CRC-32 uses reflected polynomial `EDB88320`, initial/final XOR `FFFFFFFF`.
CRC-16 uses polynomial `1021`, initial value `FFFF`, no reflection or final XOR.

`build/tests/test_calculator` covers exact full-width values, overflow, byte order,
bit extraction, CRC vectors, hex decoding, native controls and rendering.
