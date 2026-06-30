# Dolphin Lua Scripting

Scripts placed in this folder (`Sys/Scripts`) can be started from
**Tools → Lua Scripts** while a game is running. A script whose file name starts
with an underscore (e.g. `_autostart.lua`) is launched automatically when a game
boots.

Scripts are plain Lua 5.3. Dolphin drives a script through optional global
callback functions and exposes a set of host functions described below.

## Callbacks

Define any of these as global functions in your script:

| Callback           | When it runs                                              |
| ------------------ | -------------------------------------------------------- |
| `onScriptStart()`  | Once, immediately after the script is loaded.            |
| `onScriptUpdate()` | Once per polled input frame (~60 Hz while running).      |
| `onScriptCancel()` | Once, when the script is stopped from the dialog.        |
| `onStateSaved()`   | After a `SaveState()` you requested has completed.       |
| `onStateLoaded()`  | After a `LoadState()` you requested has completed.       |

## Memory — reading

All addresses are GameCube/Wii effective addresses (e.g. `0x80xxxxxx`).
The read functions accept either a single address, or a base address followed by
a chain of offsets to follow a multi-level pointer:

```lua
local v = ReadValue32(0x805C0000)            -- direct
local v = ReadValue32(0x805C0000, 0x10, 0x4) -- pointer chain
```

| Function                          | Returns                                  |
| --------------------------------- | ---------------------------------------- |
| `ReadValue8(addr [, off...])`     | unsigned byte                            |
| `ReadValue16(addr [, off...])`    | unsigned 16-bit                          |
| `ReadValue32(addr [, off...])`    | unsigned 32-bit                          |
| `ReadValueFloat(addr [, off...])` | 32-bit float                             |
| `ReadValueString(addr, count)`    | string of up to `count` bytes            |
| `GetPointerNormal(addr [, off])`  | dereferenced, normalized pointer or 0    |

## Memory — writing

Writes are ignored while a movie (`.dtm`) is being **played back**.

| Function                       | Description            |
| ------------------------------ | ---------------------- |
| `WriteValue8(addr, value)`     | write unsigned byte    |
| `WriteValue16(addr, value)`    | write unsigned 16-bit  |
| `WriteValue32(addr, value)`    | write unsigned 32-bit  |
| `WriteValueFloat(addr, value)` | write 32-bit float     |
| `WriteValueString(addr, text)` | write raw string bytes |

## Controller input (GameCube)

Input functions are ignored while a movie is being played back. Injected input
is applied on top of the real controller and is recorded into a movie if one is
being recorded.

| Function                | Description                                   |
| ----------------------- | --------------------------------------------- |
| `PressButton(name)`     | hold a button down                            |
| `ReleaseButton(name)`   | release a button                              |
| `SetMainStickX(0..255)` | main analog stick X (128 = center)            |
| `SetMainStickY(0..255)` | main analog stick Y (128 = center)            |
| `SetCStickX(0..255)`    | C-stick X (128 = center)                      |
| `SetCStickY(0..255)`    | C-stick Y (128 = center)                      |

Valid button names: `A`, `B`, `X`, `Y`, `Z`, `L`, `R`, `Start`,
`D-Up`, `D-Down`, `D-Left`, `D-Right`.

## Savestates

```lua
SaveState(true, 1)            -- save to slot 1
SaveState(false, "my.sav")    -- save to <User>/StateSaves/my.sav
LoadState(true, 1)            -- load slot 1
LoadState(false, "my.sav")    -- load a named file
```

The actual save/load runs on Dolphin's host thread; your `onStateSaved` /
`onStateLoaded` callback fires once it has finished. `LoadState` is ignored
during movie playback.

## Misc

| Function                      | Description                                            |
| ----------------------------- | ------------------------------------------------------ |
| `GetGameID()`                 | the 6-character game ID string                          |
| `GetScriptsDir()`             | absolute path of this Scripts folder                    |
| `GetFrameCount()`             | current video frame number                              |
| `GetInputFrameCount()`        | current input (poll) frame number                       |
| `SetScreenText(text)`         | show a line of on-screen text (replaced each call)      |
| `MsgBox(text [, ms])`         | show an on-screen message for `ms` milliseconds         |
| `PauseEmulation()`            | pause the core                                          |
| `SetFrameAndAudioDump(bool)`  | enable/disable frame + audio dumping                    |
| `CancelScript()`              | stop the calling script                                 |
| `SetInfoDisplay()`            | no-op (kept for compatibility with older scripts)       |

See `Example.lua` for a working starting point.
