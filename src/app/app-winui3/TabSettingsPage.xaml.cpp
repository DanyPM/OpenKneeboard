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
// clang-format off
#include "pch.h"
#include "TabSettingsPage.xaml.h"
#include "TabUIData.g.cpp"
#include "TabSettingsPage.g.cpp"
// clang-format on

#include <OpenKneeboard/FilePageSource.h>
#include <OpenKneeboard/ITab.h>
#include <OpenKneeboard/KneeboardState.h>
#include <OpenKneeboard/TabTypes.h>
#include <OpenKneeboard/TabView.h>
#include <OpenKneeboard/dprint.h>
#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>
#include <winrt/windows.storage.pickers.h>

#include <ranges>

#include "Globals.h"

using namespace OpenKneeboard;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::OpenKneeboardApp::implementation {

TabSettingsPage::TabSettingsPage() {
  InitializeComponent();

  auto tabs = winrt::single_threaded_observable_vector<IInspectable>();
  for (const auto& tab: gKneeboard->GetTabs()) {
    auto winrtTab = winrt::make<TabUIData>();
    winrtTab.Title(to_hstring(tab->GetTitle()));
    winrtTab.InstanceID(tab->GetRuntimeID().GetTemporaryValue());
    tabs.Append(winrtTab);
  }
  List().ItemsSource(tabs);
  tabs.VectorChanged({this, &TabSettingsPage::OnTabsChanged});

  auto tabTypes = AddTabFlyout().Items();
#define IT(label, name) \
  MenuFlyoutItem name##Item; \
  name##Item.Text(to_hstring(label)); \
  name##Item.Tag(box_value<uint64_t>(TABTYPE_IDX_##name)); \
  name##Item.Click({this, &TabSettingsPage::CreateTab}); \
  tabTypes.Append(name##Item);
  OPENKNEEBOARD_TAB_TYPES
#undef IT
}

fire_and_forget TabSettingsPage::RemoveTab(
  const IInspectable& sender,
  const RoutedEventArgs&) {
  const auto tabID = ITab::RuntimeID::FromTemporaryValue(
    unbox_value<uint64_t>(sender.as<Button>().Tag()));
  const auto tabs = gKneeboard->GetTabs();
  auto it = std::ranges::find(tabs, tabID, &ITab::GetRuntimeID);
  if (it == tabs.end()) {
    co_return;
  }
  const auto& tab = *it;

  ContentDialog dialog;
  dialog.XamlRoot(this->XamlRoot());
  dialog.Title(
    box_value(to_hstring(std::format(_("Remove {}?"), tab->GetTitle()))));
  dialog.Content(box_value(to_hstring(
    std::format(_("Do you want to remove the '{}' tab?"), tab->GetTitle()))));

  dialog.PrimaryButtonText(to_hstring(_("Yes")));
  dialog.CloseButtonText(to_hstring(_("No")));
  dialog.DefaultButton(ContentDialogButton::Primary);

  auto result = co_await dialog.ShowAsync();
  if (result != ContentDialogResult::Primary) {
    co_return;
  }

  auto idx = static_cast<uint8_t>(it - tabs.begin());
  gKneeboard->RemoveTab(idx);
  List()
    .ItemsSource()
    .as<Windows::Foundation::Collections::IVector<IInspectable>>()
    .RemoveAt(idx);
}

void TabSettingsPage::CreateTab(
  const IInspectable& sender,
  const RoutedEventArgs&) noexcept {
  auto tabType = static_cast<TabType>(
    unbox_value<uint64_t>(sender.as<MenuFlyoutItem>().Tag()));
  switch (tabType) {
    case TabType::Folder:
      CreateFolderTab();
      return;
    case TabType::SingleFile:
      CreateFileTab();
      return;
  }
#define IT(_, type) \
  if (tabType == TabType::type) { \
    if constexpr (std::constructible_from< \
                    type##Tab, \
                    DXResources, \
                    KneeboardState*>) { \
      AddTabs({std::make_shared<type##Tab>(gDXResources, gKneeboard.get())}); \
      return; \
    } else { \
      throw std::logic_error( \
        std::format("Don't know how to construct {}Tab", #type)); \
    } \
  }
  OPENKNEEBOARD_TAB_TYPES
#undef IT
  throw std::logic_error(
    std::format("Unhandled tab type: {}", static_cast<uint8_t>(tabType)));
}

fire_and_forget TabSettingsPage::CreateFileTab() {
  auto picker = Windows::Storage::Pickers::FileOpenPicker();
  picker.as<IInitializeWithWindow>()->Initialize(gMainWindow);
  picker.SettingsIdentifier(L"chooseFileForTab");
  picker.SuggestedStartLocation(
    Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
  auto filter = picker.FileTypeFilter();
  for (const auto& extension:
       FilePageSource::GetSupportedExtensions(gDXResources)) {
    picker.FileTypeFilter().Append(winrt::to_hstring(extension));
  }

  auto files = co_await picker.PickMultipleFilesAsync();

  std::vector<std::shared_ptr<OpenKneeboard::ITab>> newTabs;
  for (const auto& file: files) {
    auto stringPath = file.Path();
    if (stringPath.empty()) {
      continue;
    }

    const auto path
      = std::filesystem::canonical(std::wstring_view {stringPath});
    const auto title = path.stem();

    newTabs.push_back(std::make_shared<SingleFileTab>(
      gDXResources, gKneeboard.get(), title, path));
  }

  this->AddTabs(newTabs);
}

fire_and_forget TabSettingsPage::CreateFolderTab() {
  auto picker = Windows::Storage::Pickers::FolderPicker();
  picker.as<IInitializeWithWindow>()->Initialize(gMainWindow);
  picker.SettingsIdentifier(L"chooseFileForTab");
  picker.FileTypeFilter().Append(L"*");
  picker.SuggestedStartLocation(
    Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);

  auto folder = co_await picker.PickSingleFolderAsync();
  if (!folder) {
    co_return;
  }
  auto stringPath = folder.Path();
  if (stringPath.empty()) {
    co_return;
  }

  const auto path = std::filesystem::canonical(std::wstring_view {stringPath});
  const auto title = path.stem();

  this->AddTabs(
    {std::make_shared<FolderTab>(gDXResources, gKneeboard.get(), title, path)});
}

void TabSettingsPage::AddTabs(const std::vector<std::shared_ptr<ITab>>& tabs) {
  auto initialIndex = List().SelectedIndex();
  if (initialIndex == -1) {
    initialIndex = 0;
  }

  auto idx = initialIndex;
  auto allTabs = gKneeboard->GetTabs();
  for (const auto& tab: tabs) {
    allTabs.insert(allTabs.begin() + idx++, tab);
  }
  gKneeboard->SetTabs(allTabs);

  idx = initialIndex;
  auto items = List()
                 .ItemsSource()
                 .as<Windows::Foundation::Collections::IVector<IInspectable>>();
  for (const auto& tab: tabs) {
    auto winrtTab = winrt::make<TabUIData>();
    winrtTab.Title(to_hstring(tab->GetTitle()));
    winrtTab.InstanceID(tab->GetRuntimeID().GetTemporaryValue());
    items.InsertAt(idx++, winrtTab);
  }
}

void TabSettingsPage::OnTabsChanged(
  const IInspectable&,
  const Windows::Foundation::Collections::IVectorChangedEventArgs&) noexcept {
  // For add/remove, the kneeboard state is updated first, but for reorder,
  // the ListView is the source of truth.
  //
  // Reorders are two-step: a remove and an insert
  auto items = List()
                 .ItemsSource()
                 .as<Windows::Foundation::Collections::IVector<IInspectable>>();
  auto tabs = gKneeboard->GetTabs();
  if (items.Size() != tabs.size()) {
    // ignore the deletion ...
    return;
  }
  // ... but act on the insert :)

  std::vector<std::shared_ptr<ITab>> reorderedTabs;
  for (auto item: items) {
    auto id
      = ITab::RuntimeID::FromTemporaryValue(item.as<TabUIData>()->InstanceID());
    auto it = std::ranges::find(tabs, id, &ITab::GetRuntimeID);
    if (it == tabs.end()) {
      continue;
    }
    reorderedTabs.push_back(*it);
  }
  gKneeboard->SetTabs(reorderedTabs);
}

hstring TabUIData::Title() {
  return mTitle;
}

void TabUIData::Title(hstring value) {
  mTitle = value;
}

uint64_t TabUIData::InstanceID() {
  return mInstanceID;
}

void TabUIData::InstanceID(uint64_t value) {
  mInstanceID = value;
}

}// namespace winrt::OpenKneeboardApp::implementation
