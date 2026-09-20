-- Taskbar buttons: one per pinned app (whether or not it's currently
-- running) plus one per running-but-unpinned window (icon + title),
-- refreshed periodically. Click activates a running window or launches
-- a pinned-but-not-running app; right-click gives "Close"/"Pin"/"Unpin"
-- as applicable. See CLAUDE.md section 13 ("Taskbar consumes
-- RunningWindows") and TODO.md.
--
-- Lives under scripts/plugins/ (auto-loaded, see DesktopEnvironment) as
-- a real exercise of the plugin system built for exactly this kind of
-- feature, not a toy example.
local kButtonHeight = 40
local kIconSize = 28
local kLabelGapX = 8
local kButtonGapX = 20
local kStartX = 12
local kMenuPaddingX = 12
local kMenuPaddingY = 8
local kRefreshInterval = 1.0
local kMaxTitleChars = 28

-- Stacks a vertical list of {text=, onClick=} items into a fresh
-- Tooltip, sized to fit the widest item -- duplicated from desktop.lua
-- rather than shared (same precedent as WideToUtf8/Utf8ToWide in
-- core/ui LuaBindings.cpp: not worth a shared-utility module for one
-- small, two-consumer function).
local function BuildStackedMenu(items)
    local menu = Tooltip.new()
    menu:Initialize()

    -- menu:Add below only stores a non-owning C++ pointer into `text`
    -- (same contract as every other Container::Add caller -- see the
    -- note on `label` at the top of desktop.lua). Nothing else in Lua
    -- keeps any individual `text` reachable once this function returns,
    -- so without collecting them here, Lua's GC is free to collect each
    -- one the next time it runs -- leaving `menu` holding dangling
    -- children. That's what an apparently empty right-click menu (or an
    -- outright crash opening one) actually was. Returned alongside
    -- `menu` so the caller can keep both alive together for as long as
    -- it keeps the menu itself.
    local textItems = {}
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
        table.insert(textItems, text)
    end
    menu:SetBounds(RectF.new(0, 0, maxWidth + kMenuPaddingX * 2, y + kMenuPaddingY))

    return menu, textItems
end

-- Long window titles (a browser tab, a document path, ...) would
-- otherwise crowd out every other button; clip to a fixed length
-- instead. Byte-based, so a multi-byte UTF-8 title can in principle be
-- cut mid-character -- no worse than every other plain string op this
-- codebase already does on window titles, and rare in practice.
local function TruncateTitle(title)
    if #title > kMaxTitleChars then
        return title:sub(1, kMaxTitleChars - 1) .. "\xe2\x80\xa6" -- UTF-8 "..."
    end
    return title
end

-- Running-but-unpinned windows: id -> {icon=, label=, menu=, title=}. A
-- local, not a global -- but captured by the Balcony.OnUpdate closure
-- below, which itself lives forever in Balcony.UpdateHandlers, so this
-- (and everything it holds) stays reachable exactly as long as it
-- needs to.
local runningButtons = {}

-- Left-to-right order for `runningButtons`, in the order each button
-- was created -- kept across refreshes and mutated in place (append on
-- create, remove on close) rather than rebuilt from Windows.Running()
-- each time. Windows.Running() reflects Z-order (foreground window
-- first), which changes every time any window is focused or moved; if
-- layout order came straight from it, activating a button would itself
-- reorder the whole row out from under the cursor.
local runningOrder = {}

-- Pinned apps: path -> {icon=, label=, menu=, title=, windowId=}.
-- windowId is nil when the pinned app isn't currently running (a
-- launchable placeholder button) and the running window's id when it
-- is (a merged button) -- see Refresh().
local pinnedButtons = {}

-- Pin bookkeeping, loaded once at startup and kept in sync with
-- Persistence on every mutation (see SetPinned below). `pinnedOrder` is
-- the taskbar pin order (always append-to-end -- reordering pins is
-- out of scope for now); `pinnedSet`/`pinnedDisplayNames` are
-- path -> bool / path -> string lookups derived from it.
local pinnedOrder = {}
local pinnedSet = {}
local pinnedDisplayNames = {}
for _, app in ipairs(Persistence.PinnedApps()) do
    table.insert(pinnedOrder, app.path)
    pinnedSet[app.path] = true
    pinnedDisplayNames[app.path] = app.displayName
end

local function PersistPinned()
    local list = {}
    for _, path in ipairs(pinnedOrder) do
        table.insert(list, {path = path, displayName = pinnedDisplayNames[path]})
    end
    Persistence.SetPinnedApps(list)
end

local function RemoveButtonVisuals(entry)
    Taskbar:Remove(entry.group)
end

-- Attaches the same click handler and right-click menu to the group
-- AND to icon/label individually -- redundant by design. Container's
-- hit-testing (used for both click and right-click routing) always
-- prefers the most specific/deepest hit: land precisely on the icon or
-- the label glyph and THAT component resolves as the hit; land
-- anywhere else within the button's footprint (the gap between them,
-- or padding around either) and neither child matches, so it falls
-- through to the group itself. Wiring all three identically means the
-- button responds correctly no matter which of the three ends up being
-- the actual hit -- this is what makes the WHOLE button clickable
-- instead of only each glyph's own tight bounds.
local function WireButtonInteraction(group, icon, label, menu, onClick)
    for _, component in ipairs({group, icon, label}) do
        component:SetOnClick(onClick)
        component:SetTooltip(menu)
    end
end

-- Forward-declared: CreateUnpinnedButton/CreatePinnedButton's context
-- menus call SetPinned, and SetPinned calls Refresh, before either is
-- assigned below. Safe -- a Lua closure captures the local variable
-- itself, not its value at closure-creation time, and neither is
-- actually called until well after both are assigned (a menu item
-- click, or the next timer tick).
local SetPinned
local Refresh

local function CreateUnpinnedButton(win)
    local icon = Image.new()
    icon:SetSize(kIconSize, kIconSize)
    if not icon:SetWindowIcon(win.id) then
        icon:SetBackgroundColor(Color.new(0.3, 0.3, 0.3, 1.0))
    end

    local label = Text.new()
    label:SetFont("Segoe UI", 14)
    label:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
    label:SetText(TruncateTitle(win.title))

    local menuItems = {
        {text = "Close", onClick = function() Windows.Close(win.id) end},
    }
    if win.path ~= "" then
        table.insert(menuItems, {text = "Pin to Taskbar", onClick = function()
            SetPinned(win.path, win.title, true)
        end})
    end
    local menu, menuItemLabels = BuildStackedMenu(menuItems)

    -- Wraps icon+label so the whole button footprint -- not just each
    -- glyph's own tight bounds -- is one clickable/right-clickable
    -- unit; see WireButtonInteraction. Positioned/sized by
    -- LayoutButtons, same as icon/label themselves.
    local group = Container.new()
    group:Add(icon)
    group:Add(label)
    WireButtonInteraction(group, icon, label, menu, function() Windows.Activate(win.id) end)

    Taskbar:Add(group)

    -- menuItemLabels kept only to keep the menu's own Text items
    -- reachable for as long as this button (and its menu) exist -- see
    -- BuildStackedMenu.
    return {group = group, icon = icon, label = label, menu = menu, menuItemLabels = menuItemLabels, title = win.title}
end

-- `win` is nil for a pinned app that isn't currently running (a
-- launchable placeholder); non-nil for one merged with a running
-- window. If a pinned app has multiple open windows, only the one
-- Windows.Running() lists first (topmost, per its Z-order) is
-- represented -- full multi-window grouping (like the real taskbar's
-- hover flyout) is out of scope, same as window previews in TODO.md.
local function CreatePinnedButton(path, win)
    local icon = Image.new()
    icon:SetSize(kIconSize, kIconSize)
    local gotIcon
    if win then
        gotIcon = icon:SetWindowIcon(win.id)
        if not gotIcon then
            gotIcon = icon:SetSystemIcon(path)
        end
    else
        gotIcon = icon:SetSystemIcon(path)
    end
    if not gotIcon then
        icon:SetBackgroundColor(Color.new(0.3, 0.3, 0.3, 1.0))
    end

    local title = win and win.title or pinnedDisplayNames[path]
    local label = Text.new()
    label:SetFont("Segoe UI", 14)
    label:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
    label:SetText(TruncateTitle(title))

    local menuItems = {}
    if win then
        table.insert(menuItems, {text = "Close", onClick = function() Windows.Close(win.id) end})
    end
    table.insert(menuItems, {text = "Unpin from Taskbar", onClick = function()
        SetPinned(path, nil, false)
    end})
    local menu, menuItemLabels = BuildStackedMenu(menuItems)

    local group = Container.new()
    group:Add(icon)
    group:Add(label)
    WireButtonInteraction(group, icon, label, menu, function()
        if win then
            Windows.Activate(win.id)
        else
            Shell.Launch(path)
        end
    end)

    Taskbar:Add(group)

    -- menuItemLabels kept only to keep the menu's own Text items
    -- reachable -- see BuildStackedMenu.
    return {group = group, icon = icon, label = label, menu = menu, menuItemLabels = menuItemLabels, title = title, windowId = win and win.id or nil}
end

-- Toggles a pin, persists immediately, and re-lays-out right away
-- rather than waiting for the next refresh tick.
SetPinned = function(path, displayName, pinned)
    if pinned then
        if not pinnedSet[path] then
            pinnedSet[path] = true
            pinnedDisplayNames[path] = displayName or path
            table.insert(pinnedOrder, path)
        end
    elseif pinnedSet[path] then
        pinnedSet[path] = nil
        pinnedDisplayNames[path] = nil
        for i, p in ipairs(pinnedOrder) do
            if p == path then
                table.remove(pinnedOrder, i)
                break
            end
        end
        -- Remove its visuals now -- otherwise this button is orphaned
        -- on screen, since nothing else in Refresh() cleans up a path
        -- that just left pinnedOrder.
        local entry = pinnedButtons[path]
        if entry then
            RemoveButtonVisuals(entry)
            pinnedButtons[path] = nil
        end
    end
    PersistPinned()
    Refresh()
end

-- Exposed globally so desktop.lua's icon context menu can pin a
-- shortcut without this plugin needing to know anything about desktop
-- icons -- the one place this feature introduces cross-plugin coupling.
-- Safe despite scripts/plugins/ loading after desktop.lua: Lua resolves
-- globals at CALL time, not closure-creation time, and all script
-- loading finishes (during DesktopEnvironment::Initialize) before the
-- message loop -- and so before any real click -- ever starts.
TaskbarApps = {
    Pin = function(path, displayName) SetPinned(path, displayName, true) end,
    Unpin = function(path) SetPinned(path, nil, false) end,
}

-- Left-aligned row, widths measured from each label's actual rendered
-- size rather than a guessed constant -- same reasoning as everywhere
-- else in this codebase that sizes from real content. Takes button
-- entries directly (not ids/paths): pinned and unpinned buttons come
-- from two different keyspaces, so the caller already has to look them
-- up itself.
local function LayoutButtons(entries)
    local bar = Taskbar:Bounds()
    local x = kStartX
    for _, b in ipairs(entries) do
        local y = bar.y + (bar.height - kButtonHeight) / 2
        b.icon:SetPosition(x, y + (kButtonHeight - kIconSize) / 2)
        local labelHeight = b.label:Bounds().height
        local labelWidth = b.label:Bounds().width
        b.label:SetPosition(x + kIconSize + kLabelGapX, y + (kButtonHeight - labelHeight) / 2)

        -- The group's own bounds are what makes the gap between icon
        -- and label (and any padding around either) clickable too --
        -- see WireButtonInteraction.
        local totalWidth = kIconSize + kLabelGapX + labelWidth
        b.group:SetBounds(RectF.new(x, y, totalWidth, kButtonHeight))

        x = x + totalWidth + kButtonGapX
    end
end

Refresh = function()
    local running = Windows.Running()

    -- First (topmost, per Windows.Running()'s Z-order) window for each
    -- distinct process path -- what a pinned app merges with.
    local runningByPath = {}
    for _, win in ipairs(running) do
        if win.path ~= "" and not runningByPath[win.path] then
            runningByPath[win.path] = win
        end
    end

    -- Pinned apps, in pin order: merge with a running instance if one
    -- matches by path, else keep/create a launchable placeholder.
    for _, path in ipairs(pinnedOrder) do
        local win = runningByPath[path]
        local newWindowId = win and win.id or nil
        local entry = pinnedButtons[path]
        if not entry then
            pinnedButtons[path] = CreatePinnedButton(path, win)
        elseif entry.windowId ~= newWindowId then
            -- Running-state changed (launched, closed, or a different
            -- window now matches) -- rebuild rather than mutate, since
            -- Tooltip has no "remove item" API to adjust an existing
            -- menu's Close item in place. Cheap at this refresh cadence.
            RemoveButtonVisuals(entry)
            pinnedButtons[path] = CreatePinnedButton(path, win)
        elseif win and entry.title ~= win.title then
            entry.label:SetText(TruncateTitle(win.title))
            entry.title = win.title
        end
    end

    -- Running-but-unpinned windows: same stable-order bookkeeping as
    -- before, just skipping anything now pinned (skipped windows are
    -- simply never marked `seen`, so the compaction pass below removes
    -- their old button the moment a window becomes pinned).
    local seen = {}
    for _, win in ipairs(running) do
        if not pinnedSet[win.path] then
            seen[win.id] = true
            local existing = runningButtons[win.id]
            if not existing then
                runningButtons[win.id] = CreateUnpinnedButton(win)
                table.insert(runningOrder, win.id)
            elseif existing.title ~= win.title then
                existing.label:SetText(TruncateTitle(win.title))
                existing.title = win.title
            end
        end
    end

    local compacted = {}
    for _, id in ipairs(runningOrder) do
        if seen[id] then
            table.insert(compacted, id)
        else
            RemoveButtonVisuals(runningButtons[id])
            runningButtons[id] = nil
        end
    end
    runningOrder = compacted

    local display = {}
    for _, path in ipairs(pinnedOrder) do
        table.insert(display, pinnedButtons[path])
    end
    for _, id in ipairs(runningOrder) do
        table.insert(display, runningButtons[id])
    end
    LayoutButtons(display)
end

Refresh()

local elapsed = 0.0
Balcony.OnUpdate(function(deltaSeconds)
    elapsed = elapsed + deltaSeconds
    if elapsed >= kRefreshInterval then
        elapsed = 0.0
        Refresh()
    end
end)
