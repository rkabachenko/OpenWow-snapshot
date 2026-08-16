#pragma once

#include <cstdint>

namespace openwow::ui::game {

void CGTooltip_InitFadeTimer(void* tooltip);
void CGTooltip_OnUpdate(void* tooltip, float elapsed);
void CGTooltip_OnLogout();
void* CGTooltip_Constructor(void* memory, int parent);
void CGTooltip_Destructor(void* tooltip);
void CGTooltip_DestructorAndFree(void* tooltip, std::uint8_t flags);
void CGTooltip_ClearLines(void* tooltip);
void CGTooltip_FinalizeTooltip(void* tooltip);
void CGTooltip_Show(void* tooltip);
void CGTooltip_Hide(void* tooltip);
void CGTooltip_SetOwnerInternal(void* tooltip, int owner_frame, int anchor_type,
                                float x_offset, float y_offset);
void CGTooltip_AddTextLine(void* tooltip, const char* left, const char* right,
                           const void* left_color, const void* right_color, int wrap);
void CGTooltip_AddLineDefaultColor(void* tooltip, const char* left,
                                   const char* right, int wrap);
void CGTooltip_AppendToFirstLine(void* tooltip, const char* text);
void CGTooltip_OnGuildQueryResolved(int arg1, int arg2, void* tooltip, bool loaded);
void CGTooltip_OnSpellTooltipAsyncItemResolved(int arg1, int arg2, void* tooltip,
                                                bool loaded);
void CGTooltip_OnQuestTemplateResolved(int arg1, int arg2, void* tooltip, bool loaded);
void CGTooltip_OnAchievementNameResolved(int arg1, int arg2, void* tooltip, bool loaded);
const char* CGTooltip_GetTypeName();
int CGTooltip_GetTypeId();
bool CGTooltip_IsTypeCompatible(int type_id);
bool CGTooltip_IsTypeNameMatch(const char* name);
int CGTooltip_OnXmlLoad(void* tooltip, unsigned int* arg2, int arg3);
int CGTooltip_RegisterEventHandler();
int CGTooltip_OnItemUpdate(std::uint64_t guid, int arg2, int arg3, int arg4,
                           void* tooltip);
int CGTooltip_OnSpellUpdate(std::uint64_t guid, int arg2, int arg3, int arg4,
                            void* tooltip);

}
