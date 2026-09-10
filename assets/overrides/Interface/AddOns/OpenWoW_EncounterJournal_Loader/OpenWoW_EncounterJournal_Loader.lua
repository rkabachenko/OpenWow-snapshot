BINDING_HEADER_OPENWOW_ENCOUNTER_JOURNAL = "OpenWoW 地下城手册"
BINDING_NAME_OPENWOW_TOGGLE_ENCOUNTER_JOURNAL = "打开/关闭地下城手册"

local ADDON_NAME = "OpenWoW_EncounterJournal"

local function ReportLoadFailure(reason)
    local message = "|cffff5050OpenWoW 地下城手册加载失败|r"
    if reason and reason ~= "" then
        message = message .. "：" .. tostring(reason)
    end
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        DEFAULT_CHAT_FRAME:AddMessage(message)
    end
    if UIErrorsFrame and UIErrorsFrame.AddMessage then
        UIErrorsFrame:AddMessage("地下城手册加载失败", 1.0, 0.2, 0.2, 1.0)
    end
end

function OpenWoWEncounterJournalLoader_Toggle()
    if not IsAddOnLoaded(ADDON_NAME) then
        local loaded, reason = LoadAddOn(ADDON_NAME)
        if not loaded then
            ReportLoadFailure(reason)
            return
        end
    end

    if type(OpenWoWEncounterJournal_Toggle) ~= "function" then
        ReportLoadFailure("入口函数未注册")
        return
    end
    OpenWoWEncounterJournal_Toggle()
end

SLASH_OPENWOWENCOUNTERJOURNAL1 = "/ej"
SLASH_OPENWOWENCOUNTERJOURNAL2 = "/journal"
SlashCmdList.OPENWOWENCOUNTERJOURNAL = OpenWoWEncounterJournalLoader_Toggle

OpenWoWEncounterJournalLauncherDB = OpenWoWEncounterJournalLauncherDB or {}
local launcherDB = OpenWoWEncounterJournalLauncherDB
launcherDB.x = tonumber(launcherDB.x) or -0.70710678
launcherDB.y = tonumber(launcherDB.y) or -0.70710678

local launcher = CreateFrame("Button", "OpenWoWEncounterJournalLauncher", Minimap)
launcher:SetSize(32, 32)
launcher:SetFrameStrata("MEDIUM")
launcher:SetFrameLevel(Minimap:GetFrameLevel() + 8)
launcher:RegisterForClicks("LeftButtonUp", "RightButtonUp")
launcher:RegisterForDrag("LeftButton")

local icon = launcher:CreateTexture(nil, "BACKGROUND")
icon:SetSize(20, 20)
icon:SetPoint("CENTER", launcher, "CENTER", 0, 1)
icon:SetTexture("Interface\\EncounterJournal\\UI-EJ-PortraitIcon")
icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)

local border = launcher:CreateTexture(nil, "OVERLAY")
border:SetSize(52, 52)
border:SetPoint("TOPLEFT", launcher, "TOPLEFT", 0, 0)
border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")

local highlight = launcher:CreateTexture(nil, "HIGHLIGHT")
highlight:SetSize(32, 32)
highlight:SetPoint("CENTER", launcher, "CENTER", 0, 0)
highlight:SetTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")
highlight:SetBlendMode("ADD")

local MINIMAP_RADIUS = 80
local function PlaceLauncher()
    local length = math.sqrt(launcherDB.x * launcherDB.x + launcherDB.y * launcherDB.y)
    if length < 0.001 then
        launcherDB.x = -0.70710678
        launcherDB.y = -0.70710678
        length = 1
    end
    launcherDB.x = launcherDB.x / length
    launcherDB.y = launcherDB.y / length
    launcher:ClearAllPoints()
    launcher:SetPoint("CENTER", Minimap, "CENTER",
        launcherDB.x * MINIMAP_RADIUS, launcherDB.y * MINIMAP_RADIUS)
end

local function UpdateLauncherDrag()
    local centerX, centerY = Minimap:GetCenter()
    local cursorX, cursorY = GetCursorPosition()
    local scale = Minimap:GetEffectiveScale()
    if not centerX or not centerY or not scale or scale <= 0 then
        return
    end
    local deltaX = cursorX / scale - centerX
    local deltaY = cursorY / scale - centerY
    local length = math.sqrt(deltaX * deltaX + deltaY * deltaY)
    if length > 0.001 then
        launcherDB.x = deltaX / length
        launcherDB.y = deltaY / length
        PlaceLauncher()
    end
end

PlaceLauncher()

launcher:SetScript("OnDragStart", function(self)
    self:SetScript("OnUpdate", UpdateLauncherDrag)
end)
launcher:SetScript("OnDragStop", function(self)
    self:SetScript("OnUpdate", nil)
end)
launcher:SetScript("OnClick", function(_, button)
    if button == "RightButton" then
        launcherDB.x = -0.70710678
        launcherDB.y = -0.70710678
        PlaceLauncher()
        return
    end
    OpenWoWEncounterJournalLoader_Toggle()
end)
launcher:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText("OpenWoW 地下城手册", 1.0, 0.82, 0.0)
    GameTooltip:AddLine("浏览 3.3.5a 副本、首领与可用技能资料。", 1.0, 1.0, 1.0, true)
    GameTooltip:AddLine("左键：打开地下城手册", 0.65, 0.65, 0.65)
    GameTooltip:AddLine("拖动：调整图标位置", 0.65, 0.65, 0.65)
    GameTooltip:AddLine("右键：重置图标位置", 0.65, 0.65, 0.65)
    GameTooltip:AddLine("也可使用 /ej、/journal 或按键绑定。", 0.65, 0.65, 0.65, true)
    GameTooltip:Show()
end)
launcher:SetScript("OnLeave", function()
    GameTooltip:Hide()
end)
