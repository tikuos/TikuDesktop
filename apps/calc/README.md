# Calculator

Build with `make -C applications apps`, then open **Applications >
Calculator** in TikuDesktop.

- A display and a keypad: the digits, a point, the four operations,
  equals, C, plus/minus and percent.  Operations bind left to right, as a
  pocket calculator's do, so `2 + 3 × 4 =` is 20.
- The keyboard works too: digits, `.`, `+ - * /`, Return for equals,
  Backspace, and Escape for C.
- Dividing by zero shows *Error* until C is pressed.
- **Calculator > Copy result** puts the display on the desktop clipboard.

`build/tests/test_calc` covers the arithmetic, the keys and the window.
For exact words, bit fields and checksums there is the Developer
Calculator.
