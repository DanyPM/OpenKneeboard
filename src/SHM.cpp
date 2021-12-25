#include "YAVRK/shm.h"

#include <Windows.h>
#include <fmt/format.h>

namespace YAVRK {

SHM::SHM(std::shared_ptr<Impl> p) : p(p) {
}

SHM::~SHM() {
}
class SHM::Impl {
 public:
  HANDLE Handle;
  void* Mapping;
  SHMHeader* Header;
  std::byte* Data;
  bool IsFeeder;
  ~Impl() {
    if (IsFeeder) {
      Header->Flags &= ~Flags::FEEDER_DETACHED;
    }
    UnmapViewOfFile(Mapping);
    CloseHandle(Handle);
  }
};

namespace {
// *****PLEASE***** change this if you fork or re-use this code
const auto PREFIX = "com.fredemmott.yavrk";
}

SHM::operator bool() const {
  if (!p) {
    return false;
  }
  if ((p->Header->Flags & Flags::FEEDER_DETACHED)) {
    return false;
  }
  return true;
}

SHM SHM::GetOrCreate(const SHMHeader& header) {
  const auto path = fmt::format("{}/{}", PREFIX, header.Version);
  const auto dataSize = 4 * header.ImageWidth * header.ImageHeight;
  const auto shmSize = sizeof(SHMHeader) + dataSize;

  HANDLE handle;
  handle = CreateFileMappingA(
    INVALID_HANDLE_VALUE,
    NULL,
    PAGE_READWRITE,
    0,
    (DWORD)shmSize,
    path.c_str());
  if (!handle) {
    return {nullptr};
  }
  void* mapping = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, shmSize);
  memcpy(mapping, &header, sizeof(SHMHeader));

  auto p = new Impl {
    .Handle = handle,
    .Mapping = mapping,
    .Header = reinterpret_cast<SHMHeader*>(mapping),
    .Data = &reinterpret_cast<std::byte*>(mapping)[sizeof(SHMHeader)],
    .IsFeeder = true};
  return {std::shared_ptr<Impl>(p)};
}

SHM SHM::MaybeGet() {
  const auto path = fmt::format("{}/{}", PREFIX, IPC_VERSION);
  HANDLE handle;
  handle = CreateFileMappingA(
    INVALID_HANDLE_VALUE,
    NULL,
    PAGE_READONLY,
    0,
    1024 * 1024 * 10,
    path.c_str());
  if ((!handle) || GetLastError() != ERROR_ALREADY_EXISTS) {
    CloseHandle(handle);
    return {nullptr};
  }

  void* mapping = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
  SHMHeader* header = reinterpret_cast<SHMHeader*>(mapping);
  if (header->Version != IPC_VERSION || header->Flags & Flags::FEEDER_DETACHED) {
    UnmapViewOfFile(mapping);
    CloseHandle(handle);
    return {nullptr};
  }

  auto p = new Impl {
    .Handle = handle,
    .Mapping = mapping,
    .Header = header,
    .Data = &reinterpret_cast<std::byte*>(mapping)[sizeof(SHMHeader)],
    .IsFeeder = false};
  return {std::shared_ptr<Impl>(p)};
}

SHMHeader SHM::Header() const {
  return *p->Header;
}

std::byte* SHM::ImageData() const {
  return p->Data;
}

uint32_t SHM::ImageDataSize() const {
  const auto& header = *p->Header;
  return 4 * header.ImageWidth * header.ImageHeight;
}

}// namespace YAVRK
