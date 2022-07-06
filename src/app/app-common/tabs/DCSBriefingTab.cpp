/*
 * OpenKneeboard
 *
 * Copyright (C) 2022 Fred Emmott <fred@fredemmott.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <OpenKneeboard/DCSBriefingTab.h>
#include <OpenKneeboard/DCSExtractedMission.h>
#include <OpenKneeboard/DCSWorld.h>
#include <OpenKneeboard/GameEvent.h>
#include <OpenKneeboard/ImagePageSource.h>
#include <OpenKneeboard/NavigationTab.h>
#include <OpenKneeboard/PlainTextPageSource.h>
#include <OpenKneeboard/dprint.h>
#include <OpenKneeboard/scope_guard.h>

#include <GeographicLib/GeoCoords.hpp>
#include <GeographicLib/TransverseMercatorExact.hpp>
#include <GeographicLib/UTMUPS.hpp>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>

extern "C" {
#include <GeomagnetismHeader.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <LuaBridge/LuaBridge.h>

using DCS = OpenKneeboard::DCSWorld;

namespace OpenKneeboard {

class DCSMagneticModel {
 public:
  DCSMagneticModel(const std::filesystem::path& dcsInstallation);
  ~DCSMagneticModel();

  float GetMagneticVariation(
    const std::chrono::year_month_day& date,
    float latitude,
    float longitude) const;

 private:
  MAGtype_MagneticModel* GetModel(
    const std::chrono::year_month_day& date) const;
  std::vector<MAGtype_MagneticModel*> mModels;
};

DCSMagneticModel::DCSMagneticModel(
  const std::filesystem::path& dcsInstallation) {
  const auto cofDir = dcsInstallation / "Data" / "MagVar" / "COF";

  for (const auto& file: std::filesystem::directory_iterator(cofDir)) {
    if (!std::filesystem::is_regular_file(file)) {
      continue;
    }
    MAGtype_MagneticModel* model = nullptr;
    MAG_robustReadMagModels(
      const_cast<char*>(file.path().string().c_str()),
      reinterpret_cast<MAGtype_MagneticModel*(*)[]>(&model),
      1);
    mModels.push_back(model);
  }
}

MAGtype_MagneticModel* DCSMagneticModel::GetModel(
  const std::chrono::year_month_day& date) const {
  MAGtype_Date magDate {
    static_cast<int>(date.year()),
    static_cast<int>(static_cast<unsigned>(date.month())),
    static_cast<int>(static_cast<unsigned>(date.day())),
  };
  char error[512];
  MAG_DateToYear(&magDate, error);
  const auto year = magDate.DecimalYear;

  MAGtype_MagneticModel* prev = nullptr;
  for (auto model: mModels) {
    if (model->epoch <= year) {
      prev = model;
      continue;
    }

    if (!prev) {
      return model;
    }

    if ((model->epoch - year) > (year - model->epoch)) {
      return model;
    }

    return prev;
  }

  // in the future!
  return mModels.back();
}

DCSMagneticModel::~DCSMagneticModel() {
  for (auto model: mModels) {
    MAG_FreeMagneticModelMemory(model);
  }
}

float DCSMagneticModel::GetMagneticVariation(
  const std::chrono::year_month_day& date,
  float latitude,
  float longitude) const {
  const MAGtype_CoordGeodetic geoCoord {
    .lambda = longitude,
    .phi = latitude,
  };

  MAGtype_Ellipsoid ellipsoid;
  MAGtype_Geoid geoid;
  MAG_SetDefaults(&ellipsoid, &geoid);
  MAGtype_CoordSpherical sphereCoord {};
  MAG_GeodeticToSpherical(ellipsoid, geoCoord, &sphereCoord);

  MAGtype_MagneticModel* model = this->GetModel(date);
  MAGtype_MagneticModel* timedModel;
  const auto nMax = model->nMax;
  // Taken from wmm_point.c sample
  timedModel = MAG_AllocateModelMemory((nMax + 1) * (nMax + 2) / 2);
  scope_guard freeTimedModel(
    [=]() { MAG_FreeMagneticModelMemory(timedModel); });

  MAGtype_Date magDate {
    static_cast<int>(date.year()),
    static_cast<int>(static_cast<unsigned>(date.month())),
    static_cast<int>(static_cast<unsigned>(date.day())),
  };
  char error[512];
  MAG_DateToYear(&magDate, error);
  MAG_TimelyModifyMagneticModel(magDate, model, timedModel);

  MAGtype_GeoMagneticElements geoElements {};
  MAG_Geomag(ellipsoid, sphereCoord, geoCoord, timedModel, &geoElements);

  return geoElements.Decl;
}

DCSBriefingTab::DCSBriefingTab(const DXResources& dxr, KneeboardState* kbs)
  : TabWithDoodles(dxr, kbs),
    mDXR(dxr),
    mImagePages(std::make_unique<ImagePageSource>(dxr)),
    mTextPages(std::make_unique<PlainTextPageSource>(dxr, _("[no briefing]"))) {
}

DCSBriefingTab::~DCSBriefingTab() {
  this->RemoveAllEventListeners();
}

utf8_string DCSBriefingTab::GetGlyph() const {
  return "\uE95D";
}

utf8_string DCSBriefingTab::GetTitle() const {
  return _("Briefing");
}

uint16_t DCSBriefingTab::GetPageCount() const {
  return mImagePages->GetPageCount() + mTextPages->GetPageCount();
}

D2D1_SIZE_U DCSBriefingTab::GetNativeContentSize(uint16_t pageIndex) {
  const auto textPageCount = mTextPages->GetPageCount();
  if (pageIndex < textPageCount) {
    return mTextPages->GetNativeContentSize(pageIndex);
  }
  return mImagePages->GetNativeContentSize(pageIndex - textPageCount);
}

static std::string GetCountries(const luabridge::LuaRef& countries) {
  std::string ret;
  for (auto&& [i, country]: luabridge::pairs(countries)) {
    if (
      country["static"].isNil() || country["helicopter"].isNil()
      || country["vehicle"].isNil() || country["plane"].isNil()) {
      continue;
    }
    if (!ret.empty()) {
      ret += ", ";
    }
    ret += country["name"].cast<std::string>();
  }
  return ret;
}

struct DCSBriefingWind {
  DCSBriefingWind(const luabridge::LuaRef& data) {
    mSpeed = data["speed"].cast<float>();
    mDirection = data["dir"].cast<int>();
    mStandardDirection = (180 + mDirection) % 360;
    if (mDirection == 0) {
      mDirection = 360;
    }
    if (mStandardDirection == 0) {
      mStandardDirection = 360;
    }
    mSpeedInKnots = mSpeed * 1.94384f;
  }

  float mSpeed;
  float mSpeedInKnots;
  int mDirection;
  int mStandardDirection;
};

void DCSBriefingTab::Reload() noexcept {
  const scope_guard emitEvents([this]() {
    this->ClearContentCache();
    this->evFullyReplacedEvent.Emit();
    this->evAvailableFeaturesChangedEvent.Emit();
    this->evNeedsRepaintEvent.Emit();
  });
  mImagePages->SetPaths({});
  mTextPages->ClearText();

  if (!mMission) {
    return;
  }

  lua_State* lua = lua_open();
  scope_guard closeLua([&lua]() { lua_close(lua); });

  luaL_openlibs(lua);

  const auto root = mMission->GetExtractedPath();

  int error = luaL_dofile(lua, to_utf8(root / "mission").c_str());
  if (error) {
    dprintf("Failed to load lua mission table: {}", lua_tostring(lua, -1));
    return;
  }

  const auto localized = root / "l10n" / "DEFAULT";
  error = luaL_dofile(lua, to_utf8(localized / "dictionary").c_str());
  if (error) {
    dprintf("Failed to load lua dictionary table: {}", lua_tostring(lua, -1));
    return;
  }

  const auto mission = luabridge::getGlobal(lua, "mission");
  const auto dictionary = luabridge::getGlobal(lua, "dictionary");

  if (std::filesystem::exists(localized / "mapResource")) {
    error = luaL_dofile(lua, to_utf8(localized / "MapResource").c_str());
    if (error) {
      dprintf("Failed to load lua mapResource: {}", lua_tostring(lua, -1));
      return;
    }

    const auto mapResource = luabridge::getGlobal(lua, "mapResource");

    std::vector<std::filesystem::path> images;

    luabridge::LuaRef force = mission[CoalitionKey(
      "pictureFileNameN", "pictureFileNameR", "pictureFileNameB")];
    for (auto&& [i, resourceName]: luabridge::pairs(force)) {
      const auto fileName = mapResource[resourceName].cast<std::string>();
      const auto path = localized / fileName;
      if (std::filesystem::is_regular_file(path)) {
        images.push_back(path);
      }
    }
    mImagePages->SetPaths(images);
  }

  const std::string title = dictionary[mission["sortie"]];

  const auto startDate = mission["date"];
  const auto startSecondsSinceMidnight = mission["start_time"];
  const auto startDateTime = std::format(
    "{:04d}-{:02d}-{:02d} {:%T}",
    startDate["Year"].cast<unsigned int>(),
    startDate["Month"].cast<unsigned int>(),
    startDate["Day"].cast<unsigned int>(),
    std::chrono::seconds {
      startSecondsSinceMidnight.cast<unsigned int>(),
    });
  const std::string redCountries
    = GetCountries(mission["coalition"]["red"]["country"]);
  const std::string blueCountries
    = GetCountries(mission["coalition"]["blue"]["country"]);
  std::string_view alliedCountries;
  std::string_view enemyCountries;
  switch (mDCSState.mCoalition) {
    case DCSWorld::Coalition::Neutral:
      break;
    case DCSWorld::Coalition::Blue:
      alliedCountries = blueCountries;
      enemyCountries = redCountries;
      break;
    case DCSWorld::Coalition::Red:
      alliedCountries = redCountries;
      enemyCountries = blueCountries;
  }

  mTextPages->PushMessage(std::format(
    _("MISSION OVERVIEW\n"
      "\n"
      "Title:    {}\n"
      "Start at: {}\n"
      "My side:  {}\n"
      "Enemies:  {}"),
    title,
    startDateTime,
    alliedCountries,
    enemyCountries));

  mTextPages->PushMessage(std::format(
    _("SITUATION\n"
      "\n"
      "{}"),
    dictionary[mission["descriptionText"]].cast<std::string>()));

  mTextPages->PushMessage(std::format(
    _("OBJECTIVE\n"
      "\n"
      "{}"),
    dictionary[mission[CoalitionKey(
                 "descriptionNeutralTask",
                 "descriptionRedTask",
                 "descriptionBlueTask")]]
      .cast<std::string>()));

  double magVar = 0.0f;

  if (mDCSState.mOrigin && mDCSState.mCoalition != DCS::Coalition::Neutral) {
    const auto& origin = mDCSState.mOrigin;
    const int zone
      = GeographicLib::UTMUPS::StandardZone(origin->mLat, origin->mLong);
    const double zoneMeridian = (6.0 * zone - 183);

    const auto key = CoalitionKey("neutral", "red", "blue");
    const auto xyBulls = mission["coalition"][key]["bullseye"];

    const auto bullsNorthing = xyBulls["x"].cast<double>();
    const auto bullsEasting = xyBulls["y"].cast<double>();

    const auto utm = GeographicLib::TransverseMercatorExact::UTM();
    double originX, originY;
    utm.Forward(zoneMeridian, origin->mLat, origin->mLong, originX, originY);

    double bullsLat;
    double bullsLong;
    const double bullsX = originX + bullsEasting;
    const double bullsY = originY + bullsNorthing;
    utm.Reverse(zoneMeridian, bullsX, bullsY, bullsLat, bullsLong);

    GeographicLib::GeoCoords bulls(bullsLat, bullsLong);

    DCSMagneticModel magModel(mInstallationPath);
    magVar = magModel.GetMagneticVariation(
      std::chrono::year_month_day {
        std::chrono::year {startDate["Year"].cast<int>()},
        std::chrono::month {startDate["Month"].cast<unsigned>()},
        std::chrono::day {startDate["Day"].cast<unsigned>()},
      },
      static_cast<float>(bullsLat),
      static_cast<float>(bullsLong));

    mTextPages->PushMessage(std::format(
      _("BULLSEYE\n"
        "\n"
        "Position: {}\n"
        "          {}\n"
        "          {}\n"
        "MagVar:   {:.01f}°"),
      bulls.DMSRepresentation(1),
      bulls.UTMUPSRepresentation(1),
      bulls.MGRSRepresentation(1),
      magVar));
  }

  const auto weather = mission["weather"];
  const auto temperature = weather["season"]["temperature"].cast<int>();
  const auto qnhMmHg = weather["qnh"].cast<int>();
  const auto qnhInHg = qnhMmHg / 25.4;
  const auto cloudBase = weather["clouds"]["base"].cast<int>();
  const auto wind = weather["wind"];
  DCSBriefingWind windAtGround {luabridge::LuaRef {wind["atGround"]}};
  DCSBriefingWind windAt2000 {luabridge::LuaRef {wind["at2000"]}};
  DCSBriefingWind windAt8000 {luabridge::LuaRef {wind["at8000"]}};

  mTextPages->PushMessage(std::format(
    _("WEATHER\n"
      "\n"
      "Temperature: {:+d}°\n"
      "QNH:         {} / {:.02f}\n"
      "Cloud cover: Base {}\n"
      "Nav wind:    At GRND {:.0f} m/s, {}° Meteo {}°\n"
      "             At 2000m {:.0f} m/s, {}° Meteo {}°\n"
      "             At 8000m {:.0f} m/s, {}° Meteo {}°"),
    temperature,
    qnhMmHg,
    qnhInHg,
    cloudBase,
    windAtGround.mSpeed,
    windAtGround.mDirection,
    windAtGround.mStandardDirection,
    windAt2000.mSpeed,
    windAt2000.mDirection,
    windAt2000.mStandardDirection,
    windAt8000.mSpeed,
    windAt8000.mDirection,
    windAt8000.mStandardDirection));

  if (mDCSState.mAircraft.starts_with("A-10C")) {
    mTextPages->PushMessage(std::format(
      _("A-10C LASTE WIND\n"
        "\n"
        "Using bullseye magvar: {:.1f}°\n"
        "\n"
        "ALT WIND   TEMP\n"
        "00  {:03.0f}/{:02.0f} {}\n"
        "01  {:03.0f}/{:02.0f} {}\n"
        "02  {:03.0f}/{:02.0f} {}\n"
        "07  {:03.0f}/{:02.0f} {}\n"
        "26  {:03.0f}/{:02.0f} {}"),
      magVar,
      // 0ft/ground
      windAtGround.mStandardDirection - magVar,
      windAtGround.mSpeedInKnots,
      temperature,
      // 1000ft
      windAtGround.mStandardDirection - magVar,
      windAtGround.mSpeedInKnots * 2,
      temperature - 2,
      // 2000ft
      windAtGround.mStandardDirection - magVar,
      windAtGround.mSpeedInKnots * 2,
      temperature - (2 * 2),
      // 7000ft/2000m
      windAt2000.mStandardDirection - magVar,
      windAt2000.mSpeedInKnots,
      temperature - (2 * 7),
      // 26000ft/8000m
      windAt8000.mStandardDirection - magVar,
      windAt8000.mSpeedInKnots,
      temperature - (2 * 26)));
  }

  this->ClearContentCache();
  this->evFullyReplacedEvent.Emit();
}

void DCSBriefingTab::OnGameEvent(
  const GameEvent& event,
  const std::filesystem::path& installPath,
  const std::filesystem::path&) {
  mInstallationPath = installPath;
  if (event.name == DCS::EVT_MISSION) {
    const auto missionZip = std::filesystem::canonical(event.value);

    if (mMission && mMission->GetZipPath() == missionZip) {
      return;
    }

    mMission = DCSExtractedMission::Get(missionZip);
    dprintf("Briefing tab: loading {}", missionZip);
    this->Reload();
    return;
  }

  auto state = mDCSState;
  if (event.name == DCS::EVT_SELF_DATA) {
    auto raw = nlohmann::json::parse(event.value);
    state.mCoalition = raw.at("CoalitionID"),
    state.mCountry = raw.at("Country");
    state.mAircraft = raw.at("Name");
  } else if (event.name == DCS::EVT_ORIGIN) {
    auto raw = nlohmann::json::parse(event.value);
    state.mOrigin = LatLong {
      .mLat = raw["latitude"],
      .mLong = raw["longitude"],
    };
  } else {
    return;
  }

  if (state != mDCSState) {
    mDCSState = state;
    this->Reload();
  }
}

void DCSBriefingTab::RenderPageContent(
  ID2D1DeviceContext* ctx,
  uint16_t pageIndex,
  const D2D1_RECT_F& rect) {
  const auto textPageCount = mTextPages->GetPageCount();
  if (pageIndex < textPageCount) {
    mTextPages->RenderPage(ctx, pageIndex, rect);
    return;
  }
  mImagePages->RenderPage(ctx, pageIndex - textPageCount, rect);
}

bool DCSBriefingTab::IsNavigationAvailable() const {
  return this->GetPageCount() > 2;
}

std::shared_ptr<ITab> DCSBriefingTab::CreateNavigationTab(
  uint16_t currentPage) {
  std::vector<NavigationTab::Entry> entries;

  const auto textCount = mTextPages->GetPageCount();
  for (uint16_t i = 0; i < textCount; ++i) {
    entries.push_back({
      std::format(_("Transcription {}/{}"), i + 1, textCount),
      i,
    });
  }

  const auto paths = mImagePages->GetPaths();

  for (uint16_t i = 0; i < paths.size(); ++i) {
    entries.push_back(
      {paths.at(i).stem(), static_cast<uint16_t>(i + textCount)});
  }

  return std::make_shared<NavigationTab>(
    mDXR, this, entries, this->GetNativeContentSize(currentPage));
}

}// namespace OpenKneeboard
