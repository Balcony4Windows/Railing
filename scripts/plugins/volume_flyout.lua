-- Volume quick-settings flyout, ported from Balcony4Windows/Railing's
-- UI/VolumeFlyout (a Direct2D popup window there) onto Balcony's own
-- primitives and its existing Tooltip popup mechanism -- see
-- AudioBackend.h/.cpp for the ported backend and Component::SetOnDrag
-- for the slider's continuous-drag primitive this needed. Opens on
-- LEFT click of its taskbar icon (matching the real Windows volume
-- flyout), unlike every other menu in this app -- that's why it goes
-- through Balcony.ShowFlyout instead of the usual right-click Tooltip
-- wiring (see DesktopEnvironment::ShowFlyout).
--
-- Everything below that draws or positions something itself (the
-- slider's Canvas, the device list) reads the relevant component's own
-- CURRENT Bounds() each time rather than caching a coordinate computed
-- once at load time: Tooltip:Show() moves the whole flyout (and every
-- child, including the slider Canvas) by translating it to wherever
-- it's opened, so anything that hardcoded its position from before
-- that translation would silently draw/position itself in the wrong
-- place the moment the flyout actually moves -- exactly the bug this
-- had at first (the slider rendered at its original (0,0)-relative
-- spot instead of inside the flyout).
local kFlyoutWidth = 280
local kPaddingX = 16
local kPaddingY = 16
local kSliderY = 56
local kSliderHeight = 4
local kSliderThumbRadius = 6
local kDeviceItemHeight = 26
local kPollInterval = 0.25

-- Segoe Fluent Icons glyphs -- the same font Windows 11's own tray
-- icons for volume/network are drawn with, so this reuses Text (no new
-- rendering primitive) for a real vector icon instead of pixel art.
local kGlyphMute = "\xee\x9d\x8f"
local kGlyphVolume1 = "\xee\xa6\x93"
local kGlyphVolume2 = "\xee\xa6\x94"
local kGlyphVolume3 = "\xee\xa6\x95"

local function VolumeGlyph(volume, muted)
    if muted or volume <= 0.0 then return kGlyphMute end
    if volume < 0.33 then return kGlyphVolume1 end
    if volume < 0.66 then return kGlyphVolume2 end
    return kGlyphVolume3
end

local function FormatPercent(fraction)
    return string.format("%d%%", math.floor(fraction * 100 + 0.5))
end

-- Text:SetText sizes Bounds() to the exact rendered glyph run (GDI-
-- measured -- see Text::SetText), not a fixed row width. A SetOnClick
-- directly on a label is therefore only clickable where its glyphs
-- actually are -- fine for something like a taskbar icon, but not for a
-- list row (a short device name leaves most of its row dead space).
-- This adds an invisible Spacer sized to the whole row instead and puts
-- the click handler on THAT -- added after the label, so
-- Container::FindHit (which checks the most-recently-added child first)
-- finds it before the label's own narrower bounds ever get a chance to.
local function AddRowClickTarget(container, x, y, width, height, onClick)
    local hit = Spacer.new()
    hit:SetBounds(RectF.new(x, y, width, height))
    hit:SetOnClick(onClick)
    container:Add(hit)
    return hit
end

-- Taskbar icon, left of the clock -- reads taskbarClock's own bounds (a
-- global from desktop.lua, which always loads first) rather than a
-- hardcoded offset, same reasoning taskbarClock itself uses relative to
-- Taskbar:Bounds(). Claims its horizontal slot via a shared, order-
-- independent accumulator (_G.TaskbarTrayReservedWidth) rather than
-- reading another specific plugin's icon bounds directly -- scripts/
-- plugins/ loads alphabetically, so this can't assume network_flyout.lua
-- (n < v) has or hasn't run yet. Any future tray-style icon plugin can
-- claim its own slot the same way, in whatever order it happens to load.
local icon = Text.new()
icon:SetFont("Segoe Fluent Icons", 16)
icon:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
icon:SetText(VolumeGlyph(Audio.GetVolume(), Audio.GetMute()))

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

-- Flyout content, all positioned (0,0)-relative initially -- correct as
-- long as the FIRST Tooltip:Show() hasn't happened yet. Once it has,
-- flyout:Bounds() reflects wherever it currently is, and everything
-- that repositions/redraws itself later reads that live, rather than
-- assuming (0,0).
local flyout = Tooltip.new()
flyout:Initialize()
-- Unlike a right-click context menu, this stays open through clicks on
-- its own contents (dragging the slider, toggling mute, switching
-- output device) -- only a click OUTSIDE it (or its own tray icon
-- again) should close it. See Tooltip::CloseOnClick.
flyout:SetCloseOnClick(false)

local titleLabel = Text.new()
titleLabel:SetFont("Segoe UI", 14)
titleLabel:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
titleLabel:SetText("Volume")
titleLabel:SetPosition(kPaddingX, kPaddingY)
flyout:Add(titleLabel)

local percentLabel = Text.new()
percentLabel:SetFont("Segoe UI", 14)
percentLabel:SetColor(Color.new(0.8, 0.8, 0.8, 1.0))
flyout:Add(percentLabel)

local sliderTrackWidth = kFlyoutWidth - kPaddingX * 2
local volumeFraction = Audio.GetVolume()
local lastPolledVolume = volumeFraction
local lastPolledMute = Audio.GetMute()

-- The slider: a Canvas drawing its own track/fill/thumb, dragged via
-- Component::SetOnDrag (continuous, absolute cursor position) rather
-- than SetOnDragEnd (fires once, at the end) -- see Component.h. Both
-- the draw callback and the drag handler read slider:Bounds() live
-- (see the file header comment) instead of the position it was first
-- created at.
local slider = Canvas.new()
slider:SetBounds(RectF.new(kPaddingX, kSliderY - kSliderThumbRadius, sliderTrackWidth, kSliderThumbRadius * 2))
slider:SetDraggable(true)
slider:SetOnDraw(function()
    local bounds = slider:Bounds()
    local trackY = bounds.y + bounds.height / 2 - kSliderHeight / 2
    slider:DrawRect(RectF.new(bounds.x, trackY, bounds.width, kSliderHeight), Color.new(1.0, 1.0, 1.0, 0.15))
    slider:DrawRect(RectF.new(bounds.x, trackY, bounds.width * volumeFraction, kSliderHeight), Color.new(0.3, 0.6, 1.0, 1.0))
    -- Thumb approximated as a small square -- Canvas only draws
    -- axis-aligned rects (see Canvas.h), close enough at this size to
    -- read as a handle.
    local thumbX = bounds.x + bounds.width * volumeFraction - kSliderThumbRadius
    local thumbY = bounds.y + bounds.height / 2 - kSliderThumbRadius
    slider:DrawRect(RectF.new(thumbX, thumbY, kSliderThumbRadius * 2, kSliderThumbRadius * 2), Color.new(1.0, 1.0, 1.0, 1.0))
end)
flyout:Add(slider)

local muteItem = Text.new()
muteItem:SetFont("Segoe UI", 14)
muteItem:SetColor(Color.new(1.0, 1.0, 1.0, 1.0))
muteItem:SetPosition(kPaddingX, kSliderY + 20)
flyout:Add(muteItem)

local function RefreshMuteLabel()
    muteItem:SetText(Audio.GetMute() and "Unmute" or "Mute")
end

local function SetVolumeFraction(fraction)
    volumeFraction = math.max(0.0, math.min(1.0, fraction))
    Audio.SetVolume(volumeFraction)
    percentLabel:SetText(FormatPercent(volumeFraction))
    icon:SetText(VolumeGlyph(volumeFraction, Audio.GetMute()))
    lastPolledVolume = volumeFraction
    Balcony.RequestRedraw()
end
slider:SetOnDrag(function(x)
    local bounds = slider:Bounds()
    SetVolumeFraction((x - bounds.x) / bounds.width)
end)

AddRowClickTarget(flyout, kPaddingX, kSliderY + 16, kFlyoutWidth - kPaddingX * 2, 22, function()
    Audio.ToggleMute()
    RefreshMuteLabel()
    icon:SetText(VolumeGlyph(volumeFraction, Audio.GetMute()))
    lastPolledMute = Audio.GetMute()
end)

-- Output device list: same stacked-item idea as BuildStackedMenu
-- elsewhere in this codebase, rebuilt whenever it needs to reflect a
-- changed device set -- when the flyout opens, and again when clicking
-- a device to switch to it (which happens while the flyout is already
-- open, i.e. already translated away from (0,0) -- see the file header
-- comment on why every position here is anchored to flyout:Bounds()
-- instead of assuming (0,0)).
local deviceItems = {}

local function RebuildDeviceList()
    for _, item in ipairs(deviceItems) do
        flyout:Remove(item)
    end
    deviceItems = {}

    local origin = flyout:Bounds()
    local devices = Audio.ListOutputDevices()
    local currentName = Audio.GetCurrentDeviceName()
    local y = kSliderY + 56

    local deviceLabel = Text.new()
    deviceLabel:SetFont("Segoe UI", 12)
    deviceLabel:SetColor(Color.new(0.7, 0.7, 0.7, 1.0))
    deviceLabel:SetText("Output device")
    deviceLabel:SetPosition(origin.x + kPaddingX, origin.y + y)
    flyout:Add(deviceLabel)
    table.insert(deviceItems, deviceLabel)
    y = y + 22

    for _, device in ipairs(devices) do
        local item = Text.new()
        item:SetFont("Segoe UI", 13)
        item:SetColor(device.name == currentName and Color.new(0.4, 0.75, 1.0, 1.0) or Color.new(1.0, 1.0, 1.0, 1.0))
        item:SetText(device.name)
        item:SetPosition(origin.x + kPaddingX, origin.y + y)
        flyout:Add(item)
        table.insert(deviceItems, item)
        local hit = AddRowClickTarget(flyout, origin.x, origin.y + y - 3, kFlyoutWidth, kDeviceItemHeight, function()
            Audio.SetDefaultDevice(device.id)
            RebuildDeviceList()
        end)
        table.insert(deviceItems, hit)
        y = y + kDeviceItemHeight
    end

    -- Width/height only -- origin.x/y (wherever the flyout currently
    -- is, or (0,0) if it's never been shown yet) must be preserved, not
    -- reset, or a device-switch rebuild while already open would snap
    -- the whole flyout back to the top-left corner.
    flyout:SetBounds(RectF.new(origin.x, origin.y, kFlyoutWidth, y + kPaddingY))
end

icon:SetOnClick(function()
    volumeFraction = Audio.GetVolume()
    lastPolledVolume = volumeFraction
    lastPolledMute = Audio.GetMute()
    percentLabel:SetText(FormatPercent(volumeFraction))
    RefreshMuteLabel()
    RebuildDeviceList()

    local iconBounds = icon:Bounds()
    local flyoutBounds = flyout:Bounds()
    Balcony.ShowFlyout(flyout, iconBounds.x + iconBounds.width - flyoutBounds.width, iconBounds.y - flyoutBounds.height - 8)
end)

-- Keeps the icon (and, while open, the slider/labels) in sync with
-- volume changes from elsewhere -- hardware keys, another app, ... --
-- same polling idiom as every other live value in this codebase; see
-- AudioBackend.h's own comment on why this is polling, not push.
percentLabel:SetText(FormatPercent(volumeFraction))
percentLabel:SetPosition(kFlyoutWidth - kPaddingX - percentLabel:Bounds().width, kPaddingY)

local elapsed = kPollInterval
Balcony.OnUpdate(function(deltaSeconds)
    elapsed = elapsed + deltaSeconds
    if elapsed < kPollInterval then
        return
    end
    elapsed = 0.0

    local liveVolume = Audio.GetVolume()
    local liveMute = Audio.GetMute()
    if liveVolume == lastPolledVolume and liveMute == lastPolledMute then
        return
    end
    lastPolledVolume = liveVolume
    lastPolledMute = liveMute
    volumeFraction = liveVolume

    icon:SetText(VolumeGlyph(liveVolume, liveMute))
    if flyout:IsVisible() then
        percentLabel:SetText(FormatPercent(liveVolume))
        RefreshMuteLabel()
    end
end)

volumeFlyout = {icon = icon, flyout = flyout, slider = slider, muteItem = muteItem, percentLabel = percentLabel, titleLabel = titleLabel}
