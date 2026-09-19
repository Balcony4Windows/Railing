-- Minimal proof that Lua can compose the desktop: creates a label,
-- styles and positions it entirely from here, then adds it to the
-- desktop's own component tree so it's part of the real render loop.
--
-- Assigned to a global, not a local: Desktop:Add() only stores a
-- non-owning pointer (same contract as every C++ caller), so if this
-- were a local it would be garbage-collected as soon as the script
-- finishes running, leaving Desktop pointing at freed memory. Keeping
-- it referenced by a global keeps it alive for as long as Desktop
-- itself needs it.
label = Text.new()
label:SetFont("Segoe UI", 32)
label:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
label:SetPosition(40, 40)
label:SetText("Hello from Lua!")

Desktop:Add(label)

-- A live clock: System.Time() is cheap to call, but SetText() re-
-- rasterizes via GDI and re-uploads a GPU texture, so only call it when
-- the displayed string actually changes -- this is exactly the kind of
-- thing a script gets to decide for itself. This is also the pattern
-- for CPU/GPU/memory/battery/network -- same System table, same idea.
clockLabel = Text.new()
clockLabel:SetFont("Consolas", 28)
clockLabel:SetColor(Color.new(0.6, 0.85, 1.0, 1.0))
clockLabel:SetPosition(40, 90)
clockLabel:SetText(System.Time())

Desktop:Add(clockLabel)

-- Taskbar widgets: Taskbar is a Container just like Desktop, so it
-- composes the same way -- this clock lives inside the taskbar strip
-- rather than on the desktop. Positioned from Taskbar:Bounds(), not
-- hardcoded coordinates, so it stays correctly placed regardless of the
-- taskbar's actual height/width.
taskbarClock = Text.new()
taskbarClock:SetFont("Consolas", 20)
taskbarClock:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
taskbarClock:SetText(System.Time())

local function AlignTaskbarClock()
    local bar = Taskbar:Bounds()
    local size = taskbarClock:Bounds()
    local margin = 16
    taskbarClock:SetPosition(bar.x + bar.width - size.width - margin, bar.y + (bar.height - size.height) / 2)
end
AlignTaskbarClock()

Taskbar:Add(taskbarClock)

local lastTime = System.Time()

-- Registered rather than a bare global `Update` function, so other
-- scripts (e.g. anything under scripts/plugins/) can each hook the
-- frame loop too without overwriting this one.
Balcony.OnUpdate(function(deltaSeconds)
    local now = System.Time()
    if now ~= lastTime then
        lastTime = now
        clockLabel:SetText(now)
        taskbarClock:SetText(now)
        AlignTaskbarClock() -- Digit width can change (e.g. "9" -> "10"), so re-align every tick.
    end
end)

-- Desktop icons: composed entirely from existing primitives (Image,
-- Text, Tooltip), no new component type needed -- see CLAUDE.md section
-- 5. Each icon is a real Windows shell icon (Image:SetSystemIcon) plus a
-- label underneath, both sharing one OnClick (launches the target via
-- Shell.Launch) and one right-click Tooltip (a small "Open" menu).
local kIconSize = 64
local kLabelGapY = 4
local kIconSpacingY = 96
local kMenuPaddingX = 12
local kMenuPaddingY = 8

-- Returns the icon/label/menu/item so the caller can keep them
-- referenced -- Desktop:Add()/Tooltip:Add() only store non-owning
-- pointers (same contract as everywhere else in this file), so anything
-- not kept alive by a global would be garbage collected out from under
-- them.
local function CreateDesktopIcon(path, displayName, x, y)
    local icon = Image.new()
    icon:SetPosition(x, y)
    icon:SetSize(kIconSize, kIconSize)
    if not icon:SetSystemIcon(path) then
        -- Still a real, clickable icon even if extraction fails for
        -- this path -- a plain swatch beats an invisible dead spot.
        icon:SetBackgroundColor(Color.new(0.3, 0.3, 0.3, 1.0))
    end

    local label = Text.new()
    label:SetFont("Segoe UI", 14)
    label:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
    label:SetText(displayName)
    local labelSize = label:Bounds()
    label:SetPosition(x + (kIconSize - labelSize.width) / 2, y + kIconSize + kLabelGapY)

    -- Right-click menu: one "Open" item, same idea as the desktop's own
    -- Quit menu, but built here since every icon needs its own. Laid
    -- out relative to the menu's own (0,0) origin -- Tooltip:Show()
    -- translates the menu and this item together, so this stays
    -- correctly aligned no matter where it ends up being shown.
    local menu = Tooltip.new()
    menu:Initialize()
    local openItem = Text.new()
    openItem:SetFont("Segoe UI", 16)
    openItem:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
    openItem:SetText("Open")
    openItem:SetPosition(kMenuPaddingX, kMenuPaddingY)
    openItem:SetOnClick(function() Shell.Launch(path) end)
    local itemSize = openItem:Bounds()
    menu:SetBounds(RectF.new(0, 0, itemSize.width + kMenuPaddingX * 2, itemSize.height + kMenuPaddingY * 2))
    menu:Add(openItem)

    local function Launch()
        Shell.Launch(path)
    end
    icon:SetOnClick(Launch)
    label:SetOnClick(Launch)
    icon:SetTooltip(menu)
    label:SetTooltip(menu)

    Desktop:Add(icon)
    Desktop:Add(label)

    return icon, label, menu, openItem
end

-- Assigned to a global table (not local) for the same reason every
-- other Desktop-added object in this file is global -- see the note at
-- the top of the file.
desktopIcons = {}
local iconEntries = {
    {path = "C:/Windows/System32/notepad.exe", name = "Notepad"},
    {path = "C:/Windows/explorer.exe", name = "File Explorer"},
    {path = "C:/Windows/System32/cmd.exe", name = "Command Prompt"},
}
for i, entry in ipairs(iconEntries) do
    local x, y = 40, 160 + (i - 1) * kIconSpacingY
    local icon, label, menu, item = CreateDesktopIcon(entry.path, entry.name, x, y)
    table.insert(desktopIcons, {icon = icon, label = label, menu = menu, item = item})
end
