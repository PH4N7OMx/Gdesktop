#pragma once

#include "data/data_types.h"
#include <QtCore/QDate>
#include <QtCore/QString>

class UserData;

namespace Jel::AccountInfo {

bool Observe(not_null<UserData*> user);
[[nodiscard]] QDate KnownRegistration(not_null<UserData*> user);
[[nodiscard]] QString PhoneCountry(not_null<UserData*> user);
[[nodiscard]] QDate EstimateRegistration(not_null<UserData*> user);

} // namespace Jel::AccountInfo
