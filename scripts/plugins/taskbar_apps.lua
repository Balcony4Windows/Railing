-- One taskbar button per top-level running window (icon + title),
-- refreshed periodically. Click activates the window; right-click gives
-- a small "Close" menu. See CLAUDE.md section 13 ("Taskbar consumes
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

-- id -> {icon=, label=, menu=, closeItem=, title=}. A local, not a
-- global -- but captured by the Balcony.OnUpdate closure below, which
-- itself lives forever in Balcony.UpdateHandlers, so this (and
-- everything it holds) stays reachable exactly as long as it needs to.
local buttons = {}

local function CreateButton(win)
    local icon = Image.new()
    icon:SetSize(kIconSize, kIconSize)
    if not icon:SetWindowIcon(win.id) then
        icon:SetBackgroundColor(Color.new(0.3, 0.3, 0.3, 1.0))
    end

    local label = Text.new()
    label:SetFont("Segoe UI", 14)
    label:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
    label:SetText(win.title)

    -- Right-click menu: a single "Close" item, same shape as every
    -- other context menu in this app -- laid out relative to the
    -- menu's own (0,0) origin, since Tooltip:Show() translates the
    -- menu and this item together to wherever it's shown.
    local menu = Tooltip.new()
    menu:Initialize()
    local closeItem = Text.new()
    closeItem:SetFont("Segoe UI", 16)
    closeItem:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
    closeItem:SetText("Close")
    closeItem:SetPosition(kMenuPaddingX, kMenuPaddingY)
    closeItem:SetOnClick(function() Windows.Close(win.id) end)
    local itemSize = closeItem:Bounds()
    menu:SetBounds(RectF.new(0, 0, itemSize.width + kMenuPaddingX * 2, itemSize.height + kMenuPaddingY * 2))
    menu:Add(closeItem)

    local function Activate() Windows.Activate(win.id) end
    icon:SetOnClick(Activate)
    label:SetOnClick(Activate)
    icon:SetTooltip(menu)
    label:SetTooltip(menu)

    Taskbar:Add(icon)
    Taskbar:Add(label)

    return {icon = icon, label = label, menu = menu, closeItem = closeItem, title = win.title}
end

local function RemoveButton(entry)
    Taskbar:Remove(entry.icon)
    Taskbar:Remove(entry.label)
end

-- Left-aligned row, widths measured from each label's actual rendered
-- size rather than a guessed constant -- same reasoning as everywhere
-- else in this codebase that sizes from real content.
local function LayoutButtons(order)
    local bar = Taskbar:Bounds()
    local x = kStartX
    for _, id in ipairs(order) do
        local b = buttons[id]
        local y = bar.y + (bar.height - kButtonHeight) / 2
        b.icon:SetPosition(x, y + (kButtonHeight - kIconSize) / 2)
        local labelHeight = b.label:Bounds().height
        b.label:SetPosition(x + kIconSize + kLabelGapX, y + (kButtonHeight - labelHeight) / 2)
        x = x + kIconSize + kLabelGapX + b.label:Bounds().width + kButtonGapX
    end
end

local function Refresh()
    local running = Windows.Running()
    local seen = {}
    local order = {}

    for _, win in ipairs(running) do
        seen[win.id] = true
        table.insert(order, win.id)

        local existing = buttons[win.id]
        if not existing then
            buttons[win.id] = CreateButton(win)
        elseif existing.title ~= win.title then
            existing.label:SetText(win.title)
            existing.title = win.title
        end
    end

    for id, entry in pairs(buttons) do
        if not seen[id] then
            RemoveButton(entry)
            buttons[id] = nil
        end
    end

    LayoutButtons(order)
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
