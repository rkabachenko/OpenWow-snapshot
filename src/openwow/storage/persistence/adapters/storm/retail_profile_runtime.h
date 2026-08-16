#pragma once

#include "openwow/storage/persistence/adapters/storm/retail_profile.h"
#include "openwow/storage/persistence/profile_document.h"

namespace openwow::storage::persistence::detail {

int LoadRetailProfileDocument(CProfile* profile,
                              const ProfileDocument& document);

}
