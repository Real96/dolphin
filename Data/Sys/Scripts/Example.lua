-- Example Dolphin Lua script.
--
-- Open the dialog under Tools -> Lua Scripts, select this file and press Start
-- while a game is running. A script whose file name begins with '_' is started
-- automatically when a game boots.
--
-- A script communicates with Dolphin through a handful of callbacks that the
-- engine calls for you:
--
--   onScriptStart()   - once, right after the script is loaded
--   onScriptUpdate()  - once per polled input frame (~60 times/second)
--   onScriptCancel()  - once, when the script is stopped from the dialog
--   onStateSaved()    - after a SaveState() you requested completes
--   onStateLoaded()   - after a LoadState() you requested completes
--
-- See Scripts/README.md for the full list of available functions.

local frames = 0

function onScriptStart()
  MsgBox("Example.lua started for game " .. GetGameID(), 4000)
end

function onScriptUpdate()
  frames = frames + 1

  -- Show some live information on screen. SetScreenText replaces the previous
  -- line each frame, so calling it every update does not stack up.
  SetScreenText(string.format(
    "Example.lua  |  game: %s  |  frame: %d  |  script frames: %d",
    GetGameID(), GetFrameCount(), frames))

  -- Example memory read (commented out: addresses are game specific).
  -- local health = ReadValue16(0x803CA764)
  -- SetScreenText("Health: " .. health)

  -- Example input injection (commented out so it does not surprise you):
  -- hold A for one second, then release it.
  -- if frames == 60 then PressButton("A") end
  -- if frames == 120 then ReleaseButton("A") end
end

function onScriptCancel()
  SetScreenText("")
  MsgBox("Example.lua stopped", 2000)
end
