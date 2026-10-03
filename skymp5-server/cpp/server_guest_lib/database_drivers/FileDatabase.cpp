#include "FileDatabase.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <save_storages/AsyncSaveStorage.h>
#include <set>
#include <sodium.h>
#include <unordered_set>
#ifdef _WIN32
#  include <Windows.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace {
constexpr size_t kMaxJournalBytes = 256 * 1024 * 1024;

struct FileNameLess
{
  bool operator()(const std::filesystem::path& lhs,
                  const std::filesystem::path& rhs) const
  {
#ifdef _WIN32
    int comparison =
      CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE);
    if (comparison == 0) {
      throw std::runtime_error("Unable to compare save filenames");
    }
    return comparison == CSTR_LESS_THAN;
#else
    return lhs.native() < rhs.native();
#endif
  }
};

std::string Checksum(const std::string& bytes)
{
  unsigned char hash[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(
    hash, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
  char hex[crypto_hash_sha256_BYTES * 2 + 1];
  sodium_bin2hex(hex, sizeof(hex), hash, sizeof(hash));
  return hex;
}

void SyncDirectory(const std::filesystem::path& directory)
{
#ifndef _WIN32
  int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    throw std::runtime_error("Unable to open save directory for sync");
  }
  int result = fsync(fd);
  close(fd);
  if (result != 0) {
    throw std::runtime_error("Unable to sync save directory");
  }
#endif
}

void ReplaceFile(const std::filesystem::path& path, const std::string& bytes,
                 bool syncDirectory = true)
{
  auto temporary = path;
  temporary += ".tmp";
#ifdef _WIN32
  auto file = _wfopen(temporary.c_str(), L"wb");
#else
  auto file = fopen(temporary.c_str(), "wb");
#endif
  if (!file) {
    throw std::runtime_error("Unable to open save file " + temporary.string());
  }
  std::unique_ptr<FILE, decltype(&fclose)> guard(file, fclose);
  if (fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size() ||
      fflush(file) != 0) {
    throw std::runtime_error("Unable to write save file " +
                             temporary.string());
  }
#ifdef _WIN32
  int result = _commit(_fileno(file));
#else
  int result = fsync(fileno(file));
#endif
  if (result != 0) {
    throw std::runtime_error("Unable to flush save file " +
                             temporary.string());
  }
  if (fclose(guard.release()) != 0) {
    throw std::runtime_error("Unable to close save file " +
                             temporary.string());
  }
  std::filesystem::rename(temporary, path);
  if (syncDirectory) {
    SyncDirectory(path.parent_path());
  }
}

void ValidateBatch(const nlohmann::json& batch)
{
#ifdef _WIN32
  const char* invalidCharacters = "/\\:*?\"<>|";
#else
  const char* invalidCharacters = "/\\";
#endif
  std::set<std::filesystem::path, FileNameLess> names;
  simdjson::dom::parser parser;
  for (const auto& entry : batch) {
    auto fileName = entry.at("file").get<std::string>();
    auto dump = entry.at("json").get<std::string>();
#ifdef _WIN32
    if (std::filesystem::path(fileName).native().size() > 255 ||
        std::any_of(fileName.begin(), fileName.end(),
                    [](unsigned char character) { return character < 32; })) {
      throw std::runtime_error("Invalid Windows save filename");
    }
#endif
    auto element = parser.parse(dump).value();
    auto desc = element["formDesc"].get_string().value();
    std::string description(desc);
    auto hex = description.substr(0, description.find(':'));
    if (hex.empty() || hex.size() > 8 ||
        hex.find_first_not_of("0123456789abcdef") != std::string::npos ||
        fileName.find_first_of(invalidCharacters) != std::string::npos ||
        fileName.find('\0') != std::string::npos ||
        FormDesc::FromString(description).ToString() != description ||
        fileName !=
          FormDesc::FromString(description).ToString('_') + ".json" ||
        !names.insert(fileName).second) {
      throw std::runtime_error("Invalid save batch actor identity");
    }
    MpChangeForm::JsonToChangeForm(element);
  }
}
}

struct FileDatabase::Impl
{
  const std::filesystem::path changeFormsDirectory;
  const std::shared_ptr<spdlog::logger> logger;

  std::filesystem::path JournalPath() const
  {
    return changeFormsDirectory / ".pending-batch";
  }

  void Recover()
  {
    auto path = JournalPath();
    if (!std::filesystem::exists(path)) {
      return;
    }
    if (std::filesystem::file_size(path) > kMaxJournalBytes) {
      throw std::runtime_error("Save batch journal exceeds its size limit");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
      throw std::runtime_error("Unable to read save batch journal");
    }
    std::string bytes((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
    if (stream.bad()) {
      throw std::runtime_error("Unable to read complete save batch journal");
    }
    stream.close();
    auto journal = nlohmann::json::parse(bytes);
    const auto& batch = journal.at("batch");
    if (journal.at("version") != 1 || !batch.is_array() || batch.empty() ||
        journal.at("checksum") != Checksum(batch.dump())) {
      throw std::runtime_error("Invalid save batch journal");
    }

    // Validate the whole batch before replacing even one actor file.
    ValidateBatch(batch);
    auto writeRange = [&](size_t start, size_t stride) {
      for (size_t i = start; i < batch.size(); i += stride) {
        const auto& entry = batch[i];
        ReplaceFile(changeFormsDirectory / entry.at("file").get<std::string>(),
                    entry.at("json").get<std::string>(), false);
      }
    };
    if (batch.size() < 256) {
      writeRange(0, 1);
    } else {
      // The save-storage worker still serializes batches. Bound parallel file
      // flushes within a large batch so NPC snapshots do not delay trading.
      std::vector<std::future<void>> writes;
      for (size_t i = 0; i < 4; ++i) {
        writes.push_back(std::async(std::launch::async, writeRange, i, 4));
      }
      std::exception_ptr failure;
      for (auto& write : writes) {
        try {
          write.get();
        } catch (...) {
          failure = std::current_exception();
        }
      }
      if (failure) {
        std::rethrow_exception(failure);
      }
    }
    SyncDirectory(changeFormsDirectory);
    std::filesystem::remove(path);
    SyncDirectory(changeFormsDirectory);
  }
};

FileDatabase::FileDatabase(std::string directory_,
                           std::shared_ptr<spdlog::logger> logger_)
{
  std::filesystem::path p = directory_;
  p /= "changeForms";

  pImpl.reset(new Impl{ p, logger_ });
  std::filesystem::create_directories(p);
  pImpl->Recover();
}

std::vector<std::optional<MpChangeForm>>&& FileDatabase::UpsertImpl(
  std::vector<std::optional<MpChangeForm>>&& changeForms,
  size_t& outNumUpserted)
{
  try {
    // Never overwrite an interrupted older batch with a newer snapshot.
    pImpl->Recover();
    auto batch = nlohmann::json::array();
    std::map<std::filesystem::path, size_t, FileNameLess> positions;
    size_t submitted = 0;
    for (const auto& changeForm : changeForms) {
      if (changeForm == std::nullopt) {
        continue;
      }

      auto fileName = changeForm->formDesc.ToString('_') + ".json";
      auto entry =
        nlohmann::json{ { "file", fileName },
                        { "json",
                          MpChangeForm::ToJson(*changeForm).dump(2) } };
      auto position = positions.emplace(fileName, batch.size());
      if (position.second) {
        batch.push_back(std::move(entry));
      } else {
        batch[position.first->second] = std::move(entry);
      }
      ++submitted;
    }
    outNumUpserted = 0;
    if (!batch.empty()) {
      ValidateBatch(batch);
      auto journal = nlohmann::json{
        { "version", 1 },
        { "batch", batch },
        { "checksum", Checksum(batch.dump()) }
      }.dump();
      if (journal.size() > kMaxJournalBytes) {
        throw std::runtime_error("Save batch journal exceeds its size limit");
      }
      ReplaceFile(pImpl->JournalPath(), journal);
      pImpl->Recover();
      outNumUpserted = submitted;
    }
    return std::move(changeForms);
  } catch (std::exception& e) {
    throw Viet::AsyncSaveStorage<
      MpChangeForm, FormDesc,
      std::vector<FormDesc>>::UpsertFailedException(std::move(changeForms),
                                                    e.what());
  }
}

void FileDatabase::Iterate(const IterateCallback& iterateCallback,
                           std::optional<std::vector<FormDesc>> filter)
{
  try {
    pImpl->Recover();
    auto p = pImpl->changeFormsDirectory;

    simdjson::dom::parser parser;

    if (!std::filesystem::exists(p)) {
      return;
    }

    std::optional<std::unordered_set<std::string>> filterSet;
    if (filter) {
      std::unordered_set<std::string>& value = filterSet.emplace();
      for (const auto& desc : *filter) {
        value.insert(desc.ToString());
      }
    }

    for (auto& entry : std::filesystem::directory_iterator(p)) {
      try {
        if (entry.path().extension() != ".json") {
          continue;
        }

        std::ifstream t(entry.path());
        std::string jsonDump((std::istreambuf_iterator<char>(t)),
                             std::istreambuf_iterator<char>());

        auto result = parser.parse(jsonDump).value();
        auto changeForm = MpChangeForm::JsonToChangeForm(result);

        if (filterSet) {
          if (filterSet->find(changeForm.formDesc.ToString()) ==
              filterSet->end()) {
            continue;
          }
        }

        iterateCallback(changeForm);
      } catch (std::exception& e) {
        pImpl->logger->error("Parsing of {} failed with {}",
                             entry.path().string(), e.what());
      }
    }

  } catch (std::exception& e) {
    throw Viet::AsyncSaveStorage<
      MpChangeForm, FormDesc,
      std::vector<FormDesc>>::IterateFailedException(std::move(filter),
                                                     e.what());
  }
}
