local state = {
    layer = 1,
    drawableWidth = 1,
    drawableHeight = 1,
    logicalWidth = 844,
    logicalHeight = 390,
    actionButtons = {},
}

RegisterForSave("OpenWoWMobileHUDShown")
RegisterForSave("OpenWoWMobileMovementShown")

local locale = GetLocale and GetLocale() or "enUS"
local L
if locale == "zhCN" then
    L = {
        enemy = "敌人",
        friendly = "友方",
        interact = "交互",
        interactHint = "此按钮与当前目标交互。采集物、宝箱等无需选中，直接双击场景中的物体。",
        jump = "跳跃",
        drawer = "功能",
        close = "返回",
        hide = "收起",
        show = "触控",
        hideHint = "隐藏触控 HUD，露出原生界面",
        showHint = "显示触控 HUD",
        gestureHint = "摇杆独立显示，可在功能菜单中单独开关。",
        hideMovement = "隐藏摇杆",
        showMovement = "显示摇杆",
        mouse = "虚拟鼠标",
        hideMouse = "关闭鼠标",
        leftMouse = "左键",
        rightMouse = "右键",
        moveMouse = "移动光标",
        mouseHint = "下半部拖动可移动光标；按住上半部左键或右键拖动，可拖拽、调整窗口大小。",
        move = "移动",
        zoomIn = "拉近",
        zoomOut = "拉远",
        character = "人物",
        spellbook = "法术",
        talents = "天赋",
        quest = "任务",
        map = "地图",
        bags = "背包",
        chat = "聊天",
        system = "系统",
        graphics = "画质",
        exportLogs = "导出日志",
        exportLogsHint = "保存日志到文件，或通过系统分享菜单发送。",
        renderScale = "世界分辨率",
        renderScaleHint = "即时生效，界面清晰度不变",
    }
else
    L = {
        enemy = "Enemy",
        friendly = "Friend",
        interact = "Use",
        interactHint = "This button interacts with your current target. Double-tap a gathering object or chest in the world to use it without selecting it.",
        jump = "Jump",
        drawer = "Menu",
        close = "Back",
        hide = "Hide",
        show = "Touch",
        hideHint = "Hide the touch HUD to access the original UI",
        showHint = "Show the touch HUD",
        gestureHint = "The stick stays visible. Toggle it separately in the menu.",
        hideMovement = "Hide stick",
        showMovement = "Show stick",
        mouse = "Mouse",
        hideMouse = "Mouse off",
        leftMouse = "Left",
        rightMouse = "Right",
        moveMouse = "Move cursor",
        mouseHint = "Drag the lower pad to move the cursor. Hold and drag either upper button to drag or resize windows.",
        move = "Move",
        zoomIn = "Zoom in",
        zoomOut = "Zoom out",
        character = "Hero",
        spellbook = "Spells",
        talents = "Talents",
        quest = "Quests",
        map = "Map",
        bags = "Bags",
        chat = "Chat",
        system = "System",
        graphics = "Graphics",
        exportLogs = "Logs",
        exportLogsHint = "Save the log to Files or send it using the system share sheet.",
        renderScale = "World resolution",
        renderScaleHint = "Applies immediately. UI stays sharp.",
    }
end

local root = CreateFrame("Frame", "OpenWoWMobileRoot", UIParent)
root:SetAllPoints(UIParent)
root:SetFrameStrata("MEDIUM")

local movementRoot = CreateFrame("Frame", "OpenWoWMobileMovementRoot", UIParent)
movementRoot:SetAllPoints(UIParent)
movementRoot:SetFrameStrata("MEDIUM")

local actionCluster = CreateFrame("Frame", "OpenWoWMobileActionCluster", root)

local function AddPanelBackground(frame, alpha)
    local background = frame:CreateTexture(nil, "BACKGROUND")
    background:SetAllPoints(frame)
    background:SetTexture(0.015, 0.02, 0.03, alpha or 0.72)
    frame.background = background
end

local function AddButtonVisuals(button, iconPath)
    local background = button:CreateTexture(nil, "BACKGROUND")
    background:SetPoint("TOPLEFT", button, "TOPLEFT", 3, -3)
    background:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -3, 3)
    background:SetTexture(0.04, 0.055, 0.075, 0.92)
    button.background = background

    local icon = button:CreateTexture(nil, "ARTWORK")
    icon:SetPoint("TOPLEFT", button, "TOPLEFT", 6, -6)
    icon:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -6, 6)
    icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    if iconPath then
        icon:SetTexture(iconPath)
    end
    button.icon = icon

    local border = button:CreateTexture(nil, "OVERLAY")
    border:SetAllPoints(button)
    border:SetTexture("Interface\\Buttons\\UI-Quickslot2")
    button.border = border

    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square")
    button:SetPushedTexture("Interface\\Buttons\\UI-Quickslot-Depress")
    button:RegisterForClicks("LeftButtonUp")
    button:EnableMouse(true)
end

local function RunMobileBinding(binding)
    if not binding then
        return
    end
    RunBinding(binding, "down")
    RunBinding(binding, "up")
end

for index = 1, 6 do
    local button = CreateFrame("Button", "OpenWoWMobileAction" .. index, actionCluster)
    button.displayIndex = index
    AddButtonVisuals(button)

    local cooldown = CreateFrame("Cooldown", nil, button)
    cooldown:SetPoint("TOPLEFT", button.icon, "TOPLEFT", 0, 0)
    cooldown:SetPoint("BOTTOMRIGHT", button.icon, "BOTTOMRIGHT", 0, 0)
    button.cooldown = cooldown

    local count = button:CreateFontString(nil, "OVERLAY", "NumberFontNormalSmall")
    count:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -7, 7)
    count:SetJustifyH("RIGHT")
    button.count = count

    local slotLabel = button:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    slotLabel:SetPoint("TOPLEFT", button, "TOPLEFT", 8, -7)
    slotLabel:SetText(index)
    button.slotLabel = slotLabel

    button:SetScript("OnClick", function(self)
        RunMobileBinding("ACTIONBUTTON" .. self.actionIndex)
    end)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetAction(((GetActionBarPage() or 1) - 1) * 12 + self.actionIndex)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    state.actionButtons[index] = button
end

local utilityPanel = CreateFrame("Frame", "OpenWoWMobileUtilityPanel", root)
utilityPanel:EnableMouse(true)
AddPanelBackground(utilityPanel, 0.9)
utilityPanel:Hide()

local graphicsPanel = CreateFrame("Frame", "OpenWoWMobileGraphicsPanel", root)
graphicsPanel:EnableMouse(true)
AddPanelBackground(graphicsPanel, 0.9)
graphicsPanel:Hide()

local function CreateLabeledButton(name, parent, label, iconPath, callback)
    local button = CreateFrame("Button", name, parent)
    AddButtonVisuals(button, iconPath)
    local text = button:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    text:SetPoint("BOTTOM", button, "BOTTOM", 0, 8)
    text:SetText(label)
    text:SetTextColor(1.0, 0.88, 0.55)
    button.label = text
    button:SetScript("OnClick", callback)
    return button
end

local targetButton = CreateLabeledButton(
    "OpenWoWMobileTargetButton", actionCluster, L.enemy,
    "Interface\\Icons\\Ability_Hunter_SniperShot",
    function() TargetNearestEnemy() end)
local friendlyButton = CreateLabeledButton(
    "OpenWoWMobileFriendlyButton", actionCluster, L.friendly,
    "Interface\\Icons\\Spell_Holy_PrayerOfHealing02",
    function() TargetNearestFriend() end)
local interactButton = CreateLabeledButton(
    "OpenWoWMobileInteractButton", actionCluster, L.interact,
    "Interface\\Icons\\INV_Misc_Hand_01",
    function() InteractUnit("target") end)
interactButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText(L.interact)
    GameTooltip:AddLine(L.interactHint, 1, 1, 1, true)
    GameTooltip:Show()
end)
interactButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
local jumpButton = CreateLabeledButton(
    "OpenWoWMobileJumpButton", actionCluster, L.jump,
    "Interface\\Icons\\Ability_Rogue_Sprint",
    function() RunMobileBinding("JUMP") end)
local drawerButton
local function SetDrawerShown(shown)
    graphicsPanel:Hide()
    if shown then
        actionCluster:Hide()
        utilityPanel:Show()
        drawerButton.label:SetText(L.close)
    else
        utilityPanel:Hide()
        actionCluster:Show()
        drawerButton.label:SetText(L.drawer)
    end
end

drawerButton = CreateLabeledButton(
    "OpenWoWMobileDrawerButton", root, L.drawer,
    "Interface\\Icons\\INV_Misc_Gear_01",
    function()
        SetDrawerShown(graphicsPanel:IsShown() or not utilityPanel:IsShown())
    end)

local utilityDefinitions = {
    {L.character, "Interface\\Icons\\INV_Shirt_05White", "TOGGLECHARACTER0"},
    {L.spellbook, "Interface\\Icons\\INV_Misc_Book_09", "TOGGLESPELLBOOK"},
    {L.talents, "Interface\\Icons\\Spell_Nature_StoneClawTotem", "TOGGLETALENTS"},
    {L.quest, "Interface\\Icons\\INV_Misc_Note_05", "TOGGLEQUESTLOG"},
    {L.map, "Interface\\Icons\\INV_Misc_Map_01", "TOGGLEWORLDMAP"},
    {L.bags, "Interface\\Icons\\INV_Misc_Bag_08", "OPENALLBAGS"},
    {L.chat, "Interface\\Icons\\INV_Letter_15", "OPENCHAT"},
    {L.system, "Interface\\Icons\\Trade_Engineering", "TOGGLEGAMEMENU"},
}
local utilityButtons = {}
for index, definition in ipairs(utilityDefinitions) do
    local binding = definition[3]
    utilityButtons[index] = CreateLabeledButton(
        "OpenWoWMobileUtility" .. index, utilityPanel,
        definition[1], definition[2],
        function()
            SetDrawerShown(false)
            RunMobileBinding(binding)
        end)
end

utilityButtons[#utilityButtons + 1] = CreateLabeledButton(
    "OpenWoWMobileZoomIn", utilityPanel, L.zoomIn,
    "Interface\\Buttons\\UI-PlusButton-UP", function() CameraZoomIn(1) end)
utilityButtons[#utilityButtons + 1] = CreateLabeledButton(
    "OpenWoWMobileZoomOut", utilityPanel, L.zoomOut,
    "Interface\\Buttons\\UI-MinusButton-UP", function() CameraZoomOut(1) end)

local renderScaleTitle = graphicsPanel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
renderScaleTitle:SetText(L.renderScale)
local renderScaleValue = graphicsPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
local renderScaleHint = graphicsPanel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
renderScaleHint:SetText(L.renderScaleHint)

local renderScaleSlider = CreateFrame("Slider", "OpenWoWMobileRenderScale", graphicsPanel)
renderScaleSlider:SetOrientation("HORIZONTAL")
renderScaleSlider:SetMinMaxValues(GetCVarMin("renderScale") * 100,
                                  GetCVarMax("renderScale") * 100)
renderScaleSlider:SetValueStep(5)
renderScaleSlider:EnableMouse(true)
local renderScaleTrack = renderScaleSlider:CreateTexture(nil, "BACKGROUND")
renderScaleTrack:SetTexture(0.3, 0.35, 0.42, 1)
renderScaleSlider:SetThumbTexture("Interface\\Buttons\\UI-SliderBar-Button-Horizontal")

local refreshingRenderScale = false
local function RefreshRenderScale()
    local scale = assert(tonumber(GetCVar("renderScale")),
                          "OpenWoWMobile: invalid renderScale CVar")
    refreshingRenderScale = true
    renderScaleSlider:SetValue(scale * 100)
    refreshingRenderScale = false
    renderScaleValue:SetText(string.format("%d%%  |  %.1f FPS",
                                           math.floor(scale * 100 + 0.5), GetFramerate()))
end

local function ApplyRenderScale(percent)
    SetCVar("renderScale", string.format("%.2f", percent / 100))
    RefreshRenderScale()
end

renderScaleSlider:SetScript("OnValueChanged", function(self, value)
    if not refreshingRenderScale then
        ApplyRenderScale(value)
    end
end)
graphicsPanel:SetScript("OnShow", RefreshRenderScale)
local renderScaleRefreshElapsed = 0
graphicsPanel:SetScript("OnUpdate", function(self, elapsed)
    renderScaleRefreshElapsed = renderScaleRefreshElapsed + elapsed
    if renderScaleRefreshElapsed >= 0.25 then
        renderScaleRefreshElapsed = 0
        RefreshRenderScale()
    end
end)

local renderScalePresets = {}
for index, percent in ipairs({50, 75, 100}) do
    renderScalePresets[index] = CreateLabeledButton(
        "OpenWoWMobileRenderScalePreset" .. index, graphicsPanel,
        percent .. "%", nil, function() ApplyRenderScale(percent) end)
end

utilityButtons[#utilityButtons + 1] = CreateLabeledButton(
    "OpenWoWMobileGraphicsButton", utilityPanel, L.graphics,
    "Interface\\Icons\\Trade_Engineering",
    function()
        utilityPanel:Hide()
        graphicsPanel:Show()
    end)

local exportLogsButton = CreateLabeledButton(
    "OpenWoWMobileExportLogs", utilityPanel, L.exportLogs,
    "Interface\\Icons\\INV_Misc_Note_01",
    function()
        if state.logExportAvailable then
            SetDrawerShown(false)
            ConsoleExec("exportlogs")
        end
    end)
state.logExportAvailable = false
exportLogsButton:Disable()
exportLogsButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText(L.exportLogs)
    GameTooltip:AddLine(L.exportLogsHint, 1, 1, 1, true)
    GameTooltip:Show()
end)
exportLogsButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
utilityButtons[#utilityButtons + 1] = exportLogsButton

local movementToggle = CreateLabeledButton(
    "OpenWoWMobileMovementToggle", utilityPanel, L.hideMovement,
    "Interface\\Icons\\Ability_Rogue_Sprint",
    function()
        OpenWoWMobileMovementShown = not movementRoot:IsShown()
        OpenWoWMobile_ApplyMovementVisibility()
    end)
utilityButtons[#utilityButtons + 1] = movementToggle

function OpenWoWMobile_ApplyMovementVisibility()
    if OpenWoWMobileMovementShown == false then
        movementRoot:Hide()
        movementToggle.label:SetText(L.showMovement)
    else
        movementRoot:Show()
        movementToggle.label:SetText(L.hideMovement)
    end
end

-- This panel is independent of the combat HUD. The native router captures the
-- control finger, then sends ordinary mouse events at the separate cursor.
local mousePanel = CreateFrame("Frame", "OpenWoWMobileMousePanel")
mousePanel:SetFrameStrata("FULLSCREEN_DIALOG")
mousePanel:SetFrameLevel(100)
mousePanel:EnableMouse(true)
mousePanel.__ow_touch_mouse = "move"
mousePanel:Hide()
mousePanel:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = {left = 4, right = 4, top = 4, bottom = 4},
})
mousePanel:SetBackdropColor(0.025, 0.035, 0.055, 0.94)
mousePanel:SetBackdropBorderColor(0.55, 0.65, 0.78, 1)

local mouseControls = {}
for index, definition in ipairs({
        {"left", L.leftMouse}, {"right", L.rightMouse}, {"move", L.moveMouse}}) do
    local control = CreateFrame("Frame", "OpenWoWMobileMouse" .. index, mousePanel)
    control:EnableMouse(true)
    control.__ow_touch_mouse = definition[1]
    AddPanelBackground(control, 0.7)
    control.label = control:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    control.label:SetPoint("CENTER", control, "CENTER", 0, 0)
    control.label:SetText(definition[2])
    mouseControls[index] = control
end

local mouseToggle = CreateLabeledButton(
    "OpenWoWMobileMouseToggle", utilityPanel, L.mouse,
    "Interface\\Icons\\INV_Misc_EngGizmos_19",
    function()
        OpenWoWMobile_SetMouseShown(not mousePanel:IsShown())
        SetDrawerShown(false)
        GameTooltip:Hide()
    end)
mouseToggle:Disable()
mouseToggle:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText(L.mouse)
    GameTooltip:AddLine(L.mouseHint, 1, 1, 1, true)
    GameTooltip:Show()
end)
mouseToggle:SetScript("OnLeave", function() GameTooltip:Hide() end)
utilityButtons[#utilityButtons + 1] = mouseToggle

function OpenWoWMobile_SetMouseShown(shown)
    if shown then
        mousePanel:Show()
        mouseToggle.label:SetText(L.hideMouse)
        OpenWoWMobile_UpdateMouse(state.drawableWidth * 0.5,
                                  state.drawableHeight * 0.5, 0)
    else
        mousePanel:Hide()
        mouseToggle.label:SetText(L.mouse)
    end
end

function OpenWoWMobile_UpdateMouse(x, y, button)
    local u = state.unitsPerPoint or 1
    local scale = UIParent:GetEffectiveScale()
    local unitsPerPixel = GetScreenHeight() / state.drawableHeight
    local cursorX = x * unitsPerPixel - UIParent:GetLeft()
    local cursorY = (state.drawableHeight - y) * unitsPerPixel - UIParent:GetBottom()
    local width, height = 144 * u, 104 * u
    local left = math.max(4 * u, math.min(cursorX - width * 0.5,
                                         UIParent:GetWidth() - width - 4 * u))
    local top = cursorY - 24 * u
    if top - height < 4 * u then top = cursorY + height + 24 * u end
    top = math.max(height + 4 * u, math.min(top, UIParent:GetHeight() - 4 * u))
    if state.mouseLeft ~= left or state.mouseTop ~= top or state.mouseUnits ~= u or
            state.mouseScale ~= scale then
        state.mouseLeft, state.mouseTop, state.mouseUnits = left, top, u
        state.mouseScale = scale
        -- The parentless control survives fullscreen panels hiding UIParent,
        -- while its size and safe-area anchor still match the mobile HUD.
        mousePanel:SetScale(scale)
        mousePanel:SetSize(width, height)
        mousePanel:ClearAllPoints()
        mousePanel:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left, top)
        for index, control in ipairs(mouseControls) do
            control:ClearAllPoints()
            control:SetSize((index == 3 and 128 or 62) * u, 42 * u)
            control:SetPoint("TOPLEFT", mousePanel, "TOPLEFT",
                             (index == 2 and 74 or 8) * u,
                             -(index == 3 and 54 or 8) * u)
            local font, _, flags = control.label:GetFont()
            control.label:SetFont(font, 12 * u, flags)
        end
    end
    if state.mouseButton ~= button then
        state.mouseButton = button
        for index, control in ipairs(mouseControls) do
            local pressed = (index == 1 and button == 1) or (index == 2 and button == 3)
            if pressed then control.background:SetTexture(0.32, 0.24, 0.08, 0.94)
            else control.background:SetTexture(0.04, 0.055, 0.075, 0.7) end
        end
    end
end

local layerButton = CreateLabeledButton(
    "OpenWoWMobileLayerButton", actionCluster, "1–6",
    "Interface\\Icons\\INV_Misc_Rune_01",
    function()
        state.layer = state.layer == 1 and 2 or 1
        OpenWoWMobile_RefreshActions()
    end)

-- The launcher must outlive the hidden HUD and a hidden minimap. Anchor to the
-- map when it is visible, but keep ownership with UIParent.
local toggleButton = CreateLabeledButton(
    "OpenWoWMobileToggleButton", UIParent, L.hide,
    "Interface\\Icons\\INV_Misc_EngGizmos_19",
    function()
        OpenWoWMobileHUDShown = not root:IsShown()
        OpenWoWMobile_ApplyVisibility()
        GameTooltip:Hide()
    end)
toggleButton:SetFrameStrata("MEDIUM")
toggleButton:SetFrameLevel(Minimap:GetFrameLevel() + 8)
toggleButton:SetClampedToScreen(true)
toggleButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText(root:IsShown() and L.hideHint or L.showHint)
    GameTooltip:AddLine(L.gestureHint, 1, 1, 1, true)
    GameTooltip:Show()
end)
toggleButton:SetScript("OnLeave", function() GameTooltip:Hide() end)

function OpenWoWMobile_ApplyVisibility()
    SetDrawerShown(false)
    if OpenWoWMobileHUDShown == false then
        root:Hide()
        toggleButton.label:SetText(L.show)
        toggleButton.border:SetVertexColor(0.55, 0.7, 0.9, 1)
    else
        root:Show()
        toggleButton.label:SetText(L.hide)
        toggleButton.border:SetVertexColor(1, 0.78, 0.2, 1)
        OpenWoWMobile_RefreshActions()
    end
end

local movementArea = CreateFrame("Frame", "OpenWoWMobileMovementArea", movementRoot)
movementArea:SetFrameStrata("BACKGROUND")
movementArea:SetFrameLevel(0)
movementArea:EnableMouse(true)
movementArea.__ow_touch_movement = true
movementArea.__ow_touch_movement_control = "OpenWoWMobileJoystick"
local joystick = CreateFrame("Frame", "OpenWoWMobileJoystick", movementRoot)
-- Explicit opt-in consumed by the native touch router. The normal frame hit
-- determines ownership, including visibility, layout and occluding panels.
joystick.__ow_touch_movement = true
joystick:EnableMouse(true)
local joystickBase = joystick:CreateTexture(nil, "BACKGROUND")
joystickBase:SetAllPoints(joystick)
joystickBase:SetTexture("Interface\\Buttons\\UI-Quickslot2")
joystickBase:SetVertexColor(0.4, 0.52, 0.66, 0.62)
local joystickKnob = CreateFrame("Frame", nil, joystick)
joystickKnob:SetPoint("CENTER", joystick, "CENTER", 0, 0)
local joystickKnobTexture = joystickKnob:CreateTexture(nil, "ARTWORK")
joystickKnobTexture:SetAllPoints(joystickKnob)
joystickKnobTexture:SetTexture("Interface\\Buttons\\UI-Quickslot2")
joystickKnobTexture:SetVertexColor(0.92, 0.76, 0.34, 0.92)
local joystickLabel = joystick:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
joystickLabel:SetPoint("BOTTOM", joystick, "BOTTOM", 0, 8)
joystickLabel:SetText(L.move)

local function UpdateActionVisual(button, actionIndex)
    button.actionIndex = actionIndex
    if button.slotLabel and button.visualActionIndex ~= actionIndex then
        button.slotLabel:SetText(actionIndex)
    end
    button.visualActionIndex = actionIndex

    local page = GetActionBarPage() or 1
    local slot = (page - 1) * 12 + actionIndex
    local texture = GetActionTexture(slot)
    if not button.visualTextureInitialized or button.visualTexture ~= texture then
        if texture then
            button.icon:SetTexture(texture)
        else
            button.icon:SetTexture(0.035, 0.045, 0.065, 1)
        end
        button.visualTexture = texture
        button.visualTextureInitialized = true
    end

    local count = GetActionCount(slot) or 0
    local countText = count > 1 and count or ""
    if button.count and button.visualCount ~= countText then
        button.count:SetText(countText)
        button.visualCount = countText
    end

    local start, duration, enable = GetActionCooldown(slot)
    local cooldownActive =
        (enable and enable ~= 0 and duration and duration > 0) and true or false
    start = cooldownActive and (start or 0) or 0
    duration = cooldownActive and duration or 0
    if button.visualCooldownStart ~= start or
            button.visualCooldownDuration ~= duration then
        button.cooldown:SetCooldown(start, duration)
        button.visualCooldownStart = start
        button.visualCooldownDuration = duration
    end
    if button.visualCooldownActive ~= cooldownActive then
        if cooldownActive then
            button.cooldown:Show()
        else
            button.cooldown:Hide()
        end
        button.visualCooldownActive = cooldownActive
    end

    local usable, noMana = IsUsableAction(slot)
    local iconColor
    if texture and not usable then
        if noMana then
            iconColor = "mana"
        else
            iconColor = "disabled"
        end
    else
        iconColor = "normal"
    end
    if button.visualIconColor ~= iconColor then
        if iconColor == "mana" then
            button.icon:SetVertexColor(0.45, 0.45, 1.0, 1)
        elseif iconColor == "disabled" then
            button.icon:SetVertexColor(0.45, 0.45, 0.45, 1)
        else
            button.icon:SetVertexColor(1, 1, 1, 1)
        end
        button.visualIconColor = iconColor
    end

    local current = IsCurrentAction(slot) and true or false
    if button.visualCurrent ~= current then
        if current then
            button.border:SetVertexColor(1.0, 0.78, 0.2, 1)
        else
            button.border:SetVertexColor(1, 1, 1, 1)
        end
        button.visualCurrent = current
    end
end

function OpenWoWMobile_RefreshActions()
    for index, button in ipairs(state.actionButtons) do
        UpdateActionVisual(button, (state.layer - 1) * 6 + index)
    end
    layerButton.label:SetText(state.layer == 1 and "1–6" or "7–12")
end

local function SizeButton(button, size, unitsPerPoint)
    button:SetSize(size * unitsPerPoint, size * unitsPerPoint)
    for _, field in ipairs({"label", "slotLabel", "count"}) do
        local text = button[field]
        if text then
            local font, _, flags = text:GetFont()
            text:SetFont(font, (field == "count" and 12 or 11) * unitsPerPoint,
                         flags)
        end
    end
    if button.label then
        button.label:ClearAllPoints()
        button.label:SetPoint("BOTTOM", button, "BOTTOM", 0, 6 * unitsPerPoint)
        button.label:SetWidth((size - 8) * unitsPerPoint)
    end
end

local function PlaceToggle()
    local u = state.unitsPerPoint
    local bounds = state.safeBounds
    local size = 48 * u
    local right = bounds.right
    local top = bounds.top
    if Minimap:IsVisible() then
        local scale = Minimap:GetEffectiveScale() / UIParent:GetEffectiveScale()
        right = Minimap:GetLeft() * scale - UIParent:GetLeft() - 8 * u
        top = (Minimap:GetTop() + Minimap:GetBottom()) * 0.5 * scale -
              UIParent:GetBottom() + size * 0.5
    end
    -- Stay inside the device safe area even if the map is moved or rescaled.
    right = math.max(bounds.left + size, math.min(bounds.right, right))
    top = math.max(bounds.bottom + size, math.min(bounds.top, top))
    toggleButton:ClearAllPoints()
    toggleButton:SetPoint("TOPRIGHT", UIParent, "BOTTOMLEFT", right, top)
end

-- Centers in physical points from the lower-right safe-area margin. The
-- primary slot sits at the thumb's rest position; the other five follow its
-- sweep. Both layers and both device classes keep the same muscle memory.
local actionLayout = {
    {88, 72, 68},
    {160, 52, 56},
    {226, 82, 56},
    {224, 148, 56},
    {158, 192, 56},
    {92, 208, 56},
}

function OpenWoWMobile_ApplyMetrics(drawableWidth, drawableHeight,
                                    logicalWidth, logicalHeight, logExportAvailable,
                                    virtualMouseAvailable)
    if state.virtualMouseAvailable ~= (virtualMouseAvailable == true) then
        state.virtualMouseAvailable = virtualMouseAvailable == true
        if state.virtualMouseAvailable then mouseToggle:Enable()
        else
            mouseToggle:Disable()
            OpenWoWMobile_SetMouseShown(false)
        end
    end
    if state.logExportAvailable ~= (logExportAvailable == true) then
        state.logExportAvailable = logExportAvailable == true
        if state.logExportAvailable then exportLogsButton:Enable()
        else exportLogsButton:Disable() end
    end
    if not logicalWidth or not logicalHeight or logicalWidth <= 0 or
            logicalHeight <= 0 or drawableWidth <= 0 or drawableHeight <= 0 then
        error("OpenWoWMobile_ApplyMetrics: invalid drawable or logical viewport")
    end
    local u = GetScreenHeight() / logicalHeight
    local rootWidth, rootHeight = UIParent:GetWidth(), UIParent:GetHeight()
    -- The host republishes metrics every frame. Only a viewport or UI-scale
    -- change may rebuild the layout and reset the floating stick's origin.
    if state.drawableWidth == drawableWidth and state.drawableHeight == drawableHeight and
            state.logicalWidth == logicalWidth and state.logicalHeight == logicalHeight and
            state.unitsPerPoint == u and state.safeBounds and
            state.safeBounds.right == rootWidth - 14 * u and
            state.safeBounds.top == rootHeight - 14 * u then
        return
    end
    state.drawableWidth = drawableWidth
    state.drawableHeight = drawableHeight
    state.logicalWidth = logicalWidth
    state.logicalHeight = logicalHeight

    -- UIParent is already inset by the native layout viewport. Size controls
    -- in device points using the full screen, then anchor inside that root.
    state.unitsPerPoint = u
    state.safeBounds = {
        left = 14 * u,
        right = rootWidth - 14 * u,
        bottom = 14 * u,
        top = rootHeight - 14 * u,
    }

    local controlScale = 0.85
    actionCluster:ClearAllPoints()
    actionCluster:SetSize(316 * controlScale * u, 236 * controlScale * u)
    actionCluster:SetPoint("BOTTOMRIGHT", UIParent, "BOTTOMRIGHT",
                           -14 * u, 14 * u)

    local function PlaceControl(button, x, y, size)
        SizeButton(button, size * controlScale, u)
        button:ClearAllPoints()
        button:SetPoint("CENTER", actionCluster, "BOTTOMRIGHT",
                        -x * controlScale * u, y * controlScale * u)
    end
    for index, position in ipairs(actionLayout) do
        PlaceControl(state.actionButtons[index], unpack(position))
    end
    PlaceControl(jumpButton, 28, 142, 56)
    PlaceControl(interactButton, 28, 210, 52)
    PlaceControl(targetButton, 290, 142, 52)
    PlaceControl(friendlyButton, 290, 208, 52)
    PlaceControl(layerButton, 158, 128, 48)
    PlaceControl(drawerButton, 290, 72, 52)

    -- The drawer replaces the combat fan instead of adding another overlay.
    local utilityGap = 6 * u
    utilityPanel:ClearAllPoints()
    local utilityRows = math.ceil(#utilityButtons / 3)
    local utilitySize = math.min(54 * u,
        (rootHeight - 28 * u - (utilityRows + 1) * utilityGap) / utilityRows)
    utilityPanel:SetSize(3 * utilitySize + 4 * utilityGap,
                         utilityRows * utilitySize + (utilityRows + 1) * utilityGap)
    utilityPanel:SetPoint("BOTTOMRIGHT", actionCluster, "BOTTOMRIGHT", 0, 0)
    for index, button in ipairs(utilityButtons) do
        local column = math.mod(index - 1, 3)
        local row = math.floor((index - 1) / 3)
        SizeButton(button, utilitySize / u, u)
        button:ClearAllPoints()
        button:SetPoint("TOPLEFT", utilityPanel, "TOPLEFT",
                        utilityGap + column * (utilitySize + utilityGap),
                        -(utilityGap + row * (utilitySize + utilityGap)))
    end

    graphicsPanel:ClearAllPoints()
    graphicsPanel:SetAllPoints(utilityPanel)
    local function PlaceGraphicsText(text, y, size)
        local font, _, flags = text:GetFont()
        text:SetFont(font, size * u, flags)
        text:ClearAllPoints()
        text:SetPoint("TOP", graphicsPanel, "TOP", 0, -y * u)
        text:SetWidth(170 * u)
    end
    PlaceGraphicsText(renderScaleTitle, 12, 14)
    PlaceGraphicsText(renderScaleValue, 42, 12)
    PlaceGraphicsText(renderScaleHint, 130, 11)
    renderScaleSlider:ClearAllPoints()
    renderScaleSlider:SetSize(162 * u, 48 * u)
    renderScaleSlider:SetPoint("TOP", graphicsPanel, "TOP", 0, -70 * u)
    renderScaleSlider:GetThumbTexture():SetSize(32 * u, 48 * u)
    renderScaleTrack:ClearAllPoints()
    renderScaleTrack:SetPoint("CENTER", renderScaleSlider, "CENTER", 0, 0)
    renderScaleTrack:SetSize(162 * u, 6 * u)
    for index, button in ipairs(renderScalePresets) do
        SizeButton(button, 48, u)
        button:ClearAllPoints()
        button:SetPoint("BOTTOMLEFT", graphicsPanel, "BOTTOMLEFT",
                        (12 + (index - 1) * 57) * u, 10 * u)
        button.label:ClearAllPoints()
        button.label:SetPoint("CENTER", button, "CENTER", 0, 0)
    end

    SizeButton(toggleButton, 48, u)
    PlaceToggle()
    movementArea:ClearAllPoints()
    movementArea:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT",
                          state.safeBounds.left, state.safeBounds.bottom)
    movementArea:SetSize((state.safeBounds.right - state.safeBounds.left) * 0.4,
                          state.safeBounds.top - state.safeBounds.bottom)
    joystick:SetSize(118 * u, 118 * u)
    joystick:ClearAllPoints()
    joystick:SetPoint("BOTTOMLEFT", movementRoot, "BOTTOMLEFT", 32 * u, 32 * u)
    joystickKnob:SetSize(54 * u, 54 * u)
    local font, _, flags = joystickLabel:GetFont()
    joystickLabel:SetFont(font, 11 * u, flags)
    joystickLabel:ClearAllPoints()
    joystickLabel:SetPoint("BOTTOM", joystick, "BOTTOM", 0, 8 * u)
    OpenWoWMobile_RefreshActions()
end

function OpenWoWMobile_PositionJoystick(x, y)
    local unitsPerPixel = GetScreenHeight() / state.drawableHeight
    local originX = x * unitsPerPixel - UIParent:GetLeft()
    local originY = (state.drawableHeight - y) * unitsPerPixel - UIParent:GetBottom()
    joystick:ClearAllPoints()
    joystick:SetPoint("CENTER", UIParent, "BOTTOMLEFT", originX, originY)
end

function OpenWoWMobile_SetJoystick(directionX, directionY, active)
    local travel = (118 - 54) * 0.5 * (state.unitsPerPoint or 1)
    joystickKnob:ClearAllPoints()
    joystickKnob:SetPoint("CENTER", joystick, "CENTER",
        active and directionX * travel or 0,
        active and -directionY * travel or 0)
    joystickBase:SetVertexColor(0.4, 0.52, 0.66, active and 0.9 or 0.62)
end

root:RegisterEvent("PLAYER_ENTERING_WORLD")
root:RegisterEvent("ACTIONBAR_PAGE_CHANGED")
root:RegisterEvent("ACTIONBAR_SLOT_CHANGED")
root:RegisterEvent("ACTIONBAR_UPDATE_STATE")
root:RegisterEvent("ACTIONBAR_UPDATE_USABLE")
root:RegisterEvent("ACTIONBAR_UPDATE_COOLDOWN")
root:RegisterEvent("UPDATE_BONUS_ACTIONBAR")
root:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_ENTERING_WORLD" then
        -- Account saved variables are restored after this internal TOC loads.
        OpenWoWMobile_ApplyVisibility()
        OpenWoWMobile_ApplyMovementVisibility()
        PlaceToggle()
    else
        OpenWoWMobile_RefreshActions()
    end
end)

OpenWoWMobile_ApplyMetrics(UIParent:GetWidth(), UIParent:GetHeight(),
                           844, 390)

Minimap:HookScript("OnShow", PlaceToggle)
Minimap:HookScript("OnHide", PlaceToggle)
Minimap:HookScript("OnSizeChanged", PlaceToggle)
