#include "openwow/data/loading/dbc_locale_adapter.h"

#include "openwow/data/archive_system.h"

namespace openwow::data::loading {

dbc::DbcLocale CurrentDbcLocale() {
  switch (GetCurrentLocaleInfo().locale_index) {
    case 1:
      return dbc::DbcLocale::kKoKr;
    case 2:
      return dbc::DbcLocale::kFrFr;
    case 3:
      return dbc::DbcLocale::kDeDe;
    case 4:
      return dbc::DbcLocale::kZhCn;
    case 5:
      return dbc::DbcLocale::kZhTw;
    case 6:
      return dbc::DbcLocale::kEsEs;
    case 7:
      return dbc::DbcLocale::kEsMx;
    case 8:
      return dbc::DbcLocale::kRuRu;
    default:
      return dbc::DbcLocale::kEnUs;
  }
}

}
