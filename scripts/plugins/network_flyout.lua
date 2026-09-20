-- Network quick-settings flyout, ported from Balcony4Windows/Railing's
-- UI/NetworkFlyout (a Direct2D popup window there) onto Balcony's own
-- primitives and its existing Tooltip popup mechanism -- see
-- NetworkBackend.h/.cpp for the ported backend. Opens on LEFT click of
-- its taskbar icon, same reasoning as volume_flyout.lua.
--
-- The network list scrolls by dragging directly on it (touch/trackpad-
-- style) rather than a separate scrollbar-thumb widget: a background
-- Canvas covering the list area is the only draggable thing there (see
-- Component::SetOnDrag), so a genuine drag scrolls it while an ordinary
-- click still reaches whichever network item is under the cursor --
-- the same click-vs-drag threshold every draggable in this codebase
-- already gets, reused here instead of a second, bespoke mechanism.
-- Items scrolled out of view are Container:Remove()'d (not destroyed --
-- Text objects are created once per scan, not re-rasterized per scroll
-- step) rather than GPU-clipped, since Balcony has no clip-rect
-- primitive anywhere yet.
--
-- Everything that draws or positions something itself (the scroll
-- indicator, list items) reads flyout:Bounds()/listBackground:Bounds()
-- live rather than caching a coordinate computed once at load time --
-- Tooltip:Show() moves the whole flyout (and every child) by
-- translating it to wherever it's opened, so anything hardcoded from
-- before that translation would draw/position itself in the wrong
-- place. See volume_flyout.lua's identical fix/comment -- same bug
-- class, same reasoning.
--
-- Secured networks without an already-saved profile show a placeholder
-- instead of a password field: Balcony has no keyboard/text-input
-- primitive at all yet, and Railing's own fallback (ms-settings: URIs)
-- launches via explorer.exe specifically, which this project intends
-- to not be running eventually. See CLAUDE.md section 2.
local kFlyoutWidth = 300
local kPaddingX = 16
local kPaddingY = 16
local kListY = 64
local kListVisibleHeight = 200
local kItemHeight = 30
local kScanPollInterval = 2.5
local kResultPollInterval = 0.25
local kIconStatusPollInterval = 1.0

-- Segoe Fluent Icons glyphs -- see volume_flyout.lua for why (the same
-- font Windows 11's own tray icons use, so this is a real vector icon
-- through the existing Text/GDI path, no new rendering primitive).
local kGlyphWifi1 = "\xee\xa1\xb2"
local kGlyphWifi2 = "\xee\xa1\xb3"
local kGlyphWifi3 = "\xee\xa1\xb4"
local kGlyphWifiOff = "\xee\xad\x9c"

local function WifiGlyph(status)
    if not status.connected then return kGlyphWifiOff end
    if status.signalQuality < 34 then return kGlyphWifi1 end
    if status.signalQuality < 67 then return kGlyphWifi2 end
    return kGlyphWifi3
end

-- Text:SetText sizes Bounds() to the exact rendered glyph run (GDI-
-- measured -- see Text::SetText), not a fixed row width. A SetOnClick
-- directly on a label is therefore only clickable where its glyphs
-- actually are -- a real problem here specifically, since SSIDs vary
-- wildly in length ("R" vs. "SpectrumJAJJCalhoun4") and a user expects
-- to click anywhere across a network's row, not just its name. This
-- creates an invisible Spacer sized to the whole row and puts the click
-- handler on THAT -- added after the label, so Container::FindHit
-- (which checks the most-recently-added child first) finds it before
-- the label's own narrower bounds ever get a chance to.
local function MakeRowClickTarget(onClick)
    local hit = Spacer.new()
    hit:SetOnClick(onClick)
    return hit
end

-- Taskbar icon -- claims its slot via the same shared, order-
-- independent accumulator volume_flyout.lua sets up (see its own
-- comment on why: scripts/plugins/ loads alphabetically, and neither
-- plugin can assume the other has or hasn't run yet).
local icon = Text.new()
icon:SetFont("Segoe Fluent Icons", 16)
icon:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
icon:SetText(WifiGlyph(Network.GetStatus()))

_G.TaskbarTrayReservedWidth = _G.TaskbarTrayReservedWidth or 0
local kIconMargin = 16
local kIconGap = 12
do
    local clockBounds = taskbarClock:Bounds()
    local iconBounds = icon:Bounds()
    local x = clockBounds.x - kIconMargin - _G.TaskbarTrayReservedWidth - iconBounds.width
    icon:SetPosition(x, clockBounds.y + (clockBounds.height - iconBounds.height) / 2)
    _G.TaskbarTrayReservedWidth = _G.TaskbarTrayReservedWidth + iconBounds.width + kIconGap
end
Taskbar:Add(icon)

-- Flyout content, all positioned (0,0)-relative -- correct as long as
-- it's set ONCE, before the first Tooltip:Show() (see the file header
-- comment) -- every one of these is a plain Text whose position
-- Container::Translate carries along correctly on every subsequent
-- Show(), so none of them need to be recomputed later, unlike the
-- Canvas draw callback and the scrolling list below.
local flyout = Tooltip.new()
flyout:Initialize()
-- Same reasoning as volume_flyout.lua: clicking a network, Refresh, or
-- scrolling the list must not close this -- only clicking outside it
-- (or its own tray icon again) should. See Tooltip::CloseOnClick.
flyout:SetCloseOnClick(false)

local titleLabel = Text.new()
titleLabel:SetFont("Segoe UI", 14)
titleLabel:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
titleLabel:SetText("Network")
titleLabel:SetPosition(kPaddingX, kPaddingY)
flyout:Add(titleLabel)

local scanButton = Text.new()
scanButton:SetFont("Segoe UI", 12)
scanButton:SetColor(Color.new(0.6, 0.8, 1.0, 1.0))
scanButton:SetText("Refresh")
scanButton:SetPosition(kFlyoutWidth - kPaddingX - scanButton:Bounds().width, kPaddingY)
flyout:Add(scanButton)

local statusLabel = Text.new()
statusLabel:SetFont("Segoe UI", 12)
statusLabel:SetColor(Color.new(0.75, 0.75, 0.75, 1.0))
statusLabel:SetPosition(kPaddingX, kPaddingY + 22)
flyout:Add(statusLabel)

local resultLabel = Text.new()
resultLabel:SetFont("Segoe UI", 12)
resultLabel:SetColor(Color.new(1.0, 0.8, 0.4, 1.0))
resultLabel:SetPosition(kPaddingX, kListY + kListVisibleHeight + 12)
flyout:Add(resultLabel)

local function SetResultMessage(text)
    resultLabel:SetText(text or "")
end
SetResultMessage("")

-- Not resultLabel:Bounds().height -- an empty string measures to
-- ~0px, which would leave no room reserved for when a real result
-- message later appears. 18px is a reasonable line height for its
-- 12pt font, same ballpark as every other label's own measured height
-- in this file.
local kResultLineHeight = 18
flyout:SetBounds(RectF.new(0, 0, kFlyoutWidth, resultLabel:Bounds().y + kResultLineHeight + kPaddingY))

-- Scrollable list -------------------------------------------------------

local networkItems = {} -- {index=, label=, added=, net=}
local scrollOffset = 0

local function MaxScroll()
    local contentHeight = #networkItems * kItemHeight
    return math.max(0, contentHeight - kListVisibleHeight)
end

local function RefreshVisibleWindow()
    -- Anchored to the flyout's CURRENT on-screen position, not (0,0) --
    -- this runs both on first open and later (scrolling, a background
    -- scan finishing) while the flyout is already translated away from
    -- (0,0). See the file header comment.
    local origin = flyout:Bounds()
    for _, entry in ipairs(networkItems) do
        local itemY = origin.y + kListY + (entry.index - 1) * kItemHeight - scrollOffset
        local visible = itemY + kItemHeight > origin.y + kListY and itemY < origin.y + kListY + kListVisibleHeight
        if visible then
            entry.label:SetPosition(origin.x + kPaddingX, itemY)
            entry.hit:SetBounds(RectF.new(origin.x, itemY - 3, kFlyoutWidth, kItemHeight))
            if not entry.added then
                flyout:Add(entry.label)
                -- After the label (see MakeRowClickTarget) so it wins
                -- Container::FindHit first across the whole row.
                flyout:Add(entry.hit)
                entry.added = true
            end
        elseif entry.added then
            flyout:Remove(entry.label)
            flyout:Remove(entry.hit)
            entry.added = false
        end
    end
end

local OnNetworkClicked -- forward-declared, same reasoning as taskbar_apps.lua's SetPinned/Refresh.

local function RebuildNetworkList()
    for _, entry in ipairs(networkItems) do
        if entry.added then
            flyout:Remove(entry.label)
            flyout:Remove(entry.hit)
        end
    end
    networkItems = {}
    -- Not reset to 0 here: this runs on every periodic background
    -- rescan (every kScanPollInterval, while the flyout may well still
    -- be open) as well as on first open and on the "Refresh" click --
    -- resetting unconditionally would snap a mid-scroll list back to
    -- the top every couple of seconds. Clamped below, once the new
    -- item count is known, in case the list got shorter.

    local scanned = Network.ScanNetworks()
    for i, net in ipairs(scanned) do
        local label = Text.new()
        label:SetFont("Segoe UI", 13)
        label:SetColor(net.connected and Color.new(0.4, 0.75, 1.0, 1.0) or Color.new(1.0, 1.0, 1.0, 1.0))
        local prefix = net.connected and "\xe2\x97\x8f " or "   " -- filled circle marks the connected network.
        local suffix = net.secure and "" or "  (Open)"
        label:SetText(prefix .. net.ssid .. suffix)
        local hit = MakeRowClickTarget(function() OnNetworkClicked(net) end)
        table.insert(networkItems, {index = i, label = label, hit = hit, added = false, net = net})
    end

    scrollOffset = math.max(0, math.min(MaxScroll(), scrollOffset))
    RefreshVisibleWindow()
end

OnNetworkClicked = function(net)
    if Network.IsConnecting() then
        return
    end
    if net.secure and not Network.HasSavedProfile(net.ssid) then
        SetResultMessage("Password entry not yet supported")
        return
    end
    SetResultMessage("Connecting to " .. net.ssid .. "...")
    Network.ConnectTo(net, "")
end

-- Background of the list area -- the only draggable thing there (see
-- this file's own header comment on why); also draws a thin scroll-
-- position indicator on the right edge, purely visual.
local listBackground = Canvas.new()
listBackground:SetBounds(RectF.new(kPaddingX, kListY, kFlyoutWidth - kPaddingX * 2, kListVisibleHeight))
listBackground:SetDraggable(true)
listBackground:SetOnDraw(function()
    local maxScroll = MaxScroll()
    if maxScroll <= 0 then
        return
    end
    local bounds = listBackground:Bounds()
    local contentHeight = bounds.height + maxScroll
    local thumbHeight = math.max(20, bounds.height * (bounds.height / contentHeight))
    local thumbY = bounds.y + (bounds.height - thumbHeight) * (scrollOffset / maxScroll)
    listBackground:DrawRect(RectF.new(bounds.x + bounds.width - 4, thumbY, 4, thumbHeight), Color.new(1.0, 1.0, 1.0, 0.25))
end)
flyout:Add(listBackground)

local dragLastY = nil
listBackground:SetOnDrag(function(_, y)
    if dragLastY then
        scrollOffset = math.max(0, math.min(MaxScroll(), scrollOffset - (y - dragLastY)))
        RefreshVisibleWindow()
        Balcony.RequestRedraw()
    end
    dragLastY = y
end)
listBackground:SetOnDragEnd(function() dragLastY = nil end)

-- Opening / refreshing ----------------------------------------------------

local function RefreshStatusLabel()
    statusLabel:SetText(System.NetworkStatus())
end

scanButton:SetOnClick(function()
    Network.RequestScan()
    SetResultMessage("Scanning...")
    RebuildNetworkList()
end)

icon:SetOnClick(function()
    RefreshStatusLabel()
    SetResultMessage(nil)
    Network.RequestScan()
    RebuildNetworkList()

    local iconBounds = icon:Bounds()
    local flyoutBounds = flyout:Bounds()
    Balcony.ShowFlyout(flyout, iconBounds.x + iconBounds.width - flyoutBounds.width, iconBounds.y - flyoutBounds.height - 8)
end)

-- Live upkeep: re-scan periodically while open-ish (matches how
-- infrequently networks actually change -- no point doing this every
-- frame), poll for a background ConnectTo() finishing, and keep the
-- taskbar icon's glyph in sync with connection status. Same polling
-- idiom as every other live value in this codebase.
local lastIconStatus = {connected = false, signalQuality = -1}
local scanElapsed = 0.0
local resultElapsed = 0.0
local iconElapsed = 0.0

Balcony.OnUpdate(function(deltaSeconds)
    resultElapsed = resultElapsed + deltaSeconds
    if resultElapsed >= kResultPollInterval then
        resultElapsed = 0.0
        local result = Network.TryTakeConnectResult()
        if result then
            SetResultMessage(result)
            RebuildNetworkList()
            RefreshStatusLabel()
        end
    end

    iconElapsed = iconElapsed + deltaSeconds
    if iconElapsed >= kIconStatusPollInterval then
        iconElapsed = 0.0
        local status = Network.GetStatus()
        if status.connected ~= lastIconStatus.connected or status.signalQuality ~= lastIconStatus.signalQuality then
            lastIconStatus = status
            icon:SetText(WifiGlyph(status))
        end
    end

    if flyout:IsVisible() then
        scanElapsed = scanElapsed + deltaSeconds
        if scanElapsed >= kScanPollInterval then
            scanElapsed = 0.0
            Network.RequestScan()
            RebuildNetworkList()
            RefreshStatusLabel()
        end
    end
end)

networkFlyout = {icon = icon, flyout = flyout, listBackground = listBackground, titleLabel = titleLabel, scanButton = scanButton, statusLabel = statusLabel, resultLabel = resultLabel}
