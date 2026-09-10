-- Minimal MoP Classic SharedXML surface used by Blizzard_EncounterJournal.
-- This intentionally models only the public behavior consumed by the journal.

local function Install(name, value)
    if _G[name] == nil then
        _G[name] = value
    end
    return _G[name]
end

function Mixin(object, ...)
    for index = 1, select("#", ...) do
        local mixin = select(index, ...)
        if type(mixin) == "table" then
            for key, value in pairs(mixin) do
                object[key] = value
            end
        end
    end
    return object
end

function CreateFromMixins(...)
    return Mixin({}, ...)
end

function tContains(tableValue, item)
    if type(tableValue) ~= "table" then
        return false
    end
    for _, value in pairs(tableValue) do
        if value == item then
            return true
        end
    end
    return false
end

local function HexByte(value)
    value = math.max(0, math.min(255, math.floor((tonumber(value) or 0) * 255 + 0.5)))
    return string.format("%02x", value)
end

local function NewColor(r, g, b, a)
    local color = { r = r or 1, g = g or 1, b = b or 1, a = a or 1 }
    function color:GetRGB()
        return self.r, self.g, self.b
    end
    function color:GetRGBA()
        return self.r, self.g, self.b, self.a
    end
    function color:GenerateHexColor()
        return HexByte(self.a) .. HexByte(self.r) .. HexByte(self.g) .. HexByte(self.b)
    end
    function color:WrapTextInColorCode(text)
        return "|c" .. self:GenerateHexColor() .. tostring(text or "") .. "|r"
    end
    return color
end

function CreateColor(r, g, b, a)
    return NewColor(r, g, b, a)
end

local qualityColors = {
    [0] = NewColor(.62, .62, .62),
    [1] = NewColor(1, 1, 1),
    [2] = NewColor(.12, 1, 0),
    [3] = NewColor(0, .44, .87),
    [4] = NewColor(.64, .21, .93),
    [5] = NewColor(1, .5, 0),
}

function WrapTextInColorCode(text, color)
    if type(color) == "number" then
        color = qualityColors[color]
    elseif type(color) == "string" then
        return "|c" .. color .. tostring(text or "") .. "|r"
    end
    if type(color) == "table" and color.WrapTextInColorCode then
        return color:WrapTextInColorCode(text)
    end
    return tostring(text or "")
end

if not SetPortraitTextureFromCreatureDisplayID then
    function SetPortraitTextureFromCreatureDisplayID(texture, displayID)
        SetPortraitTexture(texture, displayID)
    end
end

if not GameTooltip_SetTitle then
    function GameTooltip_SetTitle(tooltip, text, color)
        local red, green, blue = 1, 1, 1
        if type(color) == "table" and color.GetRGB then
            red, green, blue = color:GetRGB()
        end
        tooltip:SetText(text or "", red, green, blue)
    end
end

if not GameTooltip_AddNormalLine then
    function GameTooltip_AddNormalLine(tooltip, text)
        tooltip:AddLine(text or "", 1, .82, 0, true)
    end
end

if not GetFinalNameFromTextureKit then
    function GetFinalNameFromTextureKit(formatString, textureKit)
        return string.format(formatString or "%s", textureKit or "")
    end
end

INVALID_EQUIPMENT_COLOR = INVALID_EQUIPMENT_COLOR or NewColor(1, .1, .1)
PAPER_FRAME_EXPANDED_COLOR = PAPER_FRAME_EXPANDED_COLOR or NewColor(.93, .79, .62)
PAPER_FRAME_COLLAPSED_COLOR = PAPER_FRAME_COLLAPSED_COLOR or NewColor(.51, .36, .18)
ENCOUNTER_JOURNAL_SCROLL_BAR_BACKGROUND_COLOR =
    ENCOUNTER_JOURNAL_SCROLL_BAR_BACKGROUND_COLOR or NewColor(.11, .07, .03, .8)
HIGHLIGHT_FONT_COLOR = HIGHLIGHT_FONT_COLOR or NewColor(1, 1, 1)

Install("ABILITIES", "技能")
Install("ALL_INVENTORY_SLOTS", "全部部位")
Install("BONUS_LOOT_TOOLTIP_BODY", "以下物品由多个首领掉落。")
Install("BONUS_LOOT_TOOLTIP_TITLE", "其他来源")
Install("BOSS_INFO_STRING_TWO", "首领：%s、%s")
Install("BOSS_INFO_STRING_MANY", "首领：%s等")
Install("DRESSUP", "试穿")
Install("DUNGEONS", "地下城")
Install("EJ_ITEM_CATEGORY_EXTREMELY_RARE", "极其稀有")
Install("EJ_ITEM_CATEGORY_VERY_RARE", "非常稀有")
Install("EJ_LOOT_SLOT_FILTER_OTHER", "其他")
Install("HOME", "首页")
Install("LOOT_NOUN", "战利品")
Install("MODEL", "模型")
Install("OVERVIEW", "概览")
Install("RAIDS", "团队副本")
Install("RETRIEVING_DATA", "正在获取数据")
Install("RETRIEVING_ITEM_INFO", "正在获取物品信息")
Install("RETURN_TO_DEFAULT", "恢复默认展开状态")

LE_EXPANSION_CLASSIC = LE_EXPANSION_CLASSIC or 0
LE_EXPANSION_BURNING_CRUSADE = LE_EXPANSION_BURNING_CRUSADE or 1
LE_EXPANSION_WRATH_OF_THE_LICH_KING = LE_EXPANSION_WRATH_OF_THE_LICH_KING or 2
LE_EXPANSION_CATACLYSM = LE_EXPANSION_CATACLYSM or 3
LE_EXPANSION_MISTS_OF_PANDARIA = LE_EXPANSION_MISTS_OF_PANDARIA or 4

Enum = Enum or {}
Enum.LFGRole = Enum.LFGRole or { Damage = 1, Healer = 2, Tank = 3 }
Enum.ItemSlotFilterType = Enum.ItemSlotFilterType or {
    NoFilter = 0, Head = 1, Neck = 2, Shoulder = 3, Cloak = 4,
    Chest = 5, Wrist = 6, Hand = 7, Waist = 8, Legs = 9, Feet = 10,
    MainHand = 11, OffHand = 12, Finger = 13, Trinket = 14, Other = 15,
}
Constants = Constants or {}
Constants.LFG_ROLEConstants = Constants.LFG_ROLEConstants or { LFG_ROLE_NO_ROLE = 0 }

DifficultyUtil = DifficultyUtil or {}
DifficultyUtil.ID = DifficultyUtil.ID or {
    DungeonNormal = 1, DungeonHeroic = 2, RaidLFR = 99,
    Raid10Normal = 3, Raid10Heroic = 4, Raid25Normal = 100,
    Raid25Heroic = 101, RaidTimewalker = 102,
}
function DifficultyUtil.GetDifficultyName(difficultyID)
    local _, _, name = OpenWoWEncounterJournal_GetDifficultyInfo(difficultyID)
    if name and name ~= "" then
        return name
    end
    return (difficultyID == 2 or difficultyID == 4) and
        PLAYER_DIFFICULTY2 or PLAYER_DIFFICULTY1
end
function DifficultyUtil.GetMaxPlayers(difficultyID)
    local _, maxPlayers = OpenWoWEncounterJournal_GetDifficultyInfo(difficultyID)
    return maxPlayers
end
function DifficultyUtil.IsPrimaryRaid(difficultyID)
    return difficultyID == DifficultyUtil.ID.Raid10Normal or
        difficultyID == DifficultyUtil.ID.Raid10Heroic or
        difficultyID == DifficultyUtil.ID.Raid25Normal or
        difficultyID == DifficultyUtil.ID.Raid25Heroic
end

SOUNDKIT = SOUNDKIT or {}
SOUNDKIT.IG_ABILITY_PAGE_TURN = SOUNDKIT.IG_ABILITY_PAGE_TURN or "igAbilityPageTurn"
SOUNDKIT.IG_CHARACTER_INFO_CLOSE = SOUNDKIT.IG_CHARACTER_INFO_CLOSE or "igCharacterInfoClose"
SOUNDKIT.IG_CHARACTER_INFO_OPEN = SOUNDKIT.IG_CHARACTER_INFO_OPEN or "igCharacterInfoOpen"
SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON = SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON or "igMainMenuOptionCheckBoxOn"
SOUNDKIT.IG_SPELLBOOK_OPEN = SOUNDKIT.IG_SPELLBOOK_OPEN or "igSpellBookOpen"

function CreateDataProvider(initialData)
    local provider = { collection = {} }
    function provider:Insert(elementData)
        table.insert(self.collection, elementData)
        return elementData
    end
    function provider:GetSize()
        return #self.collection
    end
    function provider:FindByPredicate(predicate)
        for _, elementData in ipairs(self.collection) do
            if predicate(elementData) then
                return elementData
            end
        end
        return nil
    end
    if type(initialData) == "table" then
        for _, elementData in ipairs(initialData) do
            provider:Insert(elementData)
        end
    end
    return provider
end

function CreateDataProviderByIndexCount(count)
    local provider = CreateDataProvider()
    for index = 1, tonumber(count) or 0 do
        provider:Insert({ index = index })
    end
    return provider
end

local function NewScrollView(columns, left, right, top, bottom, horizontalSpacing, verticalSpacing)
    local view = {
        columns = columns or 1,
        left = left or 0,
        right = right or 0,
        top = top or 0,
        bottom = bottom or 0,
        horizontalSpacing = horizontalSpacing or 0,
        verticalSpacing = verticalSpacing or 0,
    }
    function view:SetElementInitializer(template, initializer)
        self.template = template
        self.initializer = initializer
    end
    function view:SetElementExtentCalculator(calculator)
        self.extentCalculator = calculator
    end
    function view:SetElementFactory(factory)
        self.elementFactory = factory
    end
    function view:SetPadding(leftPadding, rightPadding, topPadding, bottomPadding, spacing)
        self.left = leftPadding or 0
        self.right = rightPadding or 0
        self.top = topPadding or 0
        self.bottom = bottomPadding or 0
        self.verticalSpacing = spacing or self.verticalSpacing
    end
    return view
end

function CreateScrollBoxListLinearView()
    return NewScrollView(1)
end

function CreateScrollBoxListGridView(columns, left, right, top, bottom, horizontalSpacing, verticalSpacing)
    return NewScrollView(columns, left, right, top, bottom, horizontalSpacing, verticalSpacing)
end

local function ConfigureScrollBar(scrollBox, scrollBar, maximum, retainScrollPosition)
    if not scrollBar then
        return
    end
    scrollBar.scrollBox = scrollBox
    scrollBar:SetMinMaxValues(0, math.max(0, maximum))
    if not retainScrollPosition then
        scrollBar:SetValue(0)
    elseif scrollBar:GetValue() > maximum then
        scrollBar:SetValue(maximum)
    end
    scrollBar:SetShown(maximum > 0)
end

local function ResolveFactory(view, elementData)
    local template, initializer = view.template, view.initializer
    if view.elementFactory then
        view.elementFactory(function(factoryTemplate, factoryInitializer)
            template = factoryTemplate
            initializer = factoryInitializer
        end, elementData)
    end
    return template, initializer
end

local function AcquireElement(scrollBox, template, templateIndex)
    scrollBox.__elementPools = scrollBox.__elementPools or {}
    local pool = scrollBox.__elementPools[template]
    if not pool then
        pool = {}
        scrollBox.__elementPools[template] = pool
    end
    local frame = pool[templateIndex]
    if not frame then
        frame = CreateFrame("Button", nil, scrollBox.__content, template)
        pool[templateIndex] = frame
    end
    return frame
end

local function RefreshScrollBox(scrollBox, dataProvider, retainScrollPosition)
    local view = scrollBox.__view
    if not view then
        return
    end
    if not scrollBox.__content then
        scrollBox.__content = CreateFrame("Frame", nil, scrollBox)
        scrollBox.__content:SetPoint("TOPLEFT", scrollBox, "TOPLEFT", 0, 0)
        scrollBox:SetScrollChild(scrollBox.__content)
    end
    for _, frame in ipairs(scrollBox.__activeElements or {}) do
        frame:Hide()
    end
    scrollBox.__activeElements = {}
    local poolUses = {}
    local y = view.top
    local rowHeight = 0
    local row = 0
    local width = math.max(1, scrollBox:GetWidth())
    scrollBox.__content:SetWidth(width)

    local collection = dataProvider and dataProvider.collection or {}
    for dataIndex, elementData in ipairs(collection) do
        local template, initializer = ResolveFactory(view, elementData)
        if template then
            poolUses[template] = (poolUses[template] or 0) + 1
            local frame = AcquireElement(scrollBox, template, poolUses[template])
            frame.__elementData = elementData
            frame.GetElementData = frame.GetElementData or function(self)
                return self.__elementData
            end
            frame:ClearAllPoints()
            if initializer then
                initializer(frame, elementData)
            end
            local extent = view.extentCalculator and view.extentCalculator(dataIndex, elementData)
                or frame:GetHeight()
            extent = math.max(1, tonumber(extent) or 1)
            if view.columns > 1 then
                local column = (dataIndex - 1) % view.columns
                row = math.floor((dataIndex - 1) / view.columns)
                local elementWidth = math.max(1, frame:GetWidth())
                frame:SetPoint("TOPLEFT", scrollBox.__content, "TOPLEFT",
                    view.left + column * (elementWidth + view.horizontalSpacing),
                    -view.top - row * (extent + view.verticalSpacing))
                rowHeight = math.max(rowHeight, extent)
            else
                frame:SetPoint("TOPLEFT", scrollBox.__content, "TOPLEFT", view.left, -y)
                y = y + extent + view.verticalSpacing
            end
            frame:Show()
            table.insert(scrollBox.__activeElements, frame)
        end
    end

    local contentHeight
    if view.columns > 1 then
        local rows = math.ceil(#collection / view.columns)
        contentHeight = view.top + view.bottom + rows * rowHeight +
            math.max(0, rows - 1) * view.verticalSpacing
    else
        contentHeight = y + view.bottom
        if #collection > 0 then
            contentHeight = contentHeight - view.verticalSpacing
        end
    end
    contentHeight = math.max(scrollBox:GetHeight(), contentHeight)
    scrollBox.__content:SetHeight(contentHeight)
    ConfigureScrollBar(scrollBox, scrollBox.__scrollBar,
        contentHeight - scrollBox:GetHeight(), retainScrollPosition)
end

local function AttachScrollBoxMethods(scrollBox)
    function scrollBox:SetDataProvider(dataProvider, retainScrollPosition)
        self.__dataProvider = dataProvider
        RefreshScrollBox(self, dataProvider, retainScrollPosition)
    end
    function scrollBox:FindFrameByPredicate(predicate)
        for _, frame in ipairs(self.__activeElements or {}) do
            if predicate(frame, frame.__elementData) then
                return frame
            end
        end
        return nil
    end
end

ScrollUtil = ScrollUtil or {}
function ScrollUtil.InitScrollBoxWithScrollBar(scrollBox, scrollBar, view)
    scrollBox.__view = view
    scrollBox.__scrollBar = scrollBar
    AttachScrollBoxMethods(scrollBox)
    ConfigureScrollBar(scrollBox, scrollBar, 0, false)
end
ScrollUtil.InitScrollBoxListWithScrollBar = ScrollUtil.InitScrollBoxWithScrollBar
function ScrollUtil.RegisterScrollBoxWithScrollBar(scrollBox, scrollBar)
    scrollBox.__scrollBar = scrollBar
    scrollBar.scrollBox = scrollBox
    ConfigureScrollBar(scrollBox, scrollBar, scrollBox:GetVerticalScrollRange(), false)
end

ScrollBoxConstants = ScrollBoxConstants or { RetainScrollPosition = true }

function OpenWoWPanelScrollFrame_OnLoad(self)
    local scrollBar = self.ScrollBar
    if not scrollBar then
        error("OpenWoW Encounter Journal: panel scroll frame has no ScrollBar child")
    end
    self.__scrollBar = scrollBar
    scrollBar.scrollBox = self
end

function OpenWoWPanelScrollFrame_OnScrollRangeChanged(self, _, verticalRange)
    ConfigureScrollBar(self, self.ScrollBar, tonumber(verticalRange) or 0, true)
end

function OpenWoWScrollBox_OnMouseWheel(self, delta)
    local scrollBar = self.__scrollBar
    if scrollBar then
        local step = scrollBar.scrollStep or 48
        scrollBar:SetValue(scrollBar:GetValue() - delta * step)
    end
end

function OpenWoWScrollBar_OnValueChanged(self, value)
    if self.scrollBox then
        self.scrollBox:SetVerticalScroll(value)
    end
end

OpenWoWMinimalScrollBarMixin = {}
function OpenWoWMinimalScrollBarMixin:ScrollToBegin()
    self:SetValue(0)
end

local function JournalTab(frame, id)
    for _, tab in ipairs(frame.Tabs or {}) do
        if tab:GetID() == id then
            return tab
        end
    end
    return nil
end

local function UpdateJournalTabs(frame)
    for _, tab in ipairs(frame.Tabs or {}) do
        if tab.isDisabled then
            PanelTemplates_SetDisabledTabState(tab)
        elseif tab:GetID() == frame.selectedTab then
            PanelTemplates_SelectTab(tab)
        else
            PanelTemplates_DeselectTab(tab)
        end
    end
end

function OpenWoWJournal_SetNumTabs(frame, numTabs)
    frame.numTabs = numTabs
end

function OpenWoWJournal_SetTab(frame, id)
    frame.selectedTab = id
    UpdateJournalTabs(frame)
end

function OpenWoWJournal_SetTabEnabled(frame, id, enabled)
    local tab = JournalTab(frame, id)
    if not tab then
        error("OpenWoW Encounter Journal: missing content tab " .. tostring(id))
    end
    if enabled then
        tab.isDisabled = nil
        tab:SetDisabledFontObject(GameFontHighlightSmall)
        tab:Enable()
    else
        tab.isDisabled = true
    end
    UpdateJournalTabs(frame)
end

OpenWoWPortraitFrameMixin = {}
function OpenWoWPortraitFrameMixin:SetTitle(text)
    if self.TitleText then
        self.TitleText:SetText(text or "")
    end
end
function OpenWoWPortraitFrameMixin:SetPortraitToAsset(asset)
    if self.portrait then
        self.portrait:SetTexture(asset)
    end
end

OpenWoWScrollingFontMixin = {}
local function GetScrollingFontText(scrollingFont)
    local content = scrollingFont.Content
    local text = content and content.Text
    if not text then
        error("OpenWoW Encounter Journal: scrolling font has no Content.Text region")
    end
    return text
end
function OpenWoWScrollingFontMixin:GetScrollBox()
    return self.ScrollBox
end
function OpenWoWScrollingFontMixin:SetText(text)
    local textRegion = GetScrollingFontText(self)
    textRegion:SetText(text or "")
    local height = math.max(self:GetHeight(), textRegion:GetStringHeight() + 4)
    self.Content:SetHeight(height)
    ConfigureScrollBar(self.ScrollBox, self.ScrollBox.__scrollBar,
        height - self:GetHeight(), false)
end
function OpenWoWScrollingFontMixin:SetTextColor(colorOrRed, green, blue, alpha)
    local textRegion = GetScrollingFontText(self)
    if type(colorOrRed) == "table" and colorOrRed.GetRGBA then
        textRegion:SetTextColor(colorOrRed:GetRGBA())
    else
        textRegion:SetTextColor(colorOrRed, green, blue, alpha)
    end
end
function OpenWoWScrollingFontMixin:HasScrollableExtent()
    return self.Content:GetHeight() > self:GetHeight()
end

function OpenWoWScrollingFont_OnLoad(self)
    self.ScrollBox:SetScrollChild(self.Content)
end

OpenWoWAtlasTextureMixin = {}
local tierAtlasFiles = {
    ["UI-EJ-Classic"] = "Interface\\EncounterJournal\\OpenWoW\\mop-605327",
    ["UI-EJ-BurningCrusade"] = "Interface\\EncounterJournal\\OpenWoW\\mop-605326",
    ["UI-EJ-WrathoftheLichKing"] = "Interface\\EncounterJournal\\OpenWoW\\mop-605329",
}
function OpenWoWAtlasTextureMixin:SetAtlas(atlas)
    self:SetTexture(tierAtlasFiles[atlas] or "Interface\\EncounterJournal\\UI-EJ-Cataclysm")
end

OpenWoWMaskTextureMixin = {}
function OpenWoWMaskTextureMixin:SetMask(mask)
    self.__openWoWMask = mask
end
function OpenWoWMaskTextureMixin:SetTexture(texture)
    if self.__openWoWMask and type(texture) == "string" and texture ~= "" then
        SetPortraitToTexture(self, texture)
        return
    end
    error("OpenWoW Encounter Journal: masked texture requires a texture path")
end

OpenWoWEncounterModelMixin = {}
function OpenWoWEncounterModelMixin:SetFromModelSceneID()
end
function OpenWoWEncounterModelMixin:GetActorByTag()
    return self
end
function OpenWoWEncounterModelMixin:SetModelByCreatureDisplayID(displayID)
    self:SetDisplayInfo(displayID)
end
function OpenWoWEncounterModelMixin:GetNumActors()
    return 1
end
function OpenWoWEncounterModelMixin:GetActorAtIndex(index)
    return index == 1 and self or nil
end

AutoScalingFontStringMixin = AutoScalingFontStringMixin or {}

OpenWoWDropdownMixin = {}
local function RefreshDropdown(dropdown)
    dropdown.__choices = {}
    local description = {}
    function description:SetTag(tag)
        self.tag = tag
    end
    function description:CreateRadio(text, isSelected, setSelected, value)
        table.insert(dropdown.__choices, {
            text = text or "", isSelected = isSelected,
            setSelected = setSelected, value = value,
        })
    end
    dropdown.__builder(dropdown, description)
    local selectedText = dropdown.__defaultText or ""
    for _, choice in ipairs(dropdown.__choices) do
        if choice.isSelected and choice.isSelected(choice.value) then
            selectedText = choice.text
            break
        end
    end
    dropdown.Text:SetText(selectedText)
end

function OpenWoWDropdownMixin:SetupMenu(builder)
    self.__builder = builder
    RefreshDropdown(self)
end
function OpenWoWDropdownMixin:SetDefaultText(text)
    self.__defaultText = text
    if self.Text then
        self.Text:SetText(text or "")
    end
end
function OpenWoWDropdownMixin:SetText(text)
    self.Text:SetText(text or "")
end
function OpenWoWDropdownMixin:SetFormattedText(formatString, ...)
    self.Text:SetFormattedText(formatString, ...)
end
function OpenWoWDropdownMixin:Enable()
    self.__disabled = false
    self.Button:Enable()
end
function OpenWoWDropdownMixin:Disable()
    self.__disabled = true
    self.Button:Disable()
    if self.Menu then
        self.Menu:Hide()
    end
end
function OpenWoWDropdownMixin:IsEnabled()
    return not self.__disabled
end
function OpenWoWDropdownMixin:ToggleMenu()
    if self.__disabled or not self.__builder then
        return
    end
    RefreshDropdown(self)
    if self.Menu:IsShown() then
        self.Menu:Hide()
        return
    end
    self.Menu.choices = self.Menu.choices or {}
    for index, choice in ipairs(self.__choices) do
        local button = self.Menu.choices[index]
        if not button then
            button = CreateFrame("Button", nil, self.Menu, "OpenWoWDropdownChoiceTemplate")
            button:SetPoint("TOPLEFT", self.Menu, "TOPLEFT", 2, -2 - (index - 1) * 20)
            button:SetPoint("TOPRIGHT", self.Menu, "TOPRIGHT", -2, -2 - (index - 1) * 20)
            self.Menu.choices[index] = button
        end
        button.owner = self
        button.choice = choice
        button:SetText(choice.text)
        button:Show()
    end
    for index = #self.__choices + 1, #self.Menu.choices do
        self.Menu.choices[index]:Hide()
    end
    self.Menu:SetHeight(math.max(24, #self.__choices * 20 + 4))
    self.Menu:Show()
end

function OpenWoWDropdown_OnLoad(self)
    self.Button.owner = self
end
function OpenWoWDropdownButton_OnClick(self)
    self.owner:ToggleMenu()
end
function OpenWoWDropdownChoice_OnClick(self)
    local choice = self.choice
    if choice and choice.setSelected then
        choice.setSelected(choice.value)
    end
    self.owner.Menu:Hide()
    if self.owner.__builder then
        RefreshDropdown(self.owner)
    end
end

SearchBoxListElementMixin = SearchBoxListElementMixin or {}
function SearchBoxListElementMixin.OnEnter(self)
    if self.selectedTexture then
        self.selectedTexture:Show()
    end
end
function SearchBoxListElementMixin.OnClick(self)
    local searchBox = EncounterJournal and EncounterJournal.searchBox
    if searchBox then
        searchBox:HideSearchPreview()
    end
end

SearchBoxListMixin = SearchBoxListMixin or {}
function SearchBoxListMixin:EnsureButtons()
    if self.__buttons then
        return
    end
    self.__buttons = {}
    self.SearchPreview:SetFrameStrata("DIALOG")
    for index = 1, 5 do
        local button = CreateFrame("Button", nil, self.SearchPreview, self.buttonTemplate)
        button:SetPoint("TOPLEFT", self.SearchPreview, "TOPLEFT", 0, -(index - 1) * 27)
        self.__buttons[index] = button
    end
    self.ShowAllButton = CreateFrame("Button", nil, self.SearchPreview, self.showAllButtonTemplate)
    self.ShowAllButton:SetPoint("TOPLEFT", self.SearchPreview, "TOPLEFT", 0, -5 * 27)
end
function SearchBoxListMixin:SetSearchResultsFrame(frame)
    self.searchResultsFrame = frame
    self:EnsureButtons()
end
function SearchBoxListMixin:GetButtons()
    self:EnsureButtons()
    return self.__buttons
end
function SearchBoxListMixin:GetSearchButtonCount()
    self:EnsureButtons()
    return #self.__buttons
end
function SearchBoxListMixin:IsCurrentTextValidForSearch()
    local text = self:GetText() or ""
    return text ~= SEARCH and string.len(text) >= (self.minCharacters or 1)
end
function SearchBoxListMixin.OnTextChanged(self)
    local text = self:GetText() or ""
    return self:IsCurrentTextValidForSearch(), text
end
function SearchBoxListMixin.OnFocusGained(self)
    if SerachBoxTemplate_OnEditFocusGained then
        SerachBoxTemplate_OnEditFocusGained(self)
    end
end
function SearchBoxListMixin:HideSearchPreview()
    if self.SearchPreview then
        self.SearchPreview:Hide()
    end
end
function SearchBoxListMixin:IsSearchPreviewShown()
    return self.SearchPreview and self.SearchPreview:IsShown()
end
function SearchBoxListMixin:HideSearchProgress()
    if self.searchProgress then
        self.searchProgress:Hide()
    end
end
function SearchBoxListMixin:UpdateSearchPreview(searchFinished, databaseLoaded, numResults)
    self:EnsureButtons()
    self.SearchPreview:SetShown((tonumber(numResults) or 0) > 0 or not searchFinished)
    self.ShowAllButton:SetShown((tonumber(numResults) or 0) > #self.__buttons)
    self.ShowAllButton:SetText(string.format(ENCOUNTER_JOURNAL_SHOW_SEARCH_RESULTS,
        tonumber(numResults) or 0))
    self.SearchPreview:SetHeight(5 * 27 + ((tonumber(numResults) or 0) > 5 and 24 or 0))
end
function SearchBoxListMixin:SetSearchPreviewSelectionToAllResults()
end
function SearchBoxListMixin:Close()
    self:HideSearchPreview()
    self:ClearFocus()
end
function SearchBoxListMixin:Clear()
    self:SetText("")
    self:HideSearchPreview()
end

function OpenWoWSearchBoxList_OnLoad(self)
    if SearchBoxTemplate_OnLoad then
        SearchBoxTemplate_OnLoad(self)
    end
    self.SearchPreview = self.SearchPreview or self.searchPreview
end

C_EncounterJournal = C_EncounterJournal or {}
function C_EncounterJournal.GetLootInfoByIndex(index, occurrence)
    local name, icon, slot, armorType, itemID, link, encounterID, filterType,
        journalQuality =
        EJ_GetLootInfoByIndex(index, occurrence)
    if not itemID then
        return nil
    end
    local quality = tonumber(journalQuality) or select(3, GetItemInfo(itemID)) or 1
    return {
        name = name, icon = icon, slot = slot, armorType = armorType,
        itemID = itemID, link = link, encounterID = encounterID,
        itemQuality = quality, filterType = filterType or 0,
        handError = false, weaponTypeError = false,
    }
end
function C_EncounterJournal.GetLootInfo(itemID)
    local name, icon, slot, armorType, resolvedID, link, encounterID, filterType,
        journalQuality =
        EJ_GetLootInfo(itemID)
    if not resolvedID then
        return nil
    end
    local quality = tonumber(journalQuality) or
        select(3, GetItemInfo(resolvedID)) or 1
    return {
        name = name, icon = icon, slot = slot, armorType = armorType,
        itemID = resolvedID, link = link, encounterID = encounterID,
        itemQuality = quality, filterType = filterType or 0,
    }
end
function C_EncounterJournal.GetSectionInfo(sectionID)
    local title, description, headerType, abilityIcon, creatureDisplayID,
        siblingSectionID, firstChildSectionID, filteredByDifficulty, link, startsOpen,
        spellID, uiModelSceneID =
        EJ_GetSectionInfo(sectionID)
    if not title then
        return nil
    end
    return {
        title = title, description = description or "", headerType = headerType or 0,
        abilityIcon = abilityIcon, creatureDisplayID = creatureDisplayID or 0,
        siblingSectionID = siblingSectionID, firstChildSectionID = firstChildSectionID,
        filteredByDifficulty = filteredByDifficulty and true or false,
        link = link, startsOpen = startsOpen and true or false,
        spellID = spellID or 0, uiModelSceneID = uiModelSceneID,
    }
end
function C_EncounterJournal.GetSectionIconFlags(sectionID)
    local mask = tonumber(EJ_GetSectionIconFlags(sectionID)) or 0
    local flags = {}
    for flag = 0, 12 do
        if math.floor(mask / (2 ^ flag)) % 2 == 1 then
            table.insert(flags, flag)
        end
    end
    return flags
end
function C_EncounterJournal.InstanceHasLoot()
    return EJ_GetNumLoot() > 0
end
function C_EncounterJournal.GetSlotFilter()
    return EJ_GetSlotFilter()
end
function C_EncounterJournal.SetSlotFilter(filterType)
    EJ_SetSlotFilter(filterType)
end
function C_EncounterJournal.ResetSlotFilter()
    EJ_SetSlotFilter(0)
end
function C_EncounterJournal.SetTab(tabID)
end

C_Item = C_Item or {}
function C_Item.GetItemInfo(item)
    return GetItemInfo(item)
end
C_SpecializationInfo = C_SpecializationInfo or {}
function C_SpecializationInfo.GetSpecialization()
    return nil
end

ClassMenu = ClassMenu or {}
function ClassMenu.InitClassSpecDropdown(dropdown, getClassFilter, getSpecFilter, setFilter)
    dropdown:SetupMenu(function(_, rootDescription)
        rootDescription:CreateRadio("全部职业", function(value)
            return (getClassFilter() or 0) == value
        end, function(value)
            setFilter(value, 0)
        end, 0)
        local classNames = {
            [1] = "战士", [2] = "圣骑士", [3] = "猎人", [4] = "潜行者",
            [5] = "牧师", [6] = "死亡骑士", [7] = "萨满祭司", [8] = "法师",
            [9] = "术士", [11] = "德鲁伊",
        }
        local classIDs = { EJ_GetAvailableClasses() }
        for _, classID in ipairs(classIDs) do
            local className = classNames[classID] or tostring(classID)
            rootDescription:CreateRadio(className or tostring(classID), function(value)
                return getClassFilter() == value
            end, function(value)
                setFilter(value, 0)
            end, classID)
        end
    end)
end

AdventureGuideUtil = AdventureGuideUtil or {}
function AdventureGuideUtil.GetCurrentJournalInstance()
    local instanceID = EJ_GetCurrentInstance()
    return instanceID ~= 0 and instanceID or nil
end

ChatFrameUtil = ChatFrameUtil or {}
function ChatFrameUtil.TryInsertChatLink(link)
    return HandleModifiedItemClick and HandleModifiedItemClick(link) or false
end
function ChatFrameUtil.GetActiveWindow()
    return nil
end
function ChatFrameUtil.InsertLink(link)
end

EventRegistry = EventRegistry or {}
function EventRegistry:TriggerEvent()
end

function GetClassicExpansionLevel()
    return LE_EXPANSION_WRATH_OF_THE_LICH_KING
end
function GetExpansionForLevel()
    return LE_EXPANSION_WRATH_OF_THE_LICH_KING
end
function GetSpecializationRoleEnum()
    return Enum.LFGRole.Damage
end
function IsGMClient()
    return false
end
function MonthlyActivitiesFrame_OpenFrame()
end
function EJ_IsLootListOutOfDate()
    return false
end
if not EJ_GetNumEncountersForLootByIndex then
    function EJ_GetNumEncountersForLootByIndex()
        return 1
    end
end
function EJ_IsSearchFinished()
    return true
end
function EJ_EndSearch()
end
function EJ_GetSearchSize()
    return EJ_GetNumSearchResults()
end
function EJ_GetSearchProgress()
    return EJ_GetNumSearchResults()
end
function EJ_GetContentTuningID()
    return 0
end
function EJ_GetLootFilter()
    return EJ_GetClassFilter(), 0
end
function EJ_SetLootFilter(classID)
    EJ_SetClassLootFilter(classID)
end
function EJ_ResetLootFilter()
    EJ_SetClassLootFilter(0)
end
function SetItemButtonQuality()
end
if not SharedTooltip_SetBackdropStyle then
    function SharedTooltip_SetBackdropStyle()
    end
end
