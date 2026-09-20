-- A small live CPU/Memory/GPU usage bar chart on the desktop, built
-- entirely from the Canvas primitive (Canvas:DrawRect) plus the raw
-- System.*UsagePercent() readings -- deliberately NOT a toy example:
-- this is the proof that a custom visual (an audio visualizer, a
-- graph, ...) needs zero new C++ once Canvas exists, only Lua. See
-- Canvas.h's own comment and TODO.md's extensibility question.
local kBarWidth = 24
local kBarGapX = 12
local kBarMaxHeight = 100
local kChartX = 40
local kChartBottomY = 420 -- Below the desktop icon grid started elsewhere in desktop.lua.
local kLabelGapY = 4

-- Each bar reads a different System.*UsagePercent() function -- Canvas
-- doesn't know or care what the number means, it just draws it; the
-- composer decides the mapping. Same "expose information, let the UI
-- decide how to represent it" split as everywhere else. See CLAUDE.md
-- section 6.
local bars = {
    {name = "CPU", color = Color.new(0.95, 0.35, 0.35, 1.0), getPercent = System.CpuUsagePercent},
    {name = "MEM", color = Color.new(0.35, 0.65, 0.95, 1.0), getPercent = System.MemoryUsagePercent},
    {name = "GPU", color = Color.new(0.45, 0.85, 0.45, 1.0), getPercent = System.GpuUsagePercent},
}

local canvas = Canvas.new()
canvas:SetBounds(RectF.new(kChartX, kChartBottomY - kBarMaxHeight, (kBarWidth + kBarGapX) * #bars, kBarMaxHeight))
canvas:SetOnDraw(function()
    local x = kChartX
    for _, bar in ipairs(bars) do
        local percent = math.max(0, math.min(100, bar.getPercent()))
        local height = kBarMaxHeight * (percent / 100.0)

        -- A faint full-height track behind each bar, so 0% still shows
        -- as an outline rather than nothing at all.
        canvas:DrawRect(RectF.new(x, kChartBottomY - kBarMaxHeight, kBarWidth, kBarMaxHeight), Color.new(1.0, 1.0, 1.0, 0.08))
        canvas:DrawRect(RectF.new(x, kChartBottomY - height, kBarWidth, height), bar.color)

        x = x + kBarWidth + kBarGapX
    end
end)
Desktop:Add(canvas)

-- One label per bar, positioned once (labels don't move -- only what
-- Canvas draws above them changes frame to frame).
local labels = {}
local x = kChartX
for _, bar in ipairs(bars) do
    local label = Text.new()
    label:SetFont("Segoe UI", 12)
    label:SetColor(Color.new(0.8, 0.8, 0.8, 1.0))
    label:SetText(bar.name)
    local size = label:Bounds()
    label:SetPosition(x + (kBarWidth - size.width) / 2, kChartBottomY + kLabelGapY)
    Desktop:Add(label)
    table.insert(labels, label)
    x = x + kBarWidth + kBarGapX
end

-- Keeps canvas/labels referenced -- same reason every other
-- Desktop-added object in this codebase is kept alive by a global
-- rather than going out of scope as soon as this script finishes.
systemUsageChart = {canvas = canvas, bars = bars, labels = labels}

-- The underlying readings only actually change about once a second
-- (SystemMonitors throttles internally) -- Canvas doesn't redraw on
-- its own by default (see Canvas.h), so this is what keeps the chart
-- live: request a redraw on the same cadence the numbers can possibly
-- change, not every frame. Same "only touch the screen when something
-- really changed" discipline as the clock in desktop.lua, just applied
-- through Balcony.RequestRedraw() instead of a SetText call.
local kPollInterval = 1.0
local elapsed = kPollInterval -- Forces an immediate first draw.
Balcony.OnUpdate(function(deltaSeconds)
    elapsed = elapsed + deltaSeconds
    if elapsed >= kPollInterval then
        elapsed = 0.0
        Balcony.RequestRedraw()
    end
end)
