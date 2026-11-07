# Valeo 11 wire protocol (as implemented)

This is a reference for the commands ValeoCapture sends, as observed on the wire between a
Valeo 11 and a host. It describes behaviour, not anyone's implementation. Anything not listed
here is unknown and must not be sent: the app enforces this list with an allowlist
(`src/command_gate.cpp`) and refuses everything else.

## Transport

- FireWire SBP-2, one logical unit. Discovery candidate (Config ROM / unit directory):
  `Vendor_ID 0x0006CD`, `Command_Set_Spec_ID 0x00609E`, `Command_Set 0x0104D8`,
  `Device_Type 2` (from the Logical_Unit_Number entry, `0x020000`). Matching these is **not**
  identification; the named identity reads below are.
- The unit answers a standard SCSI INQUIRY (peripheral type 2, vendor `Intoto`,
  product `SBP-2 Printer`, revision `0.90`), but all camera work uses vendor commands. Its SCSI
  front end appears to be an SBP-2 printer reference stack: the Config ROM's text leaf is the
  IEEE 1284 device ID `MFG:HP;MDL:DJ890C/Intoto/SBP2;CLS:Printer;`.
- The unit answers only INQUIRY and the two vendor opcodes below. Any other command, for example
  TEST UNIT READY or REPORT LUNS, gets **no status at all** (not CHECK CONDITION), so a host must
  not wait on it. Management ORBs (login, LUN reset) are answered.
- INQUIRY always returns the same 36 bytes, whatever the allocation length, the ORB's data length
  or the EVPD bit and page code ask for; its ADDITIONAL LENGTH field is 0. A host driver must keep
  small data-in buffers bounded. The vendor reads stop exactly at the requested length.
- After a bus reset, while a previous login is still held, the unit answers a new login with a
  complete, error-free status block but writes no login response. It accepted a login again
  about 3.6 s after the reset. Check that the login response names a command block agent.
- Unit_Characteristics `0x00A008`: management ORB timeout 0xA0 × 500 ms (80 s), ORB size
  8 quadlets (32 bytes).

## Command descriptor block

Six bytes: `[opcode, operation, length >> 16, length >> 8, length, 0]`.

- `opcode` **0x28** = data from the back (read), **0x0F** = data to the back (write). Despite
  0x28's usual SCSI meaning, this is a six-byte vendor CDB.
- `length` is the transfer size in bytes, unsigned 24-bit. Every command carries data; a command
  with no meaningful payload is still sent with a small payload, never with length 0.

## Operations used

| Operation | Direction | Length | Meaning (as used) |
| --- | --- | --- | --- |
| `0x11` | write | 36 | Select a named parameter: BE32 `0`, then a zero-padded 32-byte ASCII name |
| `0x00` | read | 64 | Read the selected parameter; bytes 32..63 hold the value |
| `0x07` | read | 88 | Status. First BE32 = bytes ready to read (`21772608` when a frame is ready) |
| `0x22` | read | 480 | Capture parameters of the pending frame; BE32 at `0x2C` is `0` when no exposure was recorded |
| `0x14` | read | ≤ frame | Frame data, read in chunks until the announced size is drained |
| `0x12` | write | 40 | Arm: ten BE32 words (see below) |
| `0x13` | write | 56 | Start the auxiliary (dark) exposure; 56 zero bytes |
| `0x20` | write | 2 | ISO selector, BE16 (25, 50, 100, 200) — before arming only |
| `0x1C` | write | 2 | Release interface, payload `00 02` |
| `0x21` | write | 8 | Rapid mode, BE32 `[2, 10]` |
| `0x09` | write | 8 | Ready tone, BE32 `[800, 150]` |

### Identity

Named parameters read through `0x11` + `0x00` give the camera type (`VA11`), revision (`A` or `M`),
body serial (fixed width, including trailing padding), sensor size code and sensor colour code.
The firmware parameter instead starts with a binary BE32 word (`3301` on the tested back). The app
requires `VA11`, revision `A`, size code `5`, colour `C` and firmware 3301 before capture.

### Capture sequence

1. Release interface, read status, arm for a light frame, rapid mode, ready tone.
2. **Light arm** words (numbered 1–10 below): `[1, 1, 0, 0, 0, 0, 0, 4056, 2684, 21772608]`.
3. Poll status until it announces the frame, read capture parameters, then read the frame
   with `0x14` chunks (61,440 bytes each on the SCSI route).
4. **Auxiliary arm**: same words with word 2 set to `2`, then `0x13`, poll status, read the
   auxiliary frame the same way.
5. **Re-arm** for the next light frame: light-arm words with word 3 set to the auxiliary status's
   BE32 at offset `0x38`, divided by 1000; then the ready tone.

ISO changes are sent only between frames, after a status poll reports zero bytes ready.

### Failed exposure (no sync signal)

Measured on one back (firmware 3301): when the body fires with the sync cable disconnected, the
back beeps four times and announces a normal frame. The status record is byte-identical to a good
frame's, but in the capture parameters the BE32 at offset `0x2C` is `0`, and the frame is
21,772,608 bytes of `0xFFFF`. In every good light and auxiliary frame on record that word is
non-zero, and an auxiliary frame repeats its light frame's value, so it reads as the measured
exposure time. Another word that looked like a flag is `0` in many good frames and is not used.

The back stays stuck until the image cycle is finished. The app reads the capture parameters
before the frame, and when the word is `0` it drains the empty frame, runs the auxiliary step and
re-arms exactly as in steps 4 and 5, then sends the ready tone. The next shot works normally.

### Frame format

`4056 × 2684` unsigned 16-bit big-endian samples (21,772,608 bytes), 14-bit range (0..16383).
The exported image crops 24/6 and rotates 180° to a BGGR mosaic of 4008 × 2672.

## Out of scope

Firmware update, persistent calibration writes and software shutter control are not implemented
and must not be added without new, reviewed evidence. Any bus reset ends the session; the app
re-identifies the back before sending anything else.
