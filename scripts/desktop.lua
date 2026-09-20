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
-- Text, Tooltip, Container), no new component type needed -- see
-- CLAUDE.md section 5. Each icon is a real Windows shell icon
-- (Image:SetSystemIcon) plus a label underneath, both sharing one
-- OnClick (launches the target via Shell.Launch) and one right-click
-- Tooltip (a small "Open"/"Pin to Taskbar" menu). Both are wrapped in a
-- Container purely so the pair can be dragged as one unit (Container's
-- Translate already recurses into children) -- click/tooltip handling
-- stays on icon/label individually, unaffected by the wrapping.
local kIconSize = 64
local kLabelGapY = 4
local kIconSpacingY = 96
local kMenuPaddingX = 12
local kMenuPaddingY = 8

-- Stacks a vertical list of {text=, onClick=} items into a fresh
-- Tooltip, sized to fit the widest item. Small enough, and used by
-- exactly two files (this one and taskbar_apps.lua) with no shared
-- state between them, that duplicating it there is preferable to a
-- shared-utility module for one function -- same precedent as
-- WideToUtf8/Utf8ToWide in core/ui LuaBindings.cpp.
local function BuildStackedMenu(items)
    local menu = Tooltip.new()
    menu:Initialize()

    local maxWidth = 0
    local y = kMenuPaddingY
    for _, item in ipairs(items) do
        local text = Text.new()
        text:SetFont("Segoe UI", 16)
        text:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
        text:SetText(item.text)
        text:SetOnClick(item.onClick)
        text:SetPosition(kMenuPaddingX, y)
        local size = text:Bounds()
        maxWidth = math.max(maxWidth, size.width)
        y = y + size.height
        menu:Add(text)
    end
    menu:SetBounds(RectF.new(0, 0, maxWidth + kMenuPaddingX * 2, y + kMenuPaddingY))

    return menu
end

-- Returns the group/icon/label/menu so the caller can keep them
-- referenced -- Desktop:Add()/Tooltip:Add()/Container:Add() only store
-- non-owning pointers (same contract as everywhere else in this file),
-- so anything not kept alive by a global would be garbage collected out
-- from under them.
local function CreateDesktopIcon(path, displayName, defaultX, defaultY)
    local saved = Persistence.DesktopIconPosition(path)
    local x, y = defaultX, defaultY
    if saved then
        x, y = saved.x, saved.y
    end

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

    -- Right-click menu: laid out relative to the menu's own (0,0)
    -- origin -- Tooltip:Show() translates the menu and its items
    -- together, so this stays correctly aligned no matter where it ends
    -- up being shown. "Pin to Taskbar" reuses the taskbar plugin's own
    -- pin bookkeeping via a guarded global (see taskbar_apps.lua) --
    -- guarded so this keeps working even if that plugin is ever removed.
    local menu = BuildStackedMenu({
        {text = "Open", onClick = function() Shell.Launch(path) end},
        {text = "Pin to Taskbar", onClick = function()
            if TaskbarApps then TaskbarApps.Pin(path, displayName) end
        end},
    })

    local function Launch()
        Shell.Launch(path)
    end
    icon:SetOnClick(Launch)
    label:SetOnClick(Launch)
    icon:SetTooltip(menu)
    label:SetTooltip(menu)

    -- The draggable unit is the group, not icon/label individually --
    -- Component::FindDraggable resolves to the nearest draggable
    -- ancestor, so grabbing either the icon or the label starts a drag
    -- of the whole group, while FindHit (used for clicks/right-click)
    -- still resolves to icon/label themselves, unaffected.
    local group = Container.new()
    group:SetBounds(RectF.new(x, y, math.max(kIconSize, labelSize.width), kIconSize + kLabelGapY + labelSize.height))
    group:Add(icon)
    group:Add(label)
    group:SetDraggable(true)
    group:SetOnDragEnd(function()
        local bounds = group:Bounds()
        Persistence.SetDesktopIconPosition(path, bounds.x, bounds.y)
    end)

    Desktop:Add(group)

    return group, icon, label, menu
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
    local group, icon, label, menu = CreateDesktopIcon(entry.path, entry.name, x, y)
    table.insert(desktopIcons, {group = group, icon = icon, label = label, menu = menu})
end
