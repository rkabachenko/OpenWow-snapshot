#pragma once

namespace openwow::ui::game {

[[nodiscard]] const char* GetLastSystemMessageText();
void DisplaySystemMessage(int message_index, ...);
void PetNameCache_HandlePetRenameResult(int result_code);
void DisplaySetPlayerDeclinedNamesResult(unsigned int result_index);

}
